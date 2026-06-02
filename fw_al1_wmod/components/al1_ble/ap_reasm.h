/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * ap_reasm — length-only reassembler for Attentio Protocol (AP) frames.
 *
 * The BLE bridge carries AP frames as an opaque byte stream: a central may
 * split one AP frame across several GATT writes, or pack several into one. This
 * pure, byte-fed state machine recovers whole AP frames by the AP LEN field and
 * hands each one back verbatim (SYNC..CRC) for forwarding to the STM32, which
 * remains the sole authority on AP semantics. It therefore frames by length
 * only — it does NOT validate the CRC8 or interpret the CMD byte.
 *
 * AP wire format (from the shared attentio_protocol.h core):
 *
 *   [SYNC 0xA5][LEN][CMD][PAYLOAD 0..252][CRC8],   total = LEN + 3,  4..256 B
 *
 *   LEN = length of (CMD + PAYLOAD), valid range 1..253.
 *
 * Depends only on the pure attentio_protocol header for the wire constants;
 * no ESP-IDF dependency — compiles and is unit-tested on a host.
 */
#ifndef AP_REASM_H
#define AP_REASM_H

#include <stddef.h>
#include <stdint.h>

#include "attentio_protocol.h"   /* shared AP wire constants (AP_SYNC_BYTE, …) */

#ifdef __cplusplus
extern "C" {
#endif

#define AP_MIN_LEN       1u                          /**< LEN floor (CMD only).    */
#define AP_MAX_LEN       (AP_MAX_PAYLOAD_SIZE + 1u)  /**< LEN ceiling (CMD + 252). */
#define AP_MAX_FRAME     AP_MAX_PACKET_SIZE          /**< SYNC+LEN+data+CRC = 256. */

/**
 * @brief   Whole-frame callback.
 * @param frame   Complete AP frame bytes (SYNC..CRC); valid only in the call.
 * @param len     Frame length in bytes (== frame[1] + 3).
 * @param user    Opaque pointer passed at init.
 */
typedef void (*ap_reasm_cb_t)(const uint8_t *frame, uint16_t len, void *user);

/** @brief Reassembler states. */
typedef enum {
    AP_REASM_SYNC = 0,  /**< Hunting for the 0xA5 sync byte.                 */
    AP_REASM_LEN,       /**< Reading the LEN byte.                          */
    AP_REASM_DATA,      /**< Collecting CMD + PAYLOAD + CRC (LEN + 1 bytes). */
} ap_reasm_state_t;

/**
 * @brief   Reassembler context. Holds a full-frame buffer; keep off small task
 *          stacks (allocate static / heap, e.g. one per BLE connection).
 */
typedef struct {
    ap_reasm_state_t state;
    uint16_t need;                 /**< Bytes still to collect in DATA.      */
    uint16_t idx;                  /**< Write cursor into @ref frame.        */
    uint8_t  frame[AP_MAX_FRAME];
    ap_reasm_cb_t cb;
    void    *user;
    uint32_t frames;               /**< Whole frames emitted.                */
    uint32_t len_err;              /**< LEN bytes rejected (0 or > max).     */
} ap_reasm_t;

/** @brief Initialise (or reset) a reassembler. */
void ap_reasm_init(ap_reasm_t *r, ap_reasm_cb_t cb, void *user);

/** @brief Feed received bytes; invokes the callback for each whole AP frame. */
void ap_reasm_feed(ap_reasm_t *r, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AP_REASM_H */
