#!/usr/bin/env python3
"""
vox_dongle_listen.py — minimal Python listener for the VOX dongle wire
protocol.

A standalone reimplementation of the same framer + CRC that
tools/vox_dongle_proto.c implements in C.  Used during slice
G-bridge bring-up to verify that framed bytes arriving on
/dev/ttyACMx (the ST-Link VCP, today; the chip's native USB CDC
later) decode cleanly.

Run with:
    python3 tools/vox_dongle_listen.py /dev/ttyACM0 921600

Prints one line per fully decoded frame.  Logs CRC failures and
resync events to stderr but keeps running.

Single-file by design — no Python package dependencies except
pyserial.  Install: `pip install pyserial` (or `apt install python3-serial`).
"""

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.stderr.write(
        "missing pyserial. install it with:\n"
        "  pip install pyserial   # or\n"
        "  apt install python3-serial\n")
    sys.exit(1)

# --- Wire format constants (must stay in sync with tools/vox_dongle_proto.h)
SYNC0 = 0x76    # 'v'
SYNC1 = 0x78    # 'x'
MAX_PAYLOAD = 720 - 7

# Message types — names mirror VoxMsgType in C
MSG_TYPES = {
    0x01: "HELLO",
    0x02: "STATE_FRAME",
    0x03: "LOG",
    0x04: "ACK",
    0x80: "QUERY_HELLO",
    0x81: "SET_TUNING",
    0x82: "INJECT_PCM",
    0x83: "SET_MODE",
    0x84: "FW_BLOCK",
}


# --- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect)
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# --- Streaming parser state
class Parser:
    IDLE, GOT_SYNC0, GOT_TYPE, GOT_LEN_LO, GOT_LEN_HI, IN_PAYLOAD, WAIT_CRC = range(7)

    def __init__(self):
        self.state = self.IDLE
        self.type = 0
        self.length = 0
        self.payload = bytearray()
        self.crc_lo = 0
        self.crc_hi_pending = False
        self.stats = dict(ok=0, bad_sync=0, crc_drop=0, overlong=0)

    def feed(self, data: bytes):
        for b in data:
            yield from self._step(b)

    def _step(self, b: int):
        if self.state == self.IDLE:
            if b == SYNC0:
                self.state = self.GOT_SYNC0
            else:
                self.stats["bad_sync"] += 1
        elif self.state == self.GOT_SYNC0:
            if b == SYNC1:
                self.state = self.GOT_TYPE
            else:
                self.state = self.GOT_SYNC0 if b == SYNC0 else self.IDLE
                self.stats["bad_sync"] += 1
        elif self.state == self.GOT_TYPE:
            self.type = b
            self.state = self.GOT_LEN_LO
        elif self.state == self.GOT_LEN_LO:
            self.length = b
            self.state = self.GOT_LEN_HI
        elif self.state == self.GOT_LEN_HI:
            self.length |= (b << 8)
            if self.length > MAX_PAYLOAD:
                self.stats["overlong"] += 1
                self._reset()
                return
            self.payload = bytearray()
            self.state = self.IN_PAYLOAD if self.length else self.WAIT_CRC
            self.crc_hi_pending = False
        elif self.state == self.IN_PAYLOAD:
            self.payload.append(b)
            if len(self.payload) >= self.length:
                self.state = self.WAIT_CRC
                self.crc_hi_pending = False
        elif self.state == self.WAIT_CRC:
            if not self.crc_hi_pending:
                self.crc_lo = b
                self.crc_hi_pending = True
            else:
                got = self.crc_lo | (b << 8)
                hdr3 = bytes([self.type,
                              self.length & 0xFF,
                              (self.length >> 8) & 0xFF])
                want = crc16(hdr3 + bytes(self.payload))
                if got == want:
                    self.stats["ok"] += 1
                    yield self.type, bytes(self.payload)
                else:
                    self.stats["crc_drop"] += 1
                    sys.stderr.write(
                        f"CRC fail: type=0x{self.type:02x} len={self.length} "
                        f"got=0x{got:04x} want=0x{want:04x}\n")
                self._reset()

    def _reset(self):
        self.state = self.IDLE
        self.type = 0
        self.length = 0
        self.payload = bytearray()
        self.crc_hi_pending = False


