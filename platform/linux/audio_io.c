#include "audio_io.h"

#include <alsa/asoundlib.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Per-stream FIFO accumulator                                          */
/* Holds up to ACCUM_CAP samples (~512 ms @ 8 kHz) to absorb PA chunk */
/* boundaries without losing audio between frames.                     */
/* ------------------------------------------------------------------ */
#define ACCUM_CAP 4096

typedef struct {
    int16_t buf[ACCUM_CAP];
    int count; /* samples stored */
} StreamAccum;

static void accum_push(StreamAccum *a, const int16_t *src, int n)
{
    if (n <= 0)
        return;
    if (a->count + n > ACCUM_CAP) {
        /* Overrun: drop oldest samples to make room */
        int drop = (a->count + n) - ACCUM_CAP;
        memmove(a->buf, a->buf + drop, (size_t)(a->count - drop) * sizeof(int16_t));
        a->count -= drop;
    }
    memcpy(a->buf + a->count, src, (size_t)n * sizeof(int16_t));
    a->count += n;
}

static void accum_push_zeros(StreamAccum *a, int n)
{
    if (n <= 0)
        return;
    if (a->count + n > ACCUM_CAP) {
        int drop = (a->count + n) - ACCUM_CAP;
        memmove(a->buf, a->buf + drop, (size_t)(a->count - drop) * sizeof(int16_t));
        a->count -= drop;
    }
    memset(a->buf + a->count, 0, (size_t)n * sizeof(int16_t));
    a->count += n;
}

static int accum_pop(StreamAccum *a, int16_t *dst, int n)
{
    if (a->count < n)
        return -1;
    memcpy(dst, a->buf, (size_t)n * sizeof(int16_t));
    a->count -= n;
    memmove(a->buf, a->buf + n, (size_t)a->count * sizeof(int16_t));
    return 0;
}

/* ------------------------------------------------------------------ */
/* audio_io struct                                                      */
/* ------------------------------------------------------------------ */

struct audio_io {
    int frame_size;
    int sample_rate;

    /* Set when both streams use the shared Pulse async mainloop */
    int use_pulse_async;

    /* ALSA capture handles (used when not using shared Pulse path) */
    snd_pcm_t *mic_alsa;
    snd_pcm_t *rx_alsa;

    /* pa_simple fallback (mixed ALSA+Pulse, or if async open fails) */
    pa_simple *mic_simple;
    pa_simple *rx_simple;

    /* Shared Pulse async path: single mainloop, single context, two streams */
    pa_mainloop *mainloop;
    pa_context  *context;
    pa_stream   *mic_stream;
    pa_stream   *rx_stream;
    StreamAccum  mic_accum;
    StreamAccum  rx_accum;
};

typedef struct {
    char *default_source;
    char *default_sink;
    int done;
    int success;
} PulseDefaults;

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static int is_auto_device(const char *spec)
{
    return !spec || !spec[0] || strcmp(spec, "auto") == 0;
}

static char *dup_string(const char *s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (!copy)
        return NULL;

    memcpy(copy, s, len);
    return copy;
}

static char *make_monitor_name(const char *sink_name)
{
    static const char suffix[] = ".monitor";
    size_t len;
    char *monitor_name;

    if (!sink_name)
        return NULL;

    len = strlen(sink_name) + sizeof(suffix);
    monitor_name = malloc(len);
    if (!monitor_name)
        return NULL;

    snprintf(monitor_name, len, "%s%s", sink_name, suffix);
    return monitor_name;
}

static char *make_pulse_spec(const char *source_name)
{
    static const char prefix[] = "pulse:";
    size_t len;
    char *spec;

    if (!source_name)
        return NULL;

    len = strlen(prefix) + strlen(source_name) + 1;
    spec = malloc(len);
    if (!spec)
        return NULL;

    snprintf(spec, len, "%s%s", prefix, source_name);
    return spec;
}

static void on_server_info(pa_context *context, const pa_server_info *info, void *userdata)
{
    PulseDefaults *defaults = userdata;

    (void)context;

    defaults->default_source = dup_string(info->default_source_name);
    defaults->default_sink = dup_string(info->default_sink_name);
    defaults->success = defaults->default_source && defaults->default_sink;
    defaults->done = 1;
}

