#include "aec.h"
#include <speex/speex_echo.h>
#include <stdlib.h>

struct aec_state {
    SpeexEchoState *echo;
    int frame_size;
};

AecState *aec_create(int frame_size, int filter_len, int sample_rate)
{
    AecState *aec = calloc(1, sizeof(*aec));
    if (!aec)
        return NULL;

    aec->echo = speex_echo_state_init(frame_size, filter_len);
    if (!aec->echo) {
        free(aec);
        return NULL;
    }

    speex_echo_ctl(aec->echo, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate);
    aec->frame_size = frame_size;
    return aec;
}

void aec_process(AecState *aec, const int16_t *mic_in,
                 const int16_t *rx_ref, int16_t *mic_out)
{
    speex_echo_cancellation(aec->echo, mic_in, rx_ref, mic_out);
}

void aec_destroy(AecState *aec)
{
    if (!aec)
        return;
    speex_echo_state_destroy(aec->echo);
    free(aec);
}
