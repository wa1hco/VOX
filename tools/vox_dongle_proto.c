/*
 * vox_dongle_proto.c — encoder, parser, and CRC for the VOX dongle
 * wire protocol.  See vox_dongle_proto.h for the format reference and
 * design rationale.
 *
 * This file is plain C, freestanding-friendly: no malloc, no libc
 * features beyond <string.h>'s memcpy.  Intended to compile cleanly
 * for both the host (linked into vox_qt) and the cortex-m4 firmware
 * (linked into vox_<board>.elf).
 */

#include "vox_dongle_proto.h"
#include <string.h>

/* ---------- CRC-16/CCITT-FALSE -------------------------------------- */
/*
 * Polynomial 0x1021, init 0xFFFF, no reflect, xorout 0x0000.
 *
 * Bit-serial implementation: ~25 cycles per byte on Cortex-M4.  At
 * 50 fps with the largest framed payload (~640 B INJECT_PCM) that's
 * ~800k cycles/s = 0.5% CPU at 170 MHz.  Negligible; keep it small
 * over fast.  If we ever need more headroom, swap for a 256-entry
 * table (512 bytes flash, ~5 cycles/byte).
 */
uint16_t vox_proto_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ---------- Encoder ------------------------------------------------- */

size_t vox_proto_encode(uint8_t       *out_buf,
                        size_t         out_cap,
                        VoxMsgType     type,
                        const void    *payload,
                        size_t         payload_len)
{
    if (!out_buf)
        return 0;
    if (payload_len > VOX_PROTO_MAX_PAYLOAD)
        return 0;
    const size_t total = VOX_PROTO_OVERHEAD + payload_len;
    if (out_cap < total)
        return 0;
    if (payload_len > 0 && !payload)
        return 0;

    out_buf[0] = VOX_PROTO_SYNC0;
    out_buf[1] = VOX_PROTO_SYNC1;
    out_buf[2] = (uint8_t)type;
    out_buf[3] = (uint8_t)(payload_len & 0xFFu);
    out_buf[4] = (uint8_t)((payload_len >> 8) & 0xFFu);
    if (payload_len > 0)
        memcpy(out_buf + VOX_PROTO_HEADER_BYTES, payload, payload_len);

    /* CRC covers TYPE + LEN + PAYLOAD (i.e. everything after the sync bytes,
     * stopping before the CRC field itself). */
    const uint16_t crc = vox_proto_crc16(out_buf + 2, payload_len + 3);
    out_buf[VOX_PROTO_HEADER_BYTES + payload_len + 0] = (uint8_t)(crc & 0xFFu);
    out_buf[VOX_PROTO_HEADER_BYTES + payload_len + 1] = (uint8_t)((crc >> 8) & 0xFFu);

    return total;
}

/* ---------- Streaming parser ---------------------------------------- */

/*
 * State machine.  Each call to vox_proto_parser_feed() runs the
 * machine over `len` incoming bytes; if a fully framed and CRC-valid
 * message lands inside the buffer, the callback fires once with that
 * message's payload before the function returns.  Multiple messages
 * inside a single feed() call all fire in order.
 *
 * State diagram:
 *
 *   IDLE        -- byte == SYNC0 -->  GOT_SYNC0
 *   GOT_SYNC0   -- byte == SYNC1 -->  GOT_TYPE   (else IDLE; resync)
 *   GOT_TYPE    -- 1 byte  -->        GOT_LEN_LO
 *   GOT_LEN_LO  -- 1 byte  -->        GOT_LEN_HI
 *   GOT_LEN_HI  -- if length > MAX -->IDLE (drop, count overlong)
 *               -- else if length==0 -->WAIT_CRC  (no payload)
 *               -- else                -->IN_PAYLOAD
 *   IN_PAYLOAD  -- length bytes -->   WAIT_CRC
 *   WAIT_CRC    -- 2 bytes -->        IDLE  (validate, dispatch)
 *
 * CRC-fail or any out-of-range field returns the parser to IDLE
 * without invoking the callback.
 */
