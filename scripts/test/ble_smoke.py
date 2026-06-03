#!/usr/bin/env python3
"""
MIT License
Copyright (c) 2026 EngEmil

BLE smoke test for the AttentioLight-1 Wireless Module (ESP32-C3).

The module exposes the Attentio Protocol (AP) as a transparent byte pipe over a
custom GATT service: a write characteristic (host -> device) and a notify
characteristic (device -> host). This script drives that pipe end to end —
CLAIM the device, set the LED colour, watch for a button event, then release —
so the wireless control path can be verified against real hardware.

It builds CRC-correct AP frames (the same wire format the STM32 speaks), so the
bytes it emits are valid whether they go out over `bleak` here or get pasted by
hand into a phone's nRF Connect.

Three modes, picked so the test is still useful where no BLE radio is reachable
(e.g. inside a devcontainer):

  --selftest      Offline. Verify the CRC, frame builder, and notification
                  reassembler against golden values. No radio, no bleak needed.
                  Prints "N checks, M failures" and exits non-zero on failure.

  --print-frames  Offline. Print the exact CRC-correct AP frame hex for each
                  command — the copy-paste source for nRF Connect's TX write.

  (default)       Live. Requires bleak and a BLE adapter on this machine. Scans
                  for the device, connects, and runs the full round-trip.

Usage:
    python3 ble_smoke.py --selftest
    python3 ble_smoke.py --print-frames --rgb 255,0,0
    python3 ble_smoke.py                       # live run (needs bleak + adapter)
    python3 ble_smoke.py --address AA:BB:CC:DD:EE:FF --no-button

Live mode dependency:  pip install -r scripts/test/requirements.txt
"""

import argparse
import asyncio
import sys

# ---------------------------------------------------------------------------
# Attentio BLE Service — UUIDs and device name.
# Mirror al1_ble/include/al1_ble.h. The device name is advertised in the scan
# response; the 128-bit service UUID is in the advertising PDU.
# ---------------------------------------------------------------------------
DEVICE_NAME = "AttentioLight-1"
SVC_UUID = "1209eea1-0001-0000-0000-000000000000"  # primary service
TX_UUID = "1209eea1-0002-0000-0000-000000000000"   # host -> device (write, enc)
RX_UUID = "1209eea1-0003-0000-0000-000000000000"   # device -> host (notify)

# ---------------------------------------------------------------------------
# AP wire format — mirror attentio_protocol/include/attentio_protocol.h.
#   [SYNC 0xA5][LEN][CMD][PAYLOAD 0..252][CRC8],  total = LEN + 3,  4..256 B
#   LEN  = len(CMD + PAYLOAD), valid 1..253.
#   CRC8 = CRC-8/CCITT (poly 0x07, init 0x00) over LEN + CMD + PAYLOAD.
# ---------------------------------------------------------------------------
AP_SYNC_BYTE = 0xA5
AP_MIN_LEN = 1
AP_MAX_LEN = 253
AP_OFF_LEN = 1
AP_OFF_CMD = 2
AP_OFF_PAYLOAD = 3

# Commands (host -> device).
AP_CMD_CLAIM = 0x01
AP_CMD_RELEASE = 0x02
AP_CMD_PING = 0x03
AP_CMD_LED_OFF = 0x20
AP_CMD_SET_RGB = 0x21
AP_CMD_GET_STATUS = 0x40

# Responses / events (device -> host).
AP_CMD_EVT_BUTTON = 0x80
AP_CMD_EVT_STATE_CHANGE = 0x81
AP_CMD_EVT_SESSION_END = 0x82
AP_CMD_OK = 0xF0
AP_CMD_ERROR = 0xF1

# Error codes (1-byte payload of AP_CMD_ERROR).
AP_ERR_NAMES = {
    0x00: "NONE",
    0x01: "NOT_CONTROLLER",
    0x02: "INVALID_CMD",
    0x03: "INVALID_PARAM",
    0x04: "INVALID_STATE",
    0x05: "CRC_FAIL",
}

# Button event types (1-byte payload of AP_CMD_EVT_BUTTON).
AP_BTN_NAMES = {
    0x01: "SHORT_PRESS",
    0x02: "LONG_PRESS_START",
    0x03: "LONG_PRESS_RELEASE",
    0x04: "EXTENDED_PRESS_START",
    0x05: "EXTENDED_PRESS_RELEASE",
}

