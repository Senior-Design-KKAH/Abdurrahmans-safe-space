#include <atomic>
#include <cstdio>
#include "decimator.h"
#include "edge.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static constexpr adc_channel_t audioChannel = ADC_CHANNEL_6;   // GPIO34
static constexpr adc_atten_t attenuation = ADC_ATTEN_DB_12;
static constexpr adc_bitwidth_t bitWidth = ADC_BITWIDTH_12;
static constexpr unsigned frameBytes = 512;
static constexpr unsigned biasSamples = Decimator::inputRate / 10;
static constexpr unsigned totalSamples = samplesPerWindow * windowCount;
// The classic ESP32 I2S0 mono FIFO packs each pair of 16-bit samples in reverse.
static constexpr bool swapAdcPairs = true;
static_assert(Decimator::inputRate == LOGMEL_SAMPLE_RATE * Decimator::ratio);
static_assert(sizeof(adc_digi_output_data_t) == SOC_ADC_DIGI_RESULT_BYTES);
static_assert(std::atomic<bool>::is_always_lock_free);

struct Capture {
    QueueHandle_t freeBuffers, readyBuffers;
    TaskHandle_t consumer;
    std::atomic<bool> overflow{false};
    bool ok = false;
};

static int16_t pcm[2][samplesPerWindow];

static bool IRAM_ATTR onOverflow(adc_continuous_handle_t, const adc_continuous_evt_data_t*, void* context) {
    static_cast<Capture*>(context)->overflow.store(true, std::memory_order_relaxed);
    return false;
}

// ADC ownership stays on the capture task, including the overflow interrupt.
static esp_err_t startAdc(Capture& run, adc_continuous_handle_t& adc) {
    adc_continuous_handle_cfg_t memory{};
    memory.max_store_buf_size = 8 * frameBytes;
    memory.conv_frame_size = frameBytes;
    esp_err_t err = adc_continuous_new_handle(&memory, &adc);
    if (err != ESP_OK) return err;

    adc_digi_pattern_config_t pattern{};
    pattern.atten = attenuation;
    pattern.channel = audioChannel;
    pattern.unit = ADC_UNIT_1;
    pattern.bit_width = bitWidth;
    adc_continuous_config_t config{};
    config.pattern_num = 1;
    config.adc_pattern = &pattern;
    config.sample_freq_hz = Decimator::inputRate;
    config.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    config.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;
    err = adc_continuous_config(adc, &config);
    if (err != ESP_OK) return err;

    adc_continuous_evt_cbs_t callbacks{};
    callbacks.on_pool_ovf = onOverflow;
    err = adc_continuous_register_event_callbacks(adc, &callbacks, &run);
    return err == ESP_OK ? adc_continuous_start(adc) : err;
}

