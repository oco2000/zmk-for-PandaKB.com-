/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool zmk_perkey_rgb_is_on(void);
int zmk_perkey_rgb_set_on(bool on);

uint8_t zmk_perkey_rgb_get_brightness(void);

/* Current brightness stepped by direction, clamped. Does not apply it. */
uint8_t zmk_perkey_rgb_calc_brightness(int direction);

int zmk_perkey_rgb_set_brightness(uint8_t brightness);

/* Layer the strip is currently painted for. Set from the local keymap on the
 * central, and from the sync message on a peripheral. */
int zmk_perkey_rgb_set_layer(uint8_t layer);
uint8_t zmk_perkey_rgb_get_layer(void);

/* What a key does on a given layer, derived from the keymap at build time. */
enum zmk_perkey_rgb_role {
    ZMK_PERKEY_RGB_ROLE_NORMAL = 0,
    ZMK_PERKEY_RGB_ROLE_UNBOUND, /* &trans or &none */
    ZMK_PERKEY_RGB_ROLE_MOD,     /* holds a bare modifier */
    ZMK_PERKEY_RGB_ROLE_LAYER,   /* reaches another layer; param is that layer */
};

struct zmk_perkey_rgb_key_role {
    uint8_t role;
    uint8_t param;
};

struct zmk_perkey_rgb_key_role zmk_perkey_rgb_role_at(uint8_t layer, uint8_t position);
