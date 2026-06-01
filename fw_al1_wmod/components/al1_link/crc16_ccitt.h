/*
 * MIT License
 * Copyright (c) 2026 EngEmil
 *
 * CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0x0000).
 * Pure C, no dependencies — host-testable. Both link endpoints (ESP32 + STM32)
 * MUST use these exact parameters so the wire CRC agrees.
 */
#ifndef AL1_CRC16_CCITT_H
#define AL1_CRC16_CCITT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AL1_CRC16_INIT 0xFFFFu

/* Fold one byte into a running CRC (seed with AL1_CRC16_INIT). */
uint16_t al1_crc16_ccitt_update(uint16_t crc, uint8_t byte);

/* One-shot CRC over a buffer. */
uint16_t al1_crc16_ccitt(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* AL1_CRC16_CCITT_H */
