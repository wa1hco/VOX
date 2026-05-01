/* Regression tests for VOX.
 *
 * These tests use pre-recorded raw audio files (16-bit signed, mono, 8 kHz)
 * to verify that known inputs produce expected PTT outcomes.  This guards
 * against regressions when AEC/VAD parameters are tuned.
 *
 * Audio files live in tests/regression/data/.
 * File naming convention:
 *   <name>_mic.raw   — microphone channel
 *   <name>_rx.raw    — receive audio reference channel
 *   <name>.expected  — one line: "ptt_activations=N"
 */

#include "vox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define SAMPLE_RATE  8000
#define FRAME_SIZE   160
#define HANG_MS      300

#define DATA_DIR "tests/regression/data"

static int failures = 0;

static int read_expected(const char *path, int *activations)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int ok = (fscanf(f, "ptt_activations=%d", activations) == 1) ? 0 : -1;
    fclose(f);
    return ok;
}

static int count_ptt_activations(const char *mic_path, const char *rx_path)
{
    FILE *mic_f = fopen(mic_path, "rb");
    FILE *rx_f  = fopen(rx_path,  "rb");
    if (!mic_f || !rx_f) {
        if (mic_f) fclose(mic_f);
        if (rx_f)  fclose(rx_f);
        return -1;
    }

    VoxConfig cfg = { SAMPLE_RATE, FRAME_SIZE, HANG_MS };
    VoxState *vox = vox_create(&cfg);

    int16_t mic[FRAME_SIZE], rx[FRAME_SIZE];
    int activations = 0;
    int ptt_prev = 0;

    while (fread(mic, sizeof(int16_t), FRAME_SIZE, mic_f) == (size_t)FRAME_SIZE &&
           fread(rx,  sizeof(int16_t), FRAME_SIZE, rx_f)  == (size_t)FRAME_SIZE) {
        int ptt = vox_process(vox, mic, rx);
        if (ptt && !ptt_prev)
            activations++;
        ptt_prev = ptt;
    }

    vox_destroy(vox);
    fclose(mic_f);
    fclose(rx_f);
    return activations;
}

int main(void)
{
    DIR *dir = opendir(DATA_DIR);
    if (!dir) {
        printf("No regression data directory found (%s); skipping.\n", DATA_DIR);
        return 0;
    }

    struct dirent *ent;
    int tests_run = 0;

    while ((ent = readdir(dir)) != NULL) {
        /* Look for *.expected files */
        size_t len = strlen(ent->d_name);
        if (len < 9 || strcmp(ent->d_name + len - 9, ".expected") != 0)
            continue;

        char base[512];
        snprintf(base, sizeof(base) - 1, "%s/%.*s",
                 DATA_DIR, (int)(len - 9), ent->d_name);

        char mic_path[600], rx_path[600], exp_path[600];
        snprintf(mic_path, sizeof(mic_path), "%s_mic.raw", base);
        snprintf(rx_path,  sizeof(rx_path),  "%s_rx.raw",  base);
        snprintf(exp_path, sizeof(exp_path), "%s/%s", DATA_DIR, ent->d_name);

        int expected = 0;
        if (read_expected(exp_path, &expected) < 0) {
            fprintf(stderr, "FAIL: cannot parse %s\n", exp_path);
            failures++;
            continue;
        }

        int got = count_ptt_activations(mic_path, rx_path);
        if (got < 0) {
            fprintf(stderr, "FAIL: cannot read audio for %s\n", base);
            failures++;
            continue;
        }

        tests_run++;
        if (got == expected) {
            printf("PASS: %s  (activations=%d)\n", ent->d_name, got);
        } else {
            fprintf(stderr, "FAIL: %s  expected=%d got=%d\n",
                    ent->d_name, expected, got);
            failures++;
        }
    }
    closedir(dir);

    if (tests_run == 0) {
        printf("No regression test cases found in %s.\n", DATA_DIR);
        return 0;
    }

    if (failures == 0) {
        printf("All %d regression test(s) passed.\n", tests_run);
        return 0;
    } else {
        fprintf(stderr, "%d regression test(s) failed.\n", failures);
        return 1;
    }
}
