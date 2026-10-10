/* SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* =============================================================================
 * ws2812_fx.h - optional animation effects layered on the ws2812 driver
 *
 * These only stage pixels into the strip buffer. Call ws2812_show() or
 * ws2812_show_async() yourself once you are done composing a frame:
 *
 *     ws2812_fx_fire(&strip, frame++, 80);
 *     ws2812_show(&strip, 20);
 *
 * Nothing here is required by the driver. With -ffunction-sections and
 * -Wl,--gc-sections the effects you never call cost no flash.
 * ============================================================================= */

#ifndef WS2812_FX_H
#define WS2812_FX_H

#include "ws2812/ws2812.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Full-spectrum rainbow across the strip.
 * offset     : animation phase, increment once per frame
 * brightness : 0-255, keep at or below 128 when running many LEDs off USB
 */
void ws2812_fx_rainbow(ws2812_t *dev, uint8_t offset, uint8_t brightness);

/** Rainbow restricted to the green-to-purple arc of the hue wheel. */
void ws2812_fx_green_purple(ws2812_t *dev, uint8_t offset, uint8_t brightness);

/** Interpolated value-noise flicker in red/orange/yellow hues. */
void ws2812_fx_fire(ws2812_t *dev, uint8_t offset, uint8_t brightness);

/**
 * Hard strobe: one lit frame of white or red, the colour picked at random per
 * flash, then dark until the next flash.
 * offset     : increment once per frame
 * period     : frames per flash - one lit, the rest dark. Values below 2 are
 *              raised to 2, since a strobe needs a dark frame.
 * brightness : 0-255. A white frame draws three times the current of red, and
 *              every LED is lit at once - size the supply for that.
 */
void ws2812_fx_strobe_red_white(ws2812_t *dev, uint8_t offset, uint8_t period,
                                uint8_t brightness);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_FX_H */
