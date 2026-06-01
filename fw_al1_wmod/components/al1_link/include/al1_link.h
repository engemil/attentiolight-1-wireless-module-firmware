/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * al1_link runtime — ESP-IDF UART + FreeRTOS transport between the
 * AttentioLight-1 MainBoard (STM32C071, USART1 on PB6/PB7) and the Wireless
 * Module (ESP32-C3). The wire format and the pure builder/parser live in
 * al1_frame.h; this header adds the device-side runtime API.
 */
#ifndef AL1_LINK_H
#define AL1_LINK_H

#include "al1_frame.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UART/runtime configuration. */
typedef struct {
    int uart_num;       /**< ESP-IDF UART port number.                     */
    int tx_gpio;        /**< TX GPIO (ESP32 → STM32 RX / PB7).             */
    int rx_gpio;        /**< RX GPIO (ESP32 ← STM32 TX / PB6).             */
    int baud;           /**< Baud rate.                                    */
} al1_link_cfg_t;

/*
 * Pin mapping (al1mb1 PCB, ESP32-C3-WROOM-02).
 *
 * The link uses the ESP32-C3 UART0 pads, which are fixed on the C3:
 *   GPIO21 = U0TXD, GPIO20 = U0RXD.
 * Wired to the STM32 USART1 (WBM_TX = PB6, WBM_RX = PB7, AF0):
 *   ESP32 TX GPIO21 -> STM32 PB7 (its RX)
 *   ESP32 RX GPIO20 <- STM32 PB6 (its TX)
 *
 * Because these are the default UART0 console pins, the ROM/IDF console is
 * routed to the built-in USB-Serial-JTAG instead (see sdkconfig.defaults:
 * CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y) so the console does not drive these
 * pads. We still use UART0 as the link peripheral here (no other UART is
 * wired to the STM32).
 */
#ifndef WMOD_UART_NUM
#define WMOD_UART_NUM       0
#endif
#ifndef WMOD_UART_TX_GPIO
#define WMOD_UART_TX_GPIO   21      /* U0TXD -> STM32 PB7 (RX). */
#endif
#ifndef WMOD_UART_RX_GPIO
#define WMOD_UART_RX_GPIO   20      /* U0RXD <- STM32 PB6 (TX). */
#endif
#ifndef WMOD_UART_BAUD
#define WMOD_UART_BAUD      921600
#endif

#define AL1_LINK_DEFAULT_CFG()                 \
    (al1_link_cfg_t){                          \
        .uart_num = WMOD_UART_NUM,             \
        .tx_gpio  = WMOD_UART_TX_GPIO,         \
        .rx_gpio  = WMOD_UART_RX_GPIO,         \
        .baud     = WMOD_UART_BAUD,            \
    }

/**
 * @brief   Install the UART driver and start the RX/TX tasks.
 * @param cfg   Link configuration (NULL → AL1_LINK_DEFAULT_CFG()).
 * @return ESP_OK on success.
 */
esp_err_t al1_link_start(const al1_link_cfg_t *cfg);

/** @brief Stop the link and free resources. */
void al1_link_stop(void);

/**
 * @brief   Register the RX frame callback.
 * @note    Runs in the RX task context; keep it short and non-blocking.
 */
void al1_link_set_rx_cb(al1_frame_cb_t cb, void *user);

/**
 * @brief   Frame and enqueue a payload for transmission.
 * @param channel   Channel to send on.
 * @param payload   Payload bytes (may be NULL iff @p len == 0).
 * @param len       Payload length (0..AL1_LINK_MAX_PAYLOAD).
 * @return ESP_OK if queued; ESP_ERR_* otherwise.
 */
esp_err_t al1_link_send(uint8_t channel, const uint8_t *payload, uint16_t len);

/** @brief Copy current link statistics into @p out. */
void al1_link_get_stats(al1_link_stats_t *out);

/**
 * @brief   Enable/disable the UART's internal loopback (TX routed to RX inside
 *          the peripheral). Used by the boot self-test on an assembled board
 *          where the external line is hard-wired to the STM32 and no jumper is
 *          possible. Must be disabled again before real traffic.
 * @return ESP_OK on success.
 */
esp_err_t al1_link_set_internal_loopback(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* AL1_LINK_H */
