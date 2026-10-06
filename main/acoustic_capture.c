#include "acoustic_capture.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include <math.h>
#include <string.h>

#if !CONFIG_IDF_TARGET_ESP32
#error "This capture implementation targets ESP32-WROOM-32"
#endif

_Static_assert(ADC_CAPTURE_RATE_HZ >= SOC_ADC_SAMPLE_FREQ_THRES_LOW &&
               ADC_CAPTURE_RATE_HZ <= SOC_ADC_SAMPLE_FREQ_THRES_HIGH, "Unsupported ADC sample rate");
_Static_assert(ADC_CAPTURE_BIT_WIDTH == 12, "DSP normalization requires 12-bit ADC");
_Static_assert(ADC_DMA_FRAME_BYTES % SOC_ADC_DIGI_DATA_BYTES_PER_CONV == 0, "Invalid DMA frame size");

static const char *tag = "acoustic_capture";

static bool IRAM_ATTR on_pool_overflow(adc_continuous_handle_t handle,
                                       const adc_continuous_evt_data_t *event, void *user_data)
{
    (void)handle;
    (void)event;
    acoustic_capture_context_t *context = user_data;
    /* Defer error handling to the task; the ISR only records sample loss. */
    context->overflow = true;
    return false;
}

esp_err_t acoustic_capture_init(acoustic_capture_context_t *context)
{
    if (!context || ADC_CAPTURE_UNIT != ADC_UNIT_1 || ADC_CAPTURE_CHANNEL > ADC_CHANNEL_7 ||
        ADC_CAPTURE_CHANNEL < ADC_CHANNEL_0 || ADC_DECIMATION_CUTOFF_HZ <= 0.0f ||
        ADC_DECIMATION_CUTOFF_HZ >= ACOUSTIC_SAMPLE_RATE_HZ / 2.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(context, 0, sizeof(*context));
    const float pi = 3.14159265358979323846f;
    float cutoff = ADC_DECIMATION_CUTOFF_HZ / ADC_CAPTURE_RATE_HZ;
    /* Blackman-windowed sinc suppresses aliasing before downsampling. */
    float coefficient_sum = 0.0f;
    for (size_t k = 0; k < ADC_DECIMATION_TAPS; ++k) {
        int distance = (int)k - ADC_DECIMATION_TAPS / 2;
        float sinc = distance == 0 ? 2.0f * cutoff :
                     sinf(2.0f * pi * cutoff * distance) / (pi * distance);
        float angle = 2.0f * pi * k / (ADC_DECIMATION_TAPS - 1);
        float window = 0.42f - 0.5f * cosf(angle) + 0.08f * cosf(2.0f * angle);
        context->coefficients[k] = sinc * window;
        coefficient_sum += context->coefficients[k];
    }
    /* Unity DC gain keeps samples in the original ADC count representation. */
    for (size_t k = 0; k < ADC_DECIMATION_TAPS; ++k) {
        context->coefficients[k] /= coefficient_sum;
    }
    adc_continuous_handle_cfg_t handle_config = {
        .max_store_buf_size = ADC_DMA_POOL_BYTES,
        .conv_frame_size = ADC_DMA_FRAME_BYTES
    };
    esp_err_t err = adc_continuous_new_handle(&handle_config, &context->handle);
    if (err != ESP_OK) {
        return err;
    }
    /* A single channel gives a continuous stream from the conditioned sensor. */
    adc_digi_pattern_config_t pattern = {
        .atten = ADC_CAPTURE_ATTENUATION,
        .channel = ADC_CAPTURE_CHANNEL,
        .unit = ADC_CAPTURE_UNIT,
        .bit_width = ADC_CAPTURE_BIT_WIDTH
    };
    adc_continuous_config_t config = {
        .pattern_num = 1,
        .adc_pattern = &pattern,
        .sample_freq_hz = ADC_CAPTURE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1
    };
    err = adc_continuous_config(context->handle, &config);
    if (err == ESP_OK) {
        adc_continuous_evt_cbs_t callbacks = {.on_pool_ovf = on_pool_overflow};
        err = adc_continuous_register_event_callbacks(context->handle, &callbacks, context);
    }
    if (err != ESP_OK) {
        esp_err_t cleanup = adc_continuous_deinit(context->handle);
        if (cleanup != ESP_OK) {
            ESP_LOGE(tag, "ADC initialization cleanup failed: %s", esp_err_to_name(cleanup));
        } else {
            context->handle = NULL;
        }
    }
    return err;
}

esp_err_t acoustic_capture_record(acoustic_capture_context_t *context, uint16_t *raw,
                                  size_t sample_capacity, float *dc_midpoint)
{
    if (!context || !context->handle || !raw || !dc_midpoint) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_capacity < ACOUSTIC_SAMPLE_COUNT) {
        return ESP_ERR_INVALID_SIZE;
    }
    *dc_midpoint = 0.0f;
    context->overflow = false;
    /* Remove queued samples from any previous measurement before starting. */
    esp_err_t err = adc_continuous_flush_pool(context->handle);
    if (err != ESP_OK) {
        return err;
    }
    err = adc_continuous_start(context->handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t input_count = 0;
    size_t output_count = 0;
    size_t history_position = 0;
    uint64_t sum = 0;
    /* Hardware sets sample timing; the task consumes DMA batches. */
    while (output_count < ACOUSTIC_SAMPLE_COUNT) {
        uint32_t bytes = 0;
        err = adc_continuous_read(context->handle, context->dma_read, sizeof(context->dma_read),
                                  &bytes, ADC_READ_TIMEOUT_MS);
        if (err != ESP_OK) {
            break;
        }
        /* Reject gaps or incomplete results rather than changing stream timing. */
        if (context->overflow || bytes == 0 || bytes % SOC_ADC_DIGI_RESULT_BYTES != 0) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        for (size_t offset = 0; offset < bytes && output_count < ACOUSTIC_SAMPLE_COUNT;
             offset += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t result = {0};
            /* Copy the hardware result without assuming buffer alignment. */
            memcpy(&result, context->dma_read + offset, SOC_ADC_DIGI_RESULT_BYTES);
            if (result.type1.channel != ADC_CAPTURE_CHANNEL) {
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            uint16_t sample = result.type1.data;
            /* Seed history with the first value to avoid a DC startup step. */
            if (input_count == 0) {
                for (size_t k = 0; k < ADC_DECIMATION_TAPS; ++k) {
                    context->history[k] = sample;
                }
            }
            context->history[history_position] = sample;
            history_position = (history_position + 1) % ADC_DECIMATION_TAPS;
            ++input_count;
            /* Evaluate the FIR only at samples retained in the 8 kHz stream. */
            if (input_count % ADC_DECIMATION_FACTOR == 0) {
                float filtered = 0.0f;
                size_t position = history_position;
                for (size_t k = 0; k < ADC_DECIMATION_TAPS; ++k) {
                    position = position == 0 ? ADC_DECIMATION_TAPS - 1 : position - 1;
                    filtered += context->coefficients[k] * context->history[position];
                }
                /* FIR overshoot can exceed the unsigned 12-bit ADC range. */
                uint16_t decimated = (uint16_t)lroundf(fmaxf(0.0f, fminf(4095.0f, filtered)));
                raw[output_count++] = decimated;
                sum += decimated;
            }
        }
        if (err != ESP_OK) {
            break;
        }
    }
    /* Stop on success or failure, preserving the first error. */
    esp_err_t stop_error = adc_continuous_stop(context->handle);
    if (context->overflow && err == ESP_OK) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (stop_error != ESP_OK) {
        if (err == ESP_OK) {
            err = stop_error;
        } else {
            ESP_LOGE(tag, "ADC stop failed: %s", esp_err_to_name(stop_error));
        }
    }
    if (err == ESP_OK && output_count == ACOUSTIC_SAMPLE_COUNT &&
        input_count == ACOUSTIC_SAMPLE_COUNT * ADC_DECIMATION_FACTOR) {
        /* Use the recording's measured mean for subsequent DC removal. */
        *dc_midpoint = (float)((double)sum / ACOUSTIC_SAMPLE_COUNT);
        return ESP_OK;
    }
    return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
}

esp_err_t acoustic_capture_deinit(acoustic_capture_context_t *context)
{
    if (!context || !context->handle) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = adc_continuous_deinit(context->handle);
    if (err == ESP_OK) {
        context->handle = NULL;
    }
    return err;
}
