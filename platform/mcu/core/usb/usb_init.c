/*
 * usb_init.c — STM32G474 USB peripheral bring-up.
 *
 * Three pieces:
 *   1. RCC: route the USB peripheral clock from PLLQ (48 MHz exact),
 *      and enable the USB peripheral's APB1 clock gate.
 *   2. GPIO: PA11/PA12 as USB DM/DP — alternate function AF10.
 *   3. NVIC: enable the low-priority USB IRQ so TinyUSB's
 *      dcd_int_handler() runs on transfer-complete events.
 *
 * The VBUS sensing peripheral (USB_BCDR.DPPU pull-up) is taken care of
 * by TinyUSB's stm32_fsdev driver in tud_init().
 *
 * Pre-condition: the PLL is running with PLLQ enabled (= 48 MHz on the
 * PLLQ output).  This is set up by vox_clock_init_pll144().
 */

#include "usb_init.h"
#include "stm32g4_min.h"

#define USB_AF_PA11_DM  10U
#define USB_AF_PA12_DP  10U

void vox_usb_init(void)
{
    /* 1. Route USB peripheral clock from PLLQ.
     *    RCC_CCIPR.CLK48SEL = 0b10 selects PLL48M1CLK (= PLLQ output,
     *    48 MHz from our PLL config).  Default 0b00 (HSI48) doesn't
     *    apply to G474, which has no HSI48. */
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_CLK48SEL_Msk)
               | (0x2U << RCC_CCIPR_CLK48SEL_Pos);

    /* 2. Enable USB peripheral clock on APB1. */
    RCC->APB1ENR1 |= RCC_APB1ENR1_USBEN;

    /* 3. GPIOA already has its clock enabled by the caller; we only
     *    need to set PA11/PA12 to alternate function mode AF10. */
    GPIO_FIELD2_SET(GPIOA->MODER, 11, GPIO_MODE_AF);
    GPIO_FIELD2_SET(GPIOA->MODER, 12, GPIO_MODE_AF);
    GPIO_AF_SET(GPIOA, 11, USB_AF_PA11_DM);
    GPIO_AF_SET(GPIOA, 12, USB_AF_PA12_DP);

    /* High-speed (50 MHz) drive on the USB pins — recommended for
     * full-speed signaling integrity. */
    GPIO_FIELD2_SET(GPIOA->OSPEEDR, 11, 0x3U);
    GPIO_FIELD2_SET(GPIOA->OSPEEDR, 12, 0x3U);

    /* 4. Enable the low-priority USB IRQ.  USB_LP_IRQn is the standard
     *    name in CMSIS Device G4.  TinyUSB's dcd_int_handler(0) is
     *    invoked from our USB_LP_IRQHandler in startup_stm32g474.c. */
    NVIC_SetPriority(USB_LP_IRQn, 1);
    NVIC_EnableIRQ(USB_LP_IRQn);
}