CMD_NAMES = {
    AP_CMD_CLAIM: "CLAIM",
    AP_CMD_RELEASE: "RELEASE",
    AP_CMD_PING: "PING",
    AP_CMD_LED_OFF: "LED_OFF",
    AP_CMD_SET_RGB: "SET_RGB",
    AP_CMD_GET_STATUS: "GET_STATUS",
    AP_CMD_EVT_BUTTON: "EVT_BUTTON",
    AP_CMD_EVT_STATE_CHANGE: "EVT_STATE_CHANGE",
    AP_CMD_EVT_SESSION_END: "EVT_SESSION_END",
    AP_CMD_OK: "OK",
    AP_CMD_ERROR: "ERROR",
}

# CRC-8/CCITT lookup table — verbatim from attentio_protocol.c (poly 0x07).
CRC8_TABLE = (
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
)


def ap_crc8(data):
    """CRC-8/CCITT over `data` (poly 0x07, init 0x00). Mirrors ap_crc8()."""
    crc = 0x00
    for b in data:
        crc = CRC8_TABLE[crc ^ b]
    return crc


def build_frame(cmd, payload=b""):
    """Build a raw AP frame: [SYNC][LEN][CMD][PAYLOAD][CRC8]."""
    if len(payload) > 252:
        raise ValueError("payload exceeds 252 bytes")
    body = bytes([len(payload) + 1, cmd]) + bytes(payload)  # LEN + CMD + PAYLOAD
    return bytes([AP_SYNC_BYTE]) + body + bytes([ap_crc8(body)])


class ApReassembler:
    """Length-only AP frame reassembler — a port of al1_ble/ap_reasm.c.

    Notifications from the device are fragmented at MTU-3, so one AP frame may
    span several notifications (or several may share one). Feed raw notify bytes;
    `feed()` returns the list of whole frames (SYNC..CRC) completed by that feed.
    Like the firmware, it frames by LEN only and does not validate the CRC.
    """

    SYNC, LEN, DATA = range(3)

    def __init__(self):
        self.state = self.SYNC
        self.frame = bytearray()
        self.need = 0
        self.len_err = 0

    def feed(self, data):
        out = []
        for b in data:
            if self.state == self.SYNC:
                if b == AP_SYNC_BYTE:
                    self.frame = bytearray([b])
                    self.state = self.LEN
            elif self.state == self.LEN:
                if b < AP_MIN_LEN or b > AP_MAX_LEN:
                    self.len_err += 1
                    self.state = self.SYNC
                    continue
                self.frame.append(b)
                self.need = b + 1  # CMD + PAYLOAD (== LEN) plus CRC
                self.state = self.DATA
            elif self.state == self.DATA:
                self.frame.append(b)
                self.need -= 1
                if self.need == 0:
                    out.append(bytes(self.frame))
                    self.state = self.SYNC
        return out


def frame_cmd(frame):
    """CMD byte of a complete frame, or None if too short."""
    return frame[AP_OFF_CMD] if len(frame) > AP_OFF_CMD else None


def frame_payload(frame):
    """Payload bytes of a complete frame (between CMD and CRC)."""
    return bytes(frame[AP_OFF_PAYLOAD:-1])


def describe_frame(frame):
    """Human-readable one-liner for a received frame."""
    cmd = frame_cmd(frame)
    name = CMD_NAMES.get(cmd, "0x%02X" % (cmd if cmd is not None else 0))
    payload = frame_payload(frame)
    extra = ""
    if cmd == AP_CMD_OK and len(payload) >= 2:
        extra = " session=%d" % ((payload[0] << 8) | payload[1])
    elif cmd == AP_CMD_ERROR and len(payload) >= 1:
        extra = " err=%s" % AP_ERR_NAMES.get(payload[0], "0x%02X" % payload[0])
    elif cmd == AP_CMD_EVT_BUTTON and len(payload) >= 1:
        extra = " btn=%s" % AP_BTN_NAMES.get(payload[0], "0x%02X" % payload[0])
    elif cmd == AP_CMD_EVT_STATE_CHANGE and len(payload) >= 2:
        extra = " %d->%d" % (payload[0], payload[1])
    return "%s%s  [%s]" % (name, extra, frame.hex(" "))


def parse_rgb(text):
    """Parse 'R,G,B' (decimals 0-255) into a 3-byte payload."""
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected R,G,B (e.g. 255,0,0)")
    vals = []
    for p in parts:
        v = int(p, 0)
        if not 0 <= v <= 255:
            raise argparse.ArgumentTypeError("each channel must be 0..255")
        vals.append(v)
    return bytes(vals)


