#include "vox_mcu_board.h"
#include "vox_pins_stm32f411ceux6_hco_board_v4.h"

static const VoxMcuPinConfig g_stm32f411ceux6_hco_v4_pins = {
    .pin_mic_audio_in = VOX_PIN_MIC_AUDIO_IN,
    .pin_rx_audio_in = VOX_PIN_RX_AUDIO_IN,
    .pin_ptt_out = VOX_PIN_PTT_OUT,
    .pin_led_mic = VOX_PIN_LED_MIC,
    .pin_led_rx = VOX_PIN_LED_RX,
    .pin_led_vad = VOX_PIN_LED_VAD,
    .pin_led_aec = VOX_PIN_LED_AEC,
    .pin_led_ptt = VOX_PIN_LED_PTT,
    .pin_uart_debug_tx = VOX_PIN_UART_DEBUG_TX,
    .pin_uart_debug_rx = VOX_PIN_UART_DEBUG_RX,
    .pin_i2c_control_scl = VOX_PIN_I2C_CONTROL_SCL,
    .pin_i2c_control_sda = VOX_PIN_I2C_CONTROL_SDA,
    .adc_ch_mic_audio = VOX_ADC_CH_MIC_AUDIO,
    .adc_ch_rx_audio = VOX_ADC_CH_RX_AUDIO,
};

const VoxMcuPinConfig *vox_mcu_get_pin_config(void)
{
    return &g_stm32f411ceux6_hco_v4_pins;
}
