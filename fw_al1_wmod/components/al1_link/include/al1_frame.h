/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_link frame format + pure builder/parser (no ESP-IDF dependency, so it
 * compiles and is unit-tested on a host). See al1_link.h for the ESP-IDF UART
 * runtime built on top of this.
 *
 * Framing:
 *   [SYNC 0xA5][VER 0x01][CHANNEL][SEQ][LEN_HI][LEN_LO][PAYLOAD ≤4096][CRC_HI][CRC_LO]
 *
 *   - CRC-16/CCITT-FALSE over VER..PAYLOAD (see crc16_ccitt.h).
 *   - LEN and CRC are big-endian; SEQ is per-channel and wraps.
 */
#ifndef AL1_FRAME_H
#define AL1_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/* Wire constants                                                            */
/*===========================================================================*/

#define AL1_LINK_SYNC           0xA5u   /**< Start-of-frame marker.          */
#define AL1_LINK_VERSION        0x01u   /**< Frame format version.           */
/*
 * Max payload bytes per frame. The wire format allows up to 4096; a
 * RAM-constrained endpoint (e.g. the STM32C071, 24 KB SRAM) may override this
 * downward at build time — frames larger than its cap are dropped as length
 * errors. Both ends still agree on the 16-bit wire LEN encoding.
 */
#ifndef AL1_LINK_MAX_PAYLOAD
#define AL1_LINK_MAX_PAYLOAD    4096u
#endif
#define AL1_LINK_HEADER_LEN     6u      /**< SYNC+VER+CH+SEQ+LEN_HI+LEN_LO.  */
#define AL1_LINK_CRC_LEN        2u      /**< Trailing CRC-16.                */
#define AL1_LINK_OVERHEAD       (AL1_LINK_HEADER_LEN + AL1_LINK_CRC_LEN)
#define AL1_LINK_MAX_FRAME      (AL1_LINK_OVERHEAD + AL1_LINK_MAX_PAYLOAD)

/*===========================================================================*/
/* Channels                                                                  */
/*===========================================================================*/

/** @brief Logical channels multiplexed over the link. */
typedef enum {
    AL1_CH_AP_CTRL   = 0x01,  /**< Attentio Protocol control frames.        */
    AL1_CH_LOG       = 0x02,  /**< Human-readable log text.                 */
    AL1_CH_EVT       = 0x03,  /**< Asynchronous events.                     */
    AL1_CH_BULK      = 0x04,  /**< Bulk transfer (e.g. OTA chunks).         */
    AL1_CH_KEEPALIVE = 0xFF,  /**< Link keep-alive.                         */
} al1_link_channel_t;

/*===========================================================================*/
/* Statistics                                                                */
/*===========================================================================*/

/** @brief Running link counters (for diagnostics / loopback validation). */
typedef struct {
    uint32_t rx_frames;     /**< Frames delivered to the RX callback.       */
    uint32_t rx_crc_err;    /**< Frames dropped on CRC mismatch.            */
    uint32_t rx_ver_err;    /**< Frames dropped on bad VER byte.            */
    uint32_t rx_len_err;    /**< Frames dropped on oversized LEN.           */
    uint32_t rx_resync;     /**< Parser resync events.                      */
    uint32_t tx_frames;     /**< Frames handed to the UART.                 */
    uint32_t tx_drops;      /**< Frames dropped (queue full / too big).     */
} al1_link_stats_t;

/*===========================================================================*/
/* Builder + parser                                                          */
/*===========================================================================*/

/**
 * @brief   RX frame callback.
 * @param channel   Channel byte from the frame.
 * @param seq       Sequence byte from the frame.
 * @param payload   Pointer to payload bytes (valid only during the call).
 * @param len       Payload length in bytes (0..AL1_LINK_MAX_PAYLOAD).
 * @param user      Opaque user pointer passed at parser init.
 */
typedef void (*al1_frame_cb_t)(uint8_t channel, uint8_t seq,
                               const uint8_t *payload, uint16_t len,
                               void *user);

/** @brief RX parser state-machine states. */
typedef enum {
    AL1_PS_SYNC = 0,
    AL1_PS_VER,
    AL1_PS_CHANNEL,
    AL1_PS_SEQ,
    AL1_PS_LEN_HI,
    AL1_PS_LEN_LO,
    AL1_PS_PAYLOAD,
    AL1_PS_CRC_HI,
    AL1_PS_CRC_LO,
} al1_parse_state_t;

/**
 * @brief   Byte-fed frame parser context.
 * @note    Large (holds a full-frame payload buffer); allocate on the heap
 *          or as a static, not on a small task stack.
 */
typedef struct {
    al1_parse_state_t state;
    uint8_t  channel;
    uint8_t  seq;
    uint16_t len;
    uint16_t idx;
    uint16_t crc_calc;      /**< Running CRC over VER..PAYLOAD.             */
    uint16_t crc_rx;        /**< CRC received on the wire.                  */
    uint8_t  payload[AL1_LINK_MAX_PAYLOAD];
    al1_frame_cb_t cb;
    void    *user;
    al1_link_stats_t *stats; /**< Optional; may be NULL.                    */
} al1_parser_t;

/**
 * @brief   Build a framed packet into @p out.
 * @return Total frame length written, or 0 on error (len too big / buffer too small).
 */
size_t al1_frame_build(uint8_t *out, size_t out_cap,
                       uint8_t channel, uint8_t seq,
                       const uint8_t *payload, uint16_t len);

/** @brief Initialize a parser context. */
void al1_parser_init(al1_parser_t *p, al1_frame_cb_t cb, void *user,
                     al1_link_stats_t *stats);

/** @brief Feed received bytes; invokes the callback for each valid frame. */
void al1_parser_feed(al1_parser_t *p, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AL1_FRAME_H */
