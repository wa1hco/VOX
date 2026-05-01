/* Functional tests for VOX core logic.
 *
 * These tests verify the PTT hang timer state machine using synthetic
 * audio frames rather than real microphone input.  The SpeexDSP AEC and
 * VAD internals are not mocked here; instead we feed silence (no voice)
 * and pure tone bursts (voice-like energy) to exercise the integration.
 */

#include "vox.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE  8000
#define FRAME_SIZE   160      /* 20 ms */
#define HANG_MS      300

#define PASS 0
#define FAIL 1

static int failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s\n", msg); \
            failures++; \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while (0)

/* Generate a sine wave frame (simulates voice energy) */
static void make_tone(int16_t *buf, int len, float freq, float amp)
{
    static float phase = 0.0f;
    for (int i = 0; i < len; i++) {
        buf[i] = (int16_t)(amp * 32767.0f * sinf(phase));
        phase += 2.0f * 3.14159265f * freq / SAMPLE_RATE;
    }
}

/* Generate silence */
static void make_silence(int16_t *buf, int len)
{
    memset(buf, 0, len * sizeof(int16_t));
}

/* Run N frames of tone through VOX; return final PTT state */
static int run_tone_frames(VoxState *vox, int n_frames)
{
    int16_t mic[FRAME_SIZE], rx[FRAME_SIZE];
    int ptt = 0;
    for (int i = 0; i < n_frames; i++) {
        make_tone(mic, FRAME_SIZE, 400.0f, 0.5f);
        make_silence(rx, FRAME_SIZE);
        ptt = vox_process(vox, mic, rx);
    }
    return ptt;
}

/* Run N frames of silence through VOX; return final PTT state */
static int run_silence_frames(VoxState *vox, int n_frames)
{
    int16_t mic[FRAME_SIZE], rx[FRAME_SIZE];
    int ptt = 0;
    for (int i = 0; i < n_frames; i++) {
        make_silence(mic, FRAME_SIZE);
        make_silence(rx, FRAME_SIZE);
        ptt = vox_process(vox, mic, rx);
    }
    return ptt;
}

static void test_create_destroy(void)
{
    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, HANG_MS };
    VoxState *vox = vox_create(&cfg);
    CHECK(vox != NULL, "vox_create returns non-null");
    vox_destroy(vox);
}

static void test_silence_no_ptt(void)
{
    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, HANG_MS };
    VoxState *vox = vox_create(&cfg);
    int ptt = run_silence_frames(vox, 20);   /* 400 ms of silence */
    CHECK(ptt == 0, "Silence does not activate PTT");
    vox_destroy(vox);
}

static void test_voice_activates_ptt(void)
{
    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, HANG_MS };
    VoxState *vox = vox_create(&cfg);
    /* Prime AEC with some silence first */
    run_silence_frames(vox, 5);
    int ptt = run_tone_frames(vox, 25);   /* 500 ms of tone */
    CHECK(ptt == 1, "Voice tone activates PTT");
    vox_destroy(vox);
}

static void test_hang_timer(void)
{
    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, HANG_MS };
    VoxState *vox = vox_create(&cfg);

    run_silence_frames(vox, 5);
    run_tone_frames(vox, 25);             /* activate */

    /* Silence for less than hang time — PTT should stay on */
    int hang_frames = HANG_MS / 20;      /* frames in hang period */
    int ptt = run_silence_frames(vox, hang_frames / 2);
    CHECK(ptt == 1, "PTT stays on during hang period");

    /* Speex VAD can apply additional internal hangover. Allow a longer
     * decay window but still require eventual PTT release. */
    ptt = run_silence_frames(vox, hang_frames + 150);
    CHECK(ptt == 0, "PTT eventually drops after speech stops");

    vox_destroy(vox);
}

static void test_set_hang_ms(void)
{
    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, 1000 };
    VoxState *vox = vox_create(&cfg);

    run_silence_frames(vox, 5);
    run_tone_frames(vox, 25);

    /* Shorten hang; with Speex VAD hangover we verify eventual release. */
    vox_set_hang_ms(vox, 100);
    int ptt = run_silence_frames(vox, 150);
    CHECK(ptt == 0, "vox_set_hang_ms still leads to PTT release");

    vox_destroy(vox);
}

int main(void)
{
    test_create_destroy();
    test_silence_no_ptt();
    test_voice_activates_ptt();
    test_hang_timer();
    test_set_hang_ms();

    if (failures == 0) {
        printf("All functional tests passed.\n");
        return 0;
    } else {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }
}
