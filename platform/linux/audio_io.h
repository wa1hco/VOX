#ifndef AUDIO_IO_H
#define AUDIO_IO_H

#include <stdint.h>
#include <stdio.h>

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

/*
 * audio_io_list_devices — enumerate available capture devices to `out`.
 *
 * Produces a human-readable listing intended for `vox_linux --list-devices`:
 *   - PulseAudio sources       (mic candidates for `-m pulse:<source>`)
 *   - PulseAudio sink monitors (rx candidates for `-r pulse:<monitor>`)
 *   - ALSA capture devices     (alternate `-m alsa:hw:X,Y` form)
 *
 * Returns 0 on success, nonzero if the underlying tools (pactl, arecord)
 * are missing.  The function never exits; it only prints.
 */
int audio_io_list_devices(FILE *out);

/*
 * audio_io_enumerate — same enumeration as audio_io_list_devices, but
 * returned as a structured array for the GUI to populate dropdowns
 * with.  Both the CLI helper above and the GUI go through this one
 * function.
 *
 * Caller passes an array `out_devices` of capacity `out_capacity`.
 * Returns the total number of devices found (may be > out_capacity if
 * the array was too small; in that case only out_capacity entries are
 * filled).
 *
 * Each device's `id` is the exact string to pass back as `mic_device`
 * or `rx_device` in AudioIOConfig (e.g. "pulse:<source-name>" or
 * "alsa:hw:1,0").  `display` is a short human-friendly label.  `kind`
 * tells the caller whether it's a mic candidate, an rx candidate, or
 * both.
 */
typedef enum {
    AUDIO_DEV_MIC = 1u << 0,    /* suitable as a mic input  */
    AUDIO_DEV_RX  = 1u << 1,    /* suitable as an rx-reference (sink monitor) */
} AudioDeviceKind;

typedef struct {
    unsigned kind;          /* OR of AUDIO_DEV_* bits */
    char     id[256];       /* canonical ID for AudioIOConfig.mic/rx_device */
    char     display[256];  /* short human-readable label */
} AudioDeviceInfo;

int audio_io_enumerate(AudioDeviceInfo *out_devices, int out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_IO_H */
