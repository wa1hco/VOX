#include "vox.h"
#include "audio_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#define SAMPLE_RATE  8000
#define FRAME_MS     20
#define FRAME_SIZE   (SAMPLE_RATE * FRAME_MS / 1000)   /* 160 samples */
#define DEFAULT_HANG_MS  500
#define DEFAULT_MIC_LED_THRESHOLD 300
#define DEFAULT_RX_LED_THRESHOLD  300
#define DEFAULT_VAD_LED_PROB      60
#define DEFAULT_AEC_LED_REDUCTION 20
#define DEFAULT_RX_GUARD_VAD_BOOST 15
#define DEFAULT_RX_GUARD_SNR_PCT   145

static volatile int g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -m <device>  Microphone capture device (default: auto)\n"
        "               ALSA: hw:0,0 or alsa:plughw:CARD=PCH,DEV=0\n"
        "               Pulse source: pulse:alsa_input.pci-...\n"
        "  -r <device>  RX reference capture device (default: auto)\n"
        "               Pulse monitor example:\n"
        "               pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor\n"
    "  -h <ms>      PTT hang time in ms      (default: %d)\n"
    "  -M <level>   MIC LED threshold        (default: %d)\n"
    "  -R <level>   RX LED threshold         (default: %d)\n"
    "  -V <pct>     VAD LED probability %%    (default: %d)\n"
    "  -E <pct>     AEC LED reduction %%      (default: %d)\n"
    "  -B <pct>     RX guard VAD boost %%     (default: %d)\n"
    "  -S <pct>     RX guard SNR %% floor     (default: %d)\n",
    prog,
    DEFAULT_HANG_MS,
    DEFAULT_MIC_LED_THRESHOLD,
    DEFAULT_RX_LED_THRESHOLD,
    DEFAULT_VAD_LED_PROB,
    DEFAULT_AEC_LED_REDUCTION,
    DEFAULT_RX_GUARD_VAD_BOOST,
    DEFAULT_RX_GUARD_SNR_PCT);
}

int main(int argc, char *argv[])
{
    const char *mic_dev = "auto";
    const char *rx_dev  = "auto";
    int hang_ms = DEFAULT_HANG_MS;
    int mic_led_threshold = DEFAULT_MIC_LED_THRESHOLD;
    int rx_led_threshold = DEFAULT_RX_LED_THRESHOLD;
    int vad_led_prob_threshold = DEFAULT_VAD_LED_PROB;
    int aec_led_reduction_pct = DEFAULT_AEC_LED_REDUCTION;
    int rx_guard_vad_boost = DEFAULT_RX_GUARD_VAD_BOOST;
    int rx_guard_snr_pct = DEFAULT_RX_GUARD_SNR_PCT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            mic_dev = argv[++i];
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
            rx_dev = argv[++i];
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
            hang_ms = atoi(argv[++i]);
        else if (strcmp(argv[i], "-M") == 0 && i + 1 < argc)
            mic_led_threshold = atoi(argv[++i]);
        else if (strcmp(argv[i], "-R") == 0 && i + 1 < argc)
            rx_led_threshold = atoi(argv[++i]);
        else if (strcmp(argv[i], "-V") == 0 && i + 1 < argc)
            vad_led_prob_threshold = atoi(argv[++i]);
        else if (strcmp(argv[i], "-E") == 0 && i + 1 < argc)
            aec_led_reduction_pct = atoi(argv[++i]);
        else if (strcmp(argv[i], "-B") == 0 && i + 1 < argc)
            rx_guard_vad_boost = atoi(argv[++i]);
        else if (strcmp(argv[i], "-S") == 0 && i + 1 < argc)
            rx_guard_snr_pct = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    AudioIOConfig acfg = {
        .mic_device = mic_dev,
        .rx_device  = rx_dev,
        .sample_rate = SAMPLE_RATE,
        .frame_size  = FRAME_SIZE,
    };
    AudioIO *audio = audio_io_open(&acfg);
    if (!audio) {
        fprintf(stderr, "Failed to open audio devices\n");
        return 1;
    }

    VoxConfig vcfg = {
        .sample_rate = SAMPLE_RATE,
        .frame_size  = FRAME_SIZE,
        .hang_ms     = hang_ms,
        .mic_led_threshold = mic_led_threshold,
        .rx_led_threshold = rx_led_threshold,
        .vad_led_prob_threshold = vad_led_prob_threshold,
        .aec_led_reduction_pct = aec_led_reduction_pct,
        .rx_guard_vad_boost = rx_guard_vad_boost,
        .rx_guard_snr_pct = rx_guard_snr_pct,
    };
    VoxState *vox = vox_create(&vcfg);
    if (!vox) {
        fprintf(stderr, "Failed to create VOX state\n");
        audio_io_close(audio);
        return 1;
    }

    int16_t mic_buf[FRAME_SIZE];
    int16_t rx_buf[FRAME_SIZE];
    VoxLedState leds_prev = {0};

        printf("VOX running. mic=%s rx=%s hang=%dms M=%d R=%d V=%d E=%d B=%d S=%d  (Ctrl-C to stop)\n",
            mic_dev, rx_dev, hang_ms,
            mic_led_threshold, rx_led_threshold,
            vad_led_prob_threshold, aec_led_reduction_pct,
            rx_guard_vad_boost, rx_guard_snr_pct);

    while (g_running) {
        if (audio_io_read(audio, mic_buf, rx_buf) < 0)
            continue;

        (void)vox_process(vox, mic_buf, rx_buf);
        VoxLedState leds = {0};
        vox_get_led_state(vox, &leds);

        if (leds.mic_led != leds_prev.mic_led ||
            leds.rx_led != leds_prev.rx_led ||
            leds.ptt_led != leds_prev.ptt_led ||
            leds.vad_led != leds_prev.vad_led ||
            leds.aec_led != leds_prev.aec_led) {
                 printf("LED MIC=%s RX=%s PTT=%s VAD=%s AEC=%s  (mic=%d rx=%d vad_raw=%d%% vad=%d%% aec_red=%d%%)\n",
                   leds.mic_led ? "ON" : "OFF",
                   leds.rx_led ? "ON" : "OFF",
                   leds.ptt_led ? "ON" : "OFF",
                   leds.vad_led ? "ON" : "OFF",
                   leds.aec_led ? "ON" : "OFF",
                   leds.mic_level,
                   leds.rx_level,
                     leds.vad_probability_raw,
                   leds.vad_probability,
                   leds.aec_reduction_pct);
            fflush(stdout);
            leds_prev = leds;
        }
    }

    printf("\nStopping.\n");
    vox_destroy(vox);
    audio_io_close(audio);
    return 0;
}
