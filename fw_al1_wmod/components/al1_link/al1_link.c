/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_link runtime: ESP-IDF UART driver bring-up + RX/TX FreeRTOS tasks,
 * layered on the pure builder/parser in al1_frame.c.
 */
#include "al1_link.h"
#include "crc16_ccitt.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "al1_link";

/* UART driver buffers / queues. */
#define AL1_UART_RX_BUF     4096
#define AL1_UART_TX_BUF     4096
#define AL1_UART_EVT_QUEUE  0       /* No event queue; we block on reads.   */
#define AL1_RX_CHUNK        256     /* Bytes read per uart_read_bytes call.  */

/* Outbound queue depth (frames awaiting the wire). */
#define AL1_TX_QUEUE_LEN    32

/* One queued outbound frame (heap-owned buffer, freed by the TX task). */
typedef struct {
    uint8_t *buf;
    uint16_t len;
} al1_tx_item_t;

static struct {
    bool             running;
    al1_link_cfg_t   cfg;
    al1_parser_t    *parser;        /* Heap (~4 KB).                        */
    QueueHandle_t    tx_queue;
    TaskHandle_t     rx_task;
    TaskHandle_t     tx_task;
    al1_link_stats_t stats;
    uint8_t          tx_seq[256];   /* Per-channel TX sequence counters.    */
    al1_frame_cb_t   user_cb;
    void            *user_arg;
} s_link;

/* Trampoline so the parser callback reaches the user-registered callback. */
static void parser_trampoline(uint8_t ch, uint8_t seq,
                              const uint8_t *payload, uint16_t len, void *user)
{
    (void)user;
    if (s_link.user_cb) {
        s_link.user_cb(ch, seq, payload, len, s_link.user_arg);
    }
}

static void rx_task(void *arg)
{
    (void)arg;
    uint8_t *chunk = malloc(AL1_RX_CHUNK);
    if (chunk == NULL) {
        ESP_LOGE(TAG, "rx_task: out of memory");
        vTaskDelete(NULL);
        return;
    }

    while (s_link.running) {
        const int n = uart_read_bytes(s_link.cfg.uart_num, chunk,
                                      AL1_RX_CHUNK, pdMS_TO_TICKS(100));
        if (n > 0) {
            al1_parser_feed(s_link.parser, chunk, (size_t)n);
        }
    }

    free(chunk);
    vTaskDelete(NULL);
}

