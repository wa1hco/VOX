#include "vox.h"
#include "aec.h"
#include "vad.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define VAD_LED_PROB_THRESHOLD 60
#define MIC_ACTIVITY_THRESHOLD  300
#define RX_ACTIVE_THRESHOLD     300
#define AEC_LED_REDUCTION_PCT   20
#define RX_GUARD_VAD_BOOST      15
#define RX_GUARD_SNR_PCT        145
#define AEC_FILTER_FRAMES       16

struct vox_state {
    AecState *aec;
    VadState *vad;

    int frame_size;
    int sample_rate;
    int mic_led_threshold;
    int rx_led_threshold;
    int vad_led_prob_threshold;
    int aec_led_reduction_pct;
    int rx_guard_vad_boost;
    int rx_guard_snr_pct;

    /* PTT hang timer: counts down in frames after voice stops */
    int hang_frames;        /* current countdown */
    int hang_frames_max;    /* frames equivalent of hang_ms */

    int16_t *aec_buf;       /* scratch buffer for AEC output */

    int noise_floor;        /* adaptive post-AEC noise estimate */

    VoxLedState leds;
    VoxDebugState debug;
};

static int avg_abs(const int16_t *samples, int n)
{
    long long sum = 0;

    if (!samples || n <= 0)
        return 0;

    for (int i = 0; i < n; i++) {
        int v = samples[i];
        if (v < 0)
            v = -v;
        sum += v;
    }

    return (int)(sum / n);
}

static int threshold_or_default(int value, int default_value)
{
    if (value <= 0)
        return default_value;
    return value;
}

VoxState *vox_create(const VoxConfig *cfg)
{
    if (!cfg || cfg->frame_size <= 0 || cfg->sample_rate <= 0)
        return NULL;

    VoxState *vox = calloc(1, sizeof(*vox));
    if (!vox)
        return NULL;

    /* AEC tail length.  Default of AEC_FILTER_FRAMES (16) gives ~320 ms
     * tail at 8 kHz — good for room/speaker echo on Linux.  Embedded
     * targets that have hit speexdsp heap corruption at the large tail
     * can pass cfg->aec_filter_frames > 0 to override (M=4 = 80 ms is
     * known-good on the STM32G474 cross build).  See memory:
     * mcu-aec-tail-bug.md. */
    int filter_frames = (cfg->aec_filter_frames > 0)
                        ? cfg->aec_filter_frames
                        : AEC_FILTER_FRAMES;
    int filter_len = cfg->frame_size * filter_frames;

    vox->aec = aec_create(cfg->frame_size, filter_len, cfg->sample_rate);
    vox->vad = vad_create(cfg->frame_size, cfg->sample_rate);
    vox->aec_buf = calloc(cfg->frame_size, sizeof(int16_t));

    if (!vox->aec || !vox->vad || !vox->aec_buf) {
        vox_destroy(vox);
        return NULL;
    }

    vox->frame_size = cfg->frame_size;
    vox->sample_rate = cfg->sample_rate;
    vox->mic_led_threshold = threshold_or_default(cfg->mic_led_threshold,
                                                  MIC_ACTIVITY_THRESHOLD);
    vox->rx_led_threshold = threshold_or_default(cfg->rx_led_threshold,
                                                 RX_ACTIVE_THRESHOLD);
    vox->vad_led_prob_threshold = threshold_or_default(cfg->vad_led_prob_threshold,
                                                       VAD_LED_PROB_THRESHOLD);
    vox->aec_led_reduction_pct = threshold_or_default(cfg->aec_led_reduction_pct,
                                                      AEC_LED_REDUCTION_PCT);
    vox->rx_guard_vad_boost = threshold_or_default(cfg->rx_guard_vad_boost,
                                                   RX_GUARD_VAD_BOOST);
    vox->rx_guard_snr_pct = threshold_or_default(cfg->rx_guard_snr_pct,
                                                 RX_GUARD_SNR_PCT);
    vox_set_hang_ms(vox, cfg->hang_ms);

    return vox;
}

void vox_set_hang_ms(VoxState *vox, int hang_ms)
{
    if (!vox || hang_ms < 0)
        return;
    /* Convert ms to frames, minimum 1 frame */
    int ms_per_frame = (vox->frame_size * 1000) / vox->sample_rate;
    vox->hang_frames_max = hang_ms / ms_per_frame;
    if (vox->hang_frames_max < 1)
        vox->hang_frames_max = 1;
}

