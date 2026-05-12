#ifndef VOX_MCU_PROTO_TRANSPORT_H
#define VOX_MCU_PROTO_TRANSPORT_H

/*
 * proto_transport.h — VOX dongle protocol over USART2 (the ST-Link VCP).
 *
 * Today the dongle protocol runs over USART2 → ST-Link/V3 → USB CDC →
 * host /dev/ttyACMx.  This is the development-bridge transport; later
 * (slice G3 when the chip's native USB peripheral has a connector) the
 * same protocol bytes will flow through TinyUSB CDC instead.  Same
 * frames, same parser; only the bytes' physical path changes.
 *
 * RX is interrupt-driven (USART2_IRQHandler defined here) because at
 * 921600 baud a new byte arrives every ~11 µs and our 20 ms frame
 * loop can't poll fast enough to avoid overruns.  TX is polled —
 * we control when to write and never write faster than the line.
 */

#include <stdint.h>
#include <stddef.h>
#include "vox_dongle_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure USART2 for protocol transport at `baud` bps.  Assumes
 * PA2/PA3 are already in AF mode (set by the board's gpio_init) and
 * RCC_APB1ENR1_USART2EN is already asserted.  Enables the RXNEIE
 * interrupt and NVIC USART2_IRQn — caller must NOT have a competing
 * USART2 interrupt user. */
void vox_proto_transport_init(uint32_t sysclk_hz, uint32_t baud);

/* Encode `payload` into a framed message of `type` and push it onto
 * the TX line.  Blocks while bytes are being written (polled TXE).
 * Returns 0 on success, -1 if `payload_len` exceeds the protocol
 * maximum. */
int vox_proto_send(VoxMsgType type, const void *payload, size_t payload_len);

/* Drain any bytes that have accumulated in the RX ring buffer through
 * the supplied parser.  Call once per frame loop iteration.  The
 * parser's callback fires for each fully-decoded frame.  Returns the
 * number of bytes drained. */
size_t vox_proto_drain_rx(VoxProtoParser *parser,
                          VoxProtoOnMessage cb,
                          void *ctx);

/* Tiny convenience helper: send a `VOX_MSG_LOG` frame with a
 * null-terminated string.  Replaces the printf-style uart_write* calls
 * elsewhere in the firmware so all chip→host traffic flows through
 * the protocol. */
void vox_proto_log(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* VOX_MCU_PROTO_TRANSPORT_H */