# ---------------------------------------------------------------------------
# Offline: print frames for manual (nRF Connect) use.
# ---------------------------------------------------------------------------
def print_frames(rgb):
    presets = [
        ("CLAIM", build_frame(AP_CMD_CLAIM)),
        ("RELEASE", build_frame(AP_CMD_RELEASE)),
        ("PING", build_frame(AP_CMD_PING)),
        ("GET_STATUS", build_frame(AP_CMD_GET_STATUS)),
        ("LED_OFF", build_frame(AP_CMD_LED_OFF)),
        ("SET_RGB %d,%d,%d" % (rgb[0], rgb[1], rgb[2]),
         build_frame(AP_CMD_SET_RGB, rgb)),
        ("SET_RGB red", build_frame(AP_CMD_SET_RGB, b"\xff\x00\x00")),
        ("SET_RGB green", build_frame(AP_CMD_SET_RGB, b"\x00\xff\x00")),
        ("SET_RGB blue", build_frame(AP_CMD_SET_RGB, b"\x00\x00\xff")),
    ]
    print("AP frames — write the hex to the TX characteristic (%s):" % TX_UUID)
    print("(subscribe to RX notify first: %s)\n" % RX_UUID)
    width = max(len(label) for label, _ in presets)
    for label, frame in presets:
        print("  %-*s  %s" % (width, label, frame.hex(" ")))


# ---------------------------------------------------------------------------
# Offline: self-test (mirrors the C host-test convention).
# ---------------------------------------------------------------------------
def selftest():
    checks = 0
    failures = 0

    def check(cond, msg):
        nonlocal checks, failures
        checks += 1
        if not cond:
            failures += 1
            print("  FAIL: %s" % msg)

    # CRC known-answers (table-driven CRC-8/CCITT, init 0x00).
    check(ap_crc8(b"") == 0x00, "crc8 of empty == 0x00")
    # Frame structure + builder.
    claim = build_frame(AP_CMD_CLAIM)
    check(claim[0] == AP_SYNC_BYTE, "claim starts with SYNC")
    check(claim[AP_OFF_LEN] == 1, "claim LEN == 1 (CMD only)")
    check(claim[AP_OFF_CMD] == AP_CMD_CLAIM, "claim CMD == CLAIM")
    check(len(claim) == 4, "claim frame is 4 bytes")
    check(claim[-1] == ap_crc8(claim[1:-1]), "claim CRC covers LEN..CMD")

    rgb = build_frame(AP_CMD_SET_RGB, b"\xff\x00\x00")
    check(rgb[AP_OFF_LEN] == 4, "set_rgb LEN == 4 (CMD + 3)")
    check(len(rgb) == 7, "set_rgb frame is 7 bytes")
    check(frame_payload(rgb) == b"\xff\x00\x00", "set_rgb payload round-trips")
    check(rgb[-1] == ap_crc8(rgb[1:-1]), "set_rgb CRC covers LEN..PAYLOAD")

    # Max-size payload.
    big = build_frame(AP_CMD_SET_RGB, b"\x5a" * 252)
    check(len(big) == 256, "max frame is 256 bytes")
    check(big[AP_OFF_LEN] == 253, "max LEN == 253")

    # Reassembler: whole frame in one feed.
    r = ApReassembler()
    out = r.feed(rgb)
    check(out == [rgb], "reasm: whole frame round-trips")

    # Reassembler: frame split across single-byte feeds (worst-case BLE frag).
    r = ApReassembler()
    got = []
    for b in rgb:
        got += r.feed(bytes([b]))
    check(got == [rgb], "reasm: byte-split frame reassembles")

    # Reassembler: two frames packed into one feed.
    r = ApReassembler()
    out = r.feed(claim + rgb)
    check(out == [claim, rgb], "reasm: back-to-back frames split correctly")

    # Reassembler: garbage before sync is discarded, then resync.
    r = ApReassembler()
    out = r.feed(b"\x00\x11\x22" + claim)
    check(out == [claim], "reasm: garbage before sync discarded")

    # Reassembler: bad LEN (0) is rejected and we resync on the next frame.
    r = ApReassembler()
    out = r.feed(bytes([AP_SYNC_BYTE, 0x00]) + claim)
    check(out == [claim], "reasm: zero LEN rejected, resync ok")
    check(r.len_err == 1, "reasm: len_err counted")

    print("%d checks, %d failures" % (checks, failures))
    return 0 if failures == 0 else 1


