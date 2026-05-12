/*
 * proto_transport.c — USART2 driver carrying the VOX dongle protocol.
 *
 * RX path:  USART2 RXNE IRQ → ring buffer (power-of-two so wrap is
 *           a bitmask AND).  Main-loop side calls vox_proto_drain_rx
 *           which copies bytes out and feeds them to the protocol
 *           parser.  At 921600 baud worst-case 20 ms of buffering is
 *           ~1.85 KB; we use 4 KB for slack.
 *
 * TX path:  Polled — busy-wait on TXE before writing each byte.  At
 *           921600 baud byte time = 11 µs, so a 64-byte STATE_FRAME
 *           takes ~0.7 ms and a 651-byte INJECT_PCM takes ~7 ms.
 *           That's within our 20 ms frame budget; we'll switch to
 *           IRQ-driven TX if it ever becomes tight.
 *
 * The transport doesn't own the framer state — the caller passes a
 * VoxProtoParser to vox_proto_drain_rx so the protocol layer stays
 * stateless from the transport's view.
 */

#include "proto_transport.h"
#include "stm32g4_min.h"
#include <string.h>

/* ---- RX ring buffer (interrupt producer, main consumer) ---------- */
#define VOX_PROTO_RX_BUF_SIZE  4096u    /* must be power of two */
#define VOX_PROTO_RX_MASK      (VOX_PROTO_RX_BUF_SIZE - 1u)

static volatile uint8_t  s_rx_buf[VOX_PROTO_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;   /* IRQ writes here */
static volatile uint16_t s_rx_tail;   /* main reads from here */
static volatile uint32_t s_rx_overruns;

/* ---- USART2 IRQ -------------------------------------------------- */
/*
 * Only RXNE handled here.  ORE (overrun) is cleared by reading ISR
 * then RDR; we count it for diagnostics but don't otherwise recover —
 * the protocol parser will simply drop the corrupted frame at the
 * CRC check.
 */
void USART2_IRQHandler(void)
{
    const uint32_t isr = USART2->ISR;

    if (isr & USART_ISR_ORE) {
        /* Clear overrun.  RDR read below also clears it; explicit
         * write to ICR.ORECF documents intent. */
        USART2->ICR = USART_ICR_ORECF;
        s_rx_overruns++;
    }

    if (isr & USART_ISR_RXNE_RXFNE) {
        const uint8_t b = (uint8_t)(USART2->RDR & 0xFFu);
        const uint16_t next = (uint16_t)((s_rx_head + 1u) & VOX_PROTO_RX_MASK);
        if (next != s_rx_tail) {
            s_rx_buf[s_rx_head] = b;
            s_rx_head = next;
        } else {
            /* Buffer full — drop the byte.  Parser will resync on the
             * next valid sync sequence (worst case: one frame lost). */
            s_rx_overruns++;
        }
    }
}

/* ---- Public API -------------------------------------------------- */

void vox_proto_transport_init(uint32_t sysclk_hz, uint32_t baud)
{
    /* Disable USART2 while we reconfigure.  Caller has already enabled
     * the peripheral clock + set the GPIO AF mode. */
    USART2->CR1 = 0;
    USART2->BRR = sysclk_hz / baud;
    /* Enable USART, transmitter, receiver, RXNE interrupt. */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;

    /* Drain any stale RXNE from the now-discarded slow-baud bytes. */
    (void)USART2->RDR;
    s_rx_head = 0;
    s_rx_tail = 0;
    s_rx_overruns = 0;

    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_EnableIRQ(USART2_IRQn);
}

/* Polled byte write — used by the encode buffer pump below. */
static void tx_byte(uint8_t b)
{
    while ((USART2->ISR & USART_ISR_TXE_TXFNF) == 0u) { }
    USART2->TDR = b;
}

int vox_proto_send(VoxMsgType type, const void *payload, size_t payload_len)
{
    if (payload_len > VOX_PROTO_MAX_PAYLOAD)
        return -1;

    /* Build the framed message into a stack buffer, then write the
     * whole thing out.  Stack-friendly: max frame is ~650 B.
     * Building first means the CRC is computed once at the end, not
     * incrementally on the wire. */
    uint8_t frame[VOX_PROTO_MAX_FRAME];
    const size_t n = vox_proto_encode(frame, sizeof(frame),
                                      type, payload, payload_len);
    if (n == 0)
        return -1;

    for (size_t i = 0; i < n; i++)
        tx_byte(frame[i]);

    return 0;
}

size_t vox_proto_drain_rx(VoxProtoParser *parser,
                          VoxProtoOnMessage cb,
                          void *ctx)
{
    if (!parser)
        return 0;

    /* Snapshot head once — we only consume up to where the IRQ has
     * written.  The IRQ may keep adding while we're draining, but
     * we'll catch the new bytes on the next call. */
    const uint16_t head_now = s_rx_head;
    size_t drained = 0;

    while (s_rx_tail != head_now) {
        /* Feed in chunks so the parser doesn't see one byte at a
         * time when many are queued — its state machine is the same
         * either way, but bigger chunks mean fewer function calls. */
        uint16_t end = head_now;
        if (end < s_rx_tail) {
            /* Wrap: drain to end-of-buffer first, then come back for
             * 0..head_now on the next iteration. */
            end = VOX_PROTO_RX_BUF_SIZE;
        }
        const size_t chunk = (size_t)(end - s_rx_tail);
        vox_proto_parser_feed(parser, (const uint8_t *)&s_rx_buf[s_rx_tail],
                              chunk, cb, ctx);
        drained += chunk;
        s_rx_tail = (uint16_t)((s_rx_tail + chunk) & VOX_PROTO_RX_MASK);
    }
    return drained;
}

void vox_proto_log(const char *s)
{
    if (!s) return;
    const size_t len = strlen(s);
    /* LOG payload is just ASCII chars — no header. */
    (void)vox_proto_send(VOX_MSG_LOG, s, len);
}