static int wait_for_context_ready(pa_mainloop *mainloop, pa_context *context)
{
    int ret;

    for (;;) {
        pa_context_state_t state = pa_context_get_state(context);

        if (state == PA_CONTEXT_READY)
            return 0;
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            return -1;

        if (pa_mainloop_iterate(mainloop, 1, &ret) < 0)
            return -1;
    }
}

static int pulse_get_defaults(PulseDefaults *defaults)
{
    pa_mainloop *mainloop;
    pa_context *context;
    pa_operation *op;
    int ret;

    memset(defaults, 0, sizeof(*defaults));

    mainloop = pa_mainloop_new();
    if (!mainloop)
        return -1;

    context = pa_context_new(pa_mainloop_get_api(mainloop), "vox_linux_probe");
    if (!context) {
        pa_mainloop_free(mainloop);
        return -1;
    }

    if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return -1;
    }

    if (wait_for_context_ready(mainloop, context) < 0) {
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return -1;
    }

    op = pa_context_get_server_info(context, on_server_info, defaults);
    if (!op) {
        pa_context_disconnect(context);
        pa_context_unref(context);
        pa_mainloop_free(mainloop);
        return -1;
    }

    while (!defaults->done) {
        if (pa_mainloop_iterate(mainloop, 1, &ret) < 0)
            break;
    }

    pa_operation_unref(op);
    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_mainloop_free(mainloop);

    if (!defaults->success) {
        free(defaults->default_source);
        free(defaults->default_sink);
        defaults->default_source = NULL;
        defaults->default_sink = NULL;
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* ALSA capture                                                         */
/* ------------------------------------------------------------------ */

static snd_pcm_t *open_alsa_capture(const char *device, int sample_rate, int frame_size)
{
    snd_pcm_t *pcm = NULL;
    snd_pcm_hw_params_t *params = NULL;
    unsigned int rate = (unsigned int)sample_rate;
    int err;

    err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "audio_io: cannot open ALSA '%s': %s\n", device, snd_strerror(err));
        return NULL;
    }

    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, 1);
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, 0);
    snd_pcm_hw_params_set_period_size(pcm, params, (snd_pcm_uframes_t)frame_size, 0);

    err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        fprintf(stderr, "audio_io: ALSA hw_params failed for '%s': %s\n", device, snd_strerror(err));
        snd_pcm_close(pcm);
        return NULL;
    }

    snd_pcm_prepare(pcm);
    return pcm;
}

/* ------------------------------------------------------------------ */
/* pa_simple fallback (mixed ALSA+Pulse, or if async init fails)       */
/* ------------------------------------------------------------------ */

static pa_simple *open_pulse_simple(const char *source_name, int sample_rate,
                                    int frame_size, const char *stream_name)
{
    pa_sample_spec ss;
    pa_buffer_attr attr;
    int err;

    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = (uint32_t)sample_rate;
    ss.channels = 1;

    attr.maxlength = (uint32_t)(frame_size * (int)sizeof(int16_t) * 4);
    attr.tlength   = (uint32_t)-1;
    attr.prebuf    = (uint32_t)-1;
    attr.minreq    = (uint32_t)-1;
    attr.fragsize  = (uint32_t)(frame_size * (int)sizeof(int16_t));

    pa_simple *s = pa_simple_new(
        NULL, "vox_linux", PA_STREAM_RECORD,
        (source_name && source_name[0]) ? source_name : NULL,
        stream_name, &ss, NULL, &attr, &err);

    if (!s)
        fprintf(stderr, "audio_io: cannot open Pulse source '%s': %s\n",
                (source_name && source_name[0]) ? source_name : "(default)",
                pa_strerror(err));
    return s;
}

/* ------------------------------------------------------------------ */
/* Pulse async: shared mainloop + context + two pa_stream objects      */
/*                                                                      */
/* Both streams are driven by a single pa_mainloop, so they share the  */
/* same server clock.  audio_io_read pumps the loop until both          */
/* accumulators hold >= frame_size samples, then pops one frame each.  */
/* This eliminates the sequential blocking skew of two pa_simple reads. */
/* ------------------------------------------------------------------ */

