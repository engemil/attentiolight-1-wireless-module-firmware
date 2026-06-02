/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * AP bridge — couples the BLE byte pipe (al1_ble) to the STM32 UART link
 * (al1_link), carrying Attentio Protocol frames in both directions, and owns
 * the BLE-side session model.
 *
 * Session model (single active session — the STM32 MICB tracks one BLE
 * controller at a time):
 *   - A BLE client CLAIMs control; the STM32 replies OK with a 2-byte session
 *     id. The bridge binds that connection as the controller and routes
 *     subsequent AP responses/events to it.
 *   - Because every BLE client looks like the single MICB "BLE" interface, the
 *     bridge GATES locally: a non-controlling connection may not issue
 *     claim-requiring commands (ap_cmd_requires_claim) — those are rejected with
 *     an AP error rather than forwarded, so one client cannot drive the device
 *     while another holds control.
 *   - On disconnect of the controller, the bridge synthesizes a RELEASE so the
 *     MICB session does not leak on a radio drop.
 *
 * Framing is length-only (ap_reasm); the STM32 remains the sole AP authority
 * (CRC, full semantics). The bridge only peeks the CMD byte and the OK
 * session-id for routing/gating.
 */

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"            /* BLE_HS_CONN_HANDLE_NONE */

#include "al1_ble.h"
#include "al1_link.h"
#include "ap_reasm.h"
#include "attentio_protocol.h"

static const char *TAG = "al1_ble_bridge";

/* Offsets within a whole AP frame: [SYNC][LEN][CMD][PAYLOAD..][CRC]. */
#define AP_OFF_LEN      1   /* LEN = CMD + payload length */
#define AP_OFF_CMD      2
#define AP_OFF_PAYLOAD  3

#define CONN_NONE       BLE_HS_CONN_HANDLE_NONE

/* One reassembler per active connection so interleaved client streams cannot
 * corrupt each other. Keyed by BLE connection handle. */
typedef struct {
    bool       in_use;
    uint16_t   handle;
    ap_reasm_t reasm;
} bridge_slot_t;

static bridge_slot_t s_slots[AL1_BLE_MAX_CONN];

/* Single active session state. */
static uint16_t s_controlling_conn = CONN_NONE;  /* who holds the claim       */
static uint16_t s_session_id;                    /* MICB session id (from OK)  */
static uint16_t s_pending_claim_conn = CONN_NONE;/* awaiting OK for this conn  */

static bridge_slot_t *slot_find(uint16_t handle)
{
    for (int i = 0; i < AL1_BLE_MAX_CONN; i++) {
        if (s_slots[i].in_use && s_slots[i].handle == handle) {
            return &s_slots[i];
        }
    }
    return NULL;
}

static void clear_binding(void)
{
    s_controlling_conn = CONN_NONE;
    s_session_id = 0;
}

static void forward_to_stm32(const uint8_t *frame, uint16_t len)
{
    esp_err_t err = al1_link_send(AL1_CH_AP_CTRL, frame, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP_CTRL forward failed (%s)", esp_err_to_name(err));
    }
}

static void reject_not_controller(uint16_t conn)
{
    uint8_t buf[AP_MIN_PACKET_SIZE + 1];
    size_t n = ap_build_error(buf, sizeof(buf), AP_ERR_NOT_CONTROLLER);
    if (n > 0) {
        al1_ble_notify_conn(conn, buf, (uint16_t)n);
    }
}

/*
 * ap_reasm callback: a whole AP frame from connection `user` is ready.
 * Apply the outbound session policy, then forward (or reject) it.
 */
static void on_ap_frame(const uint8_t *frame, uint16_t len, void *user)
{
    const bridge_slot_t *s = (const bridge_slot_t *)user;
    uint16_t conn = s->handle;
    uint8_t cmd = (len > AP_OFF_CMD) ? frame[AP_OFF_CMD] : 0xFF;

    if (cmd == AP_CMD_CLAIM) {
        /* Remember who is claiming so the OK can be routed + bound. */
        s_pending_claim_conn = conn;
        forward_to_stm32(frame, len);
        return;
    }

    if (conn == s_controlling_conn) {
        forward_to_stm32(frame, len);
        if (cmd == AP_CMD_RELEASE) {
            clear_binding();   /* controller voluntarily released */
        }
        return;
    }

    /* Non-controlling connection. */
    if (ap_cmd_requires_claim(cmd)) {
        ESP_LOGW(TAG, "gate: conn=%u cmd=0x%02x blocked (not controller)",
                 conn, cmd);
        reject_not_controller(conn);
        return;
    }

    /* Claim-free command (PING, queries, RELEASE-from-non-controller): the
     * STM32 handles authorization for these itself. */
    forward_to_stm32(frame, len);
}

