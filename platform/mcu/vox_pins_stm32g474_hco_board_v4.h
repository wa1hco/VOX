#ifndef VOX_PINS_STM32G474_HCO_BOARD_V4_H
#define VOX_PINS_STM32G474_HCO_BOARD_V4_H

#include "vox_mcu_pins.h"

/*
 * STM32G474 board wiring table for VOX HCO board v4 style mapping.
 *
 * This preserves the functional assignments used by the previous scaffold,
 * while migrating MCU target metadata to STM32G474.
 */

/* Raw board wiring aliases. */
#define VOX_STM32G474_MIC_ADC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 0)
#define VOX_STM32G474_RX_ADC_PIN        VOX_PIN(VOX_GPIO_PORT_A, 1)
#define VOX_STM32G474_PTT_OUT_PIN       VOX_PIN(VOX_GPIO_PORT_A, 4)

#define VOX_STM32G474_LED_MIC_PIN       VOX_PIN(VOX_GPIO_PORT_A, 5)
#define VOX_STM32G474_LED_RX_PIN        VOX_PIN(VOX_GPIO_PORT_A, 6)
#define VOX_STM32G474_LED_VAD_PIN       VOX_PIN(VOX_GPIO_PORT_A, 7)
#define VOX_STM32G474_LED_AEC_PIN       VOX_PIN(VOX_GPIO_PORT_B, 0)
#define VOX_STM32G474_LED_PTT_PIN       VOX_PIN(VOX_GPIO_PORT_B, 1)

#define VOX_STM32G474_UART_DEBUG_TX_PIN VOX_PIN(VOX_GPIO_PORT_A, 9)
#define VOX_STM32G474_UART_DEBUG_RX_PIN VOX_PIN(VOX_GPIO_PORT_A, 10)

#define VOX_STM32G474_I2C_CTRL_SCL_PIN  VOX_PIN(VOX_GPIO_PORT_B, 10)
#define VOX_STM32G474_I2C_CTRL_SDA_PIN  VOX_PIN(VOX_GPIO_PORT_B, 9)

/* Functional aliases used by MCU glue code. */
#define VOX_PIN_MIC_AUDIO_IN            VOX_STM32G474_MIC_ADC_PIN
#define VOX_PIN_RX_AUDIO_IN             VOX_STM32G474_RX_ADC_PIN
#define VOX_PIN_PTT_OUT                 VOX_STM32G474_PTT_OUT_PIN

#define VOX_PIN_LED_MIC                 VOX_STM32G474_LED_MIC_PIN
#define VOX_PIN_LED_RX                  VOX_STM32G474_LED_RX_PIN
#define VOX_PIN_LED_VAD                 VOX_STM32G474_LED_VAD_PIN
#define VOX_PIN_LED_AEC                 VOX_STM32G474_LED_AEC_PIN
#define VOX_PIN_LED_PTT                 VOX_STM32G474_LED_PTT_PIN

#define VOX_PIN_UART_DEBUG_TX           VOX_STM32G474_UART_DEBUG_TX_PIN
#define VOX_PIN_UART_DEBUG_RX           VOX_STM32G474_UART_DEBUG_RX_PIN

#define VOX_PIN_I2C_CONTROL_SCL         VOX_STM32G474_I2C_CTRL_SCL_PIN
#define VOX_PIN_I2C_CONTROL_SDA         VOX_STM32G474_I2C_CTRL_SDA_PIN

/* ADC channel metadata. */
#define VOX_ADC_CH_MIC_AUDIO            1u
#define VOX_ADC_CH_RX_AUDIO             2u

#endif /* VOX_PINS_STM32G474_HCO_BOARD_V4_H */
