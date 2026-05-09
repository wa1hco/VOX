/*
 * systick.c — Cortex-M SysTick driver, 1 ms tick.
 *
 * SysTick is part of the Cortex-M core (not an STM32 peripheral), so
 * this code is portable across any Cortex-M0+/M3/M4/M7 — including the
 * G474.  Registers live at 0xE000E010 per ARMv7-M architecture ref.
 *
 * Interaction with the rest of the firmware:
 *   - SysTick_Handler is declared __weak in startup_stm32g474.c, so
 *     this file's strong override wins at link time.
 *   - g_systick_ms is incremented in interrupt context; readers
 *     read it without a lock.  On Cortex-M a 32-bit aligned read is
 *     atomic, so the worst that happens is a stale value for one tick.
 */

#include "systick.h"

/* SCB / SysTick register block on Cortex-M (ARMv7-M). */
#define SYST_CSR    (*(volatile uint32_t *)0xE000E010UL)  /* control & status */
#define SYST_RVR    (*(volatile uint32_t *)0xE000E014UL)  /* reload value     */
#define SYST_CVR    (*(volatile uint32_t *)0xE000E018UL)  /* current value    */

#define SYST_CSR_ENABLE     (1U << 0)
#define SYST_CSR_TICKINT    (1U << 1)
#define SYST_CSR_CLKSOURCE  (1U << 2)   /* 1 = processor clock */

#define SYST_RVR_MAX        0x00FFFFFFu /* 24-bit reload */

static volatile uint32_t g_systick_ms;

void SysTick_Handler(void)
{
    g_systick_ms++;
}

int vox_systick_init(uint32_t sysclk_hz)
{
    uint32_t reload = (sysclk_hz / 1000u) - 1u;
    if (reload > SYST_RVR_MAX)
        return -1;     /* core clock too high for 1 ms at 24-bit reload */

    SYST_CSR = 0;                 /* disable while configuring */
    SYST_RVR = reload;
    SYST_CVR = 0;                 /* clear current value */
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
    return 0;
}

uint32_t vox_systick_now_ms(void)
{
    return g_systick_ms;
}

void vox_delay_ms(uint32_t ms)
{
    uint32_t start = g_systick_ms;
    while ((g_systick_ms - start) < ms)
        __asm__ volatile ("wfi");
}
