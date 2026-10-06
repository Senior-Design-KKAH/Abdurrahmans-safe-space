#include "payload.h"
#include "delta_codec.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Packet requires IEEE-754 float32");

/* Explicit byte writes make integer fields independent of host endianness. */
static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void put_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

uint16_t payload_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    /* CCITT-FALSE processes each byte most-significant bit first. */
    uint16_t crc = 0xffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (unsigned bit = 0; bit < 8; ++bit) {
            uint32_t shifted = (uint32_t)crc << 1;
            crc = (uint16_t)((crc & 0x8000u) ? shifted ^ 0x1021u : shifted);
        }
    }
    return crc;
}

esp_err_t encode_mfcc_payload(
    const int8_t mfcc[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
    float scale, float snr_db, uint16_t node_id, uint32_t timestamp,
    bool snr_gate_passed, uint8_t *payload, size_t payload_capacity, size_t *payload_length)
{
    if (!payload_length) {
        return ESP_ERR_INVALID_ARG;
    }
    *payload_length = 0;
    if (!mfcc || !payload || !isfinite(scale) || scale <= 0.0f || !isfinite(snr_db)) {
        return ESP_ERR_INVALID_ARG;
    }
    mfcc_delta_encoding_t encoding;
    esp_err_t err = delta_encode_mfcc(mfcc, &encoding);
    if (err != ESP_OK) {
        return err;
    }
    /* Check the exact packet size before touching the caller's buffer. */
    size_t required = PAYLOAD_HEADER_BYTES + 2u * encoding.exception_count +
                      sizeof(encoding.packed) + PAYLOAD_CRC_BYTES;
    *payload_length = required;
    if (payload_capacity < required) {
        return ESP_ERR_INVALID_SIZE;
    }
    /* Fixed metadata occupies bytes 0 through 14. */
    payload[0] = PAYLOAD_VERSION;
    payload[1] = (uint8_t)((snr_gate_passed ? PAYLOAD_FLAG_SNR_PASSED : 0u) |
                          (encoding.exception_count ? PAYLOAD_FLAG_EXCEPTIONS : 0u));
    put_u16(payload + 2, node_id);
    put_u32(payload + 4, timestamp);
    /* Saturate hundredths of a dB before converting to signed 16-bit storage. */
    double rounded = round((double)snr_db * 100.0);
    int16_t snr_q = (int16_t)fmax((double)INT16_MIN, fmin((double)INT16_MAX, rounded));
    put_u16(payload + 8, (uint16_t)snr_q);
    /* Preserve IEEE-754 bits without aliasing, then send them in network order. */
    uint32_t scale_bits;
    memcpy(&scale_bits, &scale, sizeof(scale_bits));
    put_u32(payload + 10, scale_bits);
    payload[14] = encoding.exception_count;
    /* Append the starting row, exception pairs, and fixed 91-byte delta body. */
    size_t offset = 15;
    for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
        payload[offset++] = (uint8_t)encoding.first_frame[c];
    }
    for (size_t i = 0; i < encoding.exception_count; ++i) {
        payload[offset++] = encoding.exceptions[i].delta_index;
        payload[offset++] = (uint8_t)encoding.exceptions[i].coefficient_value;
    }
    memcpy(payload + offset, encoding.packed, sizeof(encoding.packed));
    offset += sizeof(encoding.packed);
    /* The CRC covers every application byte except the CRC field itself. */
    uint16_t crc = payload_crc16_ccitt_false(payload, offset);
    put_u16(payload + offset, crc);
    offset += PAYLOAD_CRC_BYTES;
    return offset == required ? ESP_OK : ESP_ERR_INVALID_STATE;
}
