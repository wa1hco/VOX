# VOX dongle wire protocol

Protocol carried over USB CDC-ACM between a host running `vox_qt` and
the firmware on a VOX dongle.  Source of truth for the on-wire layout
is the header [tools/vox_dongle_proto.h](../tools/vox_dongle_proto.h);
this document is the human-readable companion.

## Goals

- **Bidirectional, full-duplex.** Firmware streams state at 50 fps;
  host sends commands when sliders move or files inject.
- **Resilient framing.** Sync bytes + length + CRC so a corrupt or
  dropped byte resynchronizes within one message.
- **Forward-compatible.** Firmware reports capability bits; host
  refrains from sending commands the firmware doesn't advertise
  support for; both sides silently ignore unknown message types.
- **Wire layout is fixed and verified at compile time.** Packed structs
  with `static_assert` on `sizeof` catch accidental drift.

## Frame format

All multi-byte integers little-endian.

| Offset | Size  | Field      | Notes                                        |
|-------:|------:|------------|----------------------------------------------|
|      0 |  1 B  | `SYNC0`    | constant `0x76` ('v')                        |
|      1 |  1 B  | `SYNC1`    | constant `0x78` ('x')                        |
|      2 |  1 B  | `TYPE`     | `VoxMsgType` enum                            |
|      3 |  2 B  | `LEN`      | payload length, max `VOX_PROTO_MAX_PAYLOAD`  |
|      5 | LEN B | `PAYLOAD`  | type-specific bytes                          |
|  5+LEN |  2 B  | `CRC16`    | CRC-16/CCITT-FALSE over `TYPE` + `LEN` + `PAYLOAD` |

Total overhead: **7 bytes per message**.

### CRC

CRC-16/CCITT-FALSE: polynomial `0x1021`, init `0xFFFF`, no reflect,
xorout `0x0000`.  Implementation is bit-serial (~25 cycles/byte on
Cortex-M4); at 50 fps with the largest framed payload (~640 B for
`INJECT_PCM`) that's under 1% CPU at 170 MHz.  See
`vox_proto_crc16()` in `tools/vox_dongle_proto.c`.

Standard test vector: `crc16("123456789") == 0x29B1`.

### Parser resync

The parser keeps a small state machine.  Mismatched sync bytes,
overlong length fields, or CRC failures all reset it to IDLE — at most
one message worth of stream data is lost before the next valid frame
locks again.  Three free counters (`stat_bad_sync`, `stat_overlong`,
`stat_crc_drops`) are exposed for bring-up diagnostics.

## Versioning + capabilities

Firmware emits a `HELLO` message unsolicited on connect, and again on
demand via `QUERY_HELLO`.  `HELLO` carries:

- `proto_version` — bumped only on incompatible wire changes
- `fw_revision[16]` — short null-terminated string (`"vox-1.0.0"` etc.)
- `fw_build_unix` — compile-time timestamp, lets the host say "this
  is older than what's bundled" without fetching anything else
- `capabilities` — OR of `VOX_CAP_*` bits
- `hw_id` — board / serial fingerprint

Capability bits today:

| Bit | Symbol               | What it means                              |
|----:|----------------------|--------------------------------------------|
|   0 | `VOX_CAP_TUNING`     | accepts `SET_TUNING`                       |
|   1 | `VOX_CAP_INJECT`     | accepts `INJECT_PCM`                       |
|   2 | `VOX_CAP_FW_UPDATE`  | accepts `FW_BLOCK` + `SET_MODE=FW_UPDATE`  |
|   3 | `VOX_CAP_LOG_STREAM` | emits `LOG` messages                        |

**Compatibility rule.** A host whose `proto_version` exceeds the
firmware's must downgrade gracefully: send only commands whose bits
are advertised, fall back to read-only display.  A firmware whose
`proto_version` exceeds the host's must keep emitting messages from
the host's protocol-version subset.

## Message catalog

### Firmware → host

