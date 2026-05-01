#ifndef AEC_H
#define AEC_H

#include <stdint.h>

/* Adaptive Echo Cancellation
 *
 * Removes receive audio that leaks into the microphone input,
 * preventing false VOX activation during receive.
 *
 * Built on SpeexDSP echo canceller.
 */

typedef struct aec_state AecState;

/* Create AEC state.
 * frame_size  : number of samples per processing frame
 * filter_len  : echo tail length in samples (e.g. 4x frame_size)
 * sample_rate : audio sample rate in Hz
 * Returns NULL on failure. */
AecState *aec_create(int frame_size, int filter_len, int sample_rate);

/* Process one frame.
 * mic_in  : microphone input samples (length = frame_size)
 * rx_ref  : receive audio reference samples (length = frame_size)
 * mic_out : echo-cancelled mic output (length = frame_size)
 */
void aec_process(AecState *aec, const int16_t *mic_in,
                 const int16_t *rx_ref, int16_t *mic_out);

void aec_destroy(AecState *aec);

#endif /* AEC_H */
