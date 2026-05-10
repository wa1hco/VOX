#ifndef VOX_MCU_USB_INIT_H
#define VOX_MCU_USB_INIT_H

/*
 * usb_init.h — board-side bring-up for the STM32G474 USB peripheral.
 *
 * Splits cleanly between "what the chip needs configured" (this file)
 * and "what TinyUSB does on top" (the TinyUSB device task).
 *
 * Caller responsibilities:
 *   1. The PLL must already be running so PLLQ output is at exactly
 *      48 MHz (vox_clock_init_pll144() achieves this).
 *   2. After vox_usb_init() returns, the caller calls tud_init(0) to
 *      bring up TinyUSB on rhport 0, then tud_task() periodically
 *      (or from the USB IRQ — TinyUSB supports both).
 *   3. The USB low-priority IRQ handler must call dcd_int_handler(0).
 *      That linkage is in startup_stm32g474.c.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Configure RCC + clock-source mux + GPIO AF for the USB peripheral.
 * Idempotent. */
void vox_usb_init(void);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_USB_INIT_H */
