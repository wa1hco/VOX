#include "vad.h"
#include <speex/speex_preprocess.h>
#include <stdlib.h>

struct vad_state {
    SpeexPreprocessState *pp;
    int frame_size;
    int last_prob;
};

VadState *vad_create(int frame_size, int sample_rate)
{
    VadState *vad = calloc(1, sizeof(*vad));
    if (!vad)
        return NULL;

    vad->pp = speex_preprocess_state_init(frame_size, sample_rate);
    if (!vad->pp) {
        free(vad);
        return NULL;
    }

    /* Enable VAD, disable noise suppression and AGC (we only want detection) */
    int enable = 1;
    int disable = 0;
    speex_preprocess_ctl(vad->pp, SPEEX_PREPROCESS_SET_VAD, &enable);
    speex_preprocess_ctl(vad->pp, SPEEX_PREPROCESS_SET_DENOISE, &disable);
    speex_preprocess_ctl(vad->pp, SPEEX_PREPROCESS_SET_AGC, &disable);

    vad->frame_size = frame_size;
    return vad;
}

int vad_process(VadState *vad, const int16_t *samples)
{
    /* speex_preprocess_run takes a non-const pointer but does not modify
     * the buffer when VAD-only mode is active; cast is safe here. */
    int voice = speex_preprocess_run(vad->pp, (int16_t *)samples);
    vad->last_prob = 0;
    speex_preprocess_ctl(vad->pp, SPEEX_PREPROCESS_GET_PROB, &vad->last_prob);
    return voice;
}

int vad_get_probability(const VadState *vad)
{
    if (!vad)
        return 0;
    return vad->last_prob;
}

void vad_destroy(VadState *vad)
{
    if (!vad)
        return;
    speex_preprocess_state_destroy(vad->pp);
    free(vad);
}
