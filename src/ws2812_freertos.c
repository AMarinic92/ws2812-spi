/* SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* =============================================================================
 * ws2812_freertos.c - optional FreeRTOS blocking path
 * Compiles to nothing unless WS2812_ENABLE_FREERTOS is defined.
 * ============================================================================= */

#ifdef WS2812_ENABLE_FREERTOS

#include "ws2812/ws2812_freertos.h"

#include "FreeRTOS.h"
#include "task.h"

/* Runs in DMA interrupt context. Releases the task parked in
 * ws2812_rtos_show(). */
static void ws2812_rtos_notify(uintptr_t context)
{
    ws2812_t     *dev = (ws2812_t *)context;
    BaseType_t    higher_priority_task_woken = pdFALSE;
    TaskHandle_t  waiter;

    if (dev == NULL)
    {
        return;
    }

    waiter = (TaskHandle_t)dev->waiter;

    if (waiter == NULL)
    {
        return;
    }

    dev->waiter = 0u;

    vTaskNotifyGiveFromISR(waiter, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

bool ws2812_rtos_show(ws2812_t *dev, uint32_t timeout_ms)
{
    TaskHandle_t self;

    if (dev == NULL)
    {
        return false;
    }

    self = xTaskGetCurrentTaskHandle();

    /* No scheduler yet (called from main() or an init hook) - there is no task
     * to park, so fall back to the busy-wait path. */
    if (self == NULL)
    {
        return ws2812_show(dev, timeout_ms);
    }

    if (ws2812_is_busy(dev))
    {
        return false;
    }

    dev->notify_isr = ws2812_rtos_notify;

    /* Drop any stale notification so a previous timeout cannot satisfy this
     * wait immediately. */
    (void)ulTaskNotifyTake(pdTRUE, 0u);

    /* Publish the waiter before starting the DMA. Notifications latch, so a
     * transfer that completes before we reach ulTaskNotifyTake() below is
     * still delivered rather than lost. */
    dev->waiter = (uintptr_t)self;

    if (!ws2812_show_async(dev))
    {
        dev->waiter = 0u;
        return false;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)) == 0u)
    {
        DMAC_ChannelDisable(dev->dma_ch);
        dev->waiter = 0u;
        dev->busy   = false;
        dev->error  = true;
        return false;
    }

    return !dev->error;
}

#else  /* WS2812_ENABLE_FREERTOS */

/* ISO C forbids an empty translation unit. */
typedef int ws2812_freertos_not_enabled_t;

#endif /* WS2812_ENABLE_FREERTOS */
