#ifndef VAD_H
#define VAD_H

#include <stdint.h>

/* Voice Activity Detection
 *
 * Detects operator speech in microphone audio while ignoring
 * background noise and non-voice sounds.
 *
 * Built on SpeexDSP preprocessor VAD.
 */

typedef struct vad_state VadState;

/* Create VAD state.
 * frame_size  : number of samples per processing frame
 * sample_rate : audio sample rate in Hz
 * Returns NULL on failure. */
VadState *vad_create(int frame_size, int sample_rate);

/* Process one frame of echo-cancelled mic audio.
 * samples : input samples (length = frame_size)
 * Returns 1 if voice detected, 0 if not. */
int vad_process(VadState *vad, const int16_t *samples);

/* Returns last speech probability estimate (0-100). */
int vad_get_probability(const VadState *vad);

void vad_destroy(VadState *vad);

#endif /* VAD_H */
