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
