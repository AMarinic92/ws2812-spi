# ws2812-spi

WS2812B / SK6812 addressable LED driver for Microchip SAM D5x / E5x, using a
SERCOM in SPI master mode plus a DMAC channel. Developed on an ATSAME51J20A.

Each LED bit is encoded as three SPI bits at 2.4 MHz, so the strip's timing is
produced by the SPI shift register rather than by cycle-counted bit-banging.
The CPU is uninvolved once the DMA starts, and interrupts do not corrupt the
waveform.

- No fixed strip length — you own the buffer, sizing is checked at init
- No hard-coded SERCOM or DMA channel — both are runtime parameters
- Blocking, async, and FreeRTOS-blocking transmit paths
- Optional effects layer that costs nothing when unused

## Requirements

Configure in MCC Melody:

| Peripheral | Setting |
|---|---|
| SERCOM (any) | SPI Master, **2.4 MHz** baud, MSB first, 8-bit character |
| DMAC channel | Trigger: that SERCOM's **TX**, beat size **BYTE**, source increment **on**, destination increment **off** |

SCK is driven but not connected to the strip. SS is unused.

### Wiring

```
MCU MOSI ---> 74AHCT125 (powered at 5 V) ---[330 ohm]---> LED DATA IN
```

Do not substitute a BSS138 or TXB0104 — both are too slow for the 800 kHz
edges. The 74AHCT125 has ~7 ns propagation and gives a full 5 V swing from a
3.3 V input.

## Adding it to an MPLAB X project

```sh
git submodule add https://github.com/AMarinic92/ws2812-spi.git lib/ws2812-spi
```

Then add `lib/ws2812-spi/include` to `compiler.extra-include-directories` in
your project's `.mplab.json` and regenerate.

If your project's file set globs the repository — the default for newer MPLAB
projects is `{"include": "**/*", "exclude": [...]}` — the sources under
`lib/ws2812-spi/src` are picked up automatically and compiled with the
project's own toolchain flags. Nothing else is needed; in particular you do
not need `user.cmake`, and you should not link the target by name, because the
generated executable target carries a random suffix that changes when the
project is regenerated.

For older projects with an explicit file list, add `lib/ws2812-spi/src/*`.

## Usage

```c
#include "ws2812/ws2812.h"

#define LED_COUNT 144

static uint8_t  strip_buf[WS2812_BUF_BYTES(LED_COUNT)];   /* 1346 bytes */
static ws2812_t strip;

void app_init(void)
{
    ws2812_cfg_t cfg = {
        .sercom   = SERCOM1_REGS,
        .dma_ch   = DMAC_CHANNEL_0,
        .buf      = strip_buf,
        .buf_len  = sizeof(strip_buf),
        .num_leds = LED_COUNT,
        .order    = WS2812_ORDER_GRB,
    };

    if (!ws2812_init(&strip, &cfg)) {
        /* buffer too small, or a null/zero argument */
    }
}

void app_frame(void)
{
    ws2812_set_pixel(&strip, 0, 255, 0, 60);
    ws2812_show(&strip, 20);   /* blocks ~4.5 ms at 144 LEDs */
}
```

Staging functions never transmit. Compose the whole frame, then call one of
the show functions once.

### Transmit paths

| Call | Behaviour |
|---|---|
| `ws2812_show(dev, ms)` | Busy-waits until done. Simplest; burns CPU. |
| `ws2812_show_async(dev)` | Returns immediately. Poll `ws2812_is_busy()` or use the `on_complete` hook. |
| `ws2812_rtos_show(dev, ms)` | Blocks the calling task on a notification. CPU stays free. |

`ws2812_show()`'s timeout is only enforced if you supply a `now_ms` time source
in the config; without one it spins indefinitely:

```c
    cfg.now_ms = my_millis;   /* xTaskGetTickCount, SYSTICK_TimerCounterGet, ... */
```

Do not stage pixels while a transfer is in flight — the DMA is reading the
same buffer.

### FreeRTOS

Define `WS2812_ENABLE_FREERTOS` in your toolchain's preprocessor macros;
without it `src/ws2812_freertos.c` compiles to nothing, so the file is safe to
leave in a globbed file set on a bare-metal project.

```c
#include "ws2812/ws2812_freertos.h"

void led_task(void *arg)
{
    uint8_t frame = 0;

    for (;;) {
        ws2812_fx_fire(&strip, frame++, 80);
        ws2812_rtos_show(&strip, 20);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

Requires `configUSE_TASK_NOTIFICATIONS` and
`INCLUDE_xTaskGetCurrentTaskHandle`, both already 1 in the stock MCC config.

Two gotchas worth knowing:

- The DMAC interrupt must sit at or below `configMAX_SYSCALL_INTERRUPT_PRIORITY`
  — a numerically **higher** NVIC priority value. If MCC left the DMAC at
  priority 0, the notify call from the ISR will trip a `configASSERT`.
- This uses task notification index 0. If the calling task already uses index 0
  for something else, they will conflict.

Calling `ws2812_rtos_show()` before the scheduler starts falls back to the
busy-wait path automatically, so it is safe during init.

### Effects

```c
#include "ws2812/ws2812_fx.h"

ws2812_fx_rainbow(&strip, frame++, 80);
ws2812_fx_green_purple(&strip, frame++, 80);
ws2812_fx_fire(&strip, frame++, 80);
ws2812_show(&strip, 20);
```

These stage only — you call show. With `-ffunction-sections` and
`-Wl,--gc-sections` (both on by default in MPLAB projects) the effects you
never reference cost no flash.

`ws2812_hsv_to_rgb()` is exposed for building your own.

## Multiple strips

Supported, as long as each `ws2812_t` gets its own DMA channel. They may share
a SERCOM only if you never transmit on both at once.

## Limits

- One DMA transfer is capped at 65535 bytes by the DMAC block counter, so
  7280 LEDs per strip. `ws2812_init()` rejects anything larger.
- Buffer cost is 9 bytes per LED plus 50 bytes of latch pulse.
- At 2.4 MHz a frame takes about 30 us per LED — roughly 4.5 ms for 144 LEDs,
  which puts the ceiling near 200 fps at that length.
