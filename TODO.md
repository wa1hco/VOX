# VOX project TODO

Cross-session task queue.  In-session granular subtasks live in the
TodoWrite tool; this file is for the bigger items: design decisions in
flight, queued features, low-priority bugs we don't want to forget.

Edit freely — anything ticked off can stay as history or be deleted.

Convention:
- `[ ]` queued, `[~]` in progress, `[x]` done
- Each item: short name, then a one-line "why" in italics if it isn't obvious
- New items at the bottom of their section unless they replace an existing one

Last touched: 2026-05-09

---

## GUI — vox_qt (Linux sim and, later, dongle client)

Architecture: one app, two run modes (sim mode = today; dongle mode =
post-shield, talks to STM32 over USB CDC).  Both modes share the same
widget layer; the difference is who produces the per-frame state.
See `~/.claude/projects/-home-jeff-ham-VOX/memory/gui-one-app-two-modes.md`.

### In progress
- [~] **Scrolling time-series plot** — stacked strips for levels/SNR/VAD/PTT, 30 s window
  - *Biggest debugging-power-up the GUI could get; lets you see why PTT misfired 3 s ago*
  - Design agreed (4 stacked panels, dBFS/dB/%/on-off units, ring buffer, ~280 px height)
  - Next: implement `TimeSeriesPlot` widget in `platform/linux/main_qt.cpp`

### Queued (rough priority order)
- [ ] **Device picker dropdowns in the GUI** — currently the sliders drive `vox_set_tuning` but mic/rx are `auto`-only.  CLI has `-m/-r`, GUI doesn't.
- [ ] **Tuning save/load to a JSON file** — sliders reset to defaults on every restart; need persistence so "this is my tuning for this radio" survives.
- [ ] **CSV / JSONL log export** — record button writes per-frame state for offline analysis; complements the plot for multi-minute captures.
- [ ] **Dongle protocol header** — `tools/vox_dongle_proto.h` shared between firmware and GUI: frame struct (`VoxLedState + VoxDebugState` + sequence number), tuning-write opcodes, framing.  Pure design work; unblocks dongle mode.
- [ ] **Pause/resume scrolling** in the time-series plot, click to pin a value cursor.
- [ ] **Audio waveform / scope mode** — raw mic/rx scope at 8 kHz for diagnosing analog issues before AEC even runs.
- [ ] **FFT / spectrogram strip** — for finding mains hum, whistles, etc.
- [ ] **Snapshot button** — capture last 30 s of mic+rx PCM + state to a file when something interesting happens.
- [ ] **A/B preset compare** — two slots, switch instantly, see PTT decisions diverge.
- [ ] **Wider slider ranges** — current ranges (e.g. mic threshold 50–4000) might miss some radios.

### Dongle mode (post-shield, post-USB-CDC firmware)
- [ ] **Add "connect to dongle" run-mode** to vox_qt: open `/dev/ttyACMx`, parse the protocol frames, render same widgets.
- [ ] **Bidirectional tuning** — slider moves → write opcode → dongle's `vox_set_tuning` runs on chip.
- [ ] **PC-audio-over-USB development mode** — pipe live PC mic + speaker-monitor PCM streams from vox_qt over USB CDC to the chip, run `vox_process` on real silicon, return state frames.  Lets us validate the algorithm path on the chip before the analog shield exists.  Bandwidth: 256 kbit/s in + 32 kbit/s out = <3% of USB FS.  CPU: needs slice D PLL up to 150–170 MHz first (HSI16 is borderline-to-impossible for 50 fps speex).

---

## MCU firmware

### Blocker (slice C completion)
- [ ] **FFT-direction trap in `vox_process`** — speexdsp's `kiss_fftr2` finds `t->forward->substate->inverse == 1` on first call.  Stack:
  `preprocess_analysis → spx_fft → kiss_fftr2 → speex_fatal → exit → _exit`.
  Diagnostic context preserved in the long block comment at the disabled `vox_process` site in `platform/mcu/boards/stm32g474_nucleo/main.c`.
  Things to try, roughly cheapest first:
  - [ ] Inspect `t->forward->substate->inverse` via openocd at the moment `vox_create` returns (tells us whether the field is wrong from init or gets corrupted later)
  - [ ] Try `FIXED_POINT` speexdsp instead of `FLOATING_POINT` (sidesteps FPU entirely; if it works, points to FPU/lazy-stacking)
  - [ ] Try newer `gcc-arm-none-eabi` (current 13.2.1; 14.x is out)
  - [ ] Bisect by reducing AEC tail / preprocess complexity to see at what point it stops failing

### Bring-up nice-to-haves
- [ ] **Replace busy-wait `delay_ms` with `vox_delay_ms`** in any old bring-up code paths (slice C uses SysTick already; bring-up firmware predated it).
- [ ] **Heartbeat blink on LD2 (PA5)** in addition to LED_PTT, so plug-in-power-cycle gives instant visual confirmation without needing a serial terminal.
- [ ] **Wire the linker `_min_stack_size = 0x4000` constant** to match the runtime `VOX_HEAP_STACK_GUARD` via a single source so they can't drift apart.

### Roadmap (future slices, in order)
- [ ] **Slice D: PLL clock + flash wait states** — run at ~150 MHz instead of 16 MHz HSI.  Required before AEC's FFT load is comfortable in real time.
- [ ] **Slice E: ADC + DMA + decimator** — sample mic/rx at 32 kHz via timer-triggered ADC, DMA into ring, run the existing CIC decimator down to 8 kHz frames.
- [ ] **Slice F: vox_process on real audio** — swap synthetic input source for the ADC ring (depends on slice C blocker resolved).
- [ ] **Slice G: USB CDC-ACM on PA11/PA12** — replaces UART; eventually carries the dongle protocol from the GUI section above.
- [ ] **Slice H (custom CB only): OPAMP front-end init** — OPAMP1/2/3 cascade + OPAMP4 Vmid follower with the offset calibration sequence documented in `boards/stm32g474_vox_cb/board_pins.h`.

---

## Linux side / build

- [ ] **Newlib stub warnings on cross link** — `_close`/`_fstat`/`_isatty`/`_lseek`/`_read`/`_write` "not implemented and will always fail".  All harmless (gc-sections drops them), but suppress them or provide proper stubs to keep the build log clean.
- [ ] **`docs/` directory referenced by README.md doesn't exist** — either create it with `user_manual.md` and `design_description.md` stubs, or remove the README references.
- [ ] **Tests don't cover the new vox_core configuration knobs** (`rx_guard_vad_boost`, `rx_guard_snr_pct`).  Add functional tests.

---

## Hardware (separate stream of work, tracked here for visibility)

Not currently being touched in code-side sessions; the hardware/VOXboard
KiCad work is happening on the user's side.  Listed here so we don't
forget about them when they intersect with firmware work.

- [ ] **Build the analog front end** for the Nucleo prototype shield.
- [ ] **Power up and bench-test the custom CBT3 board** once layout finalizes.

---

## Resolved / archived

- [x] Reorganize `platform/mcu` into core/ + boards/ — *done in commit `e2e8bd5`*
- [x] ARM cross toolchain + `-DBOARD=…` selection — *done*
- [x] FetchContent SpeexDSP-1.2.1 for ARM cross build — *done*
- [x] Bring-up firmware (blink + UART) on Nucleo — *done; survived power cycle*
- [x] Heap (`_sbrk`) + SysTick + 16 KB stack — *done; `vox_create` succeeds on chip*
- [x] Add `--list-devices` to `vox_linux` for desktop/laptop portability — *done*
- [x] Delete CubeMx ioc and dead F411 board files — *done*
