/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_ble — NimBLE peripheral bring-up for the AttentioLight-1 Wireless Module.
 *
 * Brings up NVS + the NimBLE host, configures Just-Works pairing with LE Secure
 * Connections and bonding persisted in NVS, registers the Attentio GATT service
 * (al1_ble_gatt.c), and advertises connectably. The byte pipe is wired to the
 * AP bridge by the caller via al1_ble_set_rx_cb() / al1_ble_notify().
 */

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "al1_ble_priv.h"

static const char *TAG = "al1_ble";

/* Address type chosen at sync time and reused for advertising. */
static uint8_t s_own_addr_type;

/* Registered inbound-write sink (set via al1_ble_set_rx_cb). */
static al1_ble_rx_cb_t s_rx_cb;
static void           *s_rx_user;

/* Registered connection state-change sink (set via al1_ble_set_conn_evt_cb). */
static al1_ble_conn_evt_cb_t s_conn_cb;
static void                 *s_conn_user;

/* Active-connection registry (for notify broadcast + per-conn bridge state). */
typedef struct {
    bool     in_use;
    uint16_t handle;
} al1_ble_conn_t;

static al1_ble_conn_t s_conns[AL1_BLE_MAX_CONN];

static void conn_add(uint16_t handle)
{
    for (int i = 0; i < AL1_BLE_MAX_CONN; i++) {
        if (!s_conns[i].in_use) {
            s_conns[i].in_use = true;
            s_conns[i].handle = handle;
            return;
        }
    }
    ESP_LOGW(TAG, "conn table full; handle=%d not tracked", handle);
}

static void conn_remove(uint16_t handle)
{
    for (int i = 0; i < AL1_BLE_MAX_CONN; i++) {
        if (s_conns[i].in_use && s_conns[i].handle == handle) {
            s_conns[i].in_use = false;
            return;
        }
    }
}

/* Provided by NimBLE's NVS-backed store (CONFIG_BT_NIMBLE_NVS_PERSIST=y). */
void ble_store_config_init(void);

static int al1_ble_gap_event(struct ble_gap_event *event, void *arg);

/*===========================================================================*/
/* Advertising                                                               */
/*===========================================================================*/

/*
 * The 128-bit service UUID (18 B) plus the device name do not both fit in the
 * 31-byte advertising PDU, so the UUID goes in the advertisement and the name
 * in the scan response.
 */
static void al1_ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    struct ble_gap_adv_params adv_params;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&al1_ble_svc_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed; rc=%d", rc);
        return;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    const char *name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t *)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;
    rsp_fields.tx_pwr_lvl_is_present = 1;
    rsp_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, al1_ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as \"%s\"", name);
}

/*===========================================================================*/
/* GAP events                                                                */
/*===========================================================================*/

static int al1_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect %s; status=%d handle=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status, event->connect.conn_handle);
        if (event->connect.status == 0) {
            conn_add(event->connect.conn_handle);
            if (s_conn_cb != NULL) {
                s_conn_cb(event->connect.conn_handle, true, s_conn_user);
            }
            /* Keep advertising so additional centrals can still connect. */
            al1_ble_advertise();
        } else {
            /* Failed; resume advertising. */
            al1_ble_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d handle=%d",
                 event->disconnect.reason, event->disconnect.conn.conn_handle);
        conn_remove(event->disconnect.conn.conn_handle);
        if (s_conn_cb != NULL) {
            s_conn_cb(event->disconnect.conn.conn_handle, false, s_conn_user);
        }
        al1_ble_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "conn update; status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption change; status=%d handle=%d",
                 event->enc_change.status, event->enc_change.conn_handle);
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "  encrypted=%d authenticated=%d bonded=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated,
                     desc.sec_state.bonded);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe; handle=%d attr=%d cur_notify=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update; handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Peer is re-pairing while a bond already exists: drop the stale bond
         * and let the new secure link proceed. */
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        al1_ble_advertise();
        return 0;

    default:
        return 0;
    }
}

/*===========================================================================*/
/* Host callbacks + task                                                     */
/*===========================================================================*/

static void al1_ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "nimble host reset; reason=%d", reason);
}

static void al1_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed; rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto addr failed; rc=%d", rc);
        return;
    }
    al1_ble_advertise();
}

static void al1_ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "nimble host task started");
    nimble_port_run(); /* Returns only on nimble_port_stop(). */
    nimble_port_freertos_deinit();
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void al1_ble_set_rx_cb(al1_ble_rx_cb_t cb, void *user)
{
    s_rx_cb = cb;
    s_rx_user = user;
}

void al1_ble_set_conn_evt_cb(al1_ble_conn_evt_cb_t cb, void *user)
{
    s_conn_cb = cb;
    s_conn_user = user;
}

int al1_ble_notify_conn(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (len == 0) {
        return 0;
    }

    /* Usable notification payload is ATT_MTU minus the 3-byte notify header;
     * fall back to the BLE minimum (23 - 3) if MTU is unknown. */
    uint16_t mtu = ble_att_mtu(conn_handle);
    uint16_t chunk = (mtu > 3) ? (uint16_t)(mtu - 3) : 20;

    int rc = 0;
    for (uint16_t off = 0; off < len; off += chunk) {
        uint16_t n = (uint16_t)(len - off);
        if (n > chunk) {
            n = chunk;
        }
        int r = al1_gatt_notify(conn_handle, data + off, n);
        if (r != 0) {
            rc = r;
        }
    }
    return rc;
}

int al1_ble_notify_all(const uint8_t *data, uint16_t len)
{
    int dispatched = 0;

    for (int i = 0; i < AL1_BLE_MAX_CONN; i++) {
        if (!s_conns[i].in_use) {
            continue;
        }
        al1_ble_notify_conn(s_conns[i].handle, data, len);
        dispatched++;
    }
    return dispatched;
}

void al1_ble_dispatch_write(uint16_t conn_handle,
                            const uint8_t *data, uint16_t len)
{
    if (s_rx_cb != NULL) {
        s_rx_cb(conn_handle, data, len, s_rx_user);
    }
}

int al1_ble_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    return al1_gatt_notify(conn_handle, data, len);
}

esp_err_t al1_ble_start(void)
{
    /* NVS — holds BLE bonds (and PHY calibration). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Host configuration. */
    ble_hs_cfg.reset_cb = al1_ble_on_reset;
    ble_hs_cfg.sync_cb = al1_ble_on_sync;
    ble_hs_cfg.gatts_register_cb = al1_gatt_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Security: Just-Works (no IO) + LE Secure Connections + bonding. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC;

    int rc = al1_gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "gatt service init failed; rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(AL1_BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "device_name_set failed; rc=%d", rc);
        return ESP_FAIL;
    }

    ble_store_config_init();

    nimble_port_freertos_init(al1_ble_host_task);
    return ESP_OK;
}
