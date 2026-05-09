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

User stories that drive the priorities below: [docs/user_stories.md](docs/user_stories.md)

---

## GUI — vox_qt (Linux sim, dongle client, dev + operator faces)

Architecture: one app, two source modes (sim ↔ dongle) and two faces
(developer ↔ operator).  Three real combinations: dev-sim, dev-dongle,
operator-dongle.  Operator face is a *subset* of the dev face — same
codebase, gated visibility.  See:
- `~/.claude/projects/-home-jeff-ham-VOX/memory/gui-one-app-two-modes.md`
- `~/.claude/projects/-home-jeff-ham-VOX/memory/gui-light-mode.md`
- [docs/user_stories.md](docs/user_stories.md)

### High priority — unblocks dongle stories
- [ ] **Dongle protocol header** — `tools/vox_dongle_proto.h` shared between firmware and GUI.  Frame struct (revision + `VoxLedState` + `VoxDebugState` + sequence number), tuning-write opcodes, framing format, "hello" handshake (firmware reports its rev + capability bitset).  Pure design work; unblocks Stories 1, 2, 3, 4.
- [ ] **Operator-face / Developer-face toggle** — view menu radio that hides advanced widgets (residual plot, debug grid, decision summary, advanced sliders) for Story 3.  All advanced widgets stay in the layout, just hidden.

### Medium priority — sim-only, useful now
- [ ] **Device picker dropdowns** — currently mic/rx are `auto`-only.  CLI has `-m/-r`, GUI doesn't.  Helps when running the same binary on desktop vs. laptop.
- [ ] **Tuning save/load to a JSON file** — sliders reset to defaults on every restart.  Operator and developer both want their tuning to survive.
- [ ] **Snapshot button on the time-series plot** — dump current 30 s of history to CSV for later analysis.  Single click; complements the 10–30 s window with a way to keep "this moment was interesting."
- [ ] **CSV / JSONL log export** — continuous version of the snapshot button; per-frame state to a file for offline grep / Python analysis.
- [ ] **Pause / resume scrolling + click-to-pin cursor** in the time-series plot.

### Lower priority — refinements & specialty diagnostics
- [ ] **Audio waveform / scope mode** — raw mic/rx scope at 8 kHz for diagnosing analog issues before AEC even runs.
- [ ] **FFT / spectrogram strip** — for finding mains hum, whistles, etc.
- [ ] **A/B preset compare** — two slots, switch instantly, see PTT decisions diverge.
- [ ] **Wider slider ranges** — current ranges (e.g. mic threshold 50–4000) might miss some radios.

### Dongle mode (post-shield, post-USB-CDC firmware)
- [ ] **"Connect to dongle" run-mode** in vox_qt — open `/dev/ttyACMx`, parse protocol frames, render same widgets.  Story 1 (dev-dongle) + Story 2 (review).
- [ ] **Bidirectional tuning** — slider moves → write opcode → dongle's `vox_set_tuning` runs on chip.  Story 1, Story 3.
- [ ] **Firmware revision readout** displayed somewhere prominent (status bar?) and on the protocol "hello" handshake.  Stories 1, 2, 4.
- [ ] **Firmware update path** — design first: STM32 native USB DFU bootloader (BOOT0-driven) vs. in-application update over USB CDC with a small persistent bootloader.  Story 1 + Story 4.
- [ ] **Test-signal injection over USB CDC** — pipe canned PCM, sine sweep, or live PC audio from vox_qt to the chip; chip runs `vox_process` on the injected signal in place of ADC frames.  Story 1 explicitly calls this out.  Bandwidth: 256 kbit/s in + 32 kbit/s out = <3% of USB FS; CPU: needs slice D's PLL up first.  Useful for validating the algorithm on real silicon before the analog shield exists.

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
- [ ] **Slice D: PLL clock + flash wait states** — run at ~150 MHz instead of 16 MHz HSI.  Required before AEC's FFT load is comfortable in real time, and before any USB-CDC streaming approach (test injection, full-rate state) becomes feasible.
- [ ] **Slice E: ADC + DMA + decimator** — sample mic/rx at 32 kHz via timer-triggered ADC, DMA into ring, run the existing CIC decimator down to 8 kHz frames.
- [ ] **Slice F: vox_process on real audio** — swap synthetic input source for the ADC ring (depends on slice C blocker resolved).
- [ ] **Slice G: USB CDC-ACM on PA11/PA12** — replaces UART; carries the dongle protocol (Stories 1–4 read/write, test injection, FW update).
- [ ] **Slice H (custom CB only): OPAMP front-end init** — OPAMP1/2/3 cascade + OPAMP4 Vmid follower with the offset calibration sequence documented in `boards/stm32g474_vox_cb/board_pins.h`.
- [ ] **Slice I: Firmware revision string and capability bits** — embed a `VOX_FIRMWARE_REVISION` constant in flash; emit it on the dongle-protocol "hello" frame.  Stories 1, 2, 4.
- [ ] **Slice J: In-application FW update** (or DFU jump) — depends on the design choice above.

---

## Linux side / build

- [ ] **Newlib stub warnings on cross link** — `_close`/`_fstat`/`_isatty`/`_lseek`/`_read`/`_write` "not implemented and will always fail".  All harmless (gc-sections drops them), but suppress them or provide proper stubs to keep the build log clean.
- [ ] **README.md → docs/ stubs** — README references `docs/user_manual.md` and `docs/design_description.md` that don't exist.  Either create them or remove the references.  (`docs/user_stories.md` does exist now.)
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
- [x] Scrolling time-series plot in vox_qt — *done in commit `82272fc`*
- [x] Light-mode sweep across all panels in vox_qt — *done in commit `82272fc`*
