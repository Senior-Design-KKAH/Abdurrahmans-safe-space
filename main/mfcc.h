#ifndef MFCC_H
#define MFCC_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "project_config.h"

/* One frame of history plus reusable FFT, Mel, and DCT workspaces. */
typedef struct {
    float window[MFCC_FRAME_LENGTH];
    float ring[MFCC_FRAME_LENGTH];
    float real[MFCC_FFT_SIZE];
    float imaginary[MFCC_FFT_SIZE];
    float twiddle_real[MFCC_FFT_SIZE / 2];
    float twiddle_imaginary[MFCC_FFT_SIZE / 2];
    float spectrum[MFCC_FFT_SIZE / 2 + 1];
    float mel_edges[MFCC_MEL_FILTERS + 2];
    float dct[MFCC_COEFFICIENTS][MFCC_MEL_FILTERS];
} mfcc_context_t;

/* Precompute the Hamming window, FFT twiddles, Mel edges, and DCT weights. */
esp_err_t mfcc_init(mfcc_context_t *context);
/* Extract coefficients 0 through 12 from one chronological float frame. */
esp_err_t mfcc_compute_frame(mfcc_context_t *context, const float *frame,
                             float coefficients[MFCC_COEFFICIENTS]);
/* Return the group's middle frame, choosing the earlier frame for ties. */
size_t mfcc_representative_index(size_t group);
/* Replay filtering and retain 15 chronological rows without a full MFCC matrix. */
esp_err_t mfcc_reduce_capture(mfcc_context_t *context, const uint16_t *raw,
                              size_t count, float dc_midpoint,
                              float output[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS]);

#endif
