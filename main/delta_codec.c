#include "delta_codec.h"
#include <stddef.h>
#include <string.h>

esp_err_t delta_encode_mfcc(const int8_t input[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
                            mfcc_delta_encoding_t *encoding)
{
    if (!input || !encoding) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(encoding, 0, sizeof(*encoding));
    /* The first row is the starting point for reconstructing each column. */
    memcpy(encoding->first_frame, input[0], sizeof(encoding->first_frame));
    for (size_t t = 1; t < MFCC_RETAINED_FRAMES; ++t) {
        for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
            size_t index = (t - 1) * MFCC_COEFFICIENTS + c;
            /* Widen before subtraction; two INT8 values can differ by 255. */
            int16_t delta = (int16_t)input[t][c] - (int16_t)input[t - 1][c];
            uint8_t nibble = 0;
            if (delta < -8 || delta > 7) {
                /* Keep a zero placeholder and append exceptions in index order. */
                mfcc_delta_exception_t *exception = &encoding->exceptions[encoding->exception_count++];
                exception->delta_index = (uint8_t)index;
                exception->coefficient_value = input[t][c];
            } else {
                /* The low four bits preserve signed two's-complement deltas. */
                nibble = (uint8_t)delta & 0x0fu;
            }
            /* Even delta indices occupy the high nibble; odd indices the low. */
            encoding->packed[index / 2] |= (uint8_t)(nibble << (index % 2 == 0 ? 4 : 0));
        }
    }
    return ESP_OK;
}
