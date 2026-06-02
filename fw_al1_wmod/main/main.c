/*
 * MIT License
 *
 * Copyright (c) 2026 EngEmil
 *
 * AttentioLight-1 Wireless Module Firmware — application entry point.
 * Brings up the STM32<->ESP32 UART link and emits a periodic heartbeat.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#include "al1_link.h"
#include "al1_ble.h"

static const char *TAG = "al1_wmod";

/*
 * Loopback bring-up self-test. With the link TX and RX pins jumpered together,
 * a frame sent on the bus returns to our own parser, exercising framing + CRC +
 * UART end-to-end on real silicon. Set to 0 once wired to the STM32.
 */
#ifndef AL1_LINK_LOOPBACK_TEST
#define AL1_LINK_LOOPBACK_TEST 1
#endif

/* Self-test handshake state, set from the RX callback. */
static const uint8_t  k_selftest_payload[] = { 0xA1, 0x1B, 0x2C, 0xED, 0x55, 0x00 };
static volatile bool  s_selftest_active;
static volatile bool  s_selftest_ok;

static void on_frame(uint8_t channel, uint8_t seq,
                     const uint8_t *payload, uint16_t len, void *user)
{
    (void)user;

    if (s_selftest_active &&
        channel == AL1_CH_AP_CTRL &&
        len == sizeof(k_selftest_payload) &&
        memcmp(payload, k_selftest_payload, len) == 0) {
        s_selftest_ok = true;
    }

    switch (channel) {
    case AL1_CH_LOG:
        ESP_LOGI(TAG, "RX LOG[seq=%u]: %.*s", seq, (int)len, (const char *)payload);
        break;
    case AL1_CH_AP_CTRL:
        /* AP frames from the STM32 → out to BLE centrals via the bridge. */
        al1_ble_ap_bridge_on_link_ap(seq, payload, len);
        break;
    default:
        ESP_LOGI(TAG, "RX ch=0x%02x seq=%u len=%u", channel, seq, len);
        break;
    }
}

static void run_loopback_selftest(void)
{
    al1_link_stats_t before;
    al1_link_get_stats(&before);

    ESP_LOGI(TAG, "self-test: internal UART loopback + AP_CTRL probe...");
    esp_err_t err = al1_link_set_internal_loopback(true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "self-test: enable loopback failed (%s)", esp_err_to_name(err));
        return;
    }

    s_selftest_ok = false;
    s_selftest_active = true;

    err = al1_link_send(AL1_CH_AP_CTRL, k_selftest_payload,
                        sizeof(k_selftest_payload));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "self-test: send failed (%s)", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(200));
    s_selftest_active = false;

    al1_link_stats_t after;
    al1_link_get_stats(&after);
    if (s_selftest_ok && after.rx_frames > before.rx_frames &&
        after.rx_crc_err == before.rx_crc_err) {
        ESP_LOGI(TAG, "self-test: PASS (probe round-tripped, CRC ok)");
    } else {
        ESP_LOGW(TAG, "self-test: FAIL (ok=%d d_rx=%" PRIu32 " d_crc=%" PRIu32 ")",
                 (int)s_selftest_ok,
                 after.rx_frames - before.rx_frames,
                 after.rx_crc_err - before.rx_crc_err);
    }

    /* Back to the real line for normal operation. */
    al1_link_set_internal_loopback(false);
}

static void print_chip_banner(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGW(TAG, "esp_flash_get_size failed");
    }

    ESP_LOGI(TAG, "Chip:       %s, %d core(s), rev v%d.%d",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             chip_info.revision / 100,
             chip_info.revision % 100);
    ESP_LOGI(TAG, "Features:   %s%s%s%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BT)       ? "BT "   : "",
             (chip_info.features & CHIP_FEATURE_BLE)      ? "BLE "  : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? "802.15.4" : "");
    ESP_LOGI(TAG, "Flash:      %" PRIu32 " MB %s",
             flash_size / (uint32_t)(1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    ESP_LOGI(TAG, "Free heap:  %" PRIu32 " bytes",
             esp_get_minimum_free_heap_size());
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Hello from AttentioLight-1 Wireless Module Firmware");

    /*
     * Radio posture: WiFi stays OFF at runtime.
     * The WiFi stack is compiled in (CONFIG_ESP_WIFI_ENABLED=y) so it can be
     * enabled later without an sdkconfig change, but the radio only powers on
     * when esp_wifi_init() + esp_wifi_start() are called — which this firmware
     * deliberately never does yet. BLE is brought up separately once that
     * work lands.
     */

    print_chip_banner();

    /* Bring up the STM32<->ESP32 UART link. */
    al1_link_set_rx_cb(on_frame, NULL);
    esp_err_t err = al1_link_start(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "al1_link_start failed: %s", esp_err_to_name(err));
    } else {
#if AL1_LINK_LOOPBACK_TEST
        run_loopback_selftest();
#endif
    }

    /* Bring up BLE: NimBLE peripheral + Attentio GATT service, advertising. */
    esp_err_t ble_err = al1_ble_start();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "al1_ble_start failed: %s", esp_err_to_name(ble_err));
    } else {
        /* Bridge BLE <-> STM32 AP_CTRL once the host is up. */
        al1_ble_ap_bridge_init();
    }

    /* 1 Hz LOG heartbeat over the link, with TX + stats visibility. */
    uint32_t tick = 0;
    char msg[48];
    while (1) {
        int n = snprintf(msg, sizeof(msg), "wmod alive tick=%" PRIu32, tick);
        if (n > 0) {
            esp_err_t tx = al1_link_send(AL1_CH_LOG, (const uint8_t *)msg,
                                         (uint16_t)n);
            ESP_LOGI(TAG, "TX LOG[tick=%" PRIu32 "] %d bytes (err=%s)",
                     tick, n, esp_err_to_name(tx));
        }

        /* Every 5 s, dump link counters so the reverse direction is visible. */
        if ((tick % 5) == 0) {
            al1_link_stats_t st;
            al1_link_get_stats(&st);
            ESP_LOGI(TAG, "stats: tx=%" PRIu32 " rx=%" PRIu32
                          " crc=%" PRIu32 " resync=%" PRIu32,
                     st.tx_frames, st.rx_frames, st.rx_crc_err, st.rx_resync);
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
