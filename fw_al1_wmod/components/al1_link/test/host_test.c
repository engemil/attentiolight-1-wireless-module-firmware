/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * Host unit test for the pure al1_link frame builder/parser + CRC. No ESP-IDF
 * required:
 *
 *   cc -I ../include -o /tmp/al1_link_test \
 *      ../al1_frame.c ../crc16_ccitt.c host_test.c && /tmp/al1_link_test
 *
 * (or, from the component root, the one-liner in README.md.)
 */
#include "al1_frame.h"
#include "crc16_ccitt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_failures;
static int g_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);              \
        }                                                                    \
    } while (0)

/* ---- Capturing RX callback ------------------------------------------------ */

typedef struct {
    int      count;
    uint8_t  channel;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[AL1_LINK_MAX_PAYLOAD];
} capture_t;

static void capture_cb(uint8_t channel, uint8_t seq,
                       const uint8_t *payload, uint16_t len, void *user)
{
    capture_t *c = (capture_t *)user;
    c->count++;
    c->channel = channel;
    c->seq = seq;
    c->len = len;
    if (len > 0) {
        memcpy(c->payload, payload, len);
    }
}

/* ---- Tests ---------------------------------------------------------------- */

static void test_crc_known_answer(void)
{
    printf("test_crc_known_answer\n");
    /* Canonical CRC-16/CCITT-FALSE check value for "123456789" is 0x29B1. */
    CHECK(al1_crc16_ccitt((const uint8_t *)"123456789", 9) == 0x29B1);
    /* Empty input returns the init value. */
    CHECK(al1_crc16_ccitt(NULL, 0) == AL1_CRC16_INIT);
    /* Incremental update matches one-shot. */
    uint16_t crc = AL1_CRC16_INIT;
    const uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
    for (size_t i = 0; i < sizeof(data); i++) {
        crc = al1_crc16_ccitt_update(crc, data[i]);
    }
    CHECK(crc == al1_crc16_ccitt(data, sizeof(data)));
}

static void test_roundtrip(void)
{
    printf("test_roundtrip\n");
    capture_t cap = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, NULL);

    const uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42 };
    uint8_t frame[AL1_LINK_MAX_FRAME];
    size_t n = al1_frame_build(frame, sizeof(frame),
                               AL1_CH_AP_CTRL, 0x17, payload, sizeof(payload));
    CHECK(n == AL1_LINK_OVERHEAD + sizeof(payload));
    CHECK(frame[0] == AL1_LINK_SYNC);
    CHECK(frame[1] == AL1_LINK_VERSION);

    al1_parser_feed(&p, frame, n);
    CHECK(cap.count == 1);
    CHECK(cap.channel == AL1_CH_AP_CTRL);
    CHECK(cap.seq == 0x17);
    CHECK(cap.len == sizeof(payload));
    CHECK(memcmp(cap.payload, payload, sizeof(payload)) == 0);
}

static void test_zero_length(void)
{
    printf("test_zero_length\n");
    capture_t cap = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, NULL);

    uint8_t frame[AL1_LINK_OVERHEAD];
    size_t n = al1_frame_build(frame, sizeof(frame), AL1_CH_KEEPALIVE, 0, NULL, 0);
    CHECK(n == AL1_LINK_OVERHEAD);

    al1_parser_feed(&p, frame, n);
    CHECK(cap.count == 1);
    CHECK(cap.channel == AL1_CH_KEEPALIVE);
    CHECK(cap.len == 0);
}

static void test_byte_at_a_time_and_seq_wrap(void)
{
    printf("test_byte_at_a_time_and_seq_wrap\n");
    capture_t cap = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, NULL);

    /* Feed 300 frames one byte at a time; SEQ wraps past 255. */
    for (int i = 0; i < 300; i++) {
        uint8_t payload = (uint8_t)i;
        uint8_t frame[AL1_LINK_MAX_FRAME];
        size_t n = al1_frame_build(frame, sizeof(frame),
                                   AL1_CH_LOG, (uint8_t)i, &payload, 1);
        for (size_t k = 0; k < n; k++) {
            al1_parser_feed(&p, &frame[k], 1);
        }
    }
    CHECK(cap.count == 300);
    CHECK(cap.seq == (uint8_t)299);
    CHECK(cap.payload[0] == (uint8_t)299);
}

