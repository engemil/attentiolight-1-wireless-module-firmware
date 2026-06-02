/*
MIT License

Copyright (c) 2026 EngEmil

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file    attentio_protocol.c
 * @brief   Attentio Protocol (AP) implementation.
 *
 * @details Implements CRC-8/CCITT, byte-by-byte packet parser, and packet
 *          builder functions for the Attentio Protocol.
 */

#include "attentio_protocol.h"
#include <string.h>

/*===========================================================================*/
/* CRC-8/CCITT                                                               */
/*===========================================================================*/

/**
 * @brief   CRC-8/CCITT lookup table.
 * @details Pre-computed for polynomial 0x07, initial value 0x00.
 *          Using a table is faster than bit-by-bit computation and costs
 *          256 bytes of flash — a reasonable trade-off for a protocol that
 *          computes CRC on every packet.
 */
static const uint8_t crc8_table[256] = {
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
};

uint8_t ap_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        crc = crc8_table[crc ^ *data++];
    }
    return crc;
}

/*===========================================================================*/
/* Parser                                                                    */
/*===========================================================================*/

void ap_parser_init(ap_parser_t *parser) {
    parser->state = AP_PARSE_SYNC;
    parser->len = 0;
    parser->idx = 0;
}

void ap_parser_reset(ap_parser_t *parser) {
    ap_parser_init(parser);
}

ap_parse_result_t ap_parse_byte(ap_parser_t *parser, uint8_t byte) {
    switch (parser->state) {

    case AP_PARSE_SYNC:
        if (byte == AP_SYNC_BYTE) {
            parser->state = AP_PARSE_LEN;
            parser->len = 0;
            parser->idx = 0;
        }
        /* Non-sync bytes are silently discarded (resync behavior). */
        return AP_PARSE_NEED_MORE;

    case AP_PARSE_LEN:
        /*
         * LEN = length of (CMD + PAYLOAD).
         * Minimum valid LEN is 1 (just the CMD byte, no payload).
         * Maximum valid LEN is 253 (1 CMD + 252 payload).
         */
        if (byte == 0) {
            parser->state = AP_PARSE_SYNC;
            return AP_PARSE_ERR_LEN;
        }
        if (byte > (AP_MAX_PAYLOAD_SIZE + 1)) {
            parser->state = AP_PARSE_SYNC;
            return AP_PARSE_ERR_OVERFLOW;
        }
        parser->len = byte;
        parser->idx = 0;
        parser->state = AP_PARSE_DATA;
        return AP_PARSE_NEED_MORE;

    case AP_PARSE_DATA:
        /* Collect CMD + PAYLOAD bytes into data buffer. */
        parser->data[parser->idx++] = byte;
        if (parser->idx >= parser->len) {
            parser->state = AP_PARSE_CRC;
        }
        return AP_PARSE_NEED_MORE;

    case AP_PARSE_CRC: {
        /*
         * CRC-8 is computed over (LEN + CMD + PAYLOAD).
         * Compute incrementally: start with LEN byte, then continue
         * over the data buffer (CMD + PAYLOAD). This avoids allocating
         * a large temporary buffer on the stack.
         */
        uint8_t computed_crc = ap_crc8(&parser->len, 1);
        /* Continue CRC over CMD + PAYLOAD already in parser->data. */
        {
            uint8_t c = computed_crc;
            for (uint8_t i = 0; i < parser->len; i++) {
                c = crc8_table[c ^ parser->data[i]];
            }
            computed_crc = c;
        }

        if (byte != computed_crc) {
            parser->state = AP_PARSE_SYNC;
            return AP_PARSE_ERR_CRC;
        }

        /* Valid packet — populate the parsed packet structure. */
        parser->pkt.cmd = parser->data[0];
        parser->pkt.payload_len = parser->len - 1;
        if (parser->pkt.payload_len > 0) {
            memcpy(parser->pkt.payload, &parser->data[1],
                   parser->pkt.payload_len);
        }

        parser->state = AP_PARSE_SYNC;
        return AP_PARSE_COMPLETE;
    }

    default:
        /* Should never happen. Reset to safe state. */
        parser->state = AP_PARSE_SYNC;
        return AP_PARSE_NEED_MORE;
    }
}

/*===========================================================================*/
/* Packet Builder                                                            */
/*===========================================================================*/

size_t ap_build_packet(uint8_t *buf, size_t buf_size,
                       uint8_t cmd,
                       const uint8_t *payload, uint8_t payload_len) {
    /* Total size: SYNC(1) + LEN(1) + CMD(1) + payload + CRC(1). */
    size_t total = (size_t)4 + payload_len;

    if (buf == NULL || buf_size < total) {
        return 0;
    }
    if (payload_len > AP_MAX_PAYLOAD_SIZE) {
        return 0;
    }

    /* LEN = CMD(1) + payload_len. */
    uint8_t len_field = 1 + payload_len;

    /* Build packet. */
    buf[0] = AP_SYNC_BYTE;
    buf[1] = len_field;
    buf[2] = cmd;
    if (payload_len > 0 && payload != NULL) {
        memcpy(&buf[3], payload, payload_len);
    }

    /* CRC over (LEN + CMD + PAYLOAD) = buf[1..2+payload_len]. */
    buf[3 + payload_len] = ap_crc8(&buf[1], (size_t)len_field + 1);

    return total;
}

size_t ap_build_ok(uint8_t *buf, size_t buf_size,
                   const uint8_t *payload, uint8_t payload_len) {
    return ap_build_packet(buf, buf_size, AP_CMD_OK, payload, payload_len);
}

size_t ap_build_error(uint8_t *buf, size_t buf_size, ap_error_t error_code) {
    uint8_t code = (uint8_t)error_code;
    return ap_build_packet(buf, buf_size, AP_CMD_ERROR, &code, 1);
}

size_t ap_build_event(uint8_t *buf, size_t buf_size,
                      uint8_t event_cmd,
                      const uint8_t *payload, uint8_t payload_len) {
    return ap_build_packet(buf, buf_size, event_cmd, payload, payload_len);
}

/*===========================================================================*/
/* Access control                                                            */
/*===========================================================================*/

bool ap_cmd_requires_claim(uint8_t cmd) {
    switch (cmd) {
    /* LED control */
    case AP_CMD_LED_OFF:
    case AP_CMD_SET_RGB:
    case AP_CMD_SET_HSV:
    case AP_CMD_SET_BRIGHTNESS:
    case AP_CMD_SET_EFFECT:
    /* Power control */
    case AP_CMD_POWER_ON:
    case AP_CMD_POWER_OFF:
    /* Settings write */
    case AP_CMD_SETTINGS_SET:
        return true;
    default:
        return false;
    }
}