static int wait_for_stream_ready(pa_mainloop *ml, pa_stream *stream)
{
    int ret;
    for (;;) {
        pa_stream_state_t state = pa_stream_get_state(stream);
        if (state == PA_STREAM_READY)
            return 0;
        if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
            return -1;
        if (pa_mainloop_iterate(ml, 1, &ret) < 0)
            return -1;
    }
}

static pa_stream *connect_pulse_record_stream(pa_context *ctx,
                                              const char *source_name,
                                              int sample_rate, int frame_size,
                                              const char *stream_name)
{
    pa_sample_spec ss;
    pa_buffer_attr attr;

    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = (uint32_t)sample_rate;
    ss.channels = 1;

    /* 1 frame per fragment, small server-side buffer: keeps AEC latency low */
    attr.maxlength = (uint32_t)(frame_size * (int)sizeof(int16_t) * 4);
    attr.tlength   = (uint32_t)-1;
    attr.prebuf    = (uint32_t)-1;
    attr.minreq    = (uint32_t)-1;
    attr.fragsize  = (uint32_t)(frame_size * (int)sizeof(int16_t));

    pa_stream *stream = pa_stream_new(ctx, stream_name, &ss, NULL);
    if (!stream)
        return NULL;

    if (pa_stream_connect_record(
            stream,
            (source_name && *source_name) ? source_name : NULL,
            &attr,
            PA_STREAM_ADJUST_LATENCY) < 0) {
        pa_stream_unref(stream);
        return NULL;
    }

    return stream;
}

/* Drain all currently available data from a pa_stream into its accum buffer */
static void drain_stream_to_accum(pa_stream *stream, StreamAccum *accum)
{
    for (;;) {
        const void *data;
        size_t nbytes;

        if (pa_stream_readable_size(stream) == 0)
            break;

        if (pa_stream_peek(stream, &data, &nbytes) < 0)
            break;

        if (nbytes == 0)
            break;

        int samples = (int)(nbytes / sizeof(int16_t));
        if (data)
            accum_push(accum, (const int16_t *)data, samples);
        else
            accum_push_zeros(accum, samples); /* PA timing hole */

        pa_stream_drop(stream);
    }
}

