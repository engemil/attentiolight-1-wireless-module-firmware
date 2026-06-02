/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_ble — NimBLE peripheral bring-up + the custom Attentio BLE Service for
 * the AttentioLight-1 Wireless Module (ESP32-C3).
 *
 * The module advertises a single primary service with two characteristics that
 * form a transparent byte pipe for Attentio Protocol (AP) frames, named from
 * the BLE central's (phone / host) point of view, mirroring the Nordic-UART
 * convention:
 *
 *   Service:      1209EEA1-0001-0000-0000-000000000000
 *   TX (write):   1209EEA1-0002-0000-0000-000000000000   host  -> device
 *   RX (notify):  1209EEA1-0003-0000-0000-000000000000   device -> host
 *
 * The central WRITEs AP frames to TX and SUBSCRIBEs to RX for AP responses and
 * events. This header exposes only the bring-up + byte-pipe seams; the AP
 * reassembly / MICB session bridge is layered on top separately.
 */
#ifndef AL1_BLE_H
#define AL1_BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Advertised / default device name. */
#ifndef AL1_BLE_DEVICE_NAME
#define AL1_BLE_DEVICE_NAME     "AttentioLight-1"
#endif

/** @brief Max simultaneous BLE connections tracked (mirrors NimBLE's limit). */
#ifdef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define AL1_BLE_MAX_CONN        CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#else
#define AL1_BLE_MAX_CONN        3
#endif

/**
 * @brief   Inbound-write callback.
 *
 * Invoked from the NimBLE host task whenever a connected central writes bytes
 * to the TX characteristic. The AP bridge registers here to reassemble the
 * bytes into AP frames and forward them to the STM32. Runs in host-task
 * context — keep it short and non-blocking (copy out, return).
 *
 * @param conn_handle  Originating BLE connection handle.
 * @param data         Written bytes (valid only for the duration of the call).
 * @param len          Number of bytes written.
 * @param user         Opaque pointer supplied to al1_ble_set_rx_cb().
 */
typedef void (*al1_ble_rx_cb_t)(uint16_t conn_handle,
                                const uint8_t *data, uint16_t len, void *user);

/**
 * @brief   Connection state-change callback.
 *
 * Invoked from the host task when a central connects or disconnects. The AP
 * bridge uses it to allocate / release per-connection reassembly state. Runs in
 * host-task context — keep it short.
 *
 * @param conn_handle  BLE connection handle.
 * @param connected    true on connect, false on disconnect.
 * @param user         Opaque pointer supplied to al1_ble_set_conn_evt_cb().
 */
typedef void (*al1_ble_conn_evt_cb_t)(uint16_t conn_handle, bool connected,
                                      void *user);

/**
 * @brief   Initialise NVS, bring up the NimBLE host (Just-Works pairing, LE
 *          Secure Connections, bonding persisted in NVS), register the Attentio
 *          GATT service, and start advertising.
 * @return  ESP_OK on success.
 */
esp_err_t al1_ble_start(void);

/** @brief Register the inbound-write callback (NULL to clear). */
void al1_ble_set_rx_cb(al1_ble_rx_cb_t cb, void *user);

/** @brief Register the connection state-change callback (NULL to clear). */
void al1_ble_set_conn_evt_cb(al1_ble_conn_evt_cb_t cb, void *user);

/**
 * @brief   Notify @p data to one connected central on the RX characteristic,
 *          fragmenting to that link's negotiated ATT MTU (minus the 3-byte
 *          notify header).
 *
 * @return  0 on success, the last NimBLE error code on any fragment failure.
 */
int al1_ble_notify_conn(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/**
 * @brief   Notify @p data to every connected central (see al1_ble_notify_conn).
 *
 * Centrals that are not subscribed simply fail their notify harmlessly. Used
 * for broadcast events with no specific session target.
 *
 * @return  Number of connections the data was dispatched to.
 */
int al1_ble_notify_all(const uint8_t *data, uint16_t len);

/**
 * @brief   Send AP bytes to a connected central as a notification on the RX
 *          characteristic.
 * @param conn_handle  Target BLE connection handle.
 * @param data         Bytes to notify.
 * @param len          Number of bytes (caller fragments to the negotiated MTU).
 * @return 0 on success, a NimBLE error code otherwise.
 */
int al1_ble_notify(uint16_t conn_handle, const uint8_t *data, uint16_t len);

/*===========================================================================*/
/* AP bridge                                                                 */
/*===========================================================================*/

/**
 * @brief   Start the AP bridge: register inbound-write + connection callbacks
 *          so bytes written by a central are reassembled into whole AP frames
 *          and forwarded to the STM32 over the link's AP_CTRL channel.
 *
 * Call once, after al1_ble_start().
 */
void al1_ble_ap_bridge_init(void);

/**
 * @brief   Feed an AP_CTRL frame received from the STM32 into the bridge for
 *          delivery to BLE centrals (currently broadcast to all connected;
 *          per-session routing comes later).
 *
 * Call from the link RX callback for AL1_CH_AP_CTRL frames.
 *
 * @param seq      Link sequence byte (diagnostic).
 * @param payload  AP frame bytes from the STM32.
 * @param len      Length of @p payload.
 */
void al1_ble_ap_bridge_on_link_ap(uint8_t seq,
                                  const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* AL1_BLE_H */
