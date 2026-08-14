/*
 * Per-key RGB renderer. Owns the whole strip on this half and repaints it only
 * when something actually changes.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_perkey_rgb

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/perkey_rgb.h>
#include <zmk/workqueue.h>

#include <dt-bindings/zmk/perkey_rgb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#error "CONFIG_ZMK_PERKEY_RGB requires CONFIG_ZMK_RGB_UNDERGLOW=n: stock underglow rewrites every " \
       "pixel every 50ms and would fight this renderer."
#endif

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define MAP_NODE   DT_INST(0, zmk_perkey_rgb_map)
#define LED_COUNT  DT_PROP(STRIP_NODE, chain_length)

/* DT_ENUM_IDX expands to a literal 0 (left) or 1 (right). */
static const uint8_t led_positions[LED_COUNT] =
    COND_CODE_0(DT_ENUM_IDX(MAP_NODE, side), (DT_PROP(MAP_NODE, left_leds)),
                (DT_PROP(MAP_NODE, right_leds)));

BUILD_ASSERT(DT_PROP_LEN(MAP_NODE, left_leds) == LED_COUNT,
             "left-leds length must equal the strip chain-length");
BUILD_ASSERT(DT_PROP_LEN(MAP_NODE, right_leds) == LED_COUNT,
             "right-leds length must equal the strip chain-length");

struct prgb_hsb {
    uint16_t h; /* 0-359 */
    uint8_t s;  /* 0-100 */
    uint8_t v;  /* 0-100 */
};

#define HSB_FROM_PROP(prop)                                                                        \
    {                                                                                              \
        .h = DT_INST_PROP_BY_IDX(0, prop, 0), .s = DT_INST_PROP_BY_IDX(0, prop, 1),                \
        .v = DT_INST_PROP_BY_IDX(0, prop, 2),                                                      \
    }

BUILD_ASSERT(DT_INST_PROP_LEN(0, default_color) == 3, "default-color must be <hue sat val>");
BUILD_ASSERT(DT_INST_PROP_LEN(0, underglow_color) == 3, "underglow-color must be <hue sat val>");

static const struct prgb_hsb color_default = HSB_FROM_PROP(default_color);
static const struct prgb_hsb color_underglow = HSB_FROM_PROP(underglow_color);

#define HAS_PRESSED_COLOR DT_INST_NODE_HAS_PROP(0, pressed_color)
#if HAS_PRESSED_COLOR
BUILD_ASSERT(DT_INST_PROP_LEN(0, pressed_color) == 3, "pressed-color must be <hue sat val>");
static const struct prgb_hsb color_pressed = HSB_FROM_PROP(pressed_color);
#endif

#define SETTINGS_VERSION 1

struct prgb_settings {
    uint8_t version;
    uint8_t on;
    uint8_t brightness;
} __packed;

static const struct device *strip;

static struct led_rgb pixels[LED_COUNT];
static struct led_rgb pixels_sent[LED_COUNT];
static bool ever_sent;

static bool state_on = IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_ON_START);
static uint8_t state_brightness = CONFIG_ZMK_PERKEY_RGB_BRT_START;

static uint64_t pressed_positions;

static struct k_work_delayable render_work;
static struct k_work_delayable save_work;
static int64_t last_emit;

#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_CHASE)
static uint8_t chase_index;
static struct k_work_delayable chase_work;
#endif

static struct led_rgb hsb_to_rgb(struct prgb_hsb hsb, uint8_t scale_percent) {
    uint16_t v = (uint16_t)hsb.v * scale_percent / 100;
    uint8_t val = (uint8_t)(v * 255 / 100);
    uint8_t sat = (uint8_t)((uint16_t)hsb.s * 255 / 100);

    if (sat == 0) {
        return (struct led_rgb){.r = val, .g = val, .b = val};
    }

    uint16_t hue = hsb.h % 360;
    uint8_t region = hue / 60;
    uint16_t remainder = (hue % 60) * 255 / 60;

    uint8_t p = (uint8_t)(((uint16_t)val * (255 - sat)) / 255);
    uint8_t q = (uint8_t)(((uint16_t)val * (255 - ((uint16_t)sat * remainder) / 255)) / 255);
    uint8_t t = (uint8_t)(((uint16_t)val * (255 - ((uint16_t)sat * (255 - remainder)) / 255)) / 255);

    switch (region) {
    case 0:
        return (struct led_rgb){.r = val, .g = t, .b = p};
    case 1:
        return (struct led_rgb){.r = q, .g = val, .b = p};
    case 2:
        return (struct led_rgb){.r = p, .g = val, .b = t};
    case 3:
        return (struct led_rgb){.r = p, .g = q, .b = val};
    case 4:
        return (struct led_rgb){.r = t, .g = p, .b = val};
    default:
        return (struct led_rgb){.r = val, .g = p, .b = q};
    }
}

static bool is_blanked(void) {
    if (!state_on) {
        return true;
    }

#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_AUTO_OFF_IDLE)
    if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        return true;
    }
#endif

    return false;
}

/* Brightness the composed colours are scaled by, as a percentage. */
static uint8_t output_scale(void) {
    return (uint8_t)((uint16_t)state_brightness * CONFIG_ZMK_PERKEY_RGB_BRT_MAX / 100);
}

static void compose(void) {
#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_CHASE)
    memset(pixels, 0, sizeof(pixels));
    pixels[chase_index] =
        hsb_to_rgb((struct prgb_hsb){.h = 0, .s = 0, .v = 100}, output_scale());
    return;
#else
    if (is_blanked()) {
        memset(pixels, 0, sizeof(pixels));
        return;
    }

    const uint8_t scale = output_scale();

    for (size_t i = 0; i < LED_COUNT; i++) {
        uint8_t position = led_positions[i];
        struct prgb_hsb color;

        if (position == PRGB_UG) {
            color = color_underglow;
        } else {
            color = color_default;
#if HAS_PRESSED_COLOR
            if (pressed_positions & BIT64(position)) {
                color = color_pressed;
            }
#endif
        }

        pixels[i] = hsb_to_rgb(color, scale);
    }
#endif
}

