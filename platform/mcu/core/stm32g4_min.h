#ifndef STM32G4_MIN_H
#define STM32G4_MIN_H

/*
 * stm32g4_min.h — Minimal STM32G474 register definitions.
 *
 * This is a hand-written stand-in for the official ST CMSIS Device
 * header (stm32g474xx.h).  It defines only the peripherals we currently
 * touch:
 *
 *   - RCC      reset & clock control
 *   - GPIOA    LEDs, UART, ADC, OPAMP pins
 *   - GPIOB    LEDs, I2C, OPAMP pins
 *   - USART1   debug UART on the custom board (PA9/PA10)
 *   - USART2   debug UART on the Nucleo via ST-Link VCP (PA2/PA3)
 *
 * When we add ADC/DMA/OPAMP/USB drivers we should drop in the full ST
 * CMSIS Device header pack (stm32g474xx.h + core_cm4.h, ~500KB) and
 * delete this file.  Until then, this lets us read register-level code
 * in a single ~150-line file.
 *
 * All addresses are from RM0440 (STM32G4 reference manual) chapter 2
 * "Memory and bus architecture" and the per-peripheral chapters.
 */

#include <stdint.h>

/* Register access type — 32-bit volatile. */
#define __IO volatile uint32_t

/* ---------- Reset and Clock Control (RCC) ----------------------------- */

typedef struct {
    __IO CR;            /* 0x00 clock control                            */
    __IO ICSCR;         /* 0x04 internal clock sources calibration       */
    __IO CFGR;          /* 0x08 clock configuration                      */
    __IO PLLCFGR;       /* 0x0C PLL configuration                        */
    __IO _RESERVED0[2]; /* 0x10                                          */
    __IO CIER;          /* 0x18 clock interrupt enable                   */
    __IO CIFR;          /* 0x1C clock interrupt flag                     */
    __IO CICR;          /* 0x20 clock interrupt clear                    */
    __IO _RESERVED1;    /* 0x24                                          */
    __IO AHB1RSTR;      /* 0x28                                          */
    __IO AHB2RSTR;      /* 0x2C                                          */
    __IO AHB3RSTR;      /* 0x30                                          */
    __IO _RESERVED2;    /* 0x34                                          */
    __IO APB1RSTR1;     /* 0x38                                          */
    __IO APB1RSTR2;     /* 0x3C                                          */
    __IO APB2RSTR;      /* 0x40                                          */
    __IO _RESERVED3;    /* 0x44                                          */
    __IO AHB1ENR;       /* 0x48 AHB1 peripheral clock enable             */
    __IO AHB2ENR;       /* 0x4C AHB2 peripheral clock enable (GPIO ports)*/
    __IO AHB3ENR;       /* 0x50                                          */
    __IO _RESERVED4;    /* 0x54                                          */
    __IO APB1ENR1;      /* 0x58 APB1 peripheral clock enable 1 (USART2)  */
    __IO APB1ENR2;      /* 0x5C                                          */
    __IO APB2ENR;       /* 0x60 APB2 peripheral clock enable (USART1)    */
} RCC_TypeDef;

#define RCC_BASE          0x40021000UL
#define RCC               ((RCC_TypeDef *)RCC_BASE)

/* RCC->AHB2ENR bits */
#define RCC_AHB2ENR_GPIOAEN  (1U << 0)
#define RCC_AHB2ENR_GPIOBEN  (1U << 1)
#define RCC_AHB2ENR_GPIOCEN  (1U << 2)

/* RCC->APB1ENR1 bits */
#define RCC_APB1ENR1_USART2EN (1U << 17)

/* RCC->APB2ENR bits */
#define RCC_APB2ENR_SYSCFGEN (1U << 0)
#define RCC_APB2ENR_USART1EN (1U << 14)

/* ---------- General Purpose I/O (GPIO) -------------------------------- */

typedef struct {
    __IO MODER;         /* 0x00 mode (00 input, 01 output, 10 AF, 11 analog) */
    __IO OTYPER;        /* 0x04 output type (0 push-pull, 1 open-drain)  */
    __IO OSPEEDR;       /* 0x08 output speed                             */
    __IO PUPDR;         /* 0x0C pull-up/pull-down                        */
    __IO IDR;           /* 0x10 input data                               */
    __IO ODR;           /* 0x14 output data                              */
    __IO BSRR;          /* 0x18 bit set/reset (atomic GPIO writes)       */
    __IO LCKR;          /* 0x1C lock                                     */
    __IO AFR[2];        /* 0x20 alternate function low (pins 0..7)
                                 alternate function high (pins 8..15)    */
    __IO BRR;           /* 0x28 bit reset                                */
} GPIO_TypeDef;

#define GPIOA_BASE        0x48000000UL
#define GPIOB_BASE        0x48000400UL
#define GPIOC_BASE        0x48000800UL
#define GPIOA             ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB             ((GPIO_TypeDef *)GPIOB_BASE)
#define GPIOC             ((GPIO_TypeDef *)GPIOC_BASE)

/* MODER values (per pin, 2 bits). */
#define GPIO_MODE_INPUT   0x0U
#define GPIO_MODE_OUTPUT  0x1U
#define GPIO_MODE_AF      0x2U
#define GPIO_MODE_ANALOG  0x3U

/* ---------- USART ----------------------------------------------------- */

typedef struct {
    __IO CR1;           /* 0x00 control 1                                */
    __IO CR2;           /* 0x04 control 2                                */
    __IO CR3;           /* 0x08 control 3                                */
    __IO BRR;           /* 0x0C baud rate                                */
    __IO GTPR;          /* 0x10 guard time and prescaler                 */
    __IO RTOR;          /* 0x14 receiver timeout                         */
    __IO RQR;           /* 0x18 request                                  */
    __IO ISR;           /* 0x1C interrupt and status                     */
    __IO ICR;           /* 0x20 interrupt clear                          */
    __IO RDR;           /* 0x24 receive data                             */
    __IO TDR;           /* 0x28 transmit data                            */
} USART_TypeDef;

#define USART1_BASE       0x40013800UL
#define USART2_BASE       0x40004400UL
#define USART1            ((USART_TypeDef *)USART1_BASE)
#define USART2            ((USART_TypeDef *)USART2_BASE)

/* USART->CR1 bits */
#define USART_CR1_UE      (1U << 0)   /* USART enable                     */
#define USART_CR1_RE      (1U << 2)   /* receive enable                   */
#define USART_CR1_TE      (1U << 3)   /* transmit enable                  */

/* USART->ISR bits */
#define USART_ISR_TXE     (1U << 7)   /* transmit data register empty (TXFNF) */
#define USART_ISR_TC      (1U << 6)   /* transmission complete            */

/* ---------- Helper macros -------------------------------------------- */

/* Set 2-bit field for `pin` (0..15) within a 32-bit GPIO register. */
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

#endif /* STM32G4_MIN_H */
