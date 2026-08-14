/*
 * Per-key RGB control behavior: on/off and brightness.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_perkey_rgb

#include <zephyr/device.h>

#include <drivers/behavior.h>

#include <zmk/perkey_rgb.h>

#include <dt-bindings/zmk/perkey_rgb.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Relative commands are resolved to absolute ones on the central before the
 * binding is relayed, so the two halves cannot drift apart if a message is
 * missed. Same approach as ZMK's own rgb_ug behavior. */
static int on_keymap_binding_convert_central_state_dependent_params(
    struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case PRGB_TOG_CMD:
        binding->param1 = zmk_perkey_rgb_is_on() ? PRGB_OFF_CMD : PRGB_ON_CMD;
        binding->param2 = 0;
        break;
    case PRGB_BRI_CMD:
        binding->param1 = PRGB_BRT_CMD;
        binding->param2 = zmk_perkey_rgb_calc_brightness(1);
        break;
    case PRGB_BRD_CMD:
        binding->param1 = PRGB_BRT_CMD;
        binding->param2 = zmk_perkey_rgb_calc_brightness(-1);
        break;
    default:
        break;
    }

    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
    case PRGB_ON_CMD:
        return zmk_perkey_rgb_set_on(true);
    case PRGB_OFF_CMD:
        return zmk_perkey_rgb_set_on(false);
    case PRGB_TOG_CMD:
        return zmk_perkey_rgb_set_on(!zmk_perkey_rgb_is_on());
    case PRGB_BRT_CMD:
        return zmk_perkey_rgb_set_brightness(binding->param2);
    case PRGB_BRI_CMD:
        return zmk_perkey_rgb_set_brightness(zmk_perkey_rgb_calc_brightness(1));
    case PRGB_BRD_CMD:
        return zmk_perkey_rgb_set_brightness(zmk_perkey_rgb_calc_brightness(-1));
    }

    LOG_ERR("Unknown perkey RGB command: %d", binding->param1);

    return -ENOTSUP;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_perkey_rgb_driver_api = {
    .binding_convert_central_state_dependent_params =
        on_keymap_binding_convert_central_state_dependent_params,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_perkey_rgb_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
