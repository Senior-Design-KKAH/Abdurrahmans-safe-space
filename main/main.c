#include "acoustic_capture.h"
#include "mfcc.h"
#include "payload.h"
#include "quantization.h"
#include "signal_processing.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *tag = "acoustic_node";
/* Allocate the compact recording once; reuse small static DSP workspaces. */
static uint16_t *raw_samples;
static acoustic_capture_context_t capture;
static power_context_t power;
static mfcc_context_t mfcc;
static float mfcc_15x13[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS];
static int8_t quantized[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS];

/* Final output for a future LoRa caller; zero length means no valid payload. */
uint8_t measurement_payload[PAYLOAD_MAX_BYTES];
size_t measurement_payload_length;

/* Capture, apply the optional SNR gate, and serialize one MFCC measurement. */
static esp_err_t run_measurement(uint32_t timestamp)
{
    measurement_payload_length = 0;
    esp_err_t err = mfcc_init(&mfcc);
    if (err != ESP_OK) {
        return err;
    }
    err = acoustic_capture_init(&capture);
    if (err != ESP_OK) {
        return err;
    }
    float dc_midpoint;
    err = acoustic_capture_record(&capture, raw_samples, ACOUSTIC_SAMPLE_COUNT, &dc_midpoint);
    /* Release DMA storage before DSP and preserve any capture failure. */
    esp_err_t cleanup = acoustic_capture_deinit(&capture);
    if (cleanup != ESP_OK) {
        if (err == ESP_OK) {
            err = cleanup;
        } else {
            ESP_LOGE(tag, "ADC cleanup failed: %s", esp_err_to_name(cleanup));
        }
    }
    if (err != ESP_OK) {
        return err;
    }
    float snr_db;
    err = signal_measure_snr(&power, raw_samples, ACOUSTIC_SAMPLE_COUNT, dc_midpoint, &snr_db);
    if (err != ESP_OK) {
        return err;
    }
    /* Record threshold status even when bench mode disables the gate. */
    bool gate_passed = snr_db >= SNR_THRESHOLD_DB;
    ESP_LOGI(tag, "SNR: %.2f dB", (double)snr_db);
    if (ENABLE_SNR_GATE && !gate_passed) {
        ESP_LOGI(tag, "Measurement skipped by SNR gate");
        return ESP_OK;
    }
    /* Retain 15 chronological frames before calculating the global INT8 scale. */
    err = mfcc_reduce_capture(&mfcc, raw_samples, ACOUSTIC_SAMPLE_COUNT, dc_midpoint, mfcc_15x13);
    if (err != ESP_OK) {
        return err;
    }
    float scale;
    err = quantize_mfcc(mfcc_15x13, quantized, &scale);
    if (err != ESP_OK) {
        return err;
    }
    err = encode_mfcc_payload(quantized, scale, snr_db, NODE_ID, timestamp, gate_passed,
                              measurement_payload, sizeof(measurement_payload), &measurement_payload_length);
    if (err != ESP_OK) {
        measurement_payload_length = 0;
        return err;
    }
    ESP_LOGI(tag, "Scale: %.8g, exceptions: %u, payload: %u bytes",
             (double)scale, (unsigned)measurement_payload[14], (unsigned)measurement_payload_length);
    return ESP_OK;
}

void app_main(void)
{
    /* Runtime allocation avoids ESP32's static DRAM limit and needs no PSRAM. */
    raw_samples = heap_caps_malloc(ACOUSTIC_SAMPLE_COUNT * sizeof(*raw_samples),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!raw_samples) {
        ESP_LOGE(tag, "Insufficient internal RAM for capture");
        return;
    }
    /* Supply UTC seconds here when available; zero is the prototype placeholder. */
    esp_err_t err = run_measurement(0);
    heap_caps_free(raw_samples);
    raw_samples = NULL;
    if (err != ESP_OK) {
        ESP_LOGE(tag, "Measurement failed: %s", esp_err_to_name(err));
    }
}
