#ifndef VOX_MCU_BOARD_H
#define VOX_MCU_BOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin_mic_audio_in;
    uint8_t pin_rx_audio_in;
    uint8_t pin_ptt_out;

    uint8_t pin_led_mic;
    uint8_t pin_led_rx;
    uint8_t pin_led_vad;
    uint8_t pin_led_aec;
    uint8_t pin_led_ptt;

    uint8_t pin_uart_debug_tx;
    uint8_t pin_uart_debug_rx;

    uint8_t pin_i2c_control_scl;
    uint8_t pin_i2c_control_sda;

    uint8_t adc_ch_mic_audio;
    uint8_t adc_ch_rx_audio;

    uint32_t adc_sample_rate_hz;
    uint32_t dsp_sample_rate_hz;
    uint16_t decimation_factor;
} VoxMcuPinConfig;

const VoxMcuPinConfig *vox_mcu_get_pin_config(void);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_BOARD_H */