| Type | Constant            | Payload struct        | Cadence / when             |
|-----:|---------------------|-----------------------|----------------------------|
| 0x01 | `VOX_MSG_HELLO`     | `VoxHello`            | once on connect; on demand |
| 0x02 | `VOX_MSG_STATE_FRAME` | `VoxStateFrameV1`   | 50 fps in `RUN_NORMAL`/`RUN_INJECT` |
| 0x03 | `VOX_MSG_LOG`       | ASCII bytes (no header) | rare, debug-only          |
| 0x04 | `VOX_MSG_ACK`       | `VoxAck`              | in response to host commands |

### Host → firmware

| Type | Constant              | Payload struct       | Notes                          |
|-----:|-----------------------|----------------------|--------------------------------|
| 0x80 | `VOX_MSG_QUERY_HELLO` | (none)               | re-emit `HELLO`                |
| 0x81 | `VOX_MSG_SET_TUNING`  | `VoxSetTuning`       | live `vox_set_tuning` on chip  |
| 0x82 | `VOX_MSG_INJECT_PCM`  | `VoxInjectPcm`       | only honored in `RUN_INJECT`   |
| 0x83 | `VOX_MSG_SET_MODE`    | `VoxSetMode`         | switch run mode                |
| 0x84 | `VOX_MSG_FW_BLOCK`    | `VoxFwBlockHeader` + image bytes | only in `RUN_FW_UPDATE` |

### Run modes (`SET_MODE.mode`)

| Value | Symbol             | Meaning                                                         |
|------:|--------------------|-----------------------------------------------------------------|
|     0 | `VOX_RUN_NORMAL`   | ADC frames feed `vox_process` (production)                       |
|     1 | `VOX_RUN_INJECT`   | `INJECT_PCM` frames feed `vox_process` (developer test)         |
|     2 | `VOX_RUN_PAUSED`   | `vox_process` not called; outputs frozen                         |
|     3 | `VOX_RUN_FW_UPDATE`| expecting `FW_BLOCK` messages, no audio processing               |

## Wire-struct sizes

These are asserted at compile time in the header.  If they disagree
with what the host or chip is built with, the build fails — preventing
silent layout drift.

| Struct              | Size (bytes) |
|---------------------|-------------:|
| `VoxHello`          |           32 |
| `VoxStateFrameV1`   |           46 |
| `VoxSetTuning`      |           28 |
| `VoxInjectPcm`      |          644 |
| `VoxSetMode`        |            4 |
| `VoxFwBlockHeader`  |           12 |
| `VoxAck`            |            8 |

Largest framed message is `INJECT_PCM`: 644 + 7 = 651 bytes on the
wire.  At 50 fps that's ~32 KB/s — well under USB FS bulk capacity.

## Mapping to user stories

| Story                                      | Messages used                                                |
|--------------------------------------------|--------------------------------------------------------------|
| 1. Developer tests new feature             | `HELLO`, `STATE_FRAME`, `SET_TUNING`, `INJECT_PCM`, `SET_MODE`, `FW_BLOCK`, `LOG` |
| 2. Developer reviews integration problem   | `HELLO`, `STATE_FRAME`, optionally `LOG`                      |
| 3. End user adjusts settings (operator)    | `HELLO`, `STATE_FRAME`, `SET_TUNING` (subset)                |
| 4. End user updates firmware               | `HELLO`, `SET_MODE=FW_UPDATE`, `FW_BLOCK`, `ACK`              |

See [docs/user_stories.md](user_stories.md).

## Adding a new message type

1. Add enum value in `VoxMsgType` (firmware → host in `0x01..0x7F`,
   host → firmware in `0x80..0xFE`).
2. Define a packed payload struct in `vox_dongle_proto.h`.
3. Add a `VOX_STATIC_ASSERT(sizeof(...) == ..., ...)` so the layout is
   pinned.
4. If it's a host → firmware command, define a capability bit
   (`VOX_CAP_…`) and require it to be advertised before the host sends.
5. Extend the round-trip tests in
   `tests/functional/test_dongle_proto.c` with a payload exercising the
   new type.
6. If the change is incompatible (existing struct grows, field
   reorders, etc.), bump `VOX_PROTO_VERSION`.

Compatible additions (new types, new capability bits) do not require a
version bump — old hosts simply won't use them.