int vox_process(VoxState *vox, const int16_t *mic_raw, const int16_t *rx_ref)
{
    if (!vox || !mic_raw || !rx_ref)
        return 0;

    /* Step 1: remove RX audio from mic */
    aec_process(vox->aec, mic_raw, rx_ref, vox->aec_buf);

    /* Step 2: detect voice in cleaned mic signal */
    int voice_raw = vad_process(vox->vad, vox->aec_buf);
    int vad_prob_raw = vad_get_probability(vox->vad);

    /* Level/status signals used by logic and indicators. */
    int rx_level = avg_abs(rx_ref, vox->frame_size);
    int mic_before = avg_abs(mic_raw, vox->frame_size);
    int mic_after = avg_abs(vox->aec_buf, vox->frame_size);

    /* Residual echo correlator: estimate how much post-AEC mic still tracks
     * the RX reference. A high value during far-end-only playback indicates
     * cancellation residue likely to cause anti-VOX issues. */
    double corr_num = 0.0;
    double corr_post_energy = 0.0;
    double corr_rx_energy = 0.0;
    for (int i = 0; i < vox->frame_size; i++) {
        double post = (double)vox->aec_buf[i];
        double rx = (double)rx_ref[i];
        corr_num += post * rx;
        corr_post_energy += post * post;
        corr_rx_energy += rx * rx;
    }

    double residual_corr = 0.0;
    double residual_corr_rms = 0.0;
    if (corr_post_energy > 1.0 && corr_rx_energy > 1.0) {
        residual_corr = fabs(corr_num) / sqrt(corr_post_energy * corr_rx_energy);
        if (residual_corr > 1.0)
            residual_corr = 1.0;

        residual_corr_rms = fabs(corr_num) / sqrt(corr_rx_energy * (double)vox->frame_size);
    }

    int residual_corr_pct = (int)(residual_corr * 100.0 + 0.5);
    int residual_corr_level = (int)(residual_corr_rms + 0.5);
    int residual_corr_dbfs_tenths = -900;
    if (residual_corr_rms > 1.0e-9) {
        double dbfs = 20.0 * log10(residual_corr_rms / 32767.0);
        if (dbfs < -90.0)
            dbfs = -90.0;
        if (dbfs > 0.0)
            dbfs = 0.0;
        residual_corr_dbfs_tenths = (int)(dbfs * 10.0 + (dbfs >= 0.0 ? 0.5 : -0.5));
    }

    int residual_corr_peak_pct = 0;
    int residual_corr_peak_delay_samples = 0;
    for (int k = 0; k < VOX_RESIDUAL_CORR_PROFILE_POINTS; k++) {
        int lag = VOX_RESIDUAL_CORR_DELAY_MIN_SAMPLES +
                  (k * VOX_RESIDUAL_CORR_DELAY_STEP_SAMPLES);
        double n = 0.0;
        double e_post = 0.0;
        double e_rx = 0.0;

        for (int i = 0; i < vox->frame_size; i++) {
            int j = i + lag;
            if (j < 0 || j >= vox->frame_size)
                continue;

            double post = (double)vox->aec_buf[i];
            double rx = (double)rx_ref[j];
            n += post * rx;
            e_post += post * post;
            e_rx += rx * rx;
        }

        int bin_pct = 0;
        if (e_post > 1.0 && e_rx > 1.0) {
            double c = fabs(n) / sqrt(e_post * e_rx);
            if (c > 1.0)
                c = 1.0;
            bin_pct = (int)(c * 100.0 + 0.5);
        }

        vox->debug.residual_corr_profile[k] = bin_pct;
        if (bin_pct > residual_corr_peak_pct) {
            residual_corr_peak_pct = bin_pct;
            residual_corr_peak_delay_samples = lag;
        }
    }

    int mic_active = (mic_before >= vox->mic_led_threshold) ? 1 : 0;
    int rx_active = (rx_level >= vox->rx_led_threshold) ? 1 : 0;

    /* Adaptive noise tracking validates VAD output without hard pre-gating.
     * This prevents stuck-high VAD/PTT in near-silence while preserving
     * sensitivity to low-level speech above the learned floor. */
    if (vox->noise_floor <= 0)
        vox->noise_floor = (mic_after > 0) ? mic_after : (vox->mic_led_threshold / 2);

    if (!voice_raw && !rx_active)
        vox->noise_floor = (vox->noise_floor * 99 + mic_after) / 100;

    int floor_min = vox->mic_led_threshold / 4;
    if (floor_min < 80)
        floor_min = 80;
    if (vox->noise_floor < floor_min)
        vox->noise_floor = floor_min;

    int snr_pct = (mic_after * 100) / (vox->noise_floor + 1);
    int snr_ok_strict = snr_pct >= 130;
    int snr_ok_relaxed = snr_pct >= 115;

    int energy_margin = mic_after - vox->noise_floor;
    int min_margin = vox->mic_led_threshold / 3;
    if (min_margin < 100)
        min_margin = 100;
    int energy_ok = energy_margin >= min_margin;

    int vad_prob = (energy_ok && snr_ok_relaxed) ? vad_prob_raw : 0;

    int rx_guard_applied = rx_active ? 1 : 0;
    int effective_vad_threshold = vox->vad_led_prob_threshold +
                                  (rx_guard_applied ? vox->rx_guard_vad_boost : 0);
    if (effective_vad_threshold > 100)
        effective_vad_threshold = 100;

    int effective_snr_threshold = rx_guard_applied ? vox->rx_guard_snr_pct : 130;
    int snr_ok_effective = snr_pct >= effective_snr_threshold;

    int voice = 0;
    if ((vad_prob >= effective_vad_threshold && snr_ok_effective) ||
        (voice_raw && energy_ok && snr_ok_effective &&
         vad_prob_raw >= (effective_vad_threshold / 2))) {
        voice = 1;
    }

    /* Step 3: hang timer state machine */
    if (voice) {
        vox->hang_frames = vox->hang_frames_max;
    } else if (vox->hang_frames > 0) {
        vox->hang_frames--;
    }

    int ptt_reason_voice = voice ? 1 : 0;
    int ptt_reason_hang = (!voice && vox->hang_frames > 0) ? 1 : 0;
    int ptt = vox->hang_frames > 0 ? 1 : 0;

    int reduction_pct = 0;
    if (rx_active && mic_before > 0 && mic_after < mic_before)
        reduction_pct = ((mic_before - mic_after) * 100) / mic_before;

    vox->leds.mic_level = mic_before;
    vox->leds.rx_level = rx_level;
    vox->leds.mic_led = mic_active;
    vox->leds.rx_led = rx_active;
    vox->leds.rx_active = rx_active;
    vox->leds.vad_probability_raw = vad_prob_raw;
    vox->leds.vad_probability = vad_prob;
    vox->leds.vad_led = voice;
    vox->leds.aec_reduction_pct = reduction_pct;
    vox->leds.aec_led = (rx_active && reduction_pct >= vox->aec_led_reduction_pct) ? 1 : 0;
    vox->leds.ptt_led = ptt;

    vox->debug.mic_level_raw = mic_before;
    vox->debug.mic_level_post_aec = mic_after;
    vox->debug.rx_level = rx_level;
    vox->debug.noise_floor = vox->noise_floor;
    vox->debug.snr_pct = snr_pct;
    vox->debug.energy_margin = energy_margin;
    vox->debug.voice_raw = voice_raw;
    vox->debug.voice_validated = voice;
    vox->debug.rx_active = rx_active;
    vox->debug.energy_ok = energy_ok;
    vox->debug.snr_ok_relaxed = snr_ok_relaxed;
    vox->debug.snr_ok_strict = snr_ok_strict;
    vox->debug.effective_vad_threshold = effective_vad_threshold;
    vox->debug.effective_snr_threshold = effective_snr_threshold;
    vox->debug.rx_guard_applied = rx_guard_applied;
    vox->debug.hang_frames = vox->hang_frames;
    vox->debug.hang_frames_max = vox->hang_frames_max;
    vox->debug.ptt_reason_voice = ptt_reason_voice;
    vox->debug.ptt_reason_hang = ptt_reason_hang;
    vox->debug.residual_corr_pct = residual_corr_pct;
    vox->debug.residual_corr_level = residual_corr_level;
    vox->debug.residual_corr_dbfs_tenths = residual_corr_dbfs_tenths;
    vox->debug.residual_corr_peak_delay_samples = residual_corr_peak_delay_samples;
    vox->debug.residual_corr_peak_pct = residual_corr_peak_pct;
    vox->debug.residual_corr_delay_min_samples = VOX_RESIDUAL_CORR_DELAY_MIN_SAMPLES;
    vox->debug.residual_corr_delay_step_samples = VOX_RESIDUAL_CORR_DELAY_STEP_SAMPLES;

    return ptt;
}

