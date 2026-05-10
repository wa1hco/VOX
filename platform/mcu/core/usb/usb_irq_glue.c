/*
 * usb_irq_glue.c — bridge from the chip's USB_LP_IRQHandler to
 * TinyUSB's device-driver entry point.
 *
 * The vector-table slot is set up in startup_stm32g474.c with a weak
 * alias to Default_Handler.  This file provides a strong override that
 * just dispatches to TinyUSB.
 */

#include "tusb.h"

void USB_LP_IRQHandler(void)
{
    tud_int_handler(0);
}
