/*
 * test_dongle_proto.c — round-trip and edge-case tests for the
 * dongle-protocol framer.
 *
 * Verified behaviors:
 *   1) HELLO encodes and decodes with the payload bytes preserved.
 *   2) STATE_FRAME encodes and decodes; sequence number / level
 *      fields survive the round trip.
 *   3) INJECT_PCM at full size (644 B payload, 651 B framed) round
 *      trips — confirms the parser handles the largest expected frame.
 *   4) Empty-payload message (e.g. QUERY_HELLO) round-trips.
 *   5) Two messages back-to-back in one feed() call both fire.
 *   6) A single-bit corruption in the payload flips the CRC and the
 *      message is dropped (callback not fired, stat_crc_drops bumps).
 *   7) Garbage bytes before a valid frame are skipped without confusing
 *      the parser (resync).
 *   8) Static asserts in the header survived compilation (implicit —
 *      this file builds, so they passed).
 */

#include "vox_dongle_proto.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Minimal harness — no test framework dependency.  Failure = assert. */
#define EXPECT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

typedef struct {
    int            count;
    VoxMsgType     last_type;
    uint8_t        last_payload[VOX_PROTO_MAX_PAYLOAD];
    size_t         last_payload_len;
} CapturedMsg;

static void on_message(void *ctx, VoxMsgType type, const uint8_t *payload, size_t len)
{
    CapturedMsg *c = (CapturedMsg *)ctx;
    c->count++;
    c->last_type = type;
    c->last_payload_len = len;
    if (len > 0)
        memcpy(c->last_payload, payload, len);
}

/* Round-trip: encode a payload, feed the encoded bytes through a
 * parser, verify the callback received the same bytes back. */
static void roundtrip(VoxMsgType type, const void *payload, size_t payload_len)
{
    uint8_t buf[VOX_PROTO_MAX_FRAME];
    size_t  n = vox_proto_encode(buf, sizeof(buf), type, payload, payload_len);
    EXPECT(n == VOX_PROTO_OVERHEAD + payload_len);

    VoxProtoParser p;
    vox_proto_parser_init(&p);
    CapturedMsg c = {0};
    vox_proto_parser_feed(&p, buf, n, on_message, &c);

    EXPECT(c.count == 1);
    EXPECT(c.last_type == type);
    EXPECT(c.last_payload_len == payload_len);
    if (payload_len > 0)
        EXPECT(memcmp(c.last_payload, payload, payload_len) == 0);
    EXPECT(p.stat_frames_ok == 1);
    EXPECT(p.stat_crc_drops == 0);
}

static void test_hello_roundtrip(void)
{
    VoxHello h = {0};
    h.proto_version = VOX_PROTO_VERSION;
    snprintf(h.fw_revision, sizeof(h.fw_revision), "vox-1.0.0");
    h.fw_build_unix = 0x66E1A2B0;  /* arbitrary */
    h.capabilities  = VOX_CAP_TUNING | VOX_CAP_LOG_STREAM;
    h.hw_id         = 0xDEADBEEFu;
    roundtrip(VOX_MSG_HELLO, &h, sizeof(h));
}

static void test_state_frame_roundtrip(void)
{
    VoxStateFrameV1 s = {0};
    s.seq                = 42;
    s.timestamp_ms       = 12345;
    s.mic_level_raw      = 1234;
    s.mic_level_post_aec = 567;
    s.rx_level           = 890;
    s.noise_floor        = 100;
    s.vad_probability_raw = 75;
    s.vad_probability    = 65;
    s.aec_reduction_pct  = 35;
    s.snr_pct            = 180;
    s.leds               = 0x1F;       /* all 5 on */
    s.flags              = 0x42;
    s.hang_frames        = 8;
    s.hang_frames_max    = 25;
    roundtrip(VOX_MSG_STATE_FRAME, &s, sizeof(s));
}

static void test_inject_pcm_max_size(void)
{
    VoxInjectPcm pcm = {0};
    pcm.frame_size = VOX_INJECT_FRAME_SAMPLES;
    for (int i = 0; i < VOX_INJECT_FRAME_SAMPLES; i++) {
        pcm.mic[i] = (int16_t)(i * 100);
        pcm.rx[i]  = (int16_t)(-i * 50);
    }
    roundtrip(VOX_MSG_INJECT_PCM, &pcm, sizeof(pcm));
}

static void test_empty_payload(void)
{
    /* QUERY_HELLO carries no payload. */
    roundtrip(VOX_MSG_QUERY_HELLO, NULL, 0);
}

/* Module-level counter — simpler than passing context structs around. */
static int g_seen_count = 0;
static VoxMsgType g_seen_types[8];

static void counting_cb(void *ctx, VoxMsgType type, const uint8_t *pl, size_t len)
{
    (void)ctx; (void)pl; (void)len;
    if (g_seen_count < 8)
        g_seen_types[g_seen_count] = type;
    g_seen_count++;
}

