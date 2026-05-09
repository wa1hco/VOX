#ifndef VOX_DONGLE_PROTO_H
#define VOX_DONGLE_PROTO_H

/*
 * vox_dongle_proto.h — wire protocol between the VOX dongle firmware
 * (STM32G474, USB CDC-ACM endpoint) and the host application
 * (vox_qt running on a Linux PC).
 *
 * Both sides #include this header.  The .c companion file
 * (vox_dongle_proto.c) provides framer, parser, and CRC helpers.
 *
 * Goals
 * -----
 * - Bidirectional full-duplex over a byte stream (USB CDC today,
 *   could be UART or wireless tomorrow without protocol changes).
 * - Resilient framing (sync bytes + length + CRC) so a corrupted
 *   transmit on either side can be re-synchronized in bounded time.
 * - Forward-compatible: a v1 firmware talking to a v2 host should
 *   keep working at v1's feature set; same for the reverse.  A
 *   capability bitset in the HELLO message is the contract.
 * - Wire structs are explicit and fixed-layout — they do not share
 *   storage with vox_core's VoxLedState / VoxDebugState, which are
 *   free to evolve over time.
 *
 * Endianness
 * ----------
 * Little-endian.  Both endpoints qualify (Cortex-M4F and x86_64).
 * If we ever target a big-endian host the helpers will need byte
 * swaps; on every platform we ship today they are no-ops.
 *
 * Compile-as-C and compile-as-C++.  Firmware is C; GUI is C++17.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Versioning ----------------------------------------------- */

/* Bump VOX_PROTO_VERSION when the wire format changes incompatibly.
 * For compatible additions (e.g. new message types, new fields at the
 * end of an existing struct guarded by a capability bit), keep the
 * version constant and add a capability flag.
 */
#define VOX_PROTO_VERSION  1

/* The maximum framed message size on the wire (header + payload + CRC).
 * Sized to comfortably fit the largest payload: a stereo INJECT_PCM at
 * 160 samples/channel = 640 bytes + small header (5) + CRC (2) = 647.
 * Round up.
 */
#define VOX_PROTO_MAX_FRAME 720
#define VOX_PROTO_MAX_PAYLOAD (VOX_PROTO_MAX_FRAME - 7)

/* ---------- Framing -------------------------------------------------- */

/*
 * Frame on the wire (all integers little-endian):
 *
 *   offset 0: u8   sync0  = 0x76 ('v')
 *   offset 1: u8   sync1  = 0x78 ('x')
 *   offset 2: u8   type   (VoxMsgType)
 *   offset 3: u16  length (payload bytes only, max VOX_PROTO_MAX_PAYLOAD)
 *   offset 5: u8[length] payload
 *   offset 5+length: u16 crc16 of (type || length || payload)
 *
 * CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no refin/out,
 * xorout 0).  Common, fast, and the implementation is small enough to
 * inline if we ever need it on the chip's hot path.
 */
#define VOX_PROTO_SYNC0    0x76u
#define VOX_PROTO_SYNC1    0x78u
#define VOX_PROTO_HEADER_BYTES 5
#define VOX_PROTO_CRC_BYTES    2
#define VOX_PROTO_OVERHEAD     (VOX_PROTO_HEADER_BYTES + VOX_PROTO_CRC_BYTES)

/* ---------- Message types -------------------------------------------- */

/*
 * Direction guidance is in the comment beside each type.  Both sides
 * silently ignore unknown TYPEs to preserve forward compatibility.
 *
 * Type IDs in 0x01..0x7F are firmware → host; 0x80..0xFE are host →
 * firmware.  This split is convention only — a future bidirectional
 * type (e.g. ping) can use either range.
 */
