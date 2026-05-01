#ifndef VOX_PINS_STM32F411CEUX6_HCO_BOARD_V4_H
#define VOX_PINS_STM32F411CEUX6_HCO_BOARD_V4_H

#include "vox_mcu_pins.h"

/*
 * STM32F411CEUx6 (UQFN48) hardware pin table.
 *
 * Duplicated in the Rotator table style so board wiring is visible in one place.
 * Function column below shows a practical VOX default wiring plan.
 *
 *-----|--------|------------------------------|------|-------|--------|----------|-----|--------|------
 * Pin | Name   | Function                     | Free | Power | Serial | ADC      | DAC | SPI    | I2C
 *-----|--------|------------------------------|------|-------|--------|----------|-----|--------|------
 *  1  | VBAT   |                              |      | VBAT  |        |          |     |        |
 *  2  | PC13   |                              |  X   |       |        |          |     |        |
 *  3  | PC14   | OSC32_IN                     |      |       |        |          |     |        |
 *  4  | PC15   | OSC32_OUT                    |      |       |        |          |     |        |
 *  5  | PH0    | OSC_IN                       |      |       |        |          |     |        |
 *  6  | PH1    | OSC_OUT                      |      |       |        |          |     |        |
 *  7  | NRST   |                              |      |       |        |          |     |        |
 *  8  | VSSA   |                              |      | GND   |        |          |     |        |
 *  9  | VREF+  |                              |      | 3V3_A |        |          |     |        |
 * 10  | PA0    | MIC audio in                 |      |       |        | ADC1_IN0 |     |        |
 * 11  | PA1    | RX audio in                  |      |       |        | ADC1_IN1 |     |        |
 * 12  | PA2    |                              |  X   |       | USART2_TX | ADC1_IN2 |  |        |
 * 13  | PA3    |                              |  X   |       | USART2_RX | ADC1_IN3 |  |        |
 * 14  | PA4    | PTT output                   |      |       |        | ADC1_IN4 |     | NSS    |
 * 15  | PA5    | LED MIC                      |      |       |        | ADC1_IN5 |     | SCK    |
 * 16  | PA6    | LED RX                       |      |       |        | ADC1_IN6 |     | MISO   |
 * 17  | PA7    | LED VAD                      |      |       |        | ADC1_IN7 |     | MOSI   |
 * 18  | PB0    | LED AEC                      |      |       |        | ADC1_IN8 |     |        |
 * 19  | PB1    | LED PTT                      |      |       |        | ADC1_IN9 |     |        |
 * 20  | PB2    |                              |  X   |       |        |          |     |        |
 * 21  | PB10   | I2C control SCL              |      |       |        |          |     |        | SCL
 * 22  | VCAP1  |                              |      | VCAP  |        |          |     |        |
 * 23  | VSS    |                              |      | GND   |        |          |     |        |
 * 24  | VDD    |                              |      | 3V3   |        |          |     |        |
 * 25  | PB12   |                              |  X   |       |        |          |     |        |
 * 26  | PB13   |                              |  X   |       |        |          |     | SCK    |
 * 27  | PB14   |                              |  X   |       |        |          |     | MISO   |
 * 28  | PB15   |                              |  X   |       |        |          |     | MOSI   |
 * 29  | PA8    |                              |  X   |       |        |          |     |        |
 * 30  | PA9    | UART debug TX                |      |       | USART1_TX |       |     |        |
 * 31  | PA10   | UART debug RX                |      |       | USART1_RX |       |     |        |
 * 32  | PA11   | USB_DM                       |      |       |        |          |     |        |
 * 33  | PA12   | USB_DP                       |      |       |        |          |     |        |
 * 34  | PA13   | SWDIO                        |      |       |        |          |     |        |
 * 35  | VSS    |                              |      | GND   |        |          |     |        |
 * 36  | VDD    |                              |      | 3V3   |        |          |     |        |
 * 37  | PA14   | SWCLK                        |      |       |        |          |     |        |
 * 38  | PA15   |                              |  X   |       |        |          |     |        |
 * 39  | PB3    |                              |  X   |       |        |          |     | SCK    |
 * 40  | PB4    |                              |  X   |       |        |          |     | MISO   |
 * 41  | PB5    |                              |  X   |       |        |          |     | MOSI   |
 * 42  | PB6    |                              |  X   |       | USART1_TX |       |     |        | SCL
 * 43  | PB7    |                              |  X   |       | USART1_RX |       |     |        | SDA
 * 44  | BOOT0  |                              |      |       |        |          |     |        |
 * 45  | PB8    |                              |  X   |       |        |          |     |        | SCL
 * 46  | PB9    | I2C control SDA              |      |       |        |          |     |        | SDA
 * 47  | VSS    |                              |      | GND   |        |          |     |        |
 * 48  | VDD    |                              |      | 3V3   |        |          |     |        |
 *-----|--------|------------------------------|------|-------|--------|----------|-----|--------|------
 */