static void emit(void) {
    if (ever_sent && memcmp(pixels, pixels_sent, sizeof(pixels)) == 0) {
        return;
    }

    int err = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (err) {
        LOG_ERR("Failed to update LED strip (%d)", err);
        return;
    }

    memcpy(pixels_sent, pixels, sizeof(pixels));
    ever_sent = true;
    last_emit = k_uptime_get();
}

static void render_work_handler(struct k_work *work) {
    int64_t since = k_uptime_get() - last_emit;
    if (ever_sent && since < CONFIG_ZMK_PERKEY_RGB_MIN_INTERVAL_MS) {
        k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &render_work,
                                  K_MSEC(CONFIG_ZMK_PERKEY_RGB_MIN_INTERVAL_MS - since));
        return;
    }

    compose();
    emit();
}

static void request_render(void) {
    /* Settings are loaded at the same init level as this module, so a render
     * can be requested before the work items exist. */
    if (!strip) {
        return;
    }

    k_work_schedule_for_queue(zmk_workqueue_lowprio_work_q(), &render_work,
                              K_MSEC(CONFIG_ZMK_PERKEY_RGB_COALESCE_MS));
}

/* Blank without going through the workqueue, for the run-up to deep sleep. */
static void blank_now(void) {
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&render_work, &sync);

    memset(pixels, 0, sizeof(pixels));
    emit();
}

static void save_work_handler(struct k_work *work) {
    struct prgb_settings saved = {
        .version = SETTINGS_VERSION,
        .on = state_on,
        .brightness = state_brightness,
    };

    settings_save_one("prgb/state", &saved, sizeof(saved));
}

static void request_save(void) {
    if (!strip) {
        return;
    }

    k_work_schedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}

static int prgb_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                             void *cb_arg) {
    const char *next;

    if (!settings_name_steq(name, "state", &next) || next) {
        return -ENOENT;
    }

    struct prgb_settings saved;
    int rc = read_cb(cb_arg, &saved, sizeof(saved));
    if (rc < (int)sizeof(saved)) {
        return rc < 0 ? rc : -EINVAL;
    }

    if (saved.version != SETTINGS_VERSION) {
        LOG_WRN("Ignoring perkey RGB settings from version %d", saved.version);
        return 0;
    }

    state_on = saved.on;
    state_brightness = MIN(saved.brightness, 100);
    request_render();

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(prgb, "prgb", NULL, prgb_settings_set, NULL, NULL);

bool zmk_perkey_rgb_is_on(void) { return state_on; }

int zmk_perkey_rgb_set_on(bool on) {
    if (state_on == on) {
        return 0;
    }

    state_on = on;
    request_render();
    request_save();

    return 0;
}

uint8_t zmk_perkey_rgb_get_brightness(void) { return state_brightness; }

uint8_t zmk_perkey_rgb_calc_brightness(int direction) {
    int next = state_brightness + direction * CONFIG_ZMK_PERKEY_RGB_BRT_STEP;

    return (uint8_t)CLAMP(next, 0, 100);
}

int zmk_perkey_rgb_set_brightness(uint8_t brightness) {
    brightness = MIN(brightness, 100);
    if (state_brightness == brightness) {
        return 0;
    }

    state_brightness = brightness;
    request_render();
    request_save();

    return 0;
}

static int event_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);
    if (activity) {
        /* Blank synchronously: the activity handler powers devices down and
         * calls sys_poweroff() right after this, so a deferred blank can lose
         * the race and leave the LEDs lit through sleep. */
        if (activity->state == ZMK_ACTIVITY_SLEEP) {
            blank_now();
        } else {
            request_render();
        }

        return ZMK_EV_EVENT_BUBBLE;
    }

#if HAS_PRESSED_COLOR
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(eh);
    if (position && position->position < 64) {
        WRITE_BIT(pressed_positions, position->position, position->state);
        request_render();
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(perkey_rgb, event_listener);
ZMK_SUBSCRIPTION(perkey_rgb, zmk_activity_state_changed);
#if HAS_PRESSED_COLOR
ZMK_SUBSCRIPTION(perkey_rgb, zmk_position_state_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_CHASE)
static void chase_work_handler(struct k_work *work) {
    chase_index = (chase_index + 1) % LED_COUNT;
    LOG_INF("perkey RGB chase: LED %d", chase_index);

    request_render();
    k_work_schedule(&chase_work, K_MSEC(CONFIG_ZMK_PERKEY_RGB_CHASE_MS));
}
#endif

static int perkey_rgb_init(void) {
    const struct device *dev = DEVICE_DT_GET(STRIP_NODE);
    if (!device_is_ready(dev)) {
        LOG_ERR("LED strip device not ready");
        return -ENODEV;
    }

    k_work_init_delayable(&render_work, render_work_handler);
    k_work_init_delayable(&save_work, save_work_handler);

#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_CHASE)
    k_work_init_delayable(&chase_work, chase_work_handler);
#endif

    /* Publishing the device last is what arms request_render(). */
    strip = dev;
    request_render();

#if IS_ENABLED(CONFIG_ZMK_PERKEY_RGB_CHASE)
    k_work_schedule(&chase_work, K_MSEC(CONFIG_ZMK_PERKEY_RGB_CHASE_MS));
#endif

    return 0;
}

SYS_INIT(perkey_rgb_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
