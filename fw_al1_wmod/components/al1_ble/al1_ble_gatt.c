/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * Attentio BLE Service — a transparent byte pipe for Attentio Protocol (AP)
 * frames over GATT. One primary service with two characteristics:
 *
 *   TX (write, host -> device): the central writes AP bytes here; each write is
 *       handed to al1_ble_dispatch_write() for AP reassembly + forwarding.
 *   RX (notify, device -> host): AP responses / events are pushed to subscribed
 *       centrals via al1_gatt_notify().
 *
 * UUIDs live in the custom 1209EEA1-xxxx family (see al1_ble.h). NimBLE adds the
 * RX characteristic's CCCD (0x2902) automatically from the NOTIFY flag.
 */

#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "al1_ble_priv.h"

static const char *TAG = "al1_ble_gatt";

/*
 * 128-bit UUIDs. BLE_UUID128_INIT takes octets least-significant first, so the
 * arrays below read as the reverse of the printed string form documented in
 * al1_ble.h. Only octet [10] (the 16-bit discriminator) differs per UUID.
 */
const ble_uuid128_t al1_ble_svc_uuid =      /* ...-0001-... primary service */
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x01, 0x00, 0xa1, 0xee, 0x09, 0x12);

static const ble_uuid128_t al1_ble_tx_uuid = /* ...-0002-... host -> device */
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x02, 0x00, 0xa1, 0xee, 0x09, 0x12);

static const ble_uuid128_t al1_ble_rx_uuid = /* ...-0003-... device -> host */
    BLE_UUID128_INIT(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                     0x00, 0x00, 0x03, 0x00, 0xa1, 0xee, 0x09, 0x12);

/* Value handle of the RX (notify) characteristic — needed to push notifies. */
static uint16_t al1_rx_val_handle;

/*
 * Largest single write/notify we copy out of an mbuf. The AP frame is ≤256 B
 * but a central may use a larger MTU; one BLE write still maps to one callback,
 * so this caps the per-write scratch copy. AP reassembly downstream spans
 * writes as needed.
 */
#define AL1_BLE_PIPE_MAX  512

static int al1_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def al1_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &al1_ble_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* TX: central writes AP frames in. Encryption required so the
                 * pipe is only usable after Just-Works/LESC pairing. */
                .uuid = &al1_ble_tx_uuid.u,
                .access_cb = al1_gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                /* RX: AP responses / events notified out. */
                .uuid = &al1_ble_rx_uuid.u,
                .access_cb = al1_gatt_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &al1_rx_val_handle,
            },
            { 0 }, /* No more characteristics. */
        },
    },
    { 0 }, /* No more services. */
};

static int al1_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR &&
        ble_uuid_cmp(ctxt->chr->uuid, &al1_ble_tx_uuid.u) == 0) {
        uint8_t  buf[AL1_BLE_PIPE_MAX];
        uint16_t out_len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &out_len);
        if (rc != 0) {
            /* Overruns the scratch buffer — reject rather than truncate. */
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        al1_ble_dispatch_write(conn_handle, buf, out_len);
        return 0;
    }

    /* The RX characteristic is notify-only; the stack should not access it. */
    return BLE_ATT_ERR_UNLIKELY;
}

int al1_gatt_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len)
{
    if (al1_rx_val_handle == 0) {
        return BLE_HS_EINVAL; /* Service not registered yet. */
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }
    /* ble_gatts_notify_custom consumes the mbuf on both success and failure. */
    return ble_gatts_notify_custom(conn_handle, al1_rx_val_handle, om);
}

void al1_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "service %s -> handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "char %s -> def=%d val=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG, "dsc %s -> handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

int al1_gatt_svc_init(void)
{
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(al1_gatt_svcs);
    if (rc != 0) {
        return rc;
    }
    return ble_gatts_add_svcs(al1_gatt_svcs);
}
