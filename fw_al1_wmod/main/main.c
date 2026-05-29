/*
 * MIT License
 *
 * Copyright (c) 2026 EngEmil
 *
 * AttentioLight-1 Wireless Module Firmware — Phase 1 hello-world entry point.
 * Prints chip info and a heartbeat; replaced in Phase 2+ by link/BLE init.
 */

#include <stdio.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "al1_wmod";

void app_main(void)
{
    ESP_LOGI(TAG, "Hello from AttentioLight-1 Wireless Module Firmware");

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

    uint32_t tick = 0;
    while (1) {
        ESP_LOGI(TAG, "alive tick=%" PRIu32, tick++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