static void tx_task(void *arg)
{
    (void)arg;
    al1_tx_item_t item;

    while (s_link.running) {
        if (xQueueReceive(s_link.tx_queue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
            uart_write_bytes(s_link.cfg.uart_num, item.buf, item.len);
            free(item.buf);
            s_link.stats.tx_frames++;
        }
    }

    /* Drain any remaining items so their buffers are not leaked. */
    while (xQueueReceive(s_link.tx_queue, &item, 0) == pdTRUE) {
        free(item.buf);
    }
    vTaskDelete(NULL);
}

void al1_link_set_rx_cb(al1_frame_cb_t cb, void *user)
{
    s_link.user_cb = cb;
    s_link.user_arg = user;
}

esp_err_t al1_link_send(uint8_t channel, const uint8_t *payload, uint16_t len)
{
    if (!s_link.running) {
        return ESP_ERR_INVALID_STATE;
    }
    if (len > AL1_LINK_MAX_PAYLOAD) {
        s_link.stats.tx_drops++;
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t frame_len = (size_t)AL1_LINK_OVERHEAD + len;
    uint8_t *buf = malloc(frame_len);
    if (buf == NULL) {
        s_link.stats.tx_drops++;
        return ESP_ERR_NO_MEM;
    }

    const uint8_t seq = s_link.tx_seq[channel]++;
    const size_t n = al1_frame_build(buf, frame_len, channel, seq, payload, len);
    if (n == 0) {
        free(buf);
        s_link.stats.tx_drops++;
        return ESP_FAIL;
    }

    al1_tx_item_t item = { .buf = buf, .len = (uint16_t)n };
    if (xQueueSend(s_link.tx_queue, &item, 0) != pdTRUE) {
        free(buf);
        s_link.stats.tx_drops++;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void al1_link_get_stats(al1_link_stats_t *out)
{
    if (out) {
        *out = s_link.stats;
    }
}

esp_err_t al1_link_set_internal_loopback(bool enable)
{
    if (!s_link.running) {
        return ESP_ERR_INVALID_STATE;
    }
    return uart_set_loop_back(s_link.cfg.uart_num, enable);
}

esp_err_t al1_link_start(const al1_link_cfg_t *cfg)
{
    if (s_link.running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_link.cfg = (cfg != NULL) ? *cfg : AL1_LINK_DEFAULT_CFG();
    memset(&s_link.stats, 0, sizeof(s_link.stats));
    memset(s_link.tx_seq, 0, sizeof(s_link.tx_seq));

    s_link.parser = malloc(sizeof(al1_parser_t));
    if (s_link.parser == NULL) {
        return ESP_ERR_NO_MEM;
    }
    al1_parser_init(s_link.parser, parser_trampoline, NULL, &s_link.stats);

    s_link.tx_queue = xQueueCreate(AL1_TX_QUEUE_LEN, sizeof(al1_tx_item_t));
    if (s_link.tx_queue == NULL) {
        free(s_link.parser);
        s_link.parser = NULL;
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uart_cfg = {
        .baud_rate = s_link.cfg.baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err;
    err = uart_driver_install(s_link.cfg.uart_num, AL1_UART_RX_BUF,
                              AL1_UART_TX_BUF, AL1_UART_EVT_QUEUE, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
        goto fail;
    }
    err = uart_param_config(s_link.cfg.uart_num, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config: %s", esp_err_to_name(err));
        goto fail_driver;
    }
    err = uart_set_pin(s_link.cfg.uart_num, s_link.cfg.tx_gpio,
                       s_link.cfg.rx_gpio, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(err));
        goto fail_driver;
    }

    s_link.running = true;

    if (xTaskCreate(rx_task, "al1_link_rx", 4096, NULL, 10, &s_link.rx_task)
            != pdPASS) {
        ESP_LOGE(TAG, "failed to create rx_task");
        s_link.running = false;
        err = ESP_ERR_NO_MEM;
        goto fail_driver;
    }
    if (xTaskCreate(tx_task, "al1_link_tx", 4096, NULL, 9, &s_link.tx_task)
            != pdPASS) {
        ESP_LOGE(TAG, "failed to create tx_task");
        s_link.running = false;
        /* rx_task will self-exit on the running=false flag. */
        err = ESP_ERR_NO_MEM;
        goto fail_driver;
    }

    ESP_LOGI(TAG, "started: uart%d tx=%d rx=%d @ %d baud",
             s_link.cfg.uart_num, s_link.cfg.tx_gpio, s_link.cfg.rx_gpio,
             s_link.cfg.baud);
    return ESP_OK;

fail_driver:
    uart_driver_delete(s_link.cfg.uart_num);
fail:
    vQueueDelete(s_link.tx_queue);
    s_link.tx_queue = NULL;
    free(s_link.parser);
    s_link.parser = NULL;
    return err;
}

void al1_link_stop(void)
{
    if (!s_link.running) {
        return;
    }
    s_link.running = false;
    /* Tasks observe the flag within their read/receive timeout and self-delete. */
    vTaskDelay(pdMS_TO_TICKS(200));

    uart_driver_delete(s_link.cfg.uart_num);
    if (s_link.tx_queue) {
        vQueueDelete(s_link.tx_queue);
        s_link.tx_queue = NULL;
    }
    free(s_link.parser);
    s_link.parser = NULL;
    s_link.rx_task = NULL;
    s_link.tx_task = NULL;
}