typedef enum {
    VOX_MSG_HELLO          = 0x01,  /* fw → host: identity + caps */
    VOX_MSG_STATE_FRAME    = 0x02,  /* fw → host: per-20ms vox state */
    VOX_MSG_LOG            = 0x03,  /* fw → host: ASCII log line */
    VOX_MSG_ACK            = 0x04,  /* fw → host: ack/status for a command */

    VOX_MSG_QUERY_HELLO    = 0x80,  /* host → fw: please re-emit HELLO */
    VOX_MSG_SET_TUNING     = 0x81,  /* host → fw: VoxConfig update */
    VOX_MSG_INJECT_PCM     = 0x82,  /* host → fw: 20 ms mic+rx PCM frame */
    VOX_MSG_SET_MODE       = 0x83,  /* host → fw: change run mode */
    VOX_MSG_FW_BLOCK       = 0x84,  /* host → fw: firmware update block */
} VoxMsgType;

/* ---------- Capability bits (HELLO.capabilities) --------------------- */

#define VOX_CAP_TUNING        (1u << 0)  /* accepts SET_TUNING */
#define VOX_CAP_INJECT        (1u << 1)  /* accepts INJECT_PCM */
#define VOX_CAP_FW_UPDATE     (1u << 2)  /* accepts FW_BLOCK + SET_MODE=FW_UPDATE */
#define VOX_CAP_LOG_STREAM    (1u << 3)  /* emits LOG messages */

/* ---------- Run modes (SET_MODE.mode) -------------------------------- */

typedef enum {
    VOX_RUN_NORMAL    = 0,  /* ADC frames feed vox_process (production) */
    VOX_RUN_INJECT    = 1,  /* INJECT_PCM frames feed vox_process */
    VOX_RUN_PAUSED    = 2,  /* vox_process not called; outputs frozen */
    VOX_RUN_FW_UPDATE = 3,  /* enter FW update mode; expect FW_BLOCKs next */
} VoxRunMode;

/* ---------- ACK status codes (ACK.status) ---------------------------- */

typedef enum {
    VOX_ACK_OK            = 0,
    VOX_ACK_BAD_TYPE      = 1,  /* unknown VoxMsgType (we still ack rather than ignore) */
    VOX_ACK_BAD_LENGTH    = 2,  /* payload length wrong for this type */
    VOX_ACK_NOT_SUPPORTED = 3,  /* capability bit not set */
    VOX_ACK_BUSY          = 4,  /* fw busy, retry */
    VOX_ACK_FW_UPDATE_BAD = 5,  /* generic FW update error (CRC, offset, ...) */
} VoxAckStatus;

/* ---------- Wire structs (packed, fixed layout) ---------------------- */

/*
 * All multi-byte integers little-endian.  All structs explicitly
 * packed so platform alignment can never inject padding bytes.
 *
 * Each struct corresponds to a single message type's *payload only*.
 * Framing (sync, type, length, CRC) is added by the encoder.
 */

#if defined(_MSC_VER)
#  define VOX_PACKED_BEGIN __pragma(pack(push,1))
#  define VOX_PACKED_END   __pragma(pack(pop))
#  define VOX_PACKED
#else
#  define VOX_PACKED_BEGIN
#  define VOX_PACKED_END
#  define VOX_PACKED __attribute__((packed))
#endif

VOX_PACKED_BEGIN

/* HELLO — sent unsolicited on connect, and on QUERY_HELLO. */
typedef struct VOX_PACKED {
    uint8_t  proto_version;     /* matches VOX_PROTO_VERSION on the sender */
    uint8_t  reserved[3];       /* pad to 4-byte alignment for following u32 */
    char     fw_revision[16];   /* null-terminated short version string */
    uint32_t fw_build_unix;     /* compile-time unix timestamp */
    uint32_t capabilities;      /* OR of VOX_CAP_* bits */
    uint32_t hw_id;             /* board / serial fingerprint */
} VoxHello;

/*
 * STATE_FRAME — emitted at 50 fps (20 ms) by the firmware.
 *
 * Subset of VoxLedState + VoxDebugState that the GUI needs to render
 * everything it shows today.  When we add new GUI panels or remove
 * old ones, this struct gets a v2 (and proto_version bumps) rather
 * than being silently extended.
 */
