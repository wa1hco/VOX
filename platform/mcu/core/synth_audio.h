#ifndef VOX_MCU_SYNTH_AUDIO_H
#define VOX_MCU_SYNTH_AUDIO_H

/*
 * synth_audio.h — bring-up scaffolding: deterministic synthetic audio.
 *
 * NOT production code.  Generates a 4-second cyclic pattern of mic/rx
 * frames so we can verify the VOX algorithm chain runs end-to-end on
 * the MCU before the analog front end is wired up.  The four phases
 * exercise:
 *
 *   Phase 0  (0.0 - 1.0 s): silence on both channels
 *                            → expect: all LEDs off, PTT off
 *   Phase 1  (1.0 - 2.0 s): RX active (1 kHz tone), mic silent
 *                            → expect: RX_LED on, MIC/VAD/PTT off
 *                                      (anti-VOX scenario — RX guard at work)
 *   Phase 2  (2.0 - 3.0 s): mic loud (voice-band burst), RX silent
 *                            → expect: MIC_LED on, VAD_LED on, PTT on
 *   Phase 3  (3.0 - 4.0 s): mic + RX (echo scenario)
 *                            → expect: MIC, RX, AEC LEDs on, VAD/PTT
 *                                      depending on AEC quality
 *
 * The "voice-band burst" is a sequence of pulses at speech-like rates
 * (around 200-400 Hz envelope), not pure tone — speex's VAD looks at
 * spectral structure, not just energy, so a flat sine fools it less
 * convincingly.  This is bring-up scaffolding, not a real test vector.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reset the scenario clock (used at boot so phase 0 starts at t=0). */
void synth_audio_reset(uint32_t now_ms);

/* Fill `mic` and `rx` buffers (each `frame_size` samples at 8 kHz)
 * with the next frame of the scenario, based on `now_ms`.  Returns
 * the current phase number (0-3) for diagnostic prints. */
int synth_audio_fill_frame(int16_t *mic, int16_t *rx, int frame_size,
                           uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_SYNTH_AUDIO_H */
