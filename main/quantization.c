#include "quantization.h"
#include <float.h>
#include <math.h>
#include <string.h>

esp_err_t quantize_mfcc(const float input[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
                        int8_t output[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS], float *scale)
{
    if (!input || !output || !scale) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Find the global magnitude limit across all 195 finite coefficients. */
    float maximum = 0.0f;
    for (size_t t = 0; t < MFCC_RETAINED_FRAMES; ++t) {
        for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
            if (!isfinite(input[t][c])) {
                return ESP_ERR_INVALID_ARG;
            }
            maximum = fmaxf(maximum, fabsf(input[t][c]));
        }
    }
    /* A nonzero scale avoids division by zero for an all-zero matrix. */
    if (maximum == 0.0f) {
        *scale = 1.0f;
        memset(output, 0, MFCC_RETAINED_FRAMES * MFCC_COEFFICIENTS * sizeof(int8_t));
        return ESP_OK;
    }
    /* Bound very small scales to avoid underflow on the MCU. */
    *scale = fmaxf(maximum / 127.0f, FLT_MIN);
    for (size_t t = 0; t < MFCC_RETAINED_FRAMES; ++t) {
        for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
            /* Round ties away from zero and reserve the symmetric range [-127,127]. */
            float value = roundf(input[t][c] / *scale);
            output[t][c] = (int8_t)fmaxf(-127.0f, fminf(127.0f, value));
        }
    }
    return ESP_OK;
}
