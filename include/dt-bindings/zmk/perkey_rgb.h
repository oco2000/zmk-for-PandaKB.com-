/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Map sentinel: this strip LED does not sit under a key. */
#define PRGB_UG 255

#define PRGB_TOG_CMD 0
#define PRGB_ON_CMD  1
#define PRGB_OFF_CMD 2
#define PRGB_BRI_CMD 3
#define PRGB_BRD_CMD 4
#define PRGB_BRT_CMD 5 /* absolute brightness, param2 = 0..100 */

#define PRGB_TOG PRGB_TOG_CMD 0
#define PRGB_ON  PRGB_ON_CMD 0
#define PRGB_OFF PRGB_OFF_CMD 0
#define PRGB_BRI PRGB_BRI_CMD 0
#define PRGB_BRD PRGB_BRD_CMD 0
