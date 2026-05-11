# VOX dongle firmware update design

How the VOX dongle accepts new firmware over its single USB cable,
without external programmers, BOOT0 jumpers, or any physical
interaction from the user beyond clicking "Update" in the app.

Status: **design only**.  Implementation is queued as slice J in
[../TODO.md](../TODO.md); this doc captures the plan so the design
decisions are durable across sessions and don't get re-litigated.

## Constraints

- The dongle ships with **one USB connector**, wired to PA11/PA12
  (the STM32G474's native USB peripheral).
- **No on-board ST-Link.**  No SWD header visible to end users.
- The same USB connector must carry: normal-mode bidirectional
  protocol traffic ([dongle_protocol.md](dongle_protocol.md)),
  firmware updates, and revision readout.
- End users must not see a command line.  The flow is "click Update,
  pick the new .bin, wait for progress bar, done."
- We don't want to write a custom in-application bootloader if we can
  avoid it — that's another chunk of safety-critical code to
  maintain.

## Choice: use ST's ROM bootloader via USB DFU + soft-jump

Every STM32G4 ships with a **factory bootloader burned into System
Memory** at flash address `0x1FFF0000`.  It can't be erased; it's
always there.  When BOOT0 is high at reset, the chip executes the ROM
bootloader instead of the application.  The G4's bootloader supports
several host interfaces; the one we care about is **USB DFU** (ST
DFuSe-compatible).

We don't need to write any of that.  We just need to **transfer
control** to it from our running firmware, and have the host run
`dfu-util` (or a libusb equivalent) when the device re-enumerates as
a DFU device.

The competitor approach — an in-application updater with our own
bootloader partition — was considered and rejected:

| | ROM bootloader + soft-jump | In-app updater |
|---|---|---|
| FW we own that handles flash writes | none | several hundred LOC, safety-critical |
| Risk of bricking firmware during update | low (ROM is tested forever) | medium (our bug → bad flash) |
| Boot time | normal (single boot path) | bootloader → app chain (slightly slower) |
| Recovery from bad upload | needs BOOT0 + dfu-util | needs SWD |
| Flash layout overhead | none | first 16–32 KB reserved for bootloader |
| Maintenance burden | none ongoing | every firmware change has to keep bootloader compatible |

The ROM bootloader path is the right call for VOX.

## Firmware side: soft-jump to ROM bootloader

The "soft-jump" trick is a well-trodden STM32 pattern (ST AN2606
documents it; stm32duino, libopencm3, and many vendor SDKs use it).

When the firmware receives `VOX_MSG_SET_MODE` with `mode = VOX_RUN_FW_UPDATE`
over USB CDC, it:

1. **Acknowledges** the request with `VOX_MSG_ACK` so the host knows
   the request is being honored.
2. **Waits a few ms** for the ACK to actually leave the USB peripheral
   (otherwise the host sees us disappear without confirming, and
   has to assume the worst — guess and retry).
3. **Disables interrupts** globally (`__disable_irq()`) so nothing
   fires during the jump.
4. **Resets all peripheral clocks** (`RCC_APB1RSTR1`, `RCC_APB2RSTR`,
   `RCC_AHB1/2RSTR`) so the ROM bootloader starts from a known clean
   state.  Crucial for USB: a running USB peripheral with stale
   descriptors will confuse the bootloader's USB init.
5. **Re-points the vector table** to System Memory (`SCB->VTOR = 0x1FFF0000`).
6. **Reloads the main stack pointer** from the bootloader's vector
   table (`__set_MSP(*(uint32_t *)0x1FFF0000)`).
7. **Jumps** to the bootloader's Reset_Handler — second word of its
   vector table — by casting the pointer to a `void (*)(void)` and
   calling it.

Pseudocode (see [src note](#why-this-isnt-portable-yet) below
about freestanding-ness):

```c
__attribute__((noreturn))
static void enter_rom_bootloader(void)
{
    /* Step 1+2 happens at the protocol-handler layer:
     * — ack the SET_MODE
     * — schedule this function with ~10 ms delay so the ack frame
     *   actually leaves USB before we tear it down.
     */

    __disable_irq();

    /* Reset every peripheral we've touched.  USB is the most
     * important — ROM bootloader will re-init it for DFU. */
    RCC->AHB1RSTR  = 0xFFFFFFFFu;  RCC->AHB1RSTR  = 0;
    RCC->AHB2RSTR  = 0xFFFFFFFFu;  RCC->AHB2RSTR  = 0;
    RCC->APB1RSTR1 = 0xFFFFFFFFu;  RCC->APB1RSTR1 = 0;
    RCC->APB1RSTR2 = 0xFFFFFFFFu;  RCC->APB1RSTR2 = 0;
    RCC->APB2RSTR  = 0xFFFFFFFFu;  RCC->APB2RSTR  = 0;

    /* Point CPU at System Memory's vector table. */
    SCB->VTOR = 0x1FFF0000u;

    /* Load MSP from word 0, jump to Reset_Handler from word 1. */
    const uint32_t *bl_vectors = (const uint32_t *)0x1FFF0000u;
    __set_MSP(bl_vectors[0]);
    void (*bl_reset)(void) = (void (*)(void))bl_vectors[1];
    bl_reset();

    /* Unreachable.  If it returns, hang. */
    for (;;) { }
}
```

After this jump, control is in ROM bootloader code; our application
is dormant until the bootloader resets the chip after finishing the
DFU transfer.

### Why this isn't portable yet

`__disable_irq`, `__set_MSP`, and `SCB->VTOR` are CMSIS Core
intrinsics — they come in via `<core_cm4.h>` which our cross build
already pulls (the slice G1 work brought in the CMSIS_5 headers).
Once slice J is implemented, the function lives in
`platform/mcu/core/fw_update.c` and the protocol handler calls it.

## Host side: orchestrate dfu-util

The vox_qt application drives the whole flow.  User picks the new
`.bin` (or the GUI auto-picks the bundled version), clicks Update,
and:

1. **Confirm capability**: check the dongle's last `HELLO` had the
   `VOX_CAP_FW_UPDATE` bit set.  If not, refuse to proceed.
2. **Send `VOX_MSG_SET_MODE`** with `mode = VOX_RUN_FW_UPDATE`.
3. **Wait for `VOX_MSG_ACK`** with status `VOX_ACK_OK`.  If we get
   `VOX_ACK_BUSY`, retry.  Other statuses surface a user-visible
   error.
4. **Watch for the device to disappear** from USB.  On Linux: poll
   `/dev/ttyACM*` or, better, subscribe to `udev` events.  Timeout
   after a few seconds; the firmware should be in ROM bootloader by
   then.
5. **Watch for the DFU device to appear** (`0483:df11`).  Same poll
   pattern; usually shows up < 1 s after the original disappears.
6. **Spawn `dfu-util`** with arguments:
   ```
   dfu-util -d 0483:df11 -a 0 -D <firmware.bin> -s 0x08000000:leave
   ```
   The `:leave` suffix tells the bootloader to reset back to the
   application after the upload finishes.  GUI watches stdout for
   progress and surfaces it in a progress bar.
7. **Wait for the VOX device to reappear** (`1209:0001`).  Open a
   new CDC connection, query `HELLO`, compare reported revision to
   the file we just uploaded.  Show "Updated to version X.Y.Z".
8. **Resume normal protocol traffic** — state-frame stream picks up
   where it left off; user sees the same widgets they had before.

vox_qt shells out to `dfu-util` for now.  In a later cleanup we can
replace the subprocess with libusb-driven DFU code (the DFU protocol
itself is small and well-specified), letting us drop the dfu-util
runtime dependency on end-user machines.

### dfu-util install

- Ubuntu / Debian: `apt install dfu-util`
- Fedora: `dnf install dfu-util`
- macOS: `brew install dfu-util`
- Windows: bundled in our Windows installer (or document `winget install dfu-util`)

For Linux, also need a udev rule so dfu-util can open `0483:df11`
without root.  Either ship a rules file in the installer or document
the one-liner.

## Failure modes and recovery

| Failure | Symptom | Recovery |
|---|---|---|
| Soft-jump didn't disable USB cleanly | DFU device never appears | host times out, app shows "couldn't enter DFU" error; user power-cycles dongle and retries.  Next boot is normal application; previous firmware unchanged. |
| dfu-util upload interrupted (cable yanked) | partial firmware in flash | dongle won't boot back to application; need SWD pogo pins + external ST-Link to recover, OR a hardware BOOT0 button if we built one in. |
| Uploaded firmware is bad (won't boot or won't enter DFU) | dongle dead from user's POV | same as above. |
| New firmware bricks somehow during running | depends on bug | application-level self-check could trigger a soft-jump to bootloader on its own, but we're not promising that. |

The first failure is *recoverable* over USB.  The second and third
are *unrecoverable* over USB — needs physical access to the SWD pads.

**Implication for the PCB design** (also noted in the earlier USB
discussion): the dongle PCB should expose **SWD test pads** —
SWDIO, SWCLK, GND, 3V3, NRST — even if no header is populated in
production.  At ~$0 cost (just board copper), they're the lifeline
for unrecoverable bricks.  A user who's bricked their dongle ships
it back; we (or the customer) solder pogo pins and recover via gdb +
external ST-Link.

We do *not* plan to add a hardware BOOT0 button.  It would protect
against a small class of "your soft-jump trick broke" failures, but
those are tractable to engineer-test before shipping firmware, and
the button has ergonomic cost (user pressing the wrong combination
at the wrong time).  SWD pads are sufficient.

## Protocol involvement

Slice G3 (and later slice J) extend the dongle protocol with:

- `VOX_MSG_SET_MODE` mode value `VOX_RUN_FW_UPDATE` — already
  reserved in [tools/vox_dongle_proto.h](../tools/vox_dongle_proto.h).
  Host sends this to ask the dongle to enter the ROM bootloader.
- `VOX_MSG_ACK` from firmware acknowledges the request before the
  jump.  Status `VOX_ACK_OK` means "I'm about to disappear from USB;
  watch for DFU device."  Status `VOX_ACK_NOT_SUPPORTED` means
  "firmware was built without the soft-jump support; user needs SWD."
- `VOX_CAP_FW_UPDATE` capability bit in `HELLO` — set when firmware
  supports the soft-jump.  Host should not send `SET_MODE=FW_UPDATE`
  unless this bit is advertised.

`VOX_MSG_FW_BLOCK` (already in the protocol) is **not used** in this
design.  It was a placeholder for the in-app updater alternative we
rejected.  Two options going forward:

- Leave `VOX_MSG_FW_BLOCK` in the protocol header but mark it
  deprecated / never sent, in case a future deployment scenario
  needs a CDC-native updater (no dfu-util on the host).
- Drop it from the header and bump `VOX_PROTO_VERSION`.

The deprecation route is simpler — keep the slot reserved, never
populate the corresponding capability bit, document in the header.

## User experience (Story 4 in [user_stories.md](user_stories.md))

```
Operator launches the VOX app
  → dongle is connected, shows up in the app's status bar
  → operator: Help → Check for updates
  → app reaches a hosted manifest (or operator picks a file from disk)
  → app: "New version 1.2.0 available.  Update now?"
  → operator: Yes
  → app: progress bar
       "Rebooting dongle into update mode…"            [chip → DFU]
       "Uploading firmware…"                            [dfu-util progress]
       "Verifying…"                                     [dfu-util verify]
       "Rebooting into new firmware…"                   [app → normal]
  → app: "Updated to 1.2.0.  Connected and ready."
  → operator continues using the dongle normally
```

Total time on a USB FS link with a ~70 KB firmware: ~5–10 seconds.

## What slice J does, in checklist form

Once we get to slice J, the work is:

- [ ] `platform/mcu/core/fw_update.c` with `vox_enter_rom_bootloader()` —
      ~30 lines of the pseudocode above.
- [ ] Wire `VOX_MSG_SET_MODE` handler in the firmware: when mode is
      `VOX_RUN_FW_UPDATE`, ack, delay 10 ms, call `vox_enter_rom_bootloader()`.
- [ ] Set `VOX_CAP_FW_UPDATE` bit in our `HELLO` emission.
- [ ] vox_qt: `FirmwareUpdater` class that orchestrates
      check-capability → SET_MODE → device-wait → dfu-util → device-wait → resume.
- [ ] Document `dfu-util` install in the README.
- [ ] Linux udev rule for `0483:df11` so users don't need root.
- [ ] Bench-test on the actual dongle (post slice E + a working USB connector).

## Out of scope (for now)

- Signed firmware updates (would require a small bootloader stub
  that verifies a signature before jumping to the app).
- Over-the-air updates (no radio yet).
- Rollback to previous firmware (would need dual-bank flash or a
  ROM-bootloader-friendly partition layout).
- Differential updates (full image only).

These are all valid features to consider eventually.  None of them
block the basic "click Update, get updated" flow.
