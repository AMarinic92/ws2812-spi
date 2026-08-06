# Migrating from Coffin-Reborn's `neopixel.c`

This library started as `src/neopixel.c` / `src/neopixel.h` in
[Coffin-Reborn](https://github.com/AMarinic92/Coffin-Reborn). The encoding
scheme, the timing, and the effects are the same code — what changed is
everything that assumed there was exactly one strip, on one SERCOM, in one
project.

## Why it could not be reused as-is

Three things were baked in at compile time:

| Was | Problem |
|---|---|
| `SERCOM1_REGS->SPIM.SERCOM_DATA` | The SPI instance was hard-coded in the `.c` file |
| `#define DMAC_CHANNEL_NEO DMAC_CHANNEL_0` | So was the DMA channel |
| `#define NUM_LEDS 144` driving `static uint8_t neo_buf[]` | One strip length per firmware image, sized at compile time |

Plus `#include "definitions.h"`, MCC's umbrella header, which drags in the
whole generated project.

## API mapping

| Old | New |
|---|---|
| `NeoPixel_Init()` | `ws2812_init(&dev, &cfg)` — returns `bool` |
| `NeoPixel_SetPixel(i, r, g, b)` | `ws2812_set_pixel(&dev, i, r, g, b)` |
| `NeoPixel_Clear()` | `ws2812_clear(&dev)` |
| `NeoPixel_Show()` | `ws2812_show(&dev, timeout_ms)` — returns `bool` |
| `NeoPixel_Rainbow(o, b)` | `ws2812_fx_rainbow(&dev, o, b)` **+ a show call** |
| `NeoPixel_GreenPurple(o, b)` | `ws2812_fx_green_purple(&dev, o, b)` **+ a show call** |
| `NeoPixel_Fire(o, b)` | `ws2812_fx_fire(&dev, o, b)` **+ a show call** |
| `NeoPixel_HSVtoRGB(...)` | `ws2812_hsv_to_rgb(...)` |
| `NUM_LEDS` | `cfg.num_leds`, runtime |
| `DMAC_CHANNEL_NEO` | `cfg.dma_ch`, runtime |
| — | `ws2812_fill(&dev, r, g, b)` |
| — | `ws2812_show_async(&dev)` / `ws2812_is_busy(&dev)` |
| — | `ws2812_rtos_show(&dev, timeout_ms)` |

### The one behavioural change that will bite

The old effects transmitted for you — `NeoPixel_Fire()` ended with
`NeoPixel_Show()`. The new ones **only stage pixels**:

```c
/* old */
NeoPixel_Fire(frame++, 80);

/* new */
ws2812_fx_fire(&strip, frame++, 80);
ws2812_show(&strip, 20);            /* or ws2812_rtos_show(...) */
```

This is what lets you compose a frame from several effects, or hand the
transmit to the RTOS path, instead of the effect deciding for you.

### Before / after

```c
/* old - Coffin-Reborn */
#define NUM_LEDS 144

NeoPixel_Init();
NeoPixel_SetPixel(0, 255, 0, 60);
NeoPixel_Show();

/* new */
static uint8_t  buf[WS2812_BUF_BYTES(144)];
static ws2812_t strip;

ws2812_cfg_t cfg = {
    .sercom   = SERCOM1_REGS,
    .dma_ch   = DMAC_CHANNEL_0,
    .buf      = buf,
    .buf_len  = sizeof(buf),
    .num_leds = 144,
    .order    = WS2812_ORDER_GRB,
};
ws2812_init(&strip, &cfg);
ws2812_set_pixel(&strip, 0, 255, 0, 60);
ws2812_show(&strip, 20);
```

## Bugs fixed in the move

- **Strips over 255 LEDs never worked.** `NeoPixel_SetPixel` took a `uint8_t`
  index, and every loop used `uint8_t i`. At `NUM_LEDS > 255` the loops in
  `Clear`, `Rainbow`, `GreenPurple` and `Fire` never terminated, and pixels
  256+ were unreachable. Indices and counters are `uint16_t` now.
- **No buffer validation.** The buffer was internal, so a mismatch was
  impossible — but now that you own it, `ws2812_init()` rejects one that is
  too small rather than letting the DMA run off the end.
- **`NeoPixel_Show()` could hang forever.** `while (!tx_done)` had no timeout.
  `ws2812_show()` takes one, and disables the channel if it expires.
- **`lerp8by16()` relied on unsigned wraparound.** `(uint32_t)(b - a)` with
  `b < a` produced a huge value that only came out right because the result
  truncated back to `uint8_t`. It was correct, but by accident. Now it
  branches on the sign explicitly.

## Also new

- `WS2812_ORDER_RGB` for clone strips that are not GRB.
- Multiple strips, as long as each gets its own DMA channel.
- FreeRTOS path (`ws2812_rtos_show`) that blocks on a task notification
  instead of busy-waiting. The old `Show()` spun for ~4.5 ms per frame at
  144 LEDs.
- Comments are ASCII. The originals had been mangled to `?` characters.

## MCC configuration

Unchanged from what Coffin-Reborn already uses — SERCOM1 SPI master at
2.4 MHz and DMAC channel 0. For reference, the settings that matter in the
generated `plib_dmac.c`:

```c
DMAC_CHCTRLA_TRIGACT(2U)   /* BURST - one beat per SERCOM TX request */
DMAC_CHCTRLA_TRIGSRC(7U)   /* SERCOM1_TX                             */
DMAC_BTCTRL_BEATSIZE_BYTE  /* not WORD                               */
DMAC_BTCTRL_SRCINC_Msk     /* source increments, destination does NOT */
```

If `DSTINC` is set, or the beat size is `WORD`, or `TRIGSRC` is 0, the channel
is still at MCC defaults and the strip will not light correctly.
