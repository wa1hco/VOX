#ifndef AUDIO_ALSA_H
#define AUDIO_ALSA_H

#include <stdint.h>

/* ALSA audio I/O for Linux simulation.
 *
 * Opens two capture devices (microphone and receive audio reference)
 * and provides synchronised frame reads.
 */

typedef struct audio_alsa AudioAlsa;

typedef struct {
    const char *mic_device;   /* e.g. "hw:0,0" */
    const char *rx_device;    /* e.g. "hw:1,0" */
    int sample_rate;          /* Hz */
    int frame_size;           /* samples per read */
} AudioAlsaConfig;

/* Open audio devices. Returns NULL on failure. */
AudioAlsa *audio_alsa_open(const AudioAlsaConfig *cfg);

/* Read one frame from each device.
 * mic_out : microphone samples (length = frame_size)
 * rx_out  : receive reference samples (length = frame_size)
 * Returns 0 on success, -1 on error. */
int audio_alsa_read(AudioAlsa *audio, int16_t *mic_out, int16_t *rx_out);

void audio_alsa_close(AudioAlsa *audio);

#endif /* AUDIO_ALSA_H */
