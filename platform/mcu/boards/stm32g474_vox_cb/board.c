#include "vox_mcu_board.h"
#include "vox_mcu_decimator.h"
#include "board_pins.h"

/*
 * stm32g474_vox_cb — VOX HCO custom board, STM32G474CBT3 (LQFP48).
 *
 * Analog mic chain: external OPAMP1/2/3 cascade (see board_pins.h table).
 * Vmid bias buffer:  OPAMP4 follower at PB12.
 * ADC sampling:      32 kHz (4x oversampled), CIC decimate-by-4 to 8 kHz DSP.
 */
static const VoxMcuPinConfig g_stm32g474_vox_cb_pins = {
    .pin_mic_audio_in    = VOX_PIN_MIC_AUDIO_IN,
    .pin_rx_audio_in     = VOX_PIN_RX_AUDIO_IN,
    .pin_ptt_out         = VOX_PIN_PTT_OUT,
    .pin_led_mic         = VOX_PIN_LED_MIC,
    .pin_led_rx          = VOX_PIN_LED_RX,
    .pin_led_vad         = VOX_PIN_LED_VAD,
    .pin_led_aec         = VOX_PIN_LED_AEC,
    .pin_led_ptt         = VOX_PIN_LED_PTT,
    .pin_uart_debug_tx   = VOX_PIN_UART_DEBUG_TX,
    .pin_uart_debug_rx   = VOX_PIN_UART_DEBUG_RX,
    .pin_i2c_control_scl = VOX_PIN_I2C_CONTROL_SCL,
    .pin_i2c_control_sda = VOX_PIN_I2C_CONTROL_SDA,
    .adc_ch_mic_audio    = VOX_ADC_CH_MIC_AUDIO,
    .adc_ch_rx_audio     = VOX_ADC_CH_RX_AUDIO,
    .adc_sample_rate_hz  = VOX_MCU_ADC_SAMPLE_RATE_HZ,
    .dsp_sample_rate_hz  = VOX_MCU_DSP_SAMPLE_RATE_HZ,
    .decimation_factor   = (uint16_t)VOX_MCU_DECIMATION_FACTOR,
};

const VoxMcuPinConfig *vox_mcu_get_pin_config(void)
{
    return &g_stm32g474_vox_cb_pins;
}
