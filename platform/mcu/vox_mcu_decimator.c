#include "vox_mcu_decimator.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define VOX_CIC_GAIN (VOX_MCU_DECIMATION_FACTOR * VOX_MCU_DECIMATION_FACTOR)

static int16_t saturate_i16(int64_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static int16_t cic2_decimate_channel(VoxMcuCic2Channel *ch)
{
    int64_t comb_1 = ch->integrator_2 - ch->comb_delay_1;
    ch->comb_delay_1 = ch->integrator_2;

    int64_t comb_2 = comb_1 - ch->comb_delay_2;
    ch->comb_delay_2 = comb_1;

    /*
     * Normalize CIC gain R^N to return to roughly ADC-count scale.
     * Optional MCU glue may apply extra gain/offset mapping as needed.
     */
    return saturate_i16(comb_2 / (int64_t)VOX_CIC_GAIN);
}

void vox_mcu_adc_frontend_init(VoxMcuAdcFrontend *frontend, uint16_t adc_midpoint)
{
    if (!frontend)
        return;

    memset(frontend, 0, sizeof(*frontend));
    frontend->adc_midpoint = adc_midpoint;
}

int vox_mcu_adc_frontend_push(VoxMcuAdcFrontend *frontend,
                              uint16_t mic_adc,
                              uint16_t rx_adc,
                              int16_t *mic_out,
                              int16_t *rx_out)
{
    if (!frontend)
        return 0;

    int32_t mic_centered = (int32_t)mic_adc - (int32_t)frontend->adc_midpoint;
    int32_t rx_centered = (int32_t)rx_adc - (int32_t)frontend->adc_midpoint;

    frontend->mic.integrator_1 += mic_centered;
    frontend->mic.integrator_2 += frontend->mic.integrator_1;

    frontend->rx.integrator_1 += rx_centered;
    frontend->rx.integrator_2 += frontend->rx.integrator_1;

    frontend->phase++;
    if (frontend->phase < VOX_MCU_DECIMATION_FACTOR)
        return 0;

    frontend->phase = 0;

    if (mic_out)
        *mic_out = cic2_decimate_channel(&frontend->mic);
    else
        (void)cic2_decimate_channel(&frontend->mic);

    if (rx_out)
        *rx_out = cic2_decimate_channel(&frontend->rx);
    else
        (void)cic2_decimate_channel(&frontend->rx);

    return 1;
}
