#ifndef DELTA_CODEC_H
#define DELTA_CODEC_H

#include <stdint.h>
#include "esp_err.h"
#include "project_config.h"

/* An exception stores the absolute coefficient, not the oversized delta. */
typedef struct {
    uint8_t delta_index;
    int8_t coefficient_value;
} mfcc_delta_exception_t;

/* Intermediate encoding only; payload serialization writes each field explicitly. */
typedef struct {
    int8_t first_frame[MFCC_COEFFICIENTS];
    mfcc_delta_exception_t exceptions[MFCC_DELTA_COUNT];
    uint8_t exception_count;
    uint8_t packed[MFCC_PACKED_DELTA_BYTES];
} mfcc_delta_encoding_t;

/* Encode temporal differences independently down each coefficient column. */
esp_err_t delta_encode_mfcc(const int8_t input[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
                            mfcc_delta_encoding_t *encoding);

#endif
