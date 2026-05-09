#ifndef VOX_BOARDS_STM32G474_NUCLEO_BOARD_PINS_H
#define VOX_BOARDS_STM32G474_NUCLEO_BOARD_PINS_H

#include "vox_mcu_pins.h"

/*
 * STM32G474RET6 (LQFP64) on the NUCLEO-G474RE dev board, with the VOX
 * prototype shield wired to mirror the HCO custom-board pin assignment.
 *
 * GPIO signal assignments live in boards/common/hco_pin_assignment.h
 * and are shared verbatim with the LQFP48 custom board so MCU code is
 * identical across the two targets.  This file owns:
 *   - the LQFP64 pin-table comment (different physical pin numbers
 *     for the same GPIOs vs the LQFP48 package)
 *   - notes specific to the Nucleo dev board (LD2 conflict, ST-Link
 *     VCP routing, clock options, programmer/debugger)
 *
 * Nucleo-specific hardware notes
 * ------------------------------
 * LD2 conflict on PA5: The Nucleo's green USER LED (LD2) is wired to PA5,
 *   which the HCO assignment uses for VOX_PIN_LED_MIC.  This is benign:
 *   LD2 just mirrors the LED_MIC state.  Nothing to fix in firmware.
 *
 * USER button B1 on PC13: not used by VOX.  Free for ad-hoc bring-up
 *   (e.g. force-PTT, mute toggle) if you want to wire one in temporarily.
 *
 * ST-Link VCP on USART2 (PA2/PA3): the Nucleo routes PA2/PA3 to the
 *   on-board ST-Link's virtual COM port at /dev/ttyACM0.  The HCO
 *   assignment puts the VOX debug UART on USART1 (PA9/PA10) instead, so
 *   the shield must bring its own USB-serial converter on those pins —
 *   the ST-Link VCP is not used.  If you want quick printf-debug without
 *   extra hardware during bring-up, temporarily switch DBG_TX/DBG_RX to
 *   USART2 PA2/PA3 in board.c (do not change hco_pin_assignment.h).
 *
 * Clock plan: Nucleo has solder bridges (SB16/SB50) that route the
 *   ST-Link MCO (8 MHz HSE) to PF0; SB54 ties PF1 to GND.  Two viable
 *   options, neither requires changes to board_pins.h:
 *     1) HSE bypass mode using the 8 MHz MCO from ST-Link → PLL.  Bring-up
 *        path; rock-stable, no calibration needed.
 *     2) HSI16 → PLL with USB CRS trim, identical to the custom board.
 *        Use this once you've validated the VOX firmware works the same
 *        way the custom board will run it.
 *
 * Power: VDDA/VREF+ on the Nucleo are tied to VDD (3V3) by default.  No
 *   separate analog rail — adequate for VOX's audio-band ADC needs but
 *   noisier than the custom board's filtered VDDA, expect ~1 LSB more
 *   ADC noise floor.
 *
 * Pin | Name       | Function | Free | Power  | Comment                       |
 *-----|------------|----------|------|--------|-------------------------------|
 *   1 | VBAT       |          |      | 3V3    |                               |
 *   2 | PC13       |          |      |        | Nucleo USER button B1         |
 *   3 | PC14       |          |      |        | LSE OSC32_IN (open by default)|
 *   4 | PC15       |          |      |        | LSE OSC32_OUT                 |
 *   5 | PF0        |          |      |        | HSE_IN — ST-Link MCO via SB16/50|
 *   6 | PF1        |          |      |        | HSE_OUT — tied GND via SB54   |
 *   7 | VSS        |          |      | GND    |                               |
 *   8 | VDD        |          |      | 3V3    |                               |
 *   9 | NRST       |          |      |        |                               |
 *  10 | PC0        |          |      |        | Arduino A0                    |
 *  11 | PC1        |          |      |        | Arduino A1                    |
 *  12 | PC2        |          |      |        | Arduino A2                    |
 *-----|------------|----------|------|--------|-------------------------------|
 *  13 | PC3        |          |      |        | Arduino A3                    |
 *  14 | PA0        | MIC_ADC  |      |        | Shield mic input              |
 *  15 | PA1        | RX_ADC   |      |        | Shield rx-reference input     |
 *  16 | PA2        |          |      |        | USART2_TX → ST-Link VCP       |
 *  17 | PA3        |          |      |        | USART2_RX → ST-Link VCP       |
 *  18 | VSS        |          |      | GND    |                               |
 *  19 | VDD        |          |      | 3V3    |                               |
 *  20 | PA4        | PTT_OUT  |      |        |                               |
 *  21 | PA5        | LED_MIC  |      |        | also drives Nucleo LD2 (green)|
 *  22 | PA6        | LED_RX   |      |        |                               |
 *  23 | PA7        | LED_VAD  |      |        |                               |
 *  24 | PC4        |          |      |        |                               |
 *-----|------------|----------|------|--------|-------------------------------|
 *  25 | PC5        |          |      |        |                               |
 *  26 | PB0        | LED_AEC  |      |        |                               |
 *  27 | PB1        | LED_PTT  |      |        |                               |
 *  28 | PB2        |          |      |        |                               |
 *  29 | PB10       | I2C_SCL  |      |        | I2C2_SCL (cross-instance)     |
 *  30 | PB11       |          |      |        |                               |
 *  31 | VSS        |          |      | GND    |                               |
 *  32 | VDD        |          |      | 3V3    |                               |
 *  33 | PB12       |          |      |        |                               |
 *  34 | PB13       |          |      |        |                               |
 *  35 | PB14       |          |      |        |                               |
 *  36 | PB15       |          |      |        |                               |
 *-----|------------|----------|------|--------|-------------------------------|
 *  37 | PC6        |          |      |        |                               |
 *  38 | PC7        |          |      |        |                               |
 *  39 | PC8        |          |      |        |                               |
 *  40 | PC9        |          |      |        |                               |
 *  41 | PA8        |          |      |        |                               |
 *  42 | PA9        | DBG_TX   |      |        | USART1_TX                     |
 *  43 | PA10       | DBG_RX   |      |        | USART1_RX                     |
 *  44 | PA11       |          |      |        | USB_DM (Nucleo USB user, n/c) |
 *  45 | PA12       |          |      |        | USB_DP (Nucleo USB user, n/c) |
 *  46 | PA13       | SWDIO    |      |        | (also USART3_RX alt fn)       |
 *  47 | PA14       | SWCLK    |      |        |                               |
 *  48 | PA15       |          |      |        |                               |
 *-----|------------|----------|------|--------|-------------------------------|
 *  49 | PC10       |          |      |        |                               |
 *  50 | PC11       |          |      |        |                               |
 *  51 | PC12       |          |      |        |                               |
 *  52 | PD2        |          |      |        |                               |
 *  53 | PB3        |          |      |        |                               |
 *  54 | PB4        |          |      |        |                               |
 *  55 | PB5        |          |      |        |                               |
 *  56 | PB6        |          |      |        |                               |
 *  57 | PB7        |          |      |        |                               |
 *  58 | PB8/BOOT0  |          |      |        |                               |
 *  59 | PB9        | I2C_SDA  |      |        | I2C1_SDA (cross-instance)     |
 *  60 | VSS        |          |      | GND    |                               |
 *  61 | VDD        |          |      | 3V3    |                               |
 *  62 | VSSA       |          |      | GND_A  |                               |
 *  63 | VREF+/VDDA |          |      | 3V3    | tied to VDD on Nucleo         |
 *  64 | VDDA       |          |      | 3V3    |                               |
 *-----|------------|----------|------|--------|-------------------------------|
 */

#include "hco_pin_assignment.h"

#endif /* VOX_BOARDS_STM32G474_NUCLEO_BOARD_PINS_H */
