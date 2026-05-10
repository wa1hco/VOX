/*
 * clock_init.c — STM32G474 PLL bring-up to 144 MHz from HSI16.
 *
 * Sequence (RM0440 §7.2.4 "PLL configuration"):
 *
 *   1. Make sure HSI16 is on and ready (it is by default after reset,
 *      but explicit checks make the code work after a soft re-init).
 *
 *   2. Raise flash wait states to 4 BEFORE the clock goes up.  The
 *      flash interface tolerates more wait states than it needs at any
 *      given clock; it does NOT tolerate too few.  Order matters here.
 *
 *   3. Disable the PLL while we reconfigure it.  Writes to PLLCFGR
 *      while PLLON=1 are ignored; this matters if we ever call this
 *      function more than once (e.g. on a soft reset path).
 *
 *   4. Configure PLL:
 *        source = HSI16  (16 MHz)
 *        /M=4            → VCO input  4 MHz   (must be 4..16 MHz)
 *        ×N=72           → VCO       288 MHz  (must be 96..344 MHz at Range 1 Normal)
 *        /R=2            → SYSCLK    144 MHz
 *        /Q=6            → 48 MHz   (USB-ready, output enabled)
 *
 *   5. Enable PLL, wait for PLLRDY.
 *
 *   6. Switch SYSCLK to PLL via RCC_CFGR.SW, wait for SWS to confirm.
 *
 * AHB / APB1 / APB2 prescalers stay at /1 (their reset default), so
 * HCLK = PCLK1 = PCLK2 = SYSCLK = 144 MHz.  All within G4 limits
 * (170 MHz on each bus).
 */

#include "clock_init.h"
#include "stm32g4_min.h"

uint32_t vox_clock_init_pll144(void)
{
    /* 1. HSI16 on + ready.  Reset-default has HSION=1 already; this is
     *    just defensive in case we're invoked from a non-reset path. */
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) { }

    /* 2. Bump flash wait states to 4 + enable instruction/data caches
     *    + prefetch.  At Range 1 Normal, the table from RM0440 §3.3.3
     *    sets thresholds at 30/60/90/120 MHz; 144 MHz needs 4 WS. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk)
               | FLASH_ACR_LATENCY_VAL(4)
               | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN
               | FLASH_ACR_DCEN;

    /* 3. PLL off while we reconfigure (writes to PLLCFGR are otherwise
     *    silently dropped). */
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }

    /* 4. Configure: HSI16 / 4 = 4 MHz × 72 = 288 MHz VCO, /2 = 144 MHz,
     *    plus PLLQ=6 driving 48 MHz for the USB peripheral that will
     *    be consumed in slice G. */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI16
                 | RCC_PLLCFGR_PLLM_VAL(4)
                 | RCC_PLLCFGR_PLLN_VAL(72)
                 | RCC_PLLCFGR_PLLR_DIV2 | RCC_PLLCFGR_PLLREN
                 | RCC_PLLCFGR_PLLQ_DIV6 | RCC_PLLCFGR_PLLQEN;

    /* 5. Enable + lock. */
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }

    /* 6. Switch SYSCLK source to PLL.  Read-back of SWS to confirm the
     *    switch actually happened — on misconfigured PLLs the hardware
     *    can refuse to switch silently. */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_PLL) { }

    return 144000000U;
}
