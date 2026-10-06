#ifndef ACOUSTIC_CAPTURE_H
#define ACOUSTIC_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_adc/adc_continuous.h"
#include "esp_err.h"
#include "project_config.h"

/* Reusable DMA read buffer and anti-alias FIR state; keep in internal SRAM. */
typedef struct {
    adc_continuous_handle_t handle;
    volatile bool overflow; /* Set by the ISR when samples are lost. */
    float coefficients[ADC_DECIMATION_TAPS];
    uint16_t history[ADC_DECIMATION_TAPS];
    uint8_t dma_read[ADC_DMA_FRAME_BYTES];
} acoustic_capture_context_t;

/* Configure ADC1 continuous capture and prepare the decimation filter. */
esp_err_t acoustic_capture_init(acoustic_capture_context_t *context);
/* Write 80,000 unsigned samples and their mean; discard output on error. */
esp_err_t acoustic_capture_record(acoustic_capture_context_t *context, uint16_t *raw,
                                  size_t sample_capacity, float *dc_midpoint);
/* Release the stopped ADC driver and its DMA storage. */
esp_err_t acoustic_capture_deinit(acoustic_capture_context_t *context);

#endif
