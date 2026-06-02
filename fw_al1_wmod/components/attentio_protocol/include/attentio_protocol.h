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
 * @file    attentio_protocol.h
 * @brief   Attentio Protocol (AP) packet format, command IDs, and parser API.
 *
 * @details The Attentio Protocol is a packet protocol used for
 *          machine-to-machine communication between the AttentioLight-1
 *          device and host applications over USB CDC (and future BLE/WiFi).
 *
 *          Packet format:
 *          [SYNC 0xA5][LEN][CMD][PAYLOAD 0-252B][CRC8]
 *
 *          LEN = length of (CMD + PAYLOAD), excludes SYNC, LEN, and CRC.
 *          CRC8 = CRC-8/CCITT over (LEN + CMD + PAYLOAD).
 *          Max packet size = 256 bytes (1+1+1+252+1).
 *
 * @note    Protocol Version: 1 (development)
 */

#ifndef ATTENTIO_PROTOCOL_H
#define ATTENTIO_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*===========================================================================*/
/* Protocol Constants                                                        */
/*===========================================================================*/

/** @brief Protocol version number. */
#define AP_PROTOCOL_VERSION         1

/** @brief Sync byte that marks the start of every AP packet. */
#define AP_SYNC_BYTE                0xA5

/** @brief Maximum payload size in bytes (252). */
#define AP_MAX_PAYLOAD_SIZE         252

/** @brief Maximum total packet size: SYNC + LEN + CMD + 252 + CRC = 256. */
#define AP_MAX_PACKET_SIZE          256

/** @brief Minimum packet size: SYNC + LEN + CMD + CRC = 4 (LEN=1, no payload). */
#define AP_MIN_PACKET_SIZE          4

/*===========================================================================*/
/* Command IDs                                                               */
/*===========================================================================*/
/**
 * @name    Command IDs
 * @details Commands are grouped by category with reserved ranges.
 * @{
 */

/* Session Control (0x00-0x0F) — Bidirectional */
#define AP_CMD_CLAIM                0x01    /**< Claim device control.     */
#define AP_CMD_RELEASE              0x02    /**< Release device control.   */
#define AP_CMD_PING                 0x03    /**< Keep-alive ping.          */

/* Power Control (0x10-0x1F) — Host -> Device */
#define AP_CMD_POWER_ON             0x10    /**< Wake from low-power mode. */
#define AP_CMD_POWER_OFF            0x11    /**< Enter low-power mode.     */

/* Direct LED Control (0x20-0x2F) — Host -> Device */
#define AP_CMD_LED_OFF              0x20    /**< Turn LEDs off.            */
#define AP_CMD_SET_RGB              0x21    /**< Set RGB color [R,G,B].    */
#define AP_CMD_SET_HSV              0x22    /**< Set HSV [H:2,S,V].       */
#define AP_CMD_SET_BRIGHTNESS       0x23    /**< Set brightness [0-100].   */

/* Custom Effects (0x30-0x3F) — Host -> Device, placeholder */
#define AP_CMD_SET_EFFECT           0x30    /**< Set effect (TBD).         */

/* Query (0x40-0x4F) — Host -> Device */
#define AP_CMD_GET_STATUS           0x40    /**< Request device status.    */
#define AP_CMD_GET_METADATA         0x43    /**< Request device metadata.  */
#define AP_CMD_METADATA_GET         0x44    /**< Get single metadata field. */

/* Settings (0x50-0x5F) — Bidirectional */
#define AP_CMD_SETTINGS_LIST        0x50    /**< List all settings.        */
#define AP_CMD_SETTINGS_GET         0x51    /**< Get setting value.        */
#define AP_CMD_SETTINGS_SET         0x52    /**< Set setting value.        */

/* Log Control (0x60-0x6F) — Bidirectional */
#define AP_CMD_LOG_GET_LEVEL        0x60    /**< Get current log level.    */
#define AP_CMD_LOG_SET_LEVEL        0x61    /**< Set runtime log level.    */

/* DFU (0x70-0x7F) — Host -> Device */
#define AP_CMD_DFU_ENTER            0x70    /**< Enter DFU bootloader.     */

/* Events (0x80-0x8F) — Device -> Host */
#define AP_CMD_EVT_BUTTON           0x80    /**< Button event. Payload: [ap_button_event_t]. */
#define AP_CMD_EVT_STATE_CHANGE     0x81    /**< System-state change. Payload: [old_state, new_state] (device system-state enum). */
#define AP_CMD_EVT_SESSION_END      0x82    /**< Session ended event. Payload: [ap_session_end_reason_t]. */

