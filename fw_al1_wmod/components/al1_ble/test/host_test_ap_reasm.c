/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * Host unit test for the pure AP frame reassembler (ap_reasm). No ESP-IDF
 * required — run from the al1_ble component directory:
 *
 *   cc -I . -I ../attentio_protocol/include -o /tmp/ap_reasm_test \
 *      ap_reasm.c test/host_test_ap_reasm.c && /tmp/ap_reasm_test
 */
#include "ap_reasm.h"

#include <stdio.h>
#include <string.h>

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

/* ---- Capturing callback --------------------------------------------------- */

typedef struct {
    int      count;
    uint16_t len;
    uint8_t  frame[AP_MAX_FRAME];
} capture_t;

static void capture_cb(const uint8_t *frame, uint16_t len, void *user)
{
    capture_t *c = (capture_t *)user;
    c->count++;
    c->len = len;
    memcpy(c->frame, frame, len);
}

/* Build a raw AP frame: [SYNC][LEN][CMD][payload...][CRC]. The CRC byte is
 * arbitrary here — the reassembler frames by length and does not check it. */
static uint16_t make_frame(uint8_t *out, uint8_t cmd,
                           const uint8_t *payload, uint8_t payload_len,
                           uint8_t crc)
{
    uint16_t i = 0;
    out[i++] = AP_SYNC_BYTE;
    out[i++] = (uint8_t)(payload_len + 1u); /* LEN = CMD + payload */
    out[i++] = cmd;
    for (uint8_t j = 0; j < payload_len; j++) {
        out[i++] = payload[j];
    }
    out[i++] = crc;
    return i;
}

/* ---- Tests ---------------------------------------------------------------- */

static void test_roundtrip_whole(void)
{
    printf("test_roundtrip_whole\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    uint8_t pl[] = { 0xFF, 0x00, 0x10 };
    uint8_t frame[AP_MAX_FRAME];
    uint16_t flen = make_frame(frame, 0x21 /* SET_RGB */, pl, sizeof(pl), 0x7E);

    ap_reasm_feed(&r, frame, flen);

    CHECK(cap.count == 1);
    CHECK(cap.len == flen);
    CHECK(cap.len == (uint16_t)(sizeof(pl) + 1 + 3));
    CHECK(memcmp(cap.frame, frame, flen) == 0);
    CHECK(r.frames == 1);
    CHECK(r.len_err == 0);
}

static void test_split_across_feeds(void)
{
    printf("test_split_across_feeds\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    uint8_t pl[] = { 1, 2, 3, 4, 5 };
    uint8_t frame[AP_MAX_FRAME];
    uint16_t flen = make_frame(frame, 0x01, pl, sizeof(pl), 0xAB);

    /* Feed one byte at a time — the worst-case BLE fragmentation. */
    for (uint16_t i = 0; i < flen; i++) {
        ap_reasm_feed(&r, &frame[i], 1);
    }

    CHECK(cap.count == 1);
    CHECK(cap.len == flen);
    CHECK(memcmp(cap.frame, frame, flen) == 0);
}

static void test_back_to_back(void)
{
    printf("test_back_to_back\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    uint8_t a[AP_MAX_FRAME], b[AP_MAX_FRAME];
    uint8_t pa[] = { 9 };
    uint8_t pb[] = { 8, 7 };
    uint16_t la = make_frame(a, 0x01, pa, sizeof(pa), 0x11);
    uint16_t lb = make_frame(b, 0x02, pb, sizeof(pb), 0x22);

    uint8_t both[2 * AP_MAX_FRAME];
    memcpy(both, a, la);
    memcpy(both + la, b, lb);

    ap_reasm_feed(&r, both, la + lb);

    /* Last frame captured should be b; two frames total. */
    CHECK(r.frames == 2);
    CHECK(cap.count == 2);
    CHECK(cap.len == lb);
    CHECK(memcmp(cap.frame, b, lb) == 0);
}

static void test_zero_len_rejected(void)
{
    printf("test_zero_len_rejected\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    /* SYNC then LEN=0 (invalid) — rejected, then a valid frame recovers. */
    uint8_t bad[] = { AP_SYNC_BYTE, 0x00 };
    ap_reasm_feed(&r, bad, sizeof(bad));
    CHECK(r.len_err == 1);
    CHECK(cap.count == 0);

    uint8_t pl[] = { 0x42 };
    uint8_t frame[AP_MAX_FRAME];
    uint16_t flen = make_frame(frame, 0x01, pl, sizeof(pl), 0x55);
    ap_reasm_feed(&r, frame, flen);
    CHECK(cap.count == 1);
    CHECK(memcmp(cap.frame, frame, flen) == 0);
}

static void test_resync_after_garbage(void)
{
    printf("test_resync_after_garbage\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    /* Leading non-sync garbage is discarded until a real SYNC arrives. */
    uint8_t garbage[] = { 0x00, 0x11, 0x22, 0x33 };
    ap_reasm_feed(&r, garbage, sizeof(garbage));
    CHECK(cap.count == 0);

    uint8_t pl[] = { 0xDE, 0xAD };
    uint8_t frame[AP_MAX_FRAME];
    uint16_t flen = make_frame(frame, 0x80, pl, sizeof(pl), 0x99);
    ap_reasm_feed(&r, frame, flen);
    CHECK(cap.count == 1);
    CHECK(memcmp(cap.frame, frame, flen) == 0);
}

static void test_max_size_frame(void)
{
    printf("test_max_size_frame\n");
    capture_t cap = {0};
    ap_reasm_t r;
    ap_reasm_init(&r, capture_cb, &cap);

    uint8_t pl[AP_MAX_PAYLOAD_SIZE];
    for (unsigned i = 0; i < AP_MAX_PAYLOAD_SIZE; i++) {
        pl[i] = (uint8_t)i;
    }
    uint8_t frame[AP_MAX_FRAME];
    uint16_t flen = make_frame(frame, 0x01, pl, AP_MAX_PAYLOAD_SIZE, 0x5A);

    CHECK(flen == AP_MAX_FRAME); /* 256-byte maximal frame. */
    ap_reasm_feed(&r, frame, flen);
    CHECK(cap.count == 1);
    CHECK(cap.len == AP_MAX_FRAME);
    CHECK(memcmp(cap.frame, frame, flen) == 0);
}

int main(void)
{
    test_roundtrip_whole();
    test_split_across_feeds();
    test_back_to_back();
    test_zero_len_rejected();
    test_resync_after_garbage();
    test_max_size_frame();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