static bool readAudio(Capture& run, adc_continuous_handle_t adc) {
    Decimator filter;
    alignas(4) adc_digi_output_data_t data[frameBytes / sizeof(adc_digi_output_data_t)];
    unsigned priming = 0, biasSum = 0, retained = 0, clipped = 0;
    unsigned minimum = 4095, maximum = 0;
    int bias = 0;
    int16_t* buffer = nullptr;
    bool complete = true;
    const int64_t deadline = esp_timer_get_time() + 11000000;
    int64_t first = 0, last = 0;

    while (complete && retained < totalSamples) {
        if (run.overflow.load(std::memory_order_relaxed) || esp_timer_get_time() > deadline) break;
        uint32_t bytes = 0;
        const esp_err_t err = adc_continuous_read(adc, reinterpret_cast<uint8_t*>(data),
                                                   sizeof(data), &bytes, 150);
        if (err != ESP_OK || bytes == 0 || bytes % (2 * sizeof(data[0]))) {
            std::printf("ADC read failed: %s, bytes=%lu\n", esp_err_to_name(err), (unsigned long)bytes);
            break;
        }
        for (unsigned i = 0; i < bytes / sizeof(data[0]) && retained < totalSamples; ++i) {
            const auto sampleWord = data[swapAdcPairs ? (i ^ 1u) : i];
            if (sampleWord.type1.channel != audioChannel) {
                std::puts("ADC channel mismatch");
                complete = false;
                break;
            }
            const unsigned raw = sampleWord.type1.data;
            if (priming < biasSamples) {
                biasSum += raw;
                if (++priming == biasSamples) bias = int((biasSum + biasSamples / 2) / biasSamples);
                continue;
            }
            if (raw < minimum) minimum = raw;
            if (raw > maximum) maximum = raw;
            if (raw == 0 || raw == 4095) ++clipped;
            int16_t sample;
            if (!filter.push(int16_t(int(raw) - bias), sample)) continue;
            const unsigned position = retained % samplesPerWindow;
            if (position == 0 && xQueueReceive(run.freeBuffers, &buffer, 0) != pdTRUE) {
                std::puts("PCM buffer overrun");
                complete = false;
                break;
            }
            if (retained == 0) first = esp_timer_get_time();
            buffer[position] = sample;
            ++retained;
            if (position == samplesPerWindow - 1) xQueueSend(run.readyBuffers, &buffer, 0);
            if (retained == totalSamples) last = esp_timer_get_time();
        }
    }

    const esp_err_t stopped = adc_continuous_stop(adc);
    const int64_t duration = last ? last - first : 0;
    std::printf("ADC samples=%u elapsed_us=%lld effective_hz=%.2f bias=%d raw_min=%u raw_max=%u rail=%u\n",
                retained, (long long)duration, duration ? (retained - 1) * 1e6 / duration : 0.0,
                bias, minimum, maximum, clipped);
    return stopped == ESP_OK && complete && retained == totalSamples &&
           duration >= 9800000 && duration <= 10200000;
}

static void captureTask(void* context) {
    auto& run = *static_cast<Capture*>(context);
    adc_continuous_handle_t adc = nullptr;
    const esp_err_t start = startAdc(run, adc);
    if (start == ESP_OK) {
        run.ok = readAudio(run, adc);
    } else {
        std::printf("ADC setup failed: %s\n", esp_err_to_name(start));
    }
    if (adc && adc_continuous_deinit(adc) != ESP_OK) run.ok = false;
    if (run.overflow.load(std::memory_order_relaxed)) {
        run.ok = false;
        std::puts("ADC DMA pool overflow");
    }
    int16_t* end = nullptr;
    xQueueSend(run.readyBuffers, &end, portMAX_DELAY);
    xTaskNotifyGive(run.consumer);
    vTaskDelete(nullptr);
}

bool captureAndScore(float& mean) {
    Capture run{};
    run.consumer = xTaskGetCurrentTaskHandle();
    run.freeBuffers = xQueueCreate(2, sizeof(int16_t*));
    run.readyBuffers = xQueueCreate(3, sizeof(int16_t*)); // two windows and the end marker
    bool ok = false;
    if (run.freeBuffers && run.readyBuffers) {
        for (auto& window : pcm) {
            int16_t* buffer = window;
            xQueueSend(run.freeBuffers, &buffer, 0);
        }
        if (xTaskCreatePinnedToCore(captureTask, "piezo", 4096, &run, 3, nullptr, 0) == pdPASS) {
            bool modelOk = true;
            unsigned windows = 0;
            float sum = 0.0f;
            int16_t* buffer;
            while (xQueueReceive(run.readyBuffers, &buffer, portMAX_DELAY) == pdTRUE && buffer) {
                float score;
                if (modelOk && scoreWindow(buffer, score)) {
                    sum += score;
                    std::printf("WINDOW %u score=%.9f\n", windows + 1, double(score));
                } else {
                    modelOk = false;
                }
                ++windows;
                xQueueSend(run.freeBuffers, &buffer, 0);
            }
            // The producer has returned from its final queue operation.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            ok = run.ok && modelOk && windows == windowCount;
            if (ok) mean = sum / float(windowCount);
        }
    }
    if (run.freeBuffers) vQueueDelete(run.freeBuffers);
    if (run.readyBuffers) vQueueDelete(run.readyBuffers);
    return ok;
}