typedef struct VOX_PACKED {
    uint32_t seq;                          /* monotonic sequence */
    uint32_t timestamp_ms;                 /* ms since fw boot */

    /* Levels — average abs PCM units per frame, 0..32767 */
    int16_t  mic_level_raw;
    int16_t  mic_level_post_aec;
    int16_t  rx_level;
    int16_t  noise_floor;

    /* Algorithm scalars */
    int16_t  vad_probability_raw;          /* 0..100 */
    int16_t  vad_probability;              /* 0..100, validated */
    int16_t  aec_reduction_pct;            /* 0..100 */
    int16_t  snr_pct;                      /* post-AEC level / noise_floor * 100 */

    /* Decision/diagnostic flags, packed */
    uint8_t  leds;                         /* bit0=MIC bit1=RX bit2=VAD bit3=AEC bit4=PTT */
    uint8_t  flags;                        /* bit0=voice_raw bit1=voice_validated
                                              bit2=rx_active bit3=energy_ok
                                              bit4=rx_guard_applied bit5=ptt_reason_voice
                                              bit6=ptt_reason_hang */
    int16_t  hang_frames;                  /* current */
    int16_t  hang_frames_max;              /* configured */

    int16_t  effective_vad_threshold;
    int16_t  effective_snr_threshold;
    int16_t  energy_margin;
    int16_t  reserved0;                    /* pad to 4-byte */

    /* Residual-correlator scalars (full profile sent separately if needed) */
    int16_t  residual_corr_pct;
    int16_t  residual_corr_dbfs_tenths;
    int16_t  residual_corr_peak_pct;
    int16_t  residual_corr_peak_delay_samples;
} VoxStateFrameV1;

/* SET_TUNING — direct mirror of VoxConfig fields the host can drive.
 * A field of -1 (or 0 for fields that must be positive) means
 * "leave unchanged."  Convention matches vox_set_tuning() on the
 * Linux side. */
typedef struct VOX_PACKED {
    int32_t hang_ms;
    int32_t mic_led_threshold;
    int32_t rx_led_threshold;
    int32_t vad_led_prob_threshold;
    int32_t aec_led_reduction_pct;
    int32_t rx_guard_vad_boost;
    int32_t rx_guard_snr_pct;
} VoxSetTuning;

/* INJECT_PCM — one 20 ms (160 sample) frame of mic + rx PCM.
 * Used in VOX_RUN_INJECT mode so the GUI can drive the chip from
 * canned waveforms or live PC audio (Story 1, "test injection"). */
#define VOX_INJECT_FRAME_SAMPLES 160
typedef struct VOX_PACKED {
    uint16_t frame_size;                       /* must equal 160 */
    uint16_t reserved;
    int16_t  mic[VOX_INJECT_FRAME_SAMPLES];
    int16_t  rx[VOX_INJECT_FRAME_SAMPLES];
} VoxInjectPcm;

/* SET_MODE — change firmware run mode (see VoxRunMode). */
typedef struct VOX_PACKED {
    uint8_t  mode;                              /* VoxRunMode */
    uint8_t  reserved[3];
} VoxSetMode;

/* FW_BLOCK — one chunk of a firmware image during update.  Variable
 * payload length: header is fixed, then `block_size` bytes of image
 * data follow.  Receiver verifies the CRC over the whole packet via
 * the framing CRC; image-level integrity is up to the FW update
 * protocol layered on top (e.g. final block carries a total CRC).
 * The struct here is a *view* of the start of the payload. */
typedef struct VOX_PACKED {
    uint32_t offset;                            /* offset into image */
    uint32_t total_size;                        /* total image size */
    uint16_t block_size;
    uint16_t reserved;
    /* uint8_t data[block_size];  // follows in the framed payload */
} VoxFwBlockHeader;

