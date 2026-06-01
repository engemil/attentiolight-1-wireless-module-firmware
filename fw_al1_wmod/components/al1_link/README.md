# al1_link — STM32 ↔ ESP32 UART link transport

A thin, framed transport that multiplexes several logical byte streams
("channels") over the single UART between the AttentioLight-1 MainBoard
(STM32C071, USART1) and the Wireless Module (ESP32-C3).

It carries **opaque payloads**. The Attentio Protocol (AP) and other higher
layers ride inside the payload of a channel; this layer never parses them.

## Frame format

```
+------+------+---------+-----+--------+--------+--------------+--------+--------+
| SYNC | VER  | CHANNEL | SEQ | LEN_HI | LEN_LO |   PAYLOAD    | CRC_HI | CRC_LO |
| 0xA5 | 0x01 |  1 byte | 1B  |      length     |  ≤ 4096 B    |  CRC-16/CCITT   |
+------+------+---------+-----+--------+--------+--------------+--------+--------+
```

- **SYNC** `0xA5` — start-of-frame marker.
- **VER** `0x01` — frame format version.
- **CHANNEL** — see table below.
- **SEQ** — per-channel, monotonic, wraps at 256. Drop detection only; there
  is no retransmission in this version.
- **LEN** — payload length, **big-endian** (HI then LO), `0 … 4096`.
- **PAYLOAD** — opaque bytes.
- **CRC** — **CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, no
  reflection, xorout `0x0000`), **big-endian**, computed over `VER … PAYLOAD`
  (everything after SYNC, through the last payload byte).

Total overhead is 8 bytes; maximum frame is `8 + 4096 = 4104` bytes.

> **Note:** this link CRC (CRC-16) is independent of the Attentio Protocol's
> own CRC-8/CCITT. Both happen to use sync byte `0xA5`, but the AP packet is
> just opaque payload inside an `AP_CTRL` link frame, so the two framings never
> conflict. Both link endpoints (ESP32 here, STM32 in the sister-PR) must use
> the exact CRC-16 parameters above.

## Channels

| Channel       | Value | Purpose                                  |
|---------------|-------|------------------------------------------|
| `AP_CTRL`     | 0x01  | Attentio Protocol control frames         |
| `LOG`         | 0x02  | Human-readable log text                  |
| `EVT`         | 0x03  | Asynchronous events                      |
| `BULK`        | 0x04  | Bulk transfer (e.g. future OTA chunks)   |
| `KEEPALIVE`   | 0xFF  | Link keep-alive                          |

## Pin mapping

The STM32 host drives this link on **USART1**: `WBM_TX = PB6` (AF2),
`WBM_RX = PB7` (see `attentiolight-1-firmware/.../boards/AL1_MB1/board.h`).

On the ESP32-C3 the matching GPIOs are set by `WMOD_UART_TX_GPIO` /
`WMOD_UART_RX_GPIO` in `include/al1_link.h`. **Confirm the exact numbers
against the al1mb1 schematic.** They must avoid GPIO18/19 (USB-Serial-JTAG)
and GPIO20/21 (default UART0 console). The placeholders are valid free pins
and are correct for the TX↔RX jumper loopback test regardless.

## API

```c
al1_link_start(NULL);                       /* uses AL1_LINK_DEFAULT_CFG() */
al1_link_set_rx_cb(my_cb, my_ctx);          /* called per received frame   */
al1_link_send(AL1_CH_LOG, data, len);       /* frames + queues for TX      */
al1_link_get_stats(&stats);
```

The frame builder/parser (`al1_frame_build`, `al1_parser_feed`) are pure C and
unit-tested on the host — see `test/`.

## Tests

```bash
# Host (no hardware, no ESP-IDF) — run from this component directory:
cc -I include -I . -o /tmp/al1_link_test \
   al1_frame.c crc16_ccitt.c test/host_test.c && /tmp/al1_link_test
```