/* Responses (0xF0-0xFF) — Device -> Host */
#define AP_CMD_OK                   0xF0    /**< Success response.         */
#define AP_CMD_ERROR                0xF1    /**< Error response.           */

/** @} */

/*===========================================================================*/
/* Error Codes                                                               */
/*===========================================================================*/
/**
 * @name    Error Codes
 * @details Sent as the 1-byte payload of AP_CMD_ERROR responses.
 * @{
 */

typedef enum {
    AP_ERR_NONE             = 0x00,     /**< No error.                     */
    AP_ERR_NOT_CONTROLLER   = 0x01,     /**< Sender is not active controller. */
    AP_ERR_INVALID_CMD      = 0x02,     /**< Unknown command ID.           */
    AP_ERR_INVALID_PARAM    = 0x03,     /**< Invalid parameter value.      */
    AP_ERR_INVALID_STATE    = 0x04,     /**< Command not valid in current state. */
    AP_ERR_CRC_FAIL         = 0x05,     /**< CRC check failed.             */
} ap_error_t;

/** @} */

/*===========================================================================*/
/* Event Payload Types                                                       */
/*===========================================================================*/

/**
 * @brief   Button event types for AP_CMD_EVT_BUTTON payload.
 */
typedef enum {
    AP_BTN_EVT_SHORT_PRESS              = 0x01,
    AP_BTN_EVT_LONG_PRESS_START         = 0x02,
    AP_BTN_EVT_LONG_PRESS_RELEASE       = 0x03,
    AP_BTN_EVT_EXTENDED_PRESS_START     = 0x04,
    AP_BTN_EVT_EXTENDED_PRESS_RELEASE   = 0x05,
} ap_button_event_t;

/**
 * @brief   Session end reasons for AP_CMD_EVT_SESSION_END payload.
 */
typedef enum {
    AP_SESSION_END_RELEASED     = 0x00, /**< Controller released.          */
    AP_SESSION_END_TAKEOVER     = 0x01, /**< Another interface claimed.    */
    AP_SESSION_END_POWEROFF     = 0x02, /**< Device powered off.           */
} ap_session_end_reason_t;

/*===========================================================================*/
/* Parsed Packet Structure                                                   */
/*===========================================================================*/

/**
 * @brief   Represents a complete, parsed AP packet.
 * @details The parser fills this structure when a valid packet is received.
 *          The builder reads from this structure to produce raw bytes.
 */
typedef struct {
    uint8_t cmd;                            /**< Command ID byte.          */
    uint8_t payload_len;                    /**< Payload length (0-252).   */
    uint8_t payload[AP_MAX_PAYLOAD_SIZE];   /**< Payload data.             */
} ap_packet_t;

/*===========================================================================*/
/* Parser                                                                    */
/*===========================================================================*/

/**
 * @brief   Parser state machine states.
 */
typedef enum {
    AP_PARSE_SYNC,      /**< Waiting for sync byte (0xA5).             */
    AP_PARSE_LEN,       /**< Waiting for LEN byte.                     */
    AP_PARSE_DATA,      /**< Receiving CMD + PAYLOAD bytes.            */
    AP_PARSE_CRC,       /**< Waiting for CRC8 byte.                    */
} ap_parse_state_t;

/**
 * @brief   Parser result codes returned by ap_parse_byte().
 */
typedef enum {
    AP_PARSE_NEED_MORE,     /**< Byte consumed, need more data.        */
    AP_PARSE_COMPLETE,      /**< Valid packet received in parser->pkt. */
    AP_PARSE_ERR_CRC,       /**< Packet received but CRC mismatch.    */
    AP_PARSE_ERR_LEN,       /**< LEN byte is 0 (invalid, need CMD).   */
    AP_PARSE_ERR_OVERFLOW,  /**< LEN exceeds max payload + 1.         */
} ap_parse_result_t;

/**
 * @brief   Byte-by-byte AP packet parser context.
 * @details Feed one byte at a time via ap_parse_byte(). When a complete
 *          packet is received, the result is AP_PARSE_COMPLETE and the
 *          parsed packet is available in the `pkt` field.
 *
 *          On any error (CRC, LEN), the parser automatically resets to
 *          the SYNC state so it can resync on the next valid packet.
 */
typedef struct {
    ap_parse_state_t state;     /**< Current parser state.             */
    uint8_t len;                /**< LEN field value (CMD + payload).  */
    uint8_t idx;                /**< Current index into data buffer.   */
    uint8_t data[AP_MAX_PAYLOAD_SIZE + 1]; /**< CMD + payload buffer.  */
    ap_packet_t pkt;            /**< Parsed packet (valid after COMPLETE). */
} ap_parser_t;

