/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_link frame builder + byte-fed parser. Pure C — no ESP-IDF / FreeRTOS
 * dependency, so this compiles and runs on a host for unit testing.
 */
#include "al1_frame.h"
#include "crc16_ccitt.h"

#include <string.h>

/*
 * Wire layout (offsets):
 *   [0] SYNC  [1] VER  [2] CHANNEL  [3] SEQ  [4] LEN_HI  [5] LEN_LO
 *   [6..6+len-1] PAYLOAD   [.. ] CRC_HI  [.. ] CRC_LO
 *
 * CRC-16/CCITT-FALSE is computed over VER..PAYLOAD (bytes [1 .. 6+len-1]).
 */

size_t al1_frame_build(uint8_t *out, size_t out_cap,
                       uint8_t channel, uint8_t seq,
                       const uint8_t *payload, uint16_t len)
{
    if (out == NULL || len > AL1_LINK_MAX_PAYLOAD) {
        return 0;
    }
    if (len > 0 && payload == NULL) {
        return 0;
    }
    const size_t total = (size_t)AL1_LINK_OVERHEAD + len;
    if (out_cap < total) {
        return 0;
    }

    out[0] = AL1_LINK_SYNC;
    out[1] = AL1_LINK_VERSION;
    out[2] = channel;
    out[3] = seq;
    out[4] = (uint8_t)(len >> 8);   /* LEN_HI (big-endian) */
    out[5] = (uint8_t)(len & 0xFF); /* LEN_LO              */
    if (len > 0) {
        memcpy(&out[AL1_LINK_HEADER_LEN], payload, len);
    }

    /* CRC over VER..PAYLOAD == out[1 .. 6+len-1] == (5 + len) bytes. */
    const uint16_t crc =
        al1_crc16_ccitt(&out[1], (size_t)(AL1_LINK_HEADER_LEN - 1) + len);

    out[AL1_LINK_HEADER_LEN + len]     = (uint8_t)(crc >> 8);   /* CRC_HI */
    out[AL1_LINK_HEADER_LEN + len + 1] = (uint8_t)(crc & 0xFF); /* CRC_LO */

    return total;
}

void al1_parser_init(al1_parser_t *p, al1_frame_cb_t cb, void *user,
                     al1_link_stats_t *stats)
{
    if (p == NULL) {
        return;
    }
    p->state = AL1_PS_SYNC;
    p->channel = 0;
    p->seq = 0;
    p->len = 0;
    p->idx = 0;
    p->crc_calc = 0;
    p->crc_rx = 0;
    p->cb = cb;
    p->user = user;
    p->stats = stats;
}

/* Reset to hunt for the next SYNC, counting a resync. */
static void parser_resync(al1_parser_t *p)
{
    if (p->stats) {
        p->stats->rx_resync++;
    }
    p->state = AL1_PS_SYNC;
}

void al1_parser_feed(al1_parser_t *p, const uint8_t *data, size_t len)
{
    if (p == NULL || data == NULL) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[i];

        switch (p->state) {
        case AL1_PS_SYNC:
            if (b == AL1_LINK_SYNC) {
                p->state = AL1_PS_VER;
            }
            break;

        case AL1_PS_VER:
            if (b != AL1_LINK_VERSION) {
                if (p->stats) {
                    p->stats->rx_ver_err++;
                }
                /* Treat a stray SYNC as the start of a new frame. */
                p->state = (b == AL1_LINK_SYNC) ? AL1_PS_VER : AL1_PS_SYNC;
                break;
            }
            p->crc_calc = al1_crc16_ccitt_update(AL1_CRC16_INIT, b);
            p->state = AL1_PS_CHANNEL;
            break;

        case AL1_PS_CHANNEL:
            p->channel = b;
            p->crc_calc = al1_crc16_ccitt_update(p->crc_calc, b);
            p->state = AL1_PS_SEQ;
            break;

        case AL1_PS_SEQ:
            p->seq = b;
            p->crc_calc = al1_crc16_ccitt_update(p->crc_calc, b);
            p->state = AL1_PS_LEN_HI;
            break;

        case AL1_PS_LEN_HI:
            p->len = (uint16_t)b << 8;
            p->crc_calc = al1_crc16_ccitt_update(p->crc_calc, b);
            p->state = AL1_PS_LEN_LO;
            break;

        case AL1_PS_LEN_LO:
            p->len |= b;
            p->crc_calc = al1_crc16_ccitt_update(p->crc_calc, b);
            if (p->len > AL1_LINK_MAX_PAYLOAD) {
                if (p->stats) {
                    p->stats->rx_len_err++;
                }
                parser_resync(p);
                break;
            }
            p->idx = 0;
            p->state = (p->len == 0) ? AL1_PS_CRC_HI : AL1_PS_PAYLOAD;
            break;

        case AL1_PS_PAYLOAD:
            p->payload[p->idx++] = b;
            p->crc_calc = al1_crc16_ccitt_update(p->crc_calc, b);
            if (p->idx >= p->len) {
                p->state = AL1_PS_CRC_HI;
            }
            break;

        case AL1_PS_CRC_HI:
            p->crc_rx = (uint16_t)b << 8;
            p->state = AL1_PS_CRC_LO;
            break;

        case AL1_PS_CRC_LO:
            p->crc_rx |= b;
            if (p->crc_rx == p->crc_calc) {
                if (p->stats) {
                    p->stats->rx_frames++;
                }
                if (p->cb) {
                    p->cb(p->channel, p->seq, p->payload, p->len, p->user);
                }
            } else if (p->stats) {
                p->stats->rx_crc_err++;
            }
            p->state = AL1_PS_SYNC;
            break;

        default:
            p->state = AL1_PS_SYNC;
            break;
        }
    }
}
