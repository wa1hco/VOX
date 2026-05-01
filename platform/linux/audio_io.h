#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Linux audio I/O abstraction.
 *
 * Device string format:
 * - Auto:  NULL, empty, or "auto" selects the default Pulse mic for MIC
 *          and the default Pulse sink monitor for RX.
 * - ALSA:  "hw:0,0", "plughw:CARD=PCH,DEV=0", "dsnoop:CARD=PCH,DEV=0"
 * - ALSA explicit: "alsa:<pcm_name>"
 * - Pulse source:  "pulse:<source_name>"
 *   Example: pulse:alsa_input.pci-0000_00_1f.3.analog-stereo
 *   Example: pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
 */

typedef struct audio_io AudioIO;

typedef struct {
    const char *mic_device;
    const char *rx_device;
    int sample_rate;
    int frame_size;
} AudioIOConfig;

AudioIO *audio_io_open(const AudioIOConfig *cfg);
int audio_io_read(AudioIO *audio, int16_t *mic_out, int16_t *rx_out);
void audio_io_close(AudioIO *audio);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_IO_H */
