/* SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* =============================================================================
 * ws2812.c - WS2812B / SK6812 driver over SERCOM SPI + DMAC
 * See include/ws2812/ws2812.h for the encoding scheme and wiring notes.
 * ============================================================================= */

#include "ws2812/ws2812.h"

#include <string.h>

/* The DMAC block counter is 16-bit, which caps a single transfer. */
#define WS2812_MAX_TX_BYTES  65535u

/* -- Bit encoding ----------------------------------------------------------- */

/*
 * Convert one 8-bit colour component into 3 SPI bytes.
 *
 * Each colour bit becomes 3 SPI bits:  '1' -> 110 (0x6),  '0' -> 100 (0x4).
 * 8 colour bits x 3 = 24 SPI bits = 3 bytes, packed MSB-first.
 *
 * Example: 0xC0 = 1100 0000
 *   bit7=1 -> 110, bit6=1 -> 110, bit5..0=0 -> 100 each
 *   = 110 110 100 100 100 100 100 100
 *   = 1101 1010 0100 1001 0010 0100
 *   = 0xDA 0x49 0x24
 */
static void encode_byte(uint8_t value, uint8_t *out)
{
    uint32_t word = 0u;
    int8_t   i;

    for (i = 7; i >= 0; i--)
    {
        word = (word << 3u) | (((value >> (uint8_t)i) & 1u) ? 0x6u : 0x4u);
    }

    out[0] = (uint8_t)(word >> 16u);
    out[1] = (uint8_t)(word >>  8u);
    out[2] = (uint8_t)(word       );
}

/* -- DMA completion --------------------------------------------------------- */

static void ws2812_dma_callback(DMAC_TRANSFER_EVENT event, uintptr_t context)
{
    ws2812_t *dev = (ws2812_t *)context;

    if (dev == NULL)
    {
        return;
    }

    dev->error = (event != DMAC_TRANSFER_EVENT_COMPLETE);
    dev->busy  = false;

    /* Optional FreeRTOS layer first so a waiting task is released promptly. */
    if (dev->notify_isr != NULL)
    {
        dev->notify_isr((uintptr_t)dev);
    }

    if (dev->on_complete != NULL)
    {
        dev->on_complete(dev->context);
    }
}

/* -- API -------------------------------------------------------------------- */

bool ws2812_init(ws2812_t *dev, const ws2812_cfg_t *cfg)
{
    uint32_t tx_len;

    if ((dev == NULL) || (cfg == NULL))
    {
        return false;
    }

    if ((cfg->sercom == NULL) || (cfg->buf == NULL) || (cfg->num_leds == 0u))
    {
        return false;
    }

    tx_len = WS2812_BUF_BYTES(cfg->num_leds);

    /* Catching this here is the whole point of passing buf_len - a buffer one
     * pixel short otherwise shows up as the DMA walking off the end. */
    if ((cfg->buf_len < tx_len) || (tx_len > WS2812_MAX_TX_BYTES))
    {
        return false;
    }

    dev->sercom      = cfg->sercom;
    dev->dma_ch      = cfg->dma_ch;
    dev->buf         = cfg->buf;
    dev->tx_len      = tx_len;
    dev->num_leds    = cfg->num_leds;
    dev->order       = cfg->order;
    dev->on_complete = cfg->on_complete;
    dev->context     = cfg->context;
    dev->now_ms      = cfg->now_ms;
    dev->busy        = false;
    dev->error       = false;
    dev->notify_isr  = NULL;
    dev->waiter      = 0u;

    /* Zero the whole transfer region so the tail reset bytes hold the line low,
     * then encode black into the pixel region. */
    (void)memset(dev->buf, 0x00, (size_t)tx_len);
    ws2812_clear(dev);

    DMAC_ChannelCallbackRegister(dev->dma_ch, ws2812_dma_callback, (uintptr_t)dev);

    return true;
}

void ws2812_set_pixel(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *p;

    if ((dev == NULL) || (index >= dev->num_leds))
    {
        return;
    }

    p = &dev->buf[(uint32_t)index * WS2812_BYTES_PER_LED];

    if (dev->order == WS2812_ORDER_RGB)
    {
        encode_byte(r, p);
        encode_byte(g, p + 3u);
    }
    else
    {
        encode_byte(g, p);
        encode_byte(r, p + 3u);
    }

    encode_byte(b, p + 6u);
}

void ws2812_fill(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;

    if (dev == NULL)
    {
        return;
    }

    for (i = 0u; i < dev->num_leds; i++)
    {
        ws2812_set_pixel(dev, i, r, g, b);
    }
}

void ws2812_clear(ws2812_t *dev)
{
    ws2812_fill(dev, 0u, 0u, 0u);
}

bool ws2812_show_async(ws2812_t *dev)
{
    if (dev == NULL)
    {
        return false;
    }

    if (dev->busy)
    {
        return false;
    }

    dev->busy  = true;
    dev->error = false;

    if (!DMAC_ChannelTransfer(dev->dma_ch,
                              (const void *)dev->buf,
                              (const void *)&dev->sercom->SPIM.SERCOM_DATA,
                              (size_t)dev->tx_len))
    {
        dev->busy = false;
        return false;
    }

    return true;
}

bool ws2812_is_busy(const ws2812_t *dev)
{
    return (dev != NULL) && dev->busy;
}

bool ws2812_show(ws2812_t *dev, uint32_t timeout_ms)
{
    uint32_t start;

    if (!ws2812_show_async(dev))
    {
        return false;
    }

    if (dev->now_ms == NULL)
    {
        /* No time source configured - documented as an unbounded spin. */
        while (dev->busy)
        {
            /* wait */
        }
    }
    else
    {
        start = dev->now_ms();

        while (dev->busy)
        {
            /* Unsigned subtraction, so this stays correct across wraparound. */
            if ((dev->now_ms() - start) >= timeout_ms)
            {
                DMAC_ChannelDisable(dev->dma_ch);
                dev->busy  = false;
                dev->error = true;
                return false;
            }
        }
    }

    return !dev->error;
}

/* -- Colour helpers --------------------------------------------------------- */

void ws2812_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t region;
    uint8_t remainder;
    uint8_t p;
    uint8_t q;
    uint8_t t;

    if ((r == NULL) || (g == NULL) || (b == NULL))
    {
        return;
    }

    if (s == 0u)
    {
        *r = v;
        *g = v;
        *b = v;
        return;
    }

    region    = (uint8_t)(h / 43u);
    remainder = (uint8_t)((h - (region * 43u)) * 6u);

    p = (uint8_t)(((uint16_t)v * (255u - s)) >> 8u);
    q = (uint8_t)(((uint16_t)v * (255u - (((uint16_t)s * remainder) >> 8u))) >> 8u);
    t = (uint8_t)(((uint16_t)v * (255u - (((uint16_t)s * (255u - remainder)) >> 8u))) >> 8u);

    switch (region)
    {
        case 0:  *r = v; *g = t; *b = p; break;
        case 1:  *r = q; *g = v; *b = p; break;
        case 2:  *r = p; *g = v; *b = t; break;
        case 3:  *r = p; *g = q; *b = v; break;
        case 4:  *r = t; *g = p; *b = v; break;
        default: *r = v; *g = p; *b = q; break;
    }
}
