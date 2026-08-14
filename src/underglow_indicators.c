/*
 * Underglow indicators: colour the strip by active layer, and by mod-color
 * while a modifier is held on the base layer.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_underglow_indicators

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/rgb.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>
#include <zmk/rgb_underglow.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define BASE_LAYER 0

/* Flattened hue/saturation pairs, see zmk,underglow-indicators.yaml. */
static const uint16_t layer_colors[] = DT_INST_PROP(0, layer_colors);
static const uint16_t mod_color[] = DT_INST_PROP(0, mod_color);

BUILD_ASSERT(ARRAY_SIZE(layer_colors) % 2 == 0,
             "layer-colors needs a hue and a saturation per layer");
BUILD_ASSERT(ARRAY_SIZE(mod_color) == 2, "mod-color needs exactly one hue and saturation");

/* Bit n corresponds to keycode HID_USAGE_KEY_KEYBOARD_LEFTCONTROL + n. */
static uint8_t held_mods;

static uint16_t applied_hue = UINT16_MAX;
static uint16_t applied_sat;

static void apply_color(void) {
    zmk_keymap_layer_index_t layer = zmk_keymap_highest_layer_active();
    if (2 * layer + 1 >= ARRAY_SIZE(layer_colors)) {
        return;
    }

    uint16_t hue = layer_colors[2 * layer];
    uint16_t sat = layer_colors[2 * layer + 1];
    if (layer == BASE_LAYER && held_mods) {
        hue = mod_color[0];
        sat = mod_color[1];
    }

    if (hue == applied_hue && sat == applied_sat) {
        return;
    }

    /* Direction 0 returns the current colour untouched, giving us the live
     * brightness to echo back so that RGB_BRI/RGB_BRD stay authoritative. */
    struct zmk_led_hsb current = zmk_rgb_underglow_calc_brt(0);

    /* Going through the rgb_ug behavior rather than calling the underglow API
     * directly: it has global locality, so the change is relayed to the other
     * half as well. */
    struct zmk_behavior_binding binding = {
        .behavior_dev = "rgb_ug",
        .param1 = RGB_COLOR_HSB_CMD,
        .param2 = RGB_COLOR_HSB_VAL(hue, sat, current.b),
    };
    struct zmk_behavior_binding_event event = {
        .layer = layer,
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    if (zmk_behavior_invoke_binding(&binding, event, true) == 0) {
        applied_hue = hue;
        applied_sat = sat;
    }
}

static int underglow_indicators_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);

    if (kc) {
        if (!is_mod(kc->usage_page, kc->keycode)) {
            return ZMK_EV_EVENT_BUBBLE;
        }

        WRITE_BIT(held_mods, kc->keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, kc->state);
    }

    apply_color();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(underglow_indicators, underglow_indicators_listener);
ZMK_SUBSCRIPTION(underglow_indicators, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(underglow_indicators, zmk_keycode_state_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