enum {
    VPS_IDLE = 0,
    VPS_GOT_SYNC0,
    VPS_GOT_TYPE,
    VPS_GOT_LEN_LO,
    VPS_GOT_LEN_HI,
    VPS_IN_PAYLOAD,
    VPS_WAIT_CRC
};

void vox_proto_parser_init(VoxProtoParser *p)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    p->state = VPS_IDLE;
}

static void parser_reset(VoxProtoParser *p)
{
    p->state    = VPS_IDLE;
    p->type     = 0;
    p->length   = 0;
    p->got      = 0;
    p->crc_got  = 0;
    p->crc_bytes = 0;
}

void vox_proto_parser_feed(VoxProtoParser    *p,
                           const uint8_t     *bytes,
                           size_t             len,
                           VoxProtoOnMessage  cb,
                           void              *ctx)
{
    if (!p || !bytes)
        return;

    for (size_t i = 0; i < len; i++) {
        const uint8_t b = bytes[i];
        switch (p->state) {
        case VPS_IDLE:
            if (b == VOX_PROTO_SYNC0) {
                p->state = VPS_GOT_SYNC0;
            } else {
                p->stat_bad_sync++;
            }
            break;

        case VPS_GOT_SYNC0:
            if (b == VOX_PROTO_SYNC1) {
                p->state = VPS_GOT_TYPE;
            } else {
                /* Maybe the byte we treated as SYNC0 was actually
                 * payload tail; try treating *this* byte as a fresh
                 * SYNC0 in case the stream is mid-sync. */
                p->state = (b == VOX_PROTO_SYNC0) ? VPS_GOT_SYNC0 : VPS_IDLE;
                p->stat_bad_sync++;
            }
            break;

        case VPS_GOT_TYPE:
            p->type = b;
            p->state = VPS_GOT_LEN_LO;
            break;

        case VPS_GOT_LEN_LO:
            p->length = b;
            p->state  = VPS_GOT_LEN_HI;
            break;

        case VPS_GOT_LEN_HI:
            p->length |= (uint16_t)b << 8;
            if (p->length > VOX_PROTO_MAX_PAYLOAD) {
                p->stat_overlong++;
                parser_reset(p);
                break;
            }
            p->got = 0;
            p->state = (p->length > 0) ? VPS_IN_PAYLOAD : VPS_WAIT_CRC;
            break;

        case VPS_IN_PAYLOAD:
            p->buf[p->got++] = b;
            if (p->got >= p->length) {
                p->state = VPS_WAIT_CRC;
                p->crc_got = 0;
                p->crc_bytes = 0;
            }
            break;

        case VPS_WAIT_CRC:
            if (p->crc_bytes == 0) {
                p->crc_got = b;
                p->crc_bytes = 1;
            } else {
                p->crc_got |= (uint16_t)b << 8;
                p->crc_bytes = 2;

                /* Validate.  CRC was computed over TYPE + LEN + PAYLOAD;
                 * recompute over the same span we just received. */
                uint8_t hdr3[3];
                hdr3[0] = p->type;
                hdr3[1] = (uint8_t)(p->length & 0xFFu);
                hdr3[2] = (uint8_t)((p->length >> 8) & 0xFFu);
                uint16_t crc = vox_proto_crc16(hdr3, 3);
                /* continue CRC over payload */
                if (p->length > 0) {
                    uint16_t c = crc;
                    for (uint16_t k = 0; k < p->length; k++) {
                        c ^= (uint16_t)p->buf[k] << 8;
                        for (int bi = 0; bi < 8; bi++) {
                            if (c & 0x8000)
                                c = (uint16_t)((c << 1) ^ 0x1021);
                            else
                                c = (uint16_t)(c << 1);
                        }
                    }
                    crc = c;
                }

                if (crc == p->crc_got) {
                    p->stat_frames_ok++;
                    if (cb)
                        cb(ctx, (VoxMsgType)p->type, p->buf, p->length);
                } else {
                    p->stat_crc_drops++;
                }
                parser_reset(p);
            }
            break;

        default:
            parser_reset(p);
            break;
        }
    }
}
