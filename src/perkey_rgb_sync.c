/*
 * Keeps the peripheral's idea of the active layer in step with the central.
 *
 * The colour tables themselves are compiled into both halves, so the only
 * thing that has to cross the split is the layer index. It rides a
 * global-locality behavior, which is the one channel ZMK v0.3 gives an
 * out-of-tree module for pushing state to peripherals.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_perkey_rgb_sync

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/perkey_rgb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PRGB_SYNC_LAYER 1

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case PRGB_SYNC_LAYER:
        return zmk_perkey_rgb_set_layer((uint8_t)binding->param2);
    }

    /* Unknown message: a firmware skew between the halves must degrade, not
     * misrender. */
    LOG_WRN("Ignoring unknown perkey RGB sync message %d", binding->param1);

    return 0;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_perkey_rgb_sync_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_perkey_rgb_sync_driver_api);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

static void publish_layer(void) {
    uint8_t layer = zmk_keymap_highest_layer_active();

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_DRV_INST(0)),
        .param1 = PRGB_SYNC_LAYER,
        .param2 = layer,
    };
    struct zmk_behavior_binding_event event = {
        .layer = layer,
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    /* Invoking rather than calling set_layer() directly: a global behavior is
     * relayed to the peripheral and then run locally, so both halves take the
     * same path. */
    zmk_behavior_invoke_binding(&binding, event, true);
}

static int central_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);

    /* Waking is the cheap moment to re-assert state: a peripheral that
     * reconnected while asleep would otherwise keep painting a stale layer
     * until the next layer change. */
    if (activity && activity->state != ZMK_ACTIVITY_ACTIVE) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    publish_layer();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(perkey_rgb_sync, central_listener);
ZMK_SUBSCRIPTION(perkey_rgb_sync, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(perkey_rgb_sync, zmk_activity_state_changed);

#endif /* central only */

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
