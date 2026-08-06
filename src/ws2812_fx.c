/* SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* =============================================================================
 * ws2812_fx.c - optional animation effects
 * Each function only stages pixels; the caller decides when to transmit.
 * ============================================================================= */

#include "ws2812/ws2812_fx.h"

/* -- Noise helpers ---------------------------------------------------------- */

static uint8_t hash8(uint16_t x)
{
    x ^= (uint16_t)(x >> 8);
    x = (uint16_t)(x * 0x9E37u);   /* golden-ratio mixing constant */
    x ^= (uint16_t)(x >> 8);
    return (uint8_t)x;
}

/* Linear interpolation between a and b, t spanning 0..65535. */
static uint8_t lerp8by16(uint8_t a, uint8_t b, uint16_t t)
{
    uint8_t delta;

    if (b >= a)
    {
        delta = (uint8_t)(b - a);
        return (uint8_t)(a + (uint8_t)(((uint32_t)delta * t) >> 16u));
    }

    delta = (uint8_t)(a - b);
    return (uint8_t)(a - (uint8_t)(((uint32_t)delta * t) >> 16u));
}

/* -- Effects ---------------------------------------------------------------- */

void ws2812_fx_rainbow(ws2812_t *dev, uint8_t offset, uint8_t brightness)
{
    uint16_t i;

    if (dev == NULL)
    {
        return;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        uint8_t hue = (uint8_t)((((uint32_t)i * 256u) / dev->num_leds) + offset);
        uint8_t r;
        uint8_t g;
        uint8_t b;

        ws2812_hsv_to_rgb(hue, 255u, brightness, &r, &g, &b);
        ws2812_set_pixel(dev, i, r, g, b);
    }
}

void ws2812_fx_green_purple(ws2812_t *dev, uint8_t offset, uint8_t brightness)
{
    const uint8_t hue_start = 85u;    /* green  */
    const uint8_t hue_end   = 200u;   /* purple */
    const uint8_t hue_range = (uint8_t)(hue_end - hue_start);

    uint16_t i;

    if (dev == NULL)
    {
        return;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        uint8_t pos = (uint8_t)((((uint32_t)i * 256u) / dev->num_leds) + offset);
        uint8_t hue = (uint8_t)(hue_start + (uint8_t)(((uint16_t)pos * hue_range) >> 8u));
        uint8_t r;
        uint8_t g;
        uint8_t b;

        ws2812_hsv_to_rgb(hue, 255u, brightness, &r, &g, &b);
        ws2812_set_pixel(dev, i, r, g, b);
    }
}

void ws2812_fx_fire(ws2812_t *dev, uint8_t offset, uint8_t brightness)
{
    uint16_t i;

    if (dev == NULL)
    {
        return;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        /* Sub-pixel position: 8.8 fixed point. Sampling the noise field at
         * higher resolution than one sample per LED is what keeps the flame
         * smooth instead of stroboscopic. */
        uint16_t x  = (uint16_t)(((uint16_t)i * 256u) + ((uint16_t)offset * 64u));
        uint16_t xi = (uint16_t)(x >> 8u);      /* integer part  */
        uint16_t xf = (uint16_t)(x & 0xFFu);    /* fraction part */

        /* Smoothstep easing so the interpolation has no visible corners. */
        uint16_t t = (uint16_t)(((uint32_t)xf * xf * (65535u - (xf << 1u))) >> 16u);

        uint8_t  noise = lerp8by16(hash8(xi), hash8((uint16_t)(xi + 1u)), t);

        /* Square the noise to bias toward dark, then lift the floor so the
         * strip never reads as dead. */
        uint8_t  heat  = (uint8_t)(((uint16_t)noise * noise) >> 8u);
        uint8_t  value;
        uint8_t  hue;
        uint8_t  r;
        uint8_t  g;
        uint8_t  b;

        heat  = (uint8_t)((heat >> 1u) + 40u);
        value = (heat > brightness) ? brightness : heat;
        hue   = (uint8_t)(((uint16_t)heat * 50u) / 255u);   /* red -> yellow */

        ws2812_hsv_to_rgb(hue, 255u, value, &r, &g, &b);
        ws2812_set_pixel(dev, i, r, g, b);
    }
}
