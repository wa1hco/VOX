#ifndef VOX_MCU_CLOCK_INIT_H
#define VOX_MCU_CLOCK_INIT_H

/*
 * clock_init.h — STM32G474 clock-tree bring-up.
 *
 * Currently provides exactly one configuration: 144 MHz SYSCLK from
 * HSI16 via PLL.  Picked for two reasons:
 *
 *   1. Stays inside Range 1 *Normal* voltage scaling (Boost mode is
 *      only required above 150 MHz on G474).  No PWR_CR5.R1MODE
 *      manipulation, lower bring-up risk for a first PLL slice.
 *
 *   2. Same VCO (288 MHz) divides cleanly to both:
 *        - 144 MHz SYSCLK         (PLLR /2)
 *        - 48 MHz USB clock       (PLLQ /6)
 *      so when slice G adds USB CDC-ACM we won't need to retune the
 *      PLL.  PLLQ is already enabled here; the USB peripheral just has
 *      to be told to use it.
 *
 * The function returns the achieved SYSCLK frequency in Hz so callers
 * can recompute UART baud rates, SysTick reload values, etc., from a
 * single source of truth.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure flash latency, set up PLL, switch SYSCLK to PLL output.
 * Idempotent — safe to call multiple times.  Returns the new SYSCLK. */
uint32_t vox_clock_init_pll144(void);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_CLOCK_INIT_H */
