/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * Length-only AP frame reassembler — see ap_reasm.h.
 */

#include "ap_reasm.h"

void ap_reasm_init(ap_reasm_t *r, ap_reasm_cb_t cb, void *user)
{
    r->state = AP_REASM_SYNC;
    r->need = 0;
    r->idx = 0;
    r->cb = cb;
    r->user = user;
    r->frames = 0;
    r->len_err = 0;
}

void ap_reasm_feed(ap_reasm_t *r, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (r->state) {
        case AP_REASM_SYNC:
            if (b == AP_SYNC_BYTE) {
                r->frame[0] = b;
                r->idx = 1;
                r->state = AP_REASM_LEN;
            }
            /* Non-sync bytes are discarded until we lock onto a frame. */
            break;

        case AP_REASM_LEN:
            if (b < AP_MIN_LEN || b > AP_MAX_LEN) {
                /* Invalid length — drop and hunt for the next sync. */
                r->len_err++;
                r->state = AP_REASM_SYNC;
                break;
            }
            r->frame[1] = b;
            r->idx = 2;
            r->need = (uint16_t)b + 1u;  /* CMD + PAYLOAD (== LEN) plus CRC. */
            r->state = AP_REASM_DATA;
            break;

        case AP_REASM_DATA:
            r->frame[r->idx++] = b;
            if (--r->need == 0) {
                r->frames++;
                if (r->cb != NULL) {
                    r->cb(r->frame, r->idx, r->user);
                }
                r->state = AP_REASM_SYNC;
            }
            break;

        default:
            r->state = AP_REASM_SYNC;
            break;
        }
    }
}
