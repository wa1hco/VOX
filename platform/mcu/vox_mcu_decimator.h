#ifndef VOX_MCU_DECIMATOR_H
#define VOX_MCU_DECIMATOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * MCU frontend sample-rate plan:
 * - ADC sampling runs at 32 MHz for easier analog anti-alias filter design.
 * - VOX DSP (AEC/VAD) runs at 8 kHz.
 */
#define VOX_MCU_ADC_SAMPLE_RATE_HZ 32000000u
#define VOX_MCU_DSP_SAMPLE_RATE_HZ 8000u
#define VOX_MCU_DECIMATION_FACTOR (VOX_MCU_ADC_SAMPLE_RATE_HZ / VOX_MCU_DSP_SAMPLE_RATE_HZ)

#if (VOX_MCU_ADC_SAMPLE_RATE_HZ % VOX_MCU_DSP_SAMPLE_RATE_HZ) != 0
#error "VOX MCU sample rates must divide to an integer decimation factor"
#endif

typedef struct {
    int64_t integrator_1;
    int64_t integrator_2;
    int64_t comb_delay_1;
    int64_t comb_delay_2;
} VoxMcuCic2Channel;

typedef struct {
    uint16_t adc_midpoint;
    uint16_t phase;
    VoxMcuCic2Channel mic;
    VoxMcuCic2Channel rx;
} VoxMcuAdcFrontend;

void vox_mcu_adc_frontend_init(VoxMcuAdcFrontend *frontend, uint16_t adc_midpoint);

/*
 * Push one ADC pair (mic, rx) sampled at VOX_MCU_ADC_SAMPLE_RATE_HZ.
 * Returns 1 when a decimated 8 kHz output sample pair is produced.
 */
int vox_mcu_adc_frontend_push(VoxMcuAdcFrontend *frontend,
                              uint16_t mic_adc,
                              uint16_t rx_adc,
                              int16_t *mic_out,
                              int16_t *rx_out);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_DECIMATOR_H */
