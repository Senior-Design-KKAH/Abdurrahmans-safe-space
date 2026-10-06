#ifndef SIGNAL_PROCESSING_H
#define SIGNAL_PROCESSING_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "project_config.h"

/* Two delay states per biquad, reset for each pass over a capture. */
typedef struct {
    float z1[2];
    float z2[2];
} bandpass_context_t;

/* Running power window and statistics for complete 100 ms envelopes. */
typedef struct {
    float power[POWER_WINDOW_SAMPLES];
    double sum;
    double envelope_sum;
    float minimum;
    size_t position;
    size_t samples;
    size_t envelopes;
} power_context_t;

/* Reset state before processing the first sample of a recording. */
void bandpass_reset(bandpass_context_t *context);
/* Filter one signed, normalized sample through both biquads. */
float bandpass_process(bandpass_context_t *context, float sample);
/* Initialize the moving average and its accumulated statistics. */
void power_reset(power_context_t *context);
/* Square one filtered sample and update the running power window. */
void power_process(power_context_t *context, float filtered_sample);
/* Estimate SNR as mean envelope power relative to its minimum window. */
esp_err_t power_snr_db(const power_context_t *context, float *snr_db);
/* Center and filter raw ADC counts without storing a float recording. */
esp_err_t signal_measure_snr(power_context_t *context, const uint16_t *raw,
                             size_t count, float dc_midpoint, float *snr_db);

#endif