static void test_two_messages_via_static_counter(void)
{
    uint8_t buf[256];
    size_t  off = 0;
    off += vox_proto_encode(buf + off, sizeof(buf) - off,
                            VOX_MSG_QUERY_HELLO, NULL, 0);
    EXPECT(off > 0);
    off += vox_proto_encode(buf + off, sizeof(buf) - off,
                            VOX_MSG_LOG, "hi", 2);
    EXPECT(off > 0);

    VoxProtoParser p;
    vox_proto_parser_init(&p);
    g_seen_count = 0;
    vox_proto_parser_feed(&p, buf, off, counting_cb, NULL);

    EXPECT(g_seen_count == 2);
    EXPECT(g_seen_types[0] == VOX_MSG_QUERY_HELLO);
    EXPECT(g_seen_types[1] == VOX_MSG_LOG);
}

static void test_corrupt_payload_drops(void)
{
    uint8_t buf[VOX_PROTO_MAX_FRAME];
    VoxHello h = {0};
    h.proto_version = VOX_PROTO_VERSION;
    h.hw_id = 0xCAFEBABEu;

    size_t n = vox_proto_encode(buf, sizeof(buf), VOX_MSG_HELLO, &h, sizeof(h));
    EXPECT(n > 0);

    /* Flip a single bit somewhere in the middle of the payload. */
    buf[VOX_PROTO_HEADER_BYTES + 5] ^= 0x01;

    VoxProtoParser p;
    vox_proto_parser_init(&p);
    g_seen_count = 0;
    vox_proto_parser_feed(&p, buf, n, counting_cb, NULL);

    EXPECT(g_seen_count == 0);
    EXPECT(p.stat_crc_drops == 1);
    EXPECT(p.stat_frames_ok == 0);
}

static void test_garbage_before_valid_frame(void)
{
    uint8_t junk[6] = {0x00, 0xFF, 0x76, 0xAA, 0x11, 0x22};
    /* Note: junk[2]=0x76 = SYNC0 — the parser should advance into
     * GOT_SYNC0, find that 0xAA != SYNC1, and resync.  This exercises
     * the "byte after SYNC0 isn't SYNC1" path. */

    uint8_t buf[VOX_PROTO_MAX_FRAME];
    size_t  n = vox_proto_encode(buf, sizeof(buf), VOX_MSG_QUERY_HELLO, NULL, 0);
    EXPECT(n > 0);

    /* Concatenate junk + valid frame, feed in one shot. */
    uint8_t stream[64];
    memcpy(stream, junk, sizeof(junk));
    memcpy(stream + sizeof(junk), buf, n);

    VoxProtoParser p;
    vox_proto_parser_init(&p);
    g_seen_count = 0;
    vox_proto_parser_feed(&p, stream, sizeof(junk) + n, counting_cb, NULL);

    EXPECT(g_seen_count == 1);
    EXPECT(g_seen_types[0] == VOX_MSG_QUERY_HELLO);
}

static void test_split_across_feed_calls(void)
{
    /* The same valid frame, but fed one byte at a time, must still
     * decode exactly once. */
    uint8_t buf[VOX_PROTO_MAX_FRAME];
    VoxSetTuning t = { .hang_ms = 500, .mic_led_threshold = 300,
                       .rx_led_threshold = 300, .vad_led_prob_threshold = 60,
                       .aec_led_reduction_pct = 20, .rx_guard_vad_boost = 15,
                       .rx_guard_snr_pct = 145 };
    size_t n = vox_proto_encode(buf, sizeof(buf), VOX_MSG_SET_TUNING, &t, sizeof(t));
    EXPECT(n > 0);

    VoxProtoParser p;
    vox_proto_parser_init(&p);
    g_seen_count = 0;
    for (size_t i = 0; i < n; i++)
        vox_proto_parser_feed(&p, buf + i, 1, counting_cb, NULL);

    EXPECT(g_seen_count == 1);
    EXPECT(g_seen_types[0] == VOX_MSG_SET_TUNING);
}

static void test_crc_known_vector(void)
{
    /* Sanity check on the CRC-16/CCITT-FALSE implementation:
     * "123456789" → 0x29B1 (per the standard test vector). */
    const uint8_t v[] = "123456789";
    EXPECT(vox_proto_crc16(v, 9) == 0x29B1);
}

int main(void)
{
    test_crc_known_vector();
    test_hello_roundtrip();
    test_state_frame_roundtrip();
    test_inject_pcm_max_size();
    test_empty_payload();
    test_two_messages_via_static_counter();
    test_corrupt_payload_drops();
    test_garbage_before_valid_frame();
    test_split_across_feed_calls();
    printf("all dongle-protocol tests passed.\n");
    return 0;
}
