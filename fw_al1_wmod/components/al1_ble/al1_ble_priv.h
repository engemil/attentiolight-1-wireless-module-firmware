/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_ble internal seams shared between al1_ble.c (host bring-up, GAP/SM,
 * advertising) and al1_ble_gatt.c (Attentio GATT service). Not a public API.
 */
#ifndef AL1_BLE_PRIV_H
#define AL1_BLE_PRIV_H

#include <stdint.h>
#include "host/ble_uuid.h"

#include "al1_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gatt_register_ctxt;

/** @brief Attentio primary-service UUID (for the advertising payload). */
extern const ble_uuid128_t al1_ble_svc_uuid;

/** @brief Register the Attentio service with the GATT server. */
int al1_gatt_svc_init(void);

/** @brief GATT registration callback (logs handles as the table is built). */
void al1_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/** @brief Notify bytes on the RX characteristic. @see al1_ble_notify. */
int al1_gatt_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/**
 * @brief   Deliver an inbound TX-characteristic write to the registered
 *          al1_ble_rx_cb_t. Defined in al1_ble.c; called by the GATT access cb.
 */
void al1_ble_dispatch_write(uint16_t conn_handle,
                            const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* AL1_BLE_PRIV_H */