/* Raw board wiring aliases. */
#define VOX_STM32F411CEUX6_MIC_ADC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 0)
#define VOX_STM32F411CEUX6_RX_ADC_PIN        VOX_PIN(VOX_GPIO_PORT_A, 1)
#define VOX_STM32F411CEUX6_PTT_OUT_PIN       VOX_PIN(VOX_GPIO_PORT_A, 4)

#define VOX_STM32F411CEUX6_LED_MIC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 5)
#define VOX_STM32F411CEUX6_LED_RX_PIN        VOX_PIN(VOX_GPIO_PORT_A, 6)
#define VOX_STM32F411CEUX6_LED_VAD_PIN       VOX_PIN(VOX_GPIO_PORT_A, 7)
#define VOX_STM32F411CEUX6_LED_AEC_PIN       VOX_PIN(VOX_GPIO_PORT_B, 0)
#define VOX_STM32F411CEUX6_LED_PTT_PIN       VOX_PIN(VOX_GPIO_PORT_B, 1)

#define VOX_STM32F411CEUX6_UART_DEBUG_TX_PIN VOX_PIN(VOX_GPIO_PORT_A, 9)
#define VOX_STM32F411CEUX6_UART_DEBUG_RX_PIN VOX_PIN(VOX_GPIO_PORT_A, 10)

#define VOX_STM32F411CEUX6_I2C_CTRL_SCL_PIN  VOX_PIN(VOX_GPIO_PORT_B, 10)
#define VOX_STM32F411CEUX6_I2C_CTRL_SDA_PIN  VOX_PIN(VOX_GPIO_PORT_B, 9)

/* Functional aliases used by MCU glue code. */
#define VOX_PIN_MIC_AUDIO_IN                 VOX_STM32F411CEUX6_MIC_ADC_PIN
#define VOX_PIN_RX_AUDIO_IN                  VOX_STM32F411CEUX6_RX_ADC_PIN
#define VOX_PIN_PTT_OUT                      VOX_STM32F411CEUX6_PTT_OUT_PIN

#define VOX_PIN_LED_MIC                      VOX_STM32F411CEUX6_LED_MIC_PIN
#define VOX_PIN_LED_RX                       VOX_STM32F411CEUX6_LED_RX_PIN
#define VOX_PIN_LED_VAD                      VOX_STM32F411CEUX6_LED_VAD_PIN
#define VOX_PIN_LED_AEC                      VOX_STM32F411CEUX6_LED_AEC_PIN
#define VOX_PIN_LED_PTT                      VOX_STM32F411CEUX6_LED_PTT_PIN

#define VOX_PIN_UART_DEBUG_TX                VOX_STM32F411CEUX6_UART_DEBUG_TX_PIN
#define VOX_PIN_UART_DEBUG_RX                VOX_STM32F411CEUX6_UART_DEBUG_RX_PIN

#define VOX_PIN_I2C_CONTROL_SCL              VOX_STM32F411CEUX6_I2C_CTRL_SCL_PIN
#define VOX_PIN_I2C_CONTROL_SDA              VOX_STM32F411CEUX6_I2C_CTRL_SDA_PIN

/* ADC channel metadata (STM32F411 ADC1 channels). */
#define VOX_ADC_CH_MIC_AUDIO                 0
#define VOX_ADC_CH_RX_AUDIO                  1

#endif /* VOX_PINS_STM32F411CEUX6_HCO_BOARD_V4_H */
