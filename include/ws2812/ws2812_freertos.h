/* SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* =============================================================================
 * ws2812_freertos.h - optional FreeRTOS blocking path for the ws2812 driver
 *
 * ws2812_show() busy-waits, which burns roughly 4.5 ms of CPU per frame at
 * 144 LEDs. This layer blocks the calling task on a direct-to-task
 * notification instead, so the CPU is free for other work while the DMA runs.
 *
 * ENABLING
 * --------
 * src/ws2812_freertos.c compiles to nothing unless WS2812_ENABLE_FREERTOS is
 * defined, so it is safe to leave in a glob-based file set on a bare-metal
 * project. Define it in your toolchain's preprocessor macros to switch on.
 *
 * REQUIREMENTS
 * ------------
 *   configUSE_TASK_NOTIFICATIONS      1
 *   INCLUDE_xTaskGetCurrentTaskHandle 1
 *
 * The DMAC interrupt must run at or below configMAX_SYSCALL_INTERRUPT_PRIORITY
 * (that is, a numerically higher NVIC priority value), otherwise the notify
 * call from the ISR will trip a configASSERT. If you left the DMAC at NVIC
 * priority 0 in MCC, raise the number.
 *
 * This uses task notification index 0, the same one used by ulTaskNotifyTake
 * elsewhere. If the calling task already relies on index 0 for something else,
 * that will conflict.
 * ============================================================================= */

#ifndef WS2812_FREERTOS_H
#define WS2812_FREERTOS_H

#include "ws2812/ws2812.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Transmit the staged frame and block the calling task until the DMA
 * completes or timeout_ms elapses.
 *
 * Returns true on success. On timeout the DMA channel is disabled and false
 * is returned. If called before the scheduler is running, this transparently
 * falls back to the busy-wait ws2812_show() so it is safe during init.
 *
 * A user on_complete callback, if configured, still fires from ISR context.
 */
bool ws2812_rtos_show(ws2812_t *dev, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_FREERTOS_H */
