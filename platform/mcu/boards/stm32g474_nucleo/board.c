#include "vox_mcu_board.h"
#include "vox_mcu_decimator.h"
#include "board_pins.h"

/*
 * stm32g474_nucleo — NUCLEO-G474RE dev board with VOX HCO-mirrored shield.
 *
 * Same logical pin map as the custom CB board (see hco_pin_assignment.h).
 * Differences live in driver code, not in this descriptor:
 *   - No external OPAMP front-end on the shield (mic feeds ADC1_IN1 direct).
 *   - Clock plan can use HSE bypass from ST-Link MCO during bring-up; once
 *     stable, switch to HSI16+CRS to match how the custom board runs.
 *   - VDDA is tied to VDD on the Nucleo, so expect a slightly higher ADC
 *     noise floor than the custom board's filtered analog rail.
 *
 * The decimator constants are kept identical to the custom board so the
 * shield's ADC sampling chain mirrors final hardware behavior:
 * 32 kHz ADC, CIC decimate-by-4 to 8 kHz DSP.  If you want to bring up
 * the Nucleo with plain 8 kHz sampling first (no CIC decimator), do
 * that in the driver layer — don't change the board descriptor's
 * stated rate.
 */
static const VoxMcuPinConfig g_stm32g474_nucleo_pins = {
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
    return &g_stm32g474_nucleo_pins;
}
