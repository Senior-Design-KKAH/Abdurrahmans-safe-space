#ifndef QUANTIZATION_H
#define QUANTIZATION_H

#include <stdint.h>
#include "esp_err.h"
#include "project_config.h"

/* Use one symmetric scale for all rows; zero input uses scale 1. */
esp_err_t quantize_mfcc(const float input[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS],
                        int8_t output[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS], float *scale);

#endif
