# VOX user stories

The "VOX app" referenced below is the Qt application currently named
`vox_qt`.  It runs against either a local Linux simulation (`vox_core`
in-process, PC audio in/out) or a connected VOX dongle (USB CDC).

## Stories

### Story 1 — Developer tests a new feature

**Persona.** Developer working on the VOX algorithm or the dongle firmware.

**Wants to:**
- Push new firmware to a dongle (USB)
- Read the current firmware revision back from the dongle
- See live signal levels (mic raw, mic post-AEC, RX reference, etc.)
- See internal algorithm state (VAD probability, SNR, hang frames,
  AEC reduction, residual-echo correlation profile, …)
- Adjust algorithm tuning knobs at runtime
- Inject test signals (canned PCM, live PC audio, sweep tones) instead
  of relying on a real microphone — *not always*, but as a debugging
  aid when reproducing an issue away from a radio

**So that:** they can iterate on a change end-to-end (build → flash →
observe → tune → ship) without rebuilding the GUI or moving the
hardware off the bench.

### Story 2 — Developer reviews an integration problem

**Persona.** Same developer, but now investigating a problem reported
in a dongle that's already deployed and running.

**Wants to:** plug the app into a *running* dongle (no flash, no power
cycle), and immediately see firmware revision, signal levels, internal
states, and current settings — read-only or read-mostly.

**So that:** they can characterize the failure in situ without
disturbing the system that's exhibiting it.

### Story 3 — End user adjusts settings

**Persona.** Ham radio operator using a VOX dongle in their station.

**Wants to:**
- Plug the app into a running dongle
- Adjust a *small* set of practical settings (PTT hang time, a master
  sensitivity, maybe an AEC on/off toggle)
- See a *simplified* status display: PTT on/off, mic-active light,
  RX-active light, "is the chip alive" indication

**So that:** they can fit the dongle to their voice / mic / radio
without becoming a DSP engineer.

### Story 4 — End user updates firmware

**Persona.** Same operator.

**Wants to:** receive a firmware update from upstream and apply it
through the same app, without flashing tools, command lines, or
power-cycling tricks.

**So that:** updates are routine, not a project.

## Cross-story analysis

### Two orthogonal modes

The four stories sort along two independent axes:

|                   | **Sim source** (PC audio → in-process `vox_core`) | **Dongle source** (USB CDC → chip's `vox_core`) |
|-------------------|---------------------------------------------------|------------------------------------------------|
| **Developer face** | Story 1 *(when no dongle handy)*                  | Stories 1, 2                                   |
| **Operator face**  | n/a (operators don't run a Linux sim)             | Stories 3, 4                                   |

Three real combinations: dev-sim, dev-dongle, operator-dongle.

### Capability matrix

What each face needs visible/exposed:

| Capability                              | Dev face | Operator face |
|-----------------------------------------|:--------:|:-------------:|
| FW revision readout                     |    ✓     |       ✓       |
| FW update                               |    ✓     |       ✓       |
| Live mic / RX levels                    |    ✓     |   simplified  |
| All algorithm state (VAD, SNR, AEC, …)  |    ✓     |     hidden    |
| Residual correlation profile            |    ✓     |     hidden    |
| Time-series history plot                |    ✓     |     hidden    |
| Tuning sliders (full set, 7 knobs)      |    ✓     |     hidden    |
| Tuning sliders (subset: hang + master)  |    ✓     |       ✓       |
| Test-signal injection                   |    ✓     |     hidden    |
| LED panel (MIC/RX/VAD/AEC/PTT)          |    ✓     |   simplified  |

### Architectural implications

1. **One app with two faces, not two apps.** Same approach as the
   sim-vs-dongle source decision: the operator face is a subset of the
   dev face, not a re-implementation.  Implement as collapsible sections
   or a "View → Operator / Developer" toggle that hides advanced
   widgets.  Reuse all the algorithm-display code; just gate visibility.

2. **Read-only vs. read-write modes are explicit.** Story 2 is
   read-mostly observation of a live unit; Story 3 is interactive
   adjustment.  The app needs to make destructive operations (flash,
   reset, restore-defaults) deliberate (confirm dialogs, dev-face only
   or "expert" toggles).

3. **The dongle wire protocol is the contract.**  All four stories
   except dev-sim depend on it.  The shape of the protocol's "hello"
   message determines Stories 1 + 2 + 4 (revision readout, capability
   list, FW update entry).

4. **FW update path needs design.** Two viable routes:
   - **STM32 native USB DFU bootloader** (BOOT0 high → MCU enumerates as
     a DFU device; `dfu-util` flashes).  Standard, no app code on the
     chip, but users have to set BOOT0 (jumper or soft entry).
   - **In-application update over USB CDC** with a small bootloader the
     factory firmware never overwrites.  More chip-side work; better UX.
     Probably the right answer for Story 4.
   This is design work that should land before either story is actively
   implemented.

5. **Test injection is now confirmed scope, not a curiosity.** Story 1
   asks for "test injection" explicitly.  PC-audio-over-USB to chip
   (already in TODO.md) is the natural mechanism — feed `vox_process`
   from a host-side PCM source instead of the ADC.

## Mapping to current code

What we have today, by story:

| Story | Today                                                         | Gap |
|-------|---------------------------------------------------------------|-----|
| 1 (dev-sim part) | `vox_qt` runs vox_core on PC audio with full diagnostic UI | none significant |
| 1 (dev-dongle)   | nothing                                                     | dongle protocol; dongle-source mode in vox_qt; FW push; rev readout; test injection |
| 2     | nothing                                                       | dongle protocol; dongle-source mode (read-mostly variant) |
| 3     | nothing                                                       | dongle protocol; operator face; subset tuning UI |
| 4     | nothing                                                       | FW update path (DFU vs. CDC bootloader); UX in operator face |

The Linux-side simulator is the one face that's basically built.
Everything else converges on the dongle protocol header as the next
piece of design work that unblocks a lot.