/* ACK — emitted in response to host commands. */
typedef struct VOX_PACKED {
    uint8_t  in_response_to;                    /* the VoxMsgType being ack'd */
    uint8_t  status;                            /* VoxAckStatus */
    uint16_t reserved;
    uint32_t info;                              /* type-specific extra info */
} VoxAck;

/* LOG — null-padded ASCII text from firmware.  No format specifier
 * support; the firmware formats its own message and ships the
 * resulting string.  Length comes from the framed payload size. */
/* (no struct — payload is just chars) */

VOX_PACKED_END

/* ---------- Compile-time wire-layout sanity checks ------------------- */

/*
 * If any of these fail, the wire format has drifted.  Bump the
 * VOX_PROTO_VERSION (or fix the struct) before merging.
 */
#define VOX_STATIC_ASSERT(cond, msg) typedef char vox_sa_##msg[(cond) ? 1 : -1]

VOX_STATIC_ASSERT(sizeof(VoxHello)         == 32, hello_is_32);
VOX_STATIC_ASSERT(sizeof(VoxStateFrameV1)  == 46, state_v1_is_46);
VOX_STATIC_ASSERT(sizeof(VoxSetTuning)     == 28, tuning_is_28);
VOX_STATIC_ASSERT(sizeof(VoxInjectPcm)     == 644, inject_is_644);
VOX_STATIC_ASSERT(sizeof(VoxSetMode)       == 4,  mode_is_4);
VOX_STATIC_ASSERT(sizeof(VoxFwBlockHeader) == 12, fwblock_hdr_is_12);
VOX_STATIC_ASSERT(sizeof(VoxAck)           == 8,  ack_is_8);

/* ---------- Helper API (implemented in vox_dongle_proto.c) ---------- */

/* Compute CRC-16/CCITT-FALSE over `data` (poly 0x1021, init 0xFFFF). */
uint16_t vox_proto_crc16(const uint8_t *data, size_t len);

/*
 * Encode a single message into `out_buf`.  Returns the number of bytes
 * written, or 0 if `out_cap` is too small or `payload_len` exceeds
 * VOX_PROTO_MAX_PAYLOAD.  `payload` may be NULL when `payload_len` is 0.
 */
size_t vox_proto_encode(uint8_t       *out_buf,
                        size_t         out_cap,
                        VoxMsgType     type,
                        const void    *payload,
                        size_t         payload_len);

/*
 * Streaming parser.  Caller maintains a VoxProtoParser and feeds it
 * bytes as they arrive on the byte stream.  Each fully decoded
 * message invokes the callback with type + payload + length.
 *
 * Designed for embedded use:
 *   - No malloc; all buffering in the parser struct itself.
 *   - Resync on first byte mismatch — a corrupt or dropped byte
 *     desynchronizes for at most one message worth of data.
 *   - CRC-failed messages are dropped silently (caller can opt-in to
 *     a counter via the .stats fields).
 */
typedef void (*VoxProtoOnMessage)(void          *ctx,
                                  VoxMsgType     type,
                                  const uint8_t *payload,
                                  size_t         payload_len);

typedef struct VoxProtoParser {
    /* state machine */
    uint8_t  state;
    uint8_t  type;
    uint16_t length;
    uint16_t got;
    uint16_t crc_got;
    uint8_t  crc_bytes;
    uint8_t  buf[VOX_PROTO_MAX_PAYLOAD];

    /* simple counters useful during bring-up */
    uint32_t stat_frames_ok;
    uint32_t stat_crc_drops;
    uint32_t stat_bad_sync;
    uint32_t stat_overlong;
} VoxProtoParser;

void vox_proto_parser_init(VoxProtoParser *p);

void vox_proto_parser_feed(VoxProtoParser    *p,
                           const uint8_t     *bytes,
                           size_t             len,
                           VoxProtoOnMessage  cb,
                           void              *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VOX_DONGLE_PROTO_H */
