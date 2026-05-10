#ifndef STM32G4_MIN_H
#define STM32G4_MIN_H

/*
 * stm32g4_min.h — thin shim over the official CMSIS device header.
 *
 * What this file *was*: a hand-written subset of register definitions
 * for RCC/GPIO/USART/FLASH so we could compile minimal firmware
 * without the CMSIS pack.  That worked while we only touched 4
 * peripherals, but doesn't scale to USB / ADC / OPAMP / DMA.
 *
 * What this file *is now*: a single place we #include the CMSIS
 * device header from, plus a handful of convenience macros that are
 * useful across the firmware but absent from CMSIS itself.  Everything
 * peripheral-shaped — RCC_TypeDef, GPIOA, FLASH, USART2, etc. — comes
 * from <stm32g474xx.h> via the cmsis_stm32g4 INTERFACE library.
 */

#include <stm32g474xx.h>

#include <stdint.h>

/* ---------- GPIO MODER values (per pin, 2 bits) ---------------------- */
#define GPIO_MODE_INPUT   0x0U
#define GPIO_MODE_OUTPUT  0x1U
#define GPIO_MODE_AF      0x2U
#define GPIO_MODE_ANALOG  0x3U

/* ---------- VOX-side port aliases for the encoded pin handles -------- */
/* vox_mcu_pins.h defines VOX_PIN(port, pin) where port is one of
 * VOX_GPIO_PORT_A..H (small integers).  We don't pull those headers
 * from here to avoid include-order coupling; the boards' main.c maps
 * VOX_GPIO_PORT_x to the actual GPIOA/GPIOB/... CMSIS pointer. */

/* ---------- Convenience macros not provided by CMSIS ----------------- */
/* Set the 2-bit field for `pin` (0..15) within a 32-bit GPIO register. */
#define GPIO_FIELD2_SET(reg, pin, val) do { \
    uint32_t _shift = (uint32_t)(pin) * 2u; \
    (reg) = ((reg) & ~(0x3u << _shift)) | (((uint32_t)(val) & 0x3u) << _shift); \
} while (0)

/* Set 4-bit alternate-function field for `pin` (0..15) in AFR[0]/AFR[1]. */
#define GPIO_AF_SET(gpio, pin, af) do { \
    uint32_t _idx = ((uint32_t)(pin) >> 3) & 1u;          /* AFR[0] for 0..7, AFR[1] for 8..15 */ \
    uint32_t _shift = ((uint32_t)(pin) & 0x7u) * 4u; \
    (gpio)->AFR[_idx] = ((gpio)->AFR[_idx] & ~(0xFu << _shift)) | (((uint32_t)(af) & 0xFu) << _shift); \
} while (0)

/* ---------- PLL field setters (CMSIS provides _Pos/_Msk only) -------- */
/*
 * CMSIS provides PLL bit positions and masks but no convenience setters
 * for "PLLM=4" → field value (M-1).  These match the encoding used by
 * clock_init.c.  Using them keeps the PLL config call site readable
 * without obscuring what value lands in which field.
 */
/* CMSIS doesn't define a value-helper for "PLLSRC = HSI16"; it has
 * RCC_PLLCFGR_PLLSRC_HSI as the equivalent (0x2 << Pos).  We alias for
 * readability of the bring-up sequence. */
#define RCC_PLLCFGR_PLLSRC_HSI16  RCC_PLLCFGR_PLLSRC_HSI

/* PLLM/PLLN convenience setters: CMSIS provides _Pos and _Msk but not
 * a "give me the field value for M=4" helper.  These match the
 * encoding rules in RM0440 §7.4.4. */
#define RCC_PLLCFGR_PLLM_VAL(m)   ((((uint32_t)(m) - 1U) & 0xFU) << RCC_PLLCFGR_PLLM_Pos)
#define RCC_PLLCFGR_PLLN_VAL(n)   (((uint32_t)(n) & 0x7FU) << RCC_PLLCFGR_PLLN_Pos)

/* FLASH_ACR latency value setter (CMSIS has the mask, not a setter). */
#define FLASH_ACR_LATENCY_VAL(n)  ((uint32_t)(n) & FLASH_ACR_LATENCY_Msk)

/* CMSIS provides RCC_CFGR_SW_PLL / RCC_CFGR_SWS_PLL but NOT the
 * PLLR_DIVn / PLLQ_DIVn helpers — those are our shim.  Encoding:
 * 00=/2, 01=/4, 10=/6, 11=/8 in the corresponding 2-bit field. */
#define RCC_PLLCFGR_PLLR_DIV2     (0x0U << RCC_PLLCFGR_PLLR_Pos)
#define RCC_PLLCFGR_PLLR_DIV4     (0x1U << RCC_PLLCFGR_PLLR_Pos)
#define RCC_PLLCFGR_PLLR_DIV6     (0x2U << RCC_PLLCFGR_PLLR_Pos)
#define RCC_PLLCFGR_PLLR_DIV8     (0x3U << RCC_PLLCFGR_PLLR_Pos)
#define RCC_PLLCFGR_PLLQ_DIV2     (0x0U << RCC_PLLCFGR_PLLQ_Pos)
#define RCC_PLLCFGR_PLLQ_DIV4     (0x1U << RCC_PLLCFGR_PLLQ_Pos)
#define RCC_PLLCFGR_PLLQ_DIV6     (0x2U << RCC_PLLCFGR_PLLQ_Pos)
#define RCC_PLLCFGR_PLLQ_DIV8     (0x3U << RCC_PLLCFGR_PLLQ_Pos)

#endif /* STM32G4_MIN_H */
