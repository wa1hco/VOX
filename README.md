# VOX — Voice Operated Switch for Ham Radio

A small-board VOX circuit using adaptive echo cancellation (AEC) and voice activity detection (VAD) to reliably key PTT from microphone audio while ignoring receive audio and background noise.

## Acronyms

- **AEC**: Adaptive Echo Cancellation
- **VAD**: Voice Activity Detection

## Features

- **AEC** — removes receive audio leaking into the microphone (anti-VOX), using SpeexDSP
- **VAD** — detects operator voice, ignores room noise, using SpeexDSP
- **Single control** — PTT hang time (ms); no level adjustments needed
- **3 LED status outputs**
	- `MIC LED`: microphone port activity/level present
	- `RX LED`: receive-audio port activity/level present
	- `VAD LED`: speech probability indicates likely operator voice
	- `AEC LED`: receive-audio leakage is being reduced by echo cancellation
	- `PTT LED`: transmit keying output is active
- **Linux simulation** — runs on a Linux PC for development and testing
- **MCU portable** — core library has no platform dependencies

## Building (Linux)

### Prerequisites

```sh
sudo apt install cmake libasound2-dev libspeexdsp-dev
```

For the Qt GUI on Linux:

```sh
sudo apt install qt6-base-dev
```

### Build

```sh
mkdir build && cd build
cmake ..
make
```

### Run

```sh
./vox_linux -h 500
```

Qt GUI run:

```sh
./vox_qt
```

The Qt test GUI shows:
- microphone level bargraph
- VAD output score (0-100)
- speaker monitor level bargraph
- AEC score (estimated reduction %)
- PTT ON/OFF indicator
- MCU-style LED panel: MIC, RX, VAD, AEC, PTT
- live tuning sliders for:
	- PTT hang time
	- MIC activity threshold
	- RX activity threshold
	- VAD probability threshold
	- AEC reduction threshold

By default on Linux desktop systems, `vox_linux` auto-selects:
- the default microphone source for `MIC`
- the monitor source of the default speaker sink for `RX`

Options:
- `-m <device>` — microphone capture device (default `auto`)
- `-r <device>` — receive-reference capture device (default `auto`)
- `-h <ms>` — PTT hang time in milliseconds (default `500`)
- `-M <level>` — MIC LED activity threshold (default `300`)
- `-R <level>` — RX LED activity threshold (default `300`)
- `-V <pct>` — VAD LED probability threshold percent (default `60`)
- `-E <pct>` — AEC LED reduction threshold percent (default `20`)

Device string formats:
- ALSA: `hw:0,0`, `plughw:CARD=PCH,DEV=0`, `alsa:hw:0,0`
- Pulse source: `pulse:<source_name>`

Laptop realistic one-command test (mic + speaker playback monitor):

```sh
./vox_linux \
	-m pulse:alsa_input.pci-0000_00_1f.3.analog-stereo \
	-r pulse:alsa_output.pci-0000_00_1f.3.analog-stereo.monitor \
	-h 500
```

You can list available Pulse source names with:

```sh
pactl list short sources
```

The Linux runner prints LED transitions:
- `MIC` LED from raw mic level activity
- `RX` LED from receive reference level activity
- `PTT` LED from VOX state machine
- `VAD` LED from Speex VAD decision/probability
- `AEC` LED from measured mic-energy reduction while RX reference is active

## Tests

```sh
cd build
ctest --output-on-failure
```

Functional tests use synthetic audio. Regression tests load raw audio files from `tests/regression/data/` (see that directory for file format).

## Project Structure

```
src/            Core library: vox.c, aec.c, vad.c
platform/linux/ Linux ALSA audio and main entry point
platform/mcu/   MCU board pin maps and MCU integration scaffolding
tests/          Functional and regression tests
docs/           User manual and design description
```

## STM32G474 Setup (Scaffold)

VOX now includes an initial STM32G474 board scaffold using a Rotator-style
pin definition table. This keeps all board wiring in one header and exposes a
single pin-config struct for MCU glue code.

ADC/DSP sample-rate plan:
- ADC front end runs at 32 MHz
- Decimation factor is 4000
- AEC/VAD processing runs at 8 kHz

Key files:
- `platform/mcu/vox_pins_stm32g474_hco_board_v4.h` — board pin definitions
- `platform/mcu/vox_mcu_pins.h` — encoded pin helper macros
- `platform/mcu/vox_mcu_board.h` — pin-config interface
- `platform/mcu/stm32g474_board.c` — concrete exported pin table
- `platform/mcu/vox_mcu_decimator.h` + `.c` — ADC decimation frontend (32 MHz to 8 kHz)

Enable the MCU scaffold target:

```sh
cmake -S . -B build -DPLATFORM_LINUX=ON -DPLATFORM_STM32G474=ON
cmake --build build
```

This does not yet include STM32 HAL/LL runtime drivers. It is the first step
to define and standardize STM32G474 board wiring before adding ADC, GPIO, and
PTT runtime integration.

## Documentation

- `docs/user_manual.md` — installation and operation
- `docs/design_description.md` — architecture and algorithm details

## License

TBD