static int open_pulse_async(AudioIO *audio,
                             const char *mic_source, const char *rx_source)
{
    pa_mainloop *ml;
    pa_context  *ctx;

    ml = pa_mainloop_new();
    if (!ml)
        return -1;

    ctx = pa_context_new(pa_mainloop_get_api(ml), "vox_linux");
    if (!ctx) {
        pa_mainloop_free(ml);
        return -1;
    }

    if (pa_context_connect(ctx, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return -1;
    }

    /* Reuse the existing wait helper: it takes (mainloop, context) */
    if (wait_for_context_ready(ml, ctx) < 0) {
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return -1;
    }

    audio->mic_stream = connect_pulse_record_stream(ctx, mic_source,
                                                    audio->sample_rate,
                                                    audio->frame_size, "vox_mic");
    audio->rx_stream  = connect_pulse_record_stream(ctx, rx_source,
                                                    audio->sample_rate,
                                                    audio->frame_size, "vox_rx");

    if (!audio->mic_stream || !audio->rx_stream)
        goto fail;

    if (wait_for_stream_ready(ml, audio->mic_stream) < 0)
        goto fail;
    if (wait_for_stream_ready(ml, audio->rx_stream) < 0)
        goto fail;

    audio->mainloop = ml;
    audio->context  = ctx;
    memset(&audio->mic_accum, 0, sizeof(audio->mic_accum));
    memset(&audio->rx_accum,  0, sizeof(audio->rx_accum));
    return 0;

fail:
    if (audio->mic_stream) {
        pa_stream_disconnect(audio->mic_stream);
        pa_stream_unref(audio->mic_stream);
        audio->mic_stream = NULL;
    }
    if (audio->rx_stream) {
        pa_stream_disconnect(audio->rx_stream);
        pa_stream_unref(audio->rx_stream);
        audio->rx_stream = NULL;
    }
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

AudioIO *audio_io_open(const AudioIOConfig *cfg)
{
    PulseDefaults defaults;
    char *rx_monitor_name = NULL;
    char *auto_mic_spec   = NULL;
    char *auto_rx_spec    = NULL;
    const char *mic_device;
    const char *rx_device;

    if (!cfg)
        return NULL;

    AudioIO *audio = calloc(1, sizeof(*audio));
    if (!audio)
        return NULL;

    audio->frame_size  = cfg->frame_size;
    audio->sample_rate = cfg->sample_rate;

    mic_device = cfg->mic_device;
    rx_device  = cfg->rx_device;

    /* Resolve "auto" devices via Pulse server info */
    if (is_auto_device(mic_device) || is_auto_device(rx_device)) {
        if (pulse_get_defaults(&defaults) < 0) {
            fprintf(stderr, "audio_io: failed to query Pulse defaults for auto mode\n");
            free(audio);
            return NULL;
        }

        if (is_auto_device(mic_device))
            mic_device = defaults.default_source;

        if (is_auto_device(rx_device)) {
            rx_monitor_name = make_monitor_name(defaults.default_sink);
            rx_device = rx_monitor_name;
        }

        fprintf(stderr, "audio_io: auto-selected mic='pulse:%s' rx='pulse:%s'\n",
                mic_device ? mic_device : "",
                rx_device  ? rx_device  : "");

        auto_mic_spec = make_pulse_spec(mic_device);
        auto_rx_spec  = make_pulse_spec(rx_device);
        if (!auto_mic_spec || !auto_rx_spec) {
            free(defaults.default_source);
            free(defaults.default_sink);
            free(rx_monitor_name);
            free(auto_mic_spec);
            free(auto_rx_spec);
            free(audio);
            return NULL;
        }

        if (is_auto_device(cfg->mic_device))
            mic_device = auto_mic_spec;
        if (is_auto_device(cfg->rx_device))
            rx_device  = auto_rx_spec;
    } else {
        memset(&defaults, 0, sizeof(defaults));
    }

    int mic_is_pulse = starts_with(mic_device ? mic_device : "", "pulse:");
    int rx_is_pulse  = starts_with(rx_device  ? rx_device  : "", "pulse:");

    if (mic_is_pulse && rx_is_pulse) {
        /* Preferred path: single shared mainloop for both streams */
        const char *mic_src = mic_device + 6; /* skip "pulse:" */
        const char *rx_src  = rx_device  + 6;

        if (open_pulse_async(audio, mic_src, rx_src) == 0) {
            audio->use_pulse_async = 1;
        } else {
            /* Fallback to two pa_simple connections */
            fprintf(stderr, "audio_io: async Pulse open failed, falling back to pa_simple\n");
            audio->mic_simple = open_pulse_simple(mic_src, cfg->sample_rate,
                                                   cfg->frame_size, "vox_mic");
            audio->rx_simple  = open_pulse_simple(rx_src,  cfg->sample_rate,
                                                   cfg->frame_size, "vox_rx");
            if (!audio->mic_simple || !audio->rx_simple)
                goto fail;
        }
    } else if (mic_is_pulse) {
        audio->mic_simple = open_pulse_simple(mic_device + 6, cfg->sample_rate,
                                               cfg->frame_size, "vox_mic");
        audio->rx_alsa    = open_alsa_capture(rx_device, cfg->sample_rate, cfg->frame_size);
        if (!audio->mic_simple || !audio->rx_alsa)
            goto fail;
    } else if (rx_is_pulse) {
        audio->mic_alsa  = open_alsa_capture(mic_device, cfg->sample_rate, cfg->frame_size);
        audio->rx_simple = open_pulse_simple(rx_device + 6, cfg->sample_rate,
                                              cfg->frame_size, "vox_rx");
        if (!audio->mic_alsa || !audio->rx_simple)
            goto fail;
    } else {
        const char *m = (starts_with(mic_device ? mic_device : "", "alsa:"))
                            ? mic_device + 5 : mic_device;
        const char *r = (starts_with(rx_device  ? rx_device  : "", "alsa:"))
                            ? rx_device  + 5 : rx_device;
        audio->mic_alsa = open_alsa_capture(m, cfg->sample_rate, cfg->frame_size);
        audio->rx_alsa  = open_alsa_capture(r, cfg->sample_rate, cfg->frame_size);
        if (!audio->mic_alsa || !audio->rx_alsa)
            goto fail;
    }

    free(defaults.default_source);
    free(defaults.default_sink);
    free(rx_monitor_name);
    free(auto_mic_spec);
    free(auto_rx_spec);
    return audio;

fail:
    audio_io_close(audio);
    free(defaults.default_source);
    free(defaults.default_sink);
    free(rx_monitor_name);
    free(auto_mic_spec);
    free(auto_rx_spec);
    return NULL;
}

int audio_io_read(AudioIO *audio, int16_t *mic_out, int16_t *rx_out)
{
    if (audio->use_pulse_async) {
        int ret;
        /* Pump the shared mainloop until both accumulators have a full frame.
         * Both streams are driven by the same event loop, keeping them aligned
         * to the same server clock — no sequential blocking skew. */
        while (audio->mic_accum.count < audio->frame_size ||
               audio->rx_accum.count  < audio->frame_size) {
            drain_stream_to_accum(audio->mic_stream, &audio->mic_accum);
            drain_stream_to_accum(audio->rx_stream,  &audio->rx_accum);

            if (audio->mic_accum.count < audio->frame_size ||
                audio->rx_accum.count  < audio->frame_size) {
                if (pa_mainloop_iterate(audio->mainloop, 1, &ret) < 0)
                    return -1;
            }
        }
        accum_pop(&audio->mic_accum, mic_out, audio->frame_size);
        accum_pop(&audio->rx_accum,  rx_out,  audio->frame_size);
        return 0;
    }

    /* pa_simple mic */
    if (audio->mic_simple) {
        int err;
        if (pa_simple_read(audio->mic_simple, mic_out,
                           (size_t)audio->frame_size * sizeof(int16_t), &err) < 0) {
            fprintf(stderr, "audio_io: Pulse mic read: %s\n", pa_strerror(err));
            return -1;
        }
    } else if (audio->mic_alsa) {
        snd_pcm_sframes_t n = snd_pcm_readi(audio->mic_alsa, mic_out,
                                             (snd_pcm_uframes_t)audio->frame_size);
        if (n < 0) {
            snd_pcm_recover(audio->mic_alsa, (int)n, 0);
            return -1;
        }
    }

    /* pa_simple or ALSA rx */
    if (audio->rx_simple) {
        int err;
        if (pa_simple_read(audio->rx_simple, rx_out,
                           (size_t)audio->frame_size * sizeof(int16_t), &err) < 0) {
            fprintf(stderr, "audio_io: Pulse rx read: %s\n", pa_strerror(err));
            return -1;
        }
    } else if (audio->rx_alsa) {
        snd_pcm_sframes_t n = snd_pcm_readi(audio->rx_alsa, rx_out,
                                             (snd_pcm_uframes_t)audio->frame_size);
        if (n < 0) {
            snd_pcm_recover(audio->rx_alsa, (int)n, 0);
            return -1;
        }
    }

    return 0;
}

void audio_io_close(AudioIO *audio)
{
    if (!audio)
        return;

    if (audio->use_pulse_async) {
        if (audio->mic_stream) {
            pa_stream_disconnect(audio->mic_stream);
            pa_stream_unref(audio->mic_stream);
        }
        if (audio->rx_stream) {
            pa_stream_disconnect(audio->rx_stream);
            pa_stream_unref(audio->rx_stream);
        }
        if (audio->context) {
            pa_context_disconnect(audio->context);
            pa_context_unref(audio->context);
        }
        if (audio->mainloop)
            pa_mainloop_free(audio->mainloop);
    }

    if (audio->mic_simple)
        pa_simple_free(audio->mic_simple);
    if (audio->rx_simple)
        pa_simple_free(audio->rx_simple);
    if (audio->mic_alsa)
        snd_pcm_close(audio->mic_alsa);
    if (audio->rx_alsa)
        snd_pcm_close(audio->rx_alsa);

    free(audio);
}