/* al1_ble connection callback: track per-connection reassembly + session. */
static void on_conn_evt(uint16_t handle, bool connected, void *user)
{
    (void)user;

    if (connected) {
        for (int i = 0; i < AL1_BLE_MAX_CONN; i++) {
            if (!s_slots[i].in_use) {
                s_slots[i].in_use = true;
                s_slots[i].handle = handle;
                ap_reasm_init(&s_slots[i].reasm, on_ap_frame, &s_slots[i]);
                return;
            }
        }
        ESP_LOGW(TAG, "no free bridge slot for handle=%d", handle);
        return;
    }

    /* Disconnect. */
    bridge_slot_t *s = slot_find(handle);
    if (s != NULL) {
        s->in_use = false;
    }
    if (handle == s_pending_claim_conn) {
        s_pending_claim_conn = CONN_NONE;
    }
    if (handle == s_controlling_conn) {
        /* Synthesize RELEASE so the MICB session does not leak on a drop. */
        uint8_t buf[AP_MIN_PACKET_SIZE];
        size_t n = ap_build_packet(buf, sizeof(buf), AP_CMD_RELEASE, NULL, 0);
        if (n > 0) {
            ESP_LOGI(TAG, "controller %u dropped; releasing session %u",
                     handle, s_session_id);
            forward_to_stm32(buf, (uint16_t)n);
        }
        clear_binding();
    }
}

/* al1_ble inbound-write callback: BLE -> reassemble -> outbound policy. */
static void on_ble_write(uint16_t handle, const uint8_t *data, uint16_t len,
                         void *user)
{
    (void)user;
    bridge_slot_t *s = slot_find(handle);
    if (s == NULL) {
        ESP_LOGW(TAG, "write from untracked handle=%d (%u bytes dropped)",
                 handle, len);
        return;
    }
    ap_reasm_feed(&s->reasm, data, len);
}

void al1_ble_ap_bridge_on_link_ap(uint8_t seq,
                                  const uint8_t *payload, uint16_t len)
{
    (void)seq;
    uint8_t cmd = (len > AP_OFF_CMD) ? payload[AP_OFF_CMD] : 0xFF;

    /* OK to a pending CLAIM: bind the controller and capture the session id. */
    if (cmd == AP_CMD_OK && s_pending_claim_conn != CONN_NONE) {
        uint16_t conn = s_pending_claim_conn;
        s_pending_claim_conn = CONN_NONE;
        s_controlling_conn = conn;

        uint8_t ap_len = payload[AP_OFF_LEN];      /* CMD + payload */
        if (ap_len >= 3) {                         /* CMD + 2 session-id bytes */
            s_session_id = ((uint16_t)payload[AP_OFF_PAYLOAD] << 8) |
                           payload[AP_OFF_PAYLOAD + 1];
        }
        ESP_LOGI(TAG, "conn %u claimed; session=%u", conn, s_session_id);
        al1_ble_notify_conn(conn, payload, len);
        return;
    }

    /* Session ended (e.g. takeover by USB): tell the controller, then unbind. */
    if (cmd == AP_CMD_EVT_SESSION_END) {
        if (s_controlling_conn != CONN_NONE) {
            al1_ble_notify_conn(s_controlling_conn, payload, len);
        }
        clear_binding();
        return;
    }

    /* Everything else routes to the controller; with no controller, broadcast
     * (covers unsolicited/broadcast events — refined in EVT routing later). */
    if (s_controlling_conn != CONN_NONE) {
        al1_ble_notify_conn(s_controlling_conn, payload, len);
    } else {
        al1_ble_notify_all(payload, len);
    }
}

void al1_ble_ap_bridge_init(void)
{
    s_controlling_conn = CONN_NONE;
    s_pending_claim_conn = CONN_NONE;
    s_session_id = 0;

    al1_ble_set_conn_evt_cb(on_conn_evt, NULL);
    al1_ble_set_rx_cb(on_ble_write, NULL);
    ESP_LOGI(TAG, "AP bridge ready (single-session)");
}