void vox_set_tuning(VoxState *vox, const VoxConfig *cfg)
{
    if (!vox || !cfg)
        return;

    if (cfg->hang_ms > 0)
        vox_set_hang_ms(vox, cfg->hang_ms);

    if (cfg->mic_led_threshold > 0)
        vox->mic_led_threshold = cfg->mic_led_threshold;
    if (cfg->rx_led_threshold > 0)
        vox->rx_led_threshold = cfg->rx_led_threshold;
    if (cfg->vad_led_prob_threshold > 0)
        vox->vad_led_prob_threshold = cfg->vad_led_prob_threshold;
    if (cfg->aec_led_reduction_pct > 0)
        vox->aec_led_reduction_pct = cfg->aec_led_reduction_pct;
    if (cfg->rx_guard_vad_boost > 0)
        vox->rx_guard_vad_boost = cfg->rx_guard_vad_boost;
    if (cfg->rx_guard_snr_pct > 0)
        vox->rx_guard_snr_pct = cfg->rx_guard_snr_pct;
}

void vox_get_led_state(const VoxState *vox, VoxLedState *state)
{
    if (!vox || !state)
        return;
    *state = vox->leds;
}

void vox_get_debug_state(const VoxState *vox, VoxDebugState *state)
{
    if (!vox || !state)
        return;
    *state = vox->debug;
}

void vox_destroy(VoxState *vox)
{
    if (!vox)
        return;
    aec_destroy(vox->aec);
    vad_destroy(vox->vad);
    free(vox->aec_buf);
    free(vox);
}
