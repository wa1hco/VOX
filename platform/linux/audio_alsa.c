#include "audio_alsa.h"
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <stdio.h>

struct audio_alsa {
    snd_pcm_t *mic_pcm;
    snd_pcm_t *rx_pcm;
    int frame_size;
};

static snd_pcm_t *open_capture(const char *device, int sample_rate,
                                int frame_size)
{
    snd_pcm_t *pcm = NULL;
    snd_pcm_hw_params_t *params = NULL;
    unsigned int rate = (unsigned int)sample_rate;
    int err;

    err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "audio_alsa: cannot open '%s': %s\n",
                device, snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, 1);
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0);
    snd_pcm_hw_params_set_period_size(pcm, params,
                                      (snd_pcm_uframes_t)frame_size, 0);

    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        fprintf(stderr, "audio_alsa: hw_params failed for '%s': %s\n",
                device, snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    snd_pcm_prepare(pcm);
    return pcm;
}

AudioAlsa *audio_alsa_open(const AudioAlsaConfig *cfg)
{
    if (!cfg)
        return NULL;

    AudioAlsa *audio = calloc(1, sizeof(*audio));
    if (!audio)
        return NULL;

    audio->frame_size = cfg->frame_size;
    audio->mic_pcm = open_capture(cfg->mic_device, cfg->sample_rate,
                                  cfg->frame_size);
    audio->rx_pcm  = open_capture(cfg->rx_device,  cfg->sample_rate,
                                  cfg->frame_size);

    if (!audio->mic_pcm || !audio->rx_pcm) {
        audio_alsa_close(audio);
        return NULL;
    }

    return audio;
}

int audio_alsa_read(AudioAlsa *audio, int16_t *mic_out, int16_t *rx_out)
{
    snd_pcm_sframes_t n;

    n = snd_pcm_readi(audio->mic_pcm, mic_out, (snd_pcm_uframes_t)audio->frame_size);
    if (n < 0) {
        snd_pcm_recover(audio->mic_pcm, (int)n, 0);
        return -1;
    }

    n = snd_pcm_readi(audio->rx_pcm, rx_out, (snd_pcm_uframes_t)audio->frame_size);
    if (n < 0) {
        snd_pcm_recover(audio->rx_pcm, (int)n, 0);
        return -1;
    }

    return 0;
}

void audio_alsa_close(AudioAlsa *audio)
{
    if (!audio)
        return;
    if (audio->mic_pcm)
        snd_pcm_close(audio->mic_pcm);
    if (audio->rx_pcm)
        snd_pcm_close(audio->rx_pcm);
    free(audio);
}