static void test_bad_crc_rejected(void)
{
    printf("test_bad_crc_rejected\n");
    capture_t cap = {0};
    al1_link_stats_t stats = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, &stats);

    const uint8_t payload[] = { 1, 2, 3, 4 };
    uint8_t frame[AL1_LINK_MAX_FRAME];
    size_t n = al1_frame_build(frame, sizeof(frame), AL1_CH_EVT, 1, payload, 4);
    frame[n - 1] ^= 0xFF;   /* Corrupt CRC_LO. */

    al1_parser_feed(&p, frame, n);
    CHECK(cap.count == 0);
    CHECK(stats.rx_crc_err == 1);
    CHECK(stats.rx_frames == 0);
}

static void test_resync_after_garbage(void)
{
    printf("test_resync_after_garbage\n");
    capture_t cap = {0};
    al1_link_stats_t stats = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, &stats);

    const uint8_t garbage[] = { 0x00, 0xFF, 0x12, 0xA5, 0x99 /* bad VER */ };
    al1_parser_feed(&p, garbage, sizeof(garbage));

    const uint8_t payload[] = { 0xAB, 0xCD };
    uint8_t frame[AL1_LINK_MAX_FRAME];
    size_t n = al1_frame_build(frame, sizeof(frame), AL1_CH_AP_CTRL, 7, payload, 2);
    al1_parser_feed(&p, frame, n);

    CHECK(cap.count == 1);
    CHECK(cap.channel == AL1_CH_AP_CTRL);
    CHECK(cap.seq == 7);
    CHECK(stats.rx_ver_err >= 1);
}

static void test_oversized_len_rejected(void)
{
    printf("test_oversized_len_rejected\n");
    capture_t cap = {0};
    al1_link_stats_t stats = {0};
    al1_parser_t p;
    al1_parser_init(&p, capture_cb, &cap, &stats);

    /* Hand-craft a header claiming LEN = MAX+1. */
    uint8_t hdr[6];
    hdr[0] = AL1_LINK_SYNC;
    hdr[1] = AL1_LINK_VERSION;
    hdr[2] = AL1_CH_BULK;
    hdr[3] = 0;
    uint16_t bad_len = AL1_LINK_MAX_PAYLOAD + 1;
    hdr[4] = (uint8_t)(bad_len >> 8);
    hdr[5] = (uint8_t)(bad_len & 0xFF);
    al1_parser_feed(&p, hdr, sizeof(hdr));

    CHECK(cap.count == 0);
    CHECK(stats.rx_len_err == 1);

    /* Parser must recover and accept a following good frame. */
    const uint8_t payload[] = { 0x55 };
    uint8_t frame[AL1_LINK_MAX_FRAME];
    size_t n = al1_frame_build(frame, sizeof(frame), AL1_CH_LOG, 1, payload, 1);
    al1_parser_feed(&p, frame, n);
    CHECK(cap.count == 1);
}

static void test_build_errors(void)
{
    printf("test_build_errors\n");
    uint8_t small[4];
    const uint8_t payload[] = { 1, 2, 3 };
    /* Buffer too small. */
    CHECK(al1_frame_build(small, sizeof(small), AL1_CH_LOG, 0, payload, 3) == 0);
    /* len > 0 but payload NULL. */
    uint8_t big[64];
    CHECK(al1_frame_build(big, sizeof(big), AL1_CH_LOG, 0, NULL, 3) == 0);
    /* Oversized payload length. */
    CHECK(al1_frame_build(big, sizeof(big), AL1_CH_LOG, 0, payload,
                          AL1_LINK_MAX_PAYLOAD + 1) == 0);
}

int main(void)
{
    test_crc_known_answer();
    test_roundtrip();
    test_zero_length();
    test_byte_at_a_time_and_seq_wrap();
    test_bad_crc_rejected();
    test_resync_after_garbage();
    test_oversized_len_rejected();
    test_build_errors();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
