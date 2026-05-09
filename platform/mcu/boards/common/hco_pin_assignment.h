#ifndef VOX_BOARDS_COMMON_HCO_PIN_ASSIGNMENT_H
#define VOX_BOARDS_COMMON_HCO_PIN_ASSIGNMENT_H

#include "vox_mcu_pins.h"

/*
 * HCO logical pin assignment shared by all STM32G474 VOX boards.
 *
 * This file holds the *signal → port/pin* mapping only.  It does NOT
 * describe a chip package.  Each board's board_pins.h owns the package
 * pin-table comment (LQFP48 for the custom CB board, LQFP64 for the
 * Nucleo G474RE shield) and #includes this file so the wiring stays
 * single-sourced.
 *
 * The two boards intentionally share GPIO assignments: the Nucleo shield
 * is wired to mirror the custom board so MCU code is identical.  Any
 * board-specific quirk (e.g. the Nucleo LD2 user-LED on PA5 driven by
 * VOX_PIN_LED_MIC, or the Nucleo's ST-Link VCP on USART2 PA2/PA3 not
 * being used by VOX) is documented in that board's board_pins.h.
 */

/* ----- Raw board wiring aliases (port/pin only) ----- */
#define VOX_HCO_MIC_ADC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 0)
#define VOX_HCO_RX_ADC_PIN        VOX_PIN(VOX_GPIO_PORT_A, 1)
#define VOX_HCO_PTT_OUT_PIN       VOX_PIN(VOX_GPIO_PORT_A, 4)

#define VOX_HCO_LED_MIC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 5)
#define VOX_HCO_LED_RX_PIN        VOX_PIN(VOX_GPIO_PORT_A, 6)
#define VOX_HCO_LED_VAD_PIN       VOX_PIN(VOX_GPIO_PORT_A, 7)
#define VOX_HCO_LED_AEC_PIN       VOX_PIN(VOX_GPIO_PORT_B, 0)
#define VOX_HCO_LED_PTT_PIN       VOX_PIN(VOX_GPIO_PORT_B, 1)

#define VOX_HCO_UART_DEBUG_TX_PIN VOX_PIN(VOX_GPIO_PORT_A, 9)
#define VOX_HCO_UART_DEBUG_RX_PIN VOX_PIN(VOX_GPIO_PORT_A, 10)

#define VOX_HCO_I2C_CTRL_SCL_PIN  VOX_PIN(VOX_GPIO_PORT_B, 10)
#define VOX_HCO_I2C_CTRL_SDA_PIN  VOX_PIN(VOX_GPIO_PORT_B, 9)

/* ----- Functional aliases consumed by MCU glue code ----- */
#define VOX_PIN_MIC_AUDIO_IN      VOX_HCO_MIC_ADC_PIN
#define VOX_PIN_RX_AUDIO_IN       VOX_HCO_RX_ADC_PIN
#define VOX_PIN_PTT_OUT           VOX_HCO_PTT_OUT_PIN

#define VOX_PIN_LED_MIC           VOX_HCO_LED_MIC_PIN
#define VOX_PIN_LED_RX            VOX_HCO_LED_RX_PIN
#define VOX_PIN_LED_VAD           VOX_HCO_LED_VAD_PIN
#define VOX_PIN_LED_AEC           VOX_HCO_LED_AEC_PIN
#define VOX_PIN_LED_PTT           VOX_HCO_LED_PTT_PIN

#define VOX_PIN_UART_DEBUG_TX     VOX_HCO_UART_DEBUG_TX_PIN
#define VOX_PIN_UART_DEBUG_RX     VOX_HCO_UART_DEBUG_RX_PIN

#define VOX_PIN_I2C_CONTROL_SCL   VOX_HCO_I2C_CTRL_SCL_PIN
#define VOX_PIN_I2C_CONTROL_SDA   VOX_HCO_I2C_CTRL_SDA_PIN

/* ADC channel metadata (signal → ADC channel index). */
#define VOX_ADC_CH_MIC_AUDIO      1u
#define VOX_ADC_CH_RX_AUDIO       2u

#endif /* VOX_BOARDS_COMMON_HCO_PIN_ASSIGNMENT_H */