# --- Per-message-type decoders (the ones a host actually cares about)
def decode_hello(payload: bytes) -> str:
    if len(payload) < 32:
        return f"<short HELLO, {len(payload)} bytes>"
    proto_ver = payload[0]
    fw_rev = payload[4:20].split(b"\0", 1)[0].decode("ascii", errors="replace")
    fw_build, caps, hw_id = struct.unpack_from("<III", payload, 20)
    cap_names = []
    if caps & (1 << 0): cap_names.append("TUNING")
    if caps & (1 << 1): cap_names.append("INJECT")
    if caps & (1 << 2): cap_names.append("FW_UPDATE")
    if caps & (1 << 3): cap_names.append("LOG_STREAM")
    cap_str = "|".join(cap_names) if cap_names else "(none)"
    return (f"proto={proto_ver} rev='{fw_rev}' build={fw_build} "
            f"caps={cap_str}(0x{caps:08x}) hw_id=0x{hw_id:08x}")


def decode_log(payload: bytes) -> str:
    return repr(payload.decode("ascii", errors="replace"))


# VoxStateFrameV1 layout (46 bytes packed, little-endian).
_STATE_FRAME_FMT = "<II 8h BB hh hhhh hhhh"
_STATE_FRAME_SIZE = struct.calcsize(_STATE_FRAME_FMT)
assert _STATE_FRAME_SIZE == 46, _STATE_FRAME_SIZE

_LED_NAMES = ("MIC", "RX", "VAD", "AEC", "PTT")
_FLAG_NAMES = ("vr", "vv", "rx", "eo", "rg", "prv", "prh")


def decode_state_frame(payload: bytes) -> str:
    if len(payload) < _STATE_FRAME_SIZE:
        return f"<short STATE_FRAME, {len(payload)} bytes>"
    (seq, ts_ms,
     mic_raw, mic_post, rx_lvl, noise_floor,
     vad_raw, vad_val, aec_red, snr_pct,
     leds, flags,
     hang, hang_max,
     eff_vad, eff_snr, energy_margin, _reserved0,
     resid_pct, resid_dbfs_tenths, resid_peak_pct, resid_peak_delay
     ) = struct.unpack_from(_STATE_FRAME_FMT, payload)

    led_str = "".join(name[0] if leds & (1 << i) else "-"
                      for i, name in enumerate(_LED_NAMES))
    return (f"seq={seq:>6}  t={ts_ms/1000:6.2f}s  "
            f"leds={led_str}  "
            f"mic={mic_raw:>5}/{mic_post:<5} rx={rx_lvl:<5} "
            f"vad={vad_val:>3}% snr={snr_pct:>4}% "
            f"aec_red={aec_red:>3}% hang={hang}/{hang_max}")


DECODERS = {
    "HELLO":       decode_hello,
    "LOG":         decode_log,
    "STATE_FRAME": decode_state_frame,
}

# Verbose every-frame printing or one in N.  STATE_FRAME at 50 fps is
# a lot to watch live; default print rate keeps the screen readable.
STATE_FRAME_PRINT_EVERY = 25   # 0.5 s at 50 fps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="serial port (e.g. /dev/ttyACM0)")
    ap.add_argument("baud", nargs="?", default=921600, type=int)
    ap.add_argument("--stats-every", default=10.0, type=float,
                    help="print parser stats every N seconds (default 10)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)
    print(f"listening on {args.port} @ {args.baud} baud", file=sys.stderr)

    parser = Parser()
    last_stats = time.monotonic()
    state_frame_count = 0
    try:
        while True:
            data = ser.read(4096)
            if data:
                for msg_type, payload in parser.feed(data):
                    name = MSG_TYPES.get(msg_type, f"0x{msg_type:02x}")
                    decoder = DECODERS.get(name)

                    # Throttle STATE_FRAME printing — at 50 fps it
                    # otherwise drowns out everything else.
                    if name == "STATE_FRAME":
                        state_frame_count += 1
                        if state_frame_count % STATE_FRAME_PRINT_EVERY != 0:
                            continue

                    detail = decoder(payload) if decoder else f"{len(payload)} bytes"
                    print(f"[{name:14s}] {detail}")

            now = time.monotonic()
            if now - last_stats >= args.stats_every:
                print(f"  --- stats: {parser.stats}", file=sys.stderr)
                last_stats = now
    except KeyboardInterrupt:
        print("\nstopping.", file=sys.stderr)
        print(f"final stats: {parser.stats}", file=sys.stderr)


if __name__ == "__main__":
    main()
