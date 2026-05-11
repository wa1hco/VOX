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
- `--list-devices` — print available capture devices and exit
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

To pick a specific mic + speaker-monitor pair on the current host, list the
devices first and copy two strings into `-m` / `-r`:

```sh
./vox_linux --list-devices
./vox_linux -m pulse:<your_mic_source> -r pulse:<your_sink>.monitor -h 500
```

The exact source names differ per machine — running across the desktop and
laptop is expected to give different `-m`/`-r` values.

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
src/                              Core library: vox.c, aec.c, vad.c
platform/linux/                   Linux ALSA/Pulse audio + Qt GUI
platform/mcu/
  core/                           Platform-independent MCU scaffold
    vox_mcu_board.h               Pin-config interface (VoxMcuPinConfig)
    vox_mcu_pins.h                GPIO port/pin uint8 encoding
    vox_mcu_decimator.{h,c}       32 MHz ADC → 8 kHz DSP CIC decimator
  boards/
    common/
      hco_pin_assignment.h        Shared HCO logical pin map
    stm32g474_vox_cb/             Custom STM32G474CBT3 board (LQFP48)
      board_pins.h                LQFP48 pin table + analog-front-end notes
      board.c                     Pin-config descriptor
      board.cmake                 Per-board CMake fragment
    stm32g474_nucleo/             NUCLEO-G474RE + HCO-mirrored shield
      board_pins.h                LQFP64 pin table + Nucleo-specific notes
      board.c
      board.cmake
cmake/
  arm-none-eabi.cmake             ARM cross-compile toolchain file
tests/                            Functional and regression tests (host)
docs/                             User manual and design description
```

## MCU builds (STM32G474)

The current MCU firmware is **bring-up only**: it blinks an LED, prints a
counter once a second on the debug UART, and proves the cross toolchain
+ linker script + flash workflow is alive.  ADC, OPAMP, DMA, USB, and
the actual VOX algorithm wiring land on top of this in later slices.

### Boards supported

- `stm32g474_nucleo` — NUCLEO-G474RE dev board with a prototype shield
  that mirrors the HCO custom-board pinout.  Bring-up uses the on-board
  ST-Link VCP (USART2 PA2/PA3 → `/dev/ttyACM0`).
- `stm32g474_vox_cb` — VOX HCO custom board (STM32G474CBT3, LQFP48).
  Bring-up uses USART1 PA9/PA10 with an external 3.3V USB-serial adapter.

Both boards share the same logical GPIO assignment via
`platform/mcu/boards/common/hco_pin_assignment.h`, so most MCU code is
identical between them.  Per-board files describe the package pinout
and any board-specific quirks (e.g. on the Nucleo, PA5 also drives the
on-board LD2 user LED, which is benign — it just mirrors `LED_MIC`).

Sample-rate plan (deferred until ADC driver lands):
- ADC front end runs at 32 kHz (4x oversampled vs the DSP rate)
- CIC decimation factor 4 → 8 kHz, gain R²=16 (no overflow concerns)
- AEC/VAD processing runs at 8 kHz

### One-time toolchain install (Ubuntu 24.04)

```sh
sudo apt-get update
sudo apt-get install -y \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    stlink-tools openocd \
    picocom
```

`gcc-arm-none-eabi` is the Cortex-M cross compiler; `stlink-tools`
provides `st-flash` / `st-info`; `openocd` is an alternative flasher
and SWD debug bridge; `picocom` is a tiny serial terminal for watching
UART output (`screen` or `minicom` work too).

After install, sanity-check:

```sh
arm-none-eabi-gcc --version       # expect 13.2.x
st-info --probe                   # expect to see a connected ST-Link if Nucleo plugged in
```

### Build, flash, and watch (Nucleo G474RE)

Plug the Nucleo into USB.  Linux should enumerate two devices:
- An ST-Link debug interface
- A USB Mass Storage drive labelled `NODE_G474RE` (for drag-drop flashing)
- A virtual COM port at `/dev/ttyACM0` (115200 8N1)

Then:

```sh
# Configure (cross-compile)
cmake -S . -B build-nucleo \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
      -DBOARD=stm32g474_nucleo

# Build the firmware (.elf + .bin + size report)
cmake --build build-nucleo

# Flash to the chip via the on-board ST-Link
cmake --build build-nucleo --target flash

# Open the VCP in another terminal to see "tick=N" lines and confirm life
picocom -b 115200 /dev/ttyACM0
#   ↑  Ctrl-A Ctrl-X to quit picocom
```

Expected output on `/dev/ttyACM0`:

```
VOX bringup: board=stm32g474_nucleo  sysclk=16MHz (HSI16)
LD2 (PA5) should be blinking at ~1 Hz.
tick=0
tick=1
tick=2
...
```

LD2 (the green LED next to the USER button) blinks at roughly 1 Hz.

### Build and flash (custom STM32G474CBT3 board)

```sh
cmake -S . -B build-vox-cb \
      -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
      -DBOARD=stm32g474_vox_cb
cmake --build build-vox-cb
cmake --build build-vox-cb --target flash    # needs an external SWD probe
```

There is no on-board ST-Link, so connect an external probe (a Nucleo
used as ST-Link works) to the SWD pads, and an external 3.3V USB-serial
adapter to PA9/PA10 to see the `tick=N` output.

### Sanity-check the pin descriptor without a cross toolchain

If you want to verify the board descriptor + decimator compile but
haven't installed `arm-none-eabi-gcc` yet:

```sh
cmake -S . -B build-nucleo-host -DBOARD=stm32g474_nucleo
cmake --build build-nucleo-host --target vox_mcu_board
```

This builds `libvox_mcu_board.a` with the host gcc — the firmware
executable is intentionally skipped in this mode.

## Documentation

Design docs (in `docs/`):

- [user_stories.md](docs/user_stories.md) — the four user stories the
  app needs to serve (developer + operator faces)
- [dongle_protocol.md](docs/dongle_protocol.md) — wire protocol
  between vox_qt and the dongle firmware (frame format, message
  catalog, capability bits)
- [firmware_update.md](docs/firmware_update.md) — how dongle firmware
  updates work: USB DFU via the chip's ROM bootloader, soft-jumped
  from the running application via a `SET_MODE` command

Still to come:

- `user_manual.md` — installation and operation (placeholder)
- `design_description.md` — architecture and algorithm details (placeholder)

## License

TBD