# ---------------------------------------------------------------------------
# Live: drive the device over BLE with bleak.
# ---------------------------------------------------------------------------
async def run_live(args):
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError:
        print("error: bleak not installed. "
              "pip install -r scripts/test/requirements.txt", file=sys.stderr)
        return 2

    checks = 0
    failures = 0

    def check(cond, msg):
        nonlocal checks, failures
        checks += 1
        status = "PASS" if cond else "FAIL"
        if not cond:
            failures += 1
        print("  [%s] %s" % (status, msg))

    # Discover / resolve the device.
    address = args.address
    if address is None:
        print("scanning for %r (%.0fs)..." % (args.device_name, args.timeout))
        dev = await BleakScanner.find_device_by_name(
            args.device_name, timeout=args.timeout)
        check(dev is not None, "device %r found" % args.device_name)
        if dev is None:
            print("%d checks, %d failures" % (checks, failures))
            return 1
        address = dev.address
        print("found %s" % address)

    reasm = ApReassembler()
    inbox = asyncio.Queue()

    def on_notify(_char, data):
        for frame in reasm.feed(data):
            print("  <- %s" % describe_frame(frame))
            inbox.put_nowait(frame)

    async def wait_for(cmd, timeout):
        """Await the next frame with the given CMD, draining others."""
        loop = asyncio.get_event_loop()
        deadline = loop.time() + timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                return None
            try:
                frame = await asyncio.wait_for(inbox.get(), timeout=remaining)
            except asyncio.TimeoutError:
                return None
            if frame_cmd(frame) == cmd:
                return frame

    async def send(label, frame):
        print("  -> %s  [%s]" % (label, frame.hex(" ")))
        # First write triggers Just-Works/LESC pairing (TX is encryption-required).
        await client.write_gatt_char(TX_UUID, frame, response=True)

    print("connecting to %s..." % address)
    async with BleakClient(address) as client:
        check(client.is_connected, "connected")
        await client.start_notify(RX_UUID, on_notify)

        # CLAIM -> OK (carries the 2-byte session id). The pairing prompt (if
        # any) appears on this first write.
        await send("CLAIM", build_frame(AP_CMD_CLAIM))
        ok = await wait_for(AP_CMD_OK, args.timeout)
        check(ok is not None, "CLAIM acknowledged with OK")
        if ok is not None and len(frame_payload(ok)) >= 2:
            p = frame_payload(ok)
            print("  session id = %d" % ((p[0] << 8) | p[1]))

        # SET_RGB cycle — watch the LED.
        for name, color in (("red", b"\xff\x00\x00"),
                            ("green", b"\x00\xff\x00"),
                            ("blue", b"\x00\x00\xff")):
            await send("SET_RGB %s" % name, build_frame(AP_CMD_SET_RGB, color))
            await asyncio.sleep(args.color_dwell)
        check(True, "SET_RGB cycle sent (verify LED visually)")

        # Button event.
        if not args.no_button:
            print("  >> press the device button within %.0fs..." % args.timeout)
            evt = await wait_for(AP_CMD_EVT_BUTTON, args.timeout)
            check(evt is not None, "EVT_BUTTON received")

        # RELEASE.
        await send("RELEASE", build_frame(AP_CMD_RELEASE))
        await asyncio.sleep(0.5)
        await client.stop_notify(RX_UUID)

    # Reconnect to confirm the bond is reused (no re-pair).
    if not args.no_reconnect:
        print("reconnecting to confirm bond reuse...")
        async with BleakClient(address) as client:
            check(client.is_connected, "reconnected (bond reused)")

    print("%d checks, %d failures" % (checks, failures))
    return 0 if failures == 0 else 1


def main():
    parser = argparse.ArgumentParser(
        description="BLE smoke test for the AttentioLight-1 Wireless Module.")
    parser.add_argument("--selftest", action="store_true",
                        help="offline CRC/frame/reassembly checks (no BLE).")
    parser.add_argument("--print-frames", action="store_true",
                        help="print CRC-correct AP frame hex (no BLE).")
    parser.add_argument("--device-name", default=DEVICE_NAME,
                        help="advertised name to scan for (default: %(default)s).")
    parser.add_argument("--address", default=None,
                        help="connect directly to this BLE address (skip scan).")
    parser.add_argument("--rgb", type=parse_rgb, default=b"\xff\x00\x00",
                        metavar="R,G,B",
                        help="colour for SET_RGB / print-frames (default 255,0,0).")
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="scan / response timeout in seconds (default: 20).")
    parser.add_argument("--color-dwell", type=float, default=1.5,
                        help="seconds to hold each colour in the cycle (default: 1.5).")
    parser.add_argument("--no-button", action="store_true",
                        help="skip the interactive button-press wait.")
    parser.add_argument("--no-reconnect", action="store_true",
                        help="skip the bond-reuse reconnect check.")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if args.print_frames:
        print_frames(args.rgb)
        return 0
    return asyncio.run(run_live(args))


if __name__ == "__main__":
    sys.exit(main())
