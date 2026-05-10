#ifndef VOX_H
#define VOX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VOX — Voice Operated Switch
 *
 * Top-level state machine.  Call vox_process() once per audio frame.
 * The caller supplies echo-cancelled VAD output and the current
 * receive-audio reference; this module owns the PTT hang timer.
 */

typedef struct vox_state VoxState;

#define VOX_RESIDUAL_CORR_PROFILE_POINTS 41
#define VOX_RESIDUAL_CORR_DELAY_MIN_SAMPLES (-80)
#define VOX_RESIDUAL_CORR_DELAY_STEP_SAMPLES 4

typedef struct {
    int sample_rate;    /* Hz, e.g. 8000 */
    int frame_size;     /* samples per frame, e.g. 160 (20 ms at 8 kHz) */
    int hang_ms;        /* PTT hang time in milliseconds after voice stops */
    int mic_led_threshold;       /* raw mic activity threshold, default 300 */
    int rx_led_threshold;        /* raw rx activity threshold, default 300 */
    int vad_led_prob_threshold;  /* VAD probability threshold, default 60 */
    int aec_led_reduction_pct;   /* AEC reduction threshold percent, default 20 */
    int rx_guard_vad_boost;      /* extra VAD threshold while RX active, default 15 */
    int rx_guard_snr_pct;        /* min SNR%% while RX active, default 145 */
    int aec_filter_frames;       /* AEC adaptive-filter tail in 20 ms blocks.
                                    0 (default) → 16 (320 ms tail).  Set
                                    smaller (e.g. 4 = 80 ms) on memory-tight
                                    targets — speexdsp on cortex-m4f
                                    corrupts heap with the default tail. */
} VoxConfig;

typedef struct {
    int mic_led;            /* 1 when microphone input level is active */
    int rx_led;             /* 1 when receive reference level is active */
    int vad_led;            /* 1 when speech probability is high */
    int aec_led;            /* 1 when echo reduction is active */
    int ptt_led;            /* 1 when PTT output is active */
    int mic_level;          /* average absolute mic level (PCM units) */
    int rx_level;           /* average absolute RX level (PCM units) */
    int vad_probability_raw;/* 0-100 raw Speex VAD probability */
    int vad_probability;    /* 0-100 validated VAD score for UI/PTT logic */
    int aec_reduction_pct;  /* 0-100 estimated mic energy reduction */
    int rx_active;          /* 1 when receive reference has significant energy */
} VoxLedState;

typedef struct {
    int mic_level_raw;      /* average absolute mic level before AEC */
    int mic_level_post_aec; /* average absolute mic level after AEC */
    int rx_level;           /* average absolute RX reference level */
    int noise_floor;        /* adaptive post-AEC noise estimate */
    int snr_pct;            /* post-AEC level as percent of learned floor */
    int energy_margin;      /* post-AEC level minus learned floor */
    int voice_raw;          /* raw VAD boolean before validation */
    int voice_validated;    /* final validated voice boolean */
    int rx_active;          /* RX activity flag after thresholding */
    int energy_ok;          /* 1 when post-AEC level is above floor margin */
    int snr_ok_relaxed;     /* 1 when SNR passes relaxed threshold */
    int snr_ok_strict;      /* 1 when SNR passes strict threshold */
    int effective_vad_threshold; /* active VAD threshold after RX guard */
    int effective_snr_threshold; /* active SNR threshold after RX guard */
    int rx_guard_applied;   /* 1 when RX guard logic is active */
    int hang_frames;        /* current hang countdown */
    int hang_frames_max;    /* configured hang countdown max */
    int ptt_reason_voice;   /* 1 if current frame refreshed hang timer */
    int ptt_reason_hang;    /* 1 if PTT held by existing hang timer */
    int residual_corr_pct;  /* 0-100 normalized |corr(post_aec, rx_ref)| */
    int residual_corr_level;/* RMS level of correlated residual component */
    int residual_corr_dbfs_tenths; /* correlated residual in dBFS * 10 */
    int residual_corr_peak_delay_samples; /* delay of strongest corr bin */
    int residual_corr_peak_pct; /* strongest corr bin value 0-100 */
    int residual_corr_delay_min_samples; /* first bin delay */
    int residual_corr_delay_step_samples; /* bin spacing */
    int residual_corr_profile[VOX_RESIDUAL_CORR_PROFILE_POINTS]; /* 0-100 */
} VoxDebugState;

/* Create VOX state with given configuration.
 * Returns NULL on failure. */
VoxState *vox_create(const VoxConfig *cfg);

/* Process one audio frame.
 * mic_raw : raw microphone samples before AEC
 * rx_ref  : receive audio reference samples
 * Returns 1 if PTT should be active, 0 if not. */
int vox_process(VoxState *vox, const int16_t *mic_raw,
                const int16_t *rx_ref);

/* Retrieve current LED/status values after vox_process(). */
void vox_get_led_state(const VoxState *vox, VoxLedState *state);

/* Retrieve intermediate debug values used by the VOX decision chain. */
void vox_get_debug_state(const VoxState *vox, VoxDebugState *state);

/* Apply runtime tuning values.
 * Any field <= 0 keeps current/default behavior for that field. */
void vox_set_tuning(VoxState *vox, const VoxConfig *cfg);

/* Change hang time at runtime (e.g. from a knob). */
void vox_set_hang_ms(VoxState *vox, int hang_ms);

void vox_destroy(VoxState *vox);

#ifdef __cplusplus
}
#endif

#endif /* VOX_H */
