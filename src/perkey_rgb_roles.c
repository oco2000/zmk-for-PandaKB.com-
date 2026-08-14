/*
 * Derives what each key does on each layer, straight from the keymap
 * devicetree at build time.
 *
 * Doing it here rather than at runtime is what keeps the two halves in step:
 * the keymap is compiled into both firmwares, so a peripheral works out the
 * same answer as the central without anything crossing the split. It also
 * means ZMK Studio must stay disabled, since a keymap edited at runtime would
 * no longer match these tables.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include <zmk/keys.h>
#include <zmk/matrix.h>
#include <zmk/perkey_rgb.h>

#include <dt-bindings/zmk/hid_usage_pages.h>

#define KEYMAP_NODE DT_INST(0, zmk_keymap)

/* Every binding of every layer, as the devicetree ordinal of the behavior it
 * points at plus its first parameter. Ordinals are stable within a build and
 * are what let us recognise a behavior without matching on its name. */

#define BINDING_ORD(node, prop, idx) DT_DEP_ORD(DT_PHANDLE_BY_IDX(node, prop, idx)),

#define BINDING_PARAM(node, prop, idx)                                                             \
    COND_CODE_0(DT_PHA_HAS_CELL_AT_IDX(node, prop, idx, param1), (0),                              \
                (DT_PHA_BY_IDX(node, prop, idx, param1))),

#define LAYER_ORDS(node) {DT_FOREACH_PROP_ELEM(node, bindings, BINDING_ORD)},
#define LAYER_PARAMS(node) {DT_FOREACH_PROP_ELEM(node, bindings, BINDING_PARAM)},

static const uint16_t binding_ords[][ZMK_KEYMAP_LEN] = {
    DT_FOREACH_CHILD_STATUS_OKAY(KEYMAP_NODE, LAYER_ORDS)};

static const uint32_t binding_params[][ZMK_KEYMAP_LEN] = {
    DT_FOREACH_CHILD_STATUS_OKAY(KEYMAP_NODE, LAYER_PARAMS)};

/* Ordinals of the behaviors we care about. Collected by compatible so that a
 * keymap using its own hold-taps is classified correctly without naming them. */

#define ORD_ENTRY(node) DT_DEP_ORD(node),

static const uint16_t unbound_ords[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_transparent, ORD_ENTRY)
        DT_FOREACH_STATUS_OKAY(zmk_behavior_none, ORD_ENTRY)};

static const uint16_t key_press_ords[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_key_press, ORD_ENTRY)};

static const uint16_t layer_ords[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_momentary_layer, ORD_ENTRY)
        DT_FOREACH_STATUS_OKAY(zmk_behavior_to_layer, ORD_ENTRY)
            DT_FOREACH_STATUS_OKAY(zmk_behavior_toggle_layer, ORD_ENTRY)};

/*
 * Hold-taps are all one compatible, so &lt and &hm are told apart by what their
 * own hold binding points at: &lt holds a momentary layer, &hm holds a key
 * press.
 */
struct hold_tap_entry {
    uint16_t ord;
    bool holds_layer;
    bool holds_key_press;
};

#define HOLD_TAP_ENTRY(node)                                                                       \
    {                                                                                              \
        .ord = DT_DEP_ORD(node),                                                                   \
        .holds_layer = DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node, bindings, 0),                    \
                                          zmk_behavior_momentary_layer),                           \
        .holds_key_press =                                                                         \
            DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node, bindings, 0), zmk_behavior_key_press),      \
    },

static const struct hold_tap_entry hold_tap_entries[] = {
    DT_FOREACH_STATUS_OKAY(zmk_behavior_hold_tap, HOLD_TAP_ENTRY)};

static bool ord_in(const uint16_t *ords, size_t len, uint16_t ord) {
    for (size_t i = 0; i < len; i++) {
        if (ords[i] == ord) {
            return true;
        }
    }

    return false;
}

/* A bare modifier. Chorded keycodes such as LC(Z) carry their modifier in the
 * top byte and report Z as the usage, so they are not modifier keys. */
static bool param_is_modifier(uint32_t param) {
    uint16_t page = ZMK_HID_USAGE_PAGE(param);

    return is_mod(page ? page : HID_USAGE_KEY, ZMK_HID_USAGE_ID(param));
}

struct zmk_perkey_rgb_key_role zmk_perkey_rgb_role_at(uint8_t layer, uint8_t position) {
    struct zmk_perkey_rgb_key_role role = {.role = ZMK_PERKEY_RGB_ROLE_NORMAL, .param = 0};

    if (layer >= ARRAY_SIZE(binding_ords) || position >= ZMK_KEYMAP_LEN) {
        return role;
    }

    uint16_t ord = binding_ords[layer][position];
    uint32_t param = binding_params[layer][position];

    if (ord_in(unbound_ords, ARRAY_SIZE(unbound_ords), ord)) {
        role.role = ZMK_PERKEY_RGB_ROLE_UNBOUND;
        return role;
    }

    if (ord_in(layer_ords, ARRAY_SIZE(layer_ords), ord)) {
        role.role = ZMK_PERKEY_RGB_ROLE_LAYER;
        role.param = (uint8_t)param;
        return role;
    }

    if (ord_in(key_press_ords, ARRAY_SIZE(key_press_ords), ord) && param_is_modifier(param)) {
        role.role = ZMK_PERKEY_RGB_ROLE_MOD;
        return role;
    }

    for (size_t i = 0; i < ARRAY_SIZE(hold_tap_entries); i++) {
        if (hold_tap_entries[i].ord != ord) {
            continue;
        }

        if (hold_tap_entries[i].holds_layer) {
            role.role = ZMK_PERKEY_RGB_ROLE_LAYER;
            role.param = (uint8_t)param;
        } else if (hold_tap_entries[i].holds_key_press && param_is_modifier(param)) {
            role.role = ZMK_PERKEY_RGB_ROLE_MOD;
        }

        break;
    }

    return role;
}
