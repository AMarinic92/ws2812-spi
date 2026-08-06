/* =============================================================================
 * ws2812.h - WS2812B / SK6812 driver over SERCOM SPI + DMAC
 *
 * Target    : Microchip SAM D5x / E5x (developed on ATSAME51J20A)
 * Toolchain : MPLAB X + MCC Melody, XC32 or ARM-GCC
 *
 * ENCODING SCHEME
 * ---------------
 * Each LED bit is sent as 3 SPI bits at 2.4 MHz (416.7 ns per SPI bit):
 *
 *   LED '1' -> SPI 1-1-0  ->  T1H = 833 ns, T1L = 417 ns  (spec 800+/-150 / 450+/-150)
 *   LED '0' -> SPI 1-0-0  ->  T0H = 417 ns, T0L = 833 ns  (spec 400+/-150 / 850+/-150)
 *
 * 24 LED bits (one pixel) -> 72 SPI bits -> 9 SPI bytes.
 * The buffer tail holds WS2812_RESET_BYTES zero bytes, which keeps MOSI low for
 * ~167 us and satisfies the >50 us latch requirement without a separate delay.
 *
 * REQUIRED MCC CONFIGURATION
 * --------------------------
 *   - A SERCOM in SPI Master mode, baud 2.4 MHz, MSB first, 8-bit character.
 *   - A DMAC channel triggered by that SERCOM's TX, beat size BYTE,
 *     source address increment ON, destination increment OFF.
 *
 * HARDWARE NOTES
 * --------------
 *   MCU MOSI ---> 74AHCT125 (powered at 5 V) ---[330 ohm]---> LED DATA-IN
 *
 *   SCK is driven by the SPI peripheral but is not connected to the LEDs.
 *   SS is unused; leave it as GPIO.
 *
 *   Do not use a BSS138 or TXB0104 level shifter - both are too slow for the
 *   800 kHz edges. The 74AHCT125 has ~7 ns propagation and gives a full 5 V
 *   swing from a 3.3 V input.
 * ============================================================================= */

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

/* Pulls in the MCC DMAC peripheral library, which in turn includes <device.h>
 * for sercom_registers_t. Both projects place src/config/default on the
 * compiler include path, so this relative path resolves unchanged. */
#include "peripheral/dmac/plib_dmac.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -- Buffer sizing ---------------------------------------------------------- */

/** SPI bytes emitted per LED (24 colour bits x 3 SPI bits / 8). */
#define WS2812_BYTES_PER_LED   9u

/** Trailing zero bytes that form the latch/reset pulse (~167 us at 2.4 MHz). */
#define WS2812_RESET_BYTES     50u

/**
 * Bytes of DMA buffer required for a strip of n LEDs.
 * Declare the buffer yourself, e.g.:
 *     static uint8_t buf[WS2812_BUF_BYTES(144)];
 */
#define WS2812_BUF_BYTES(n)    (((uint32_t)(n) * WS2812_BYTES_PER_LED) + WS2812_RESET_BYTES)

/* -- Types ------------------------------------------------------------------ */

/** Wire byte order. WS2812B and SK6812 are GRB; a few clones are RGB. */
typedef enum
{
    WS2812_ORDER_GRB = 0,
    WS2812_ORDER_RGB
} ws2812_order_t;

/**
 * Optional completion hook. Called from DMA interrupt context, so keep it
 * short - typically just give a semaphore or notify a task.
 */
typedef void (*ws2812_done_cb_t)(uintptr_t context);

/**
 * Optional millisecond time source used only by ws2812_show() to bound its
 * wait. Supply xTaskGetTickCount(), SYSTICK_TimerCounterGet(), etc. If left
 * NULL, ws2812_show() spins until the transfer completes with no timeout.
 */
typedef uint32_t (*ws2812_now_ms_t)(void);

typedef struct
{
    sercom_registers_t *sercom;      /**< e.g. SERCOM1_REGS - must be in SPI master mode */
    uint32_t            dma_ch;      /**< e.g. DMAC_CHANNEL_0 - must be TX-triggered by sercom */
    uint8_t            *buf;         /**< caller-owned, at least WS2812_BUF_BYTES(num_leds) */
    uint32_t            buf_len;     /**< size of buf in bytes */
    uint16_t            num_leds;    /**< strip length */
    ws2812_order_t      order;       /**< wire byte order */
    ws2812_done_cb_t    on_complete; /**< optional, may be NULL */
    uintptr_t           context;     /**< passed back to on_complete */
    ws2812_now_ms_t     now_ms;      /**< optional, may be NULL */
} ws2812_cfg_t;

typedef struct
{
    sercom_registers_t *sercom;
    uint32_t            dma_ch;
    uint8_t            *buf;
    uint32_t            tx_len;      /**< bytes actually sent: leds*9 + reset */
    uint16_t            num_leds;
    ws2812_order_t      order;
    ws2812_done_cb_t    on_complete;
    uintptr_t           context;
    ws2812_now_ms_t     now_ms;
    volatile bool       busy;
    volatile bool       error;

    /* Reserved for the optional FreeRTOS layer in ws2812_freertos.c.
     * Leave these alone if you are not using it. */
    ws2812_done_cb_t    notify_isr;
    volatile uintptr_t  waiter;
} ws2812_t;

/* -- API -------------------------------------------------------------------- */

/**
 * Bind a strip to a SERCOM and DMA channel, register the DMA callback, and
 * stage all pixels to black.
 *
 * Call after SYSTEM_Initialize(). Returns false if any argument is invalid or
 * the buffer is too small - check it, this is where sizing mistakes surface.
 *
 * Multiple strips are supported as long as each has its own DMA channel.
 */
bool ws2812_init(ws2812_t *dev, const ws2812_cfg_t *cfg);

/** Stage one pixel. Out-of-range indices are ignored. Does not transmit. */
void ws2812_set_pixel(ws2812_t *dev, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/** Stage every pixel to the same colour. Does not transmit. */
void ws2812_fill(ws2812_t *dev, uint8_t r, uint8_t g, uint8_t b);

/** Stage every pixel to black. Does not transmit. */
void ws2812_clear(ws2812_t *dev);

/**
 * Start the DMA transfer and return immediately. Returns false if a transfer
 * is already in flight or the DMAC rejected the request.
 *
 * Do not stage new pixels until ws2812_is_busy() reports false - the DMA is
 * reading the same buffer.
 */
bool ws2812_show_async(ws2812_t *dev);

/** True while a transfer is in flight. */
bool ws2812_is_busy(const ws2812_t *dev);

/**
 * Start a transfer and block until it finishes.
 *
 * timeout_ms is honoured only if cfg->now_ms was supplied; otherwise this
 * spins indefinitely. On timeout the DMA channel is disabled and false is
 * returned. Also returns false if the transfer could not be started or the
 * DMAC reported an error.
 *
 * Note this busy-waits. At 2.4 MHz a 144-LED frame takes ~4.5 ms, which is a
 * long time to hold a CPU under an RTOS - prefer ws2812_show_async() plus the
 * on_complete hook there.
 */
bool ws2812_show(ws2812_t *dev, uint32_t timeout_ms);

/** HSV to RGB, all channels 0-255. Exposed for building custom effects. */
void ws2812_hsv_to_rgb(uint8_t h, uint8_t s, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_H */
