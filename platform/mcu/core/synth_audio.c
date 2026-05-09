/*
 * synth_audio.c — deterministic synthetic audio for VOX bring-up.
 *
 * 8 kHz sample rate.  Three signal generators, gated per phase:
 *
 *   tone_1khz(n)        - ~5000 amplitude sine wave at 1 kHz, used for the
 *                         "RX is playing" simulation.  1 kHz is in-band
 *                         (mid-voice) and trivially loud — RX_LED should
 *                         engage immediately.
 *
 *   voice_burst(n)      - ~6000 amplitude pulses at ~250 Hz (a coarse
 *                         pitched-glottal stand-in) modulated by a
 *                         3 Hz envelope.  Mimics speech-band content
 *                         enough that speexdsp's VAD will fire; nothing
 *                         like a real voice signal.
 *
 *   silence(n)          - zeros.
 *
 * Implementation notes:
 *   - All math is in fixed-point int32_t to avoid float costs in a
 *     scenario that doesn't need them.  Stays consistent if we ever
 *     compile this for an FPU-less target.
 *   - The 1 kHz sine table has 8 samples per period at 8 kHz.  Cheap
 *     and recognizable on a scope.
 *   - Phase advance is per-frame not per-sample; that's fine for these
 *     coarse stand-ins.  Use a real generator if/when this code grows
 *     beyond bring-up scaffolding.
 */

#include "synth_audio.h"

#include <stddef.h>

#define VOX_PHASE_DURATION_MS 1000u
#define VOX_PHASE_COUNT       4u
#define VOX_SCENARIO_PERIOD_MS (VOX_PHASE_DURATION_MS * VOX_PHASE_COUNT)

static uint32_t g_scenario_start_ms;

/* Single-period (8-sample) 1 kHz sine wave at amplitude ~5000 (Q15). */
static const int16_t k_sine_1khz_8samp[8] = {
    0, 3536, 5000, 3536, 0, -3536, -5000, -3536
};

static int16_t tone_1khz(uint32_t sample_index)
{
    return k_sine_1khz_8samp[sample_index & 7u];
}

/* Voice-burst pulse train: 8 kHz / 32 samples = 250 Hz "pitch", with a
 * slow envelope (period ~333 ms / 2667 samples) so the energy isn't
 * stationary.  speex's VAD is quite tolerant; this is enough to push it
 * past threshold without modeling actual speech. */
static int16_t voice_burst(uint32_t sample_index)
{
    /* Pitched pulse: every 32nd sample is loud, decaying over 8 samples. */
    uint32_t pitch_phase = sample_index & 31u;
    int32_t pulse;
    if (pitch_phase < 8u) {
        /* Decaying envelope inside the pulse: 6000 → 0 over 8 samples. */
        int32_t env_q8 = (int32_t)((8u - pitch_phase) * 32u);  /* 256..32 */
        pulse = (int32_t)6000 * env_q8 / 256;
        if (pitch_phase & 1u)
            pulse = -pulse;     /* alternate polarity — broader spectrum */
    } else {
        pulse = 0;
    }

    /* Slow amplitude envelope so VAD sees onset/offset, not constant power. */
    uint32_t env_phase = sample_index % 2667u;
    int32_t env;
    if (env_phase < 1333u)
        env = 256;          /* full amplitude for first half */
    else
        env = 64 + (int32_t)((2667u - env_phase) * 192u / 1334u);  /* fade out */

    int32_t out = pulse * env / 256;
    if (out > 32767)
        out = 32767;
    if (out < -32768)
        out = -32768;
    return (int16_t)out;
}

void synth_audio_reset(uint32_t now_ms)
{
    g_scenario_start_ms = now_ms;
}

int synth_audio_fill_frame(int16_t *mic, int16_t *rx, int frame_size,
                           uint32_t now_ms)
{
    if (!mic || !rx || frame_size <= 0)
        return -1;

    uint32_t t_ms = (now_ms - g_scenario_start_ms) % VOX_SCENARIO_PERIOD_MS;
    int phase = (int)(t_ms / VOX_PHASE_DURATION_MS);

    /* Sample index runs continuously across phases at 8 kHz so the
     * waveforms don't restart phase on each call (would click). */
    static uint32_t s_sample_index;

    for (int i = 0; i < frame_size; i++) {
        int16_t mic_s = 0;
        int16_t rx_s  = 0;

        switch (phase) {
        case 0:                          /* silence */
            break;
        case 1:                          /* RX-only — anti-VOX scenario */
            rx_s = tone_1khz(s_sample_index);
            break;
        case 2:                          /* mic-only — VAD/PTT scenario */
            mic_s = voice_burst(s_sample_index);
            break;
        case 3:                          /* mic + RX — AEC scenario */
            mic_s = voice_burst(s_sample_index);
            /* "Echo" component: same RX content also leaking into mic
             * at half amplitude.  The AEC should remove most of the rx_s
             * energy from the mic signal it cancels against rx_s. */
            mic_s = (int16_t)((int32_t)mic_s + tone_1khz(s_sample_index) / 2);
            rx_s  = tone_1khz(s_sample_index);
            break;
        default:
            break;
        }

        mic[i] = mic_s;
        rx[i]  = rx_s;
        s_sample_index++;
    }

    return phase;
}
