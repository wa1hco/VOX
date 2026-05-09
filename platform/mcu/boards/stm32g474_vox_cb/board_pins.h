#ifndef VOX_BOARDS_STM32G474_VOX_CB_BOARD_PINS_H
#define VOX_BOARDS_STM32G474_VOX_CB_BOARD_PINS_H

#include "vox_mcu_pins.h"

/*
 * STM32G474CBT3 (LQFP48) hardware pin table — VOX HCO custom board.
 *
 * This table makes the LQFP48 pinout visible in one place.  GPIO signal
 * assignments (mic/rx/ptt/leds/uart/i2c) live in
 * boards/common/hco_pin_assignment.h and are shared with the Nucleo
 * shield, which mirrors this design.  This file owns the LQFP48-specific
 * pin numbering and the analog-front-end notes that only apply to the
 * custom board (external OPAMP cascade, Vmid buffer, USB clock plan).
 *
 * Clock note: The G474 has no HSI48.  USB clock = HSI16 → PLL → PLLQ = 48 MHz.
 * HSI16 alone is ±1% (10 000 ppm), exceeding USB full-speed ±2500 ppm budget.
 * With CRS enabled and USB SOF as the trim reference, HSI16 is held to ~±500 ppm,
 * well within spec.  No external crystal is required; PF0 and PF1 are free.
 *
 * OpAmp routing note: The G4 OPAMPs have no internal output → input route
 * between OPAMPs, so inter-stage links must be wired as external PCB
 * traces — for the planned mic chain that means PA2 (OPAMP1 OUT1) ↔ PA7
 * (OPAMP2 INP2) and PA6 (OPAMP2 OUT2) ↔ PB0 (OPAMP3 INP3).  The final
 * stage → ADC link, however, can be kept fully internal: set OPAINTOEN=1
 * in OPAMP3_CSR so OPAMP3's output reaches its internal ADC channel (see
 * RM0440 ADC channel mapping) without driving the VOUT pin (PB1).  PB1
 * then stays available for other use.
 *
 * OpAmp4 / Vmid note: OPAMP4 is configured as a voltage follower buffering
 * the Vmid analog bias (~VDDA/2) used by the single-supply mic chain and
 * any other AC-coupled analog signals.  In follower mode VINM4 is tied
 * internally to VOUT4, so the external VINM4 pin (PB10) is not consumed
 * by the buffer and stays free.  The reference voltage is applied at
 * VINP4 (PB13); the buffered Vmid is taken from VOUT4 (PB12).
 *
 * OpAmp calibration note: All four OPAMPs in the analog front end
 * (OPAMP1..3 in the mic signal path, OPAMP4 buffering Vmid) should run
 * the G4 self-offset calibration at startup — RM0440 OPAMP "User offset
 * calibration": enable → CALON=1 → sweep TRIMOFFSETN/P with CALSEL →
 * USERTRIM=1.  Each OPAMP contributes a few mV of input offset that,
 * uncalibrated, stacks up: OPAMP4's offset shifts Vmid for every
 * downstream stage; OPAMP1..3's offsets ride along the signal path and
 * get multiplied by the chain's gain.  The net effect is lost clipping
 * headroom and a residual DC at the ADC the digital chain then has to
 * remove.
 *
 * Pin | Name       | Function | Free | Power  | OpAmp  | ADC        | DAC  |
 *-----|------------|----------|------|--------|--------|------------|------|
 *   1 | VBAT       |          |      | 3V3    |        |            |      |
 *   2 | PC13       |          |      |        |        |            |      |
 *   3 | PC14       |          |      |        |        |            |      |
 *   4 | PC15       |          |      |        |        |            |      |
 *   5 | PF0        |          |      |        |        | ADC1_IN10  |      |
 *   6 | PF1        |          |      |        |        | ADC2_IN10  |      |
 *   7 | NRST       |          |      |        |        |            |      |
 *   8 | PA0        | MIC_ADC  |      |        |        | ADC1_IN1   |      |
 *   9 | PA1        | MIC_H    |      |        | INP1   | ADC2_IN2   |      |
 *  10 | PA2        | MIC_SE   |      |        | OUT1   | ADC1_IN3   |      |
 *  11 | PA3        | MIC_RTN  |      |        | INM1   | ADC1_IN4   |      |
 *  12 | PA4        | PTT_OUT  |      |        |        | ADC2_IN17  | OUT1 |
 *-----|------------|----------|------|--------|--------|------------|------|
 *  13 | PA5        | LED_MIC  |      |        |        | ADC2_IN13  | OUT2 |
 *  14 | PA6        | LED_RX   |      |        | OUT2   | ADC2_IN3   |      |
 *  15 | PA7        | LED_VAD  |      |        | INP2   | ADC2_IN4   |      |
 *  16 | PB0        | LED_AEC  |      |        | INP3   | ADC1_IN15  |      |
 *  17 | PB1        | LED_PTT  |      |        |        | ADC1_IN12  |      |
 *  18 | PB2        |          |      |        | INM3   |            |      |
 *  19 | VSSA       |          |      | GND_A  |        |            |      |
 *  20 | VREF+/VDDA |          |      | 3V3_A  |        |            |      |
 *  21 | VDDA       |          |      | 3V3_A  |        |            |      |
 *  22 | PB10       | I2C_SCL  |      |        |        |            |      |
 *  23 | VSS        |          |      | GND    |        |            |      |
 *  24 | VDD        |          |      | 3V3    |        |            |      |
 *-----|------------|----------|------|--------|--------|------------|------|
 *  25 | PB11       |          |      |        |        |            |      |
 *  26 | PB12       | VMID     |      |        | OUT4   |            |      |
 *  27 | PB13       |          |      |        | INP4   |            |      |
 *  28 | PB14       |          |      |        |        |            |      |
 *  29 | PB15       |          |      |        |        |            |      |
 *  30 | PA8        |          |      |        |        |            |      |
 *  31 | PA9        | DBG_TX   |      |        |        |            |      |
 *  32 | PA10       | DBG_RX   |      |        |        |            |      |
 *  33 | PA11       |          |      |        |        |            |      |
 *  34 | PA12       |          |      |        |        |            |      |
 *  35 | VSS        |          |      | GND    |        |            |      |
 *  36 | VDD        |          |      | 3V3    |        |            |      |
 *-----|------------|----------|------|--------|--------|------------|------|
 *  37 | PA13       | SWDIO    |      |        |        |            |      |
 *  38 | PA14       | SWCLK    |      |        |        |            |      |
 *  39 | PA15       |          |      |        |        |            |      |
 *  40 | PB3        |          |      |        |        |            |      |
 *  41 | PB4        |          |      |        |        |            |      |
 *  42 | PB5        |          |      |        |        |            |      |
 *  43 | PB6        |          |      |        |        |            |      |
 *  44 | PB7        |          |      |        |        |            |      |
 *  45 | PB8/BOOT0  |          |      |        |        |            |      |
 *  46 | PB9        | I2C_SDA  |      |        |        |            |      |
 *  47 | VSS        |          |      | GND    |        |            |      |
 *  48 | VDD        |          |      | 3V3    |        |            |      |
 *-----|------------|----------|------|--------|--------|------------|------|
 */

#include "hco_pin_assignment.h"

#endif /* VOX_BOARDS_STM32G474_VOX_CB_BOARD_PINS_H */