/*===========================================================================*/
/* Function Prototypes                                                       */
/*===========================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Compute CRC-8/CCITT over a data buffer.
 * @details Polynomial: x^8 + x^2 + x + 1 = 0x07, initial value: 0x00.
 *
 * @param[in] data      Pointer to data buffer.
 * @param[in] len       Number of bytes.
 * @return              Computed CRC-8 value.
 */
uint8_t ap_crc8(const uint8_t *data, size_t len);

/**
 * @brief   Initialize (reset) an AP parser context.
 *
 * @param[out] parser   Parser context to initialize.
 */
void ap_parser_init(ap_parser_t *parser);

/**
 * @brief   Reset an AP parser to the initial SYNC-waiting state.
 * @note    This is the same as ap_parser_init() but named for clarity
 *          when resetting after an error or completed packet.
 *
 * @param[out] parser   Parser context to reset.
 */
void ap_parser_reset(ap_parser_t *parser);

/**
 * @brief   Feed one byte into the AP parser.
 * @details Call this for each byte received from the transport (e.g., CDC1).
 *          The parser implements a state machine:
 *          SYNC -> LEN -> DATA (CMD+payload) -> CRC
 *
 *          On AP_PARSE_COMPLETE, the parsed packet is in parser->pkt.
 *          On any error, the parser auto-resets to SYNC state.
 *
 * @param[in,out] parser    Parser context.
 * @param[in]     byte      Next byte from transport.
 * @return                  Parse result code.
 */
ap_parse_result_t ap_parse_byte(ap_parser_t *parser, uint8_t byte);

/**
 * @brief   Build a raw AP packet into a buffer.
 * @details Constructs: [SYNC][LEN][CMD][PAYLOAD][CRC8]
 *
 * @param[out] buf          Output buffer (must be at least payload_len + 4 bytes).
 * @param[in]  buf_size     Size of output buffer.
 * @param[in]  cmd          Command ID byte.
 * @param[in]  payload      Payload data (may be NULL if payload_len == 0).
 * @param[in]  payload_len  Payload length (0-252).
 * @return                  Total packet size written, or 0 on error.
 */
size_t ap_build_packet(uint8_t *buf, size_t buf_size,
                       uint8_t cmd,
                       const uint8_t *payload, uint8_t payload_len);

/**
 * @brief   Build an OK response packet (AP_CMD_OK) with optional payload.
 *
 * @param[out] buf          Output buffer.
 * @param[in]  buf_size     Size of output buffer.
 * @param[in]  payload      Response payload data (may be NULL).
 * @param[in]  payload_len  Response payload length.
 * @return                  Total packet size written, or 0 on error.
 */
size_t ap_build_ok(uint8_t *buf, size_t buf_size,
                   const uint8_t *payload, uint8_t payload_len);

/**
 * @brief   Build an ERROR response packet (AP_CMD_ERROR) with error code.
 *
 * @param[out] buf          Output buffer (must be at least 5 bytes).
 * @param[in]  buf_size     Size of output buffer.
 * @param[in]  error_code   Error code (ap_error_t).
 * @return                  Total packet size written, or 0 on error.
 */
size_t ap_build_error(uint8_t *buf, size_t buf_size, ap_error_t error_code);

/**
 * @brief   Build an event packet with payload.
 *
 * @param[out] buf          Output buffer.
 * @param[in]  buf_size     Size of output buffer.
 * @param[in]  event_cmd    Event command ID (AP_CMD_EVT_*).
 * @param[in]  payload      Event payload data (may be NULL).
 * @param[in]  payload_len  Event payload length.
 * @return                  Total packet size written, or 0 on error.
 */
size_t ap_build_event(uint8_t *buf, size_t buf_size,
                      uint8_t event_cmd,
                      const uint8_t *payload, uint8_t payload_len);

/**
 * @brief   Whether a command may only be issued by the active controller.
 * @details Commands that mutate device state (LED, power, settings-write)
 *          require a claim; session control (CLAIM/RELEASE/PING) and queries
 *          do not. The single authoritative list, shared by the MICB (which
 *          enforces it) and the ESP32 wireless bridge (which pre-gates it so
 *          one BLE client cannot drive the device while another holds control).
 *
 * @param[in] cmd   AP command ID.
 * @return          true if the command requires an active claim.
 */
bool ap_cmd_requires_claim(uint8_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* ATTENTIO_PROTOCOL_H */
