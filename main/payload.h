#ifndef PAYLOAD_H
#define PAYLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "project_config.h"

/* Polynomial 0x1021, init 0xFFFF, no reflection, and no final XOR. */
uint16_t payload_crc16_ccitt_false(const uint8_t *data, size_t length);
/* Serialize version 1; insufficient capacity reports required length without writes. */
esp_err_t encode_mfcc_payload(
    const int8_t mfcc[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
    float scale, float snr_db, uint16_t node_id, uint32_t timestamp,
    bool snr_gate_passed, uint8_t *payload, size_t payload_capacity, size_t *payload_length);

#endif
