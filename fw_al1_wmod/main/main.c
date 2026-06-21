/*
 * MIT License
 *
 * Copyright (c) 2026 EngEmil
 *
 * AttentioLight-1 Wireless Module Firmware — application entry point.
 * Brings up the STM32<->ESP32 UART link and the BLE peripheral, then bridges
 * AP control traffic between the two.
 */

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

static void on_frame(uint8_t channel, uint8_t seq,
                     const uint8_t *payload, uint16_t len, void *user)
{
    (void)user;
    (void)seq;

    /* AP frames from the STM32 → out to BLE centrals via the bridge. */
    if (channel == AL1_CH_AP_CTRL) {
        al1_ble_ap_bridge_on_link_ap(seq, payload, len);
    }
    /* STM32 log lines forwarded over the UART link when USB is not enumerated
     * by a host (so the STM32 can't write to CDC0). Print them here so they're
     * visible on idf.py monitor during BLE-only operation. */
    else if (channel == AL1_CH_LOG) {
        ESP_LOGI("STM32", "%.*s", (int)len, (const char *)payload);
    }
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
     * deliberately never does yet. BLE is brought up separately below.
     */

    print_chip_banner();

    /* Bring up the STM32<->ESP32 UART link. */
    al1_link_set_rx_cb(on_frame, NULL);
    esp_err_t err = al1_link_start(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "al1_link_start failed: %s", esp_err_to_name(err));
    }

    /* Bring up BLE: NimBLE peripheral + Attentio GATT service, advertising. */
    esp_err_t ble_err = al1_ble_start();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "al1_ble_start failed: %s", esp_err_to_name(ble_err));
    } else {
        /* Bridge BLE <-> STM32 AP_CTRL once the host is up. */
        al1_ble_ap_bridge_init();
    }

    /*
     * Setup is complete. The link RX/TX and NimBLE host run in their own tasks,
     * so app_main returns and its task self-deletes.
     */
}
