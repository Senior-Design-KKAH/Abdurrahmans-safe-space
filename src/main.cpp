#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <SPI.h>
#include <LoRa.h>
#include <driver/adc.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "pipeline.h"

static int16_t *pcm[4] = {};
static Workspace workspace;
static double features[195];
static int8_t quantized[195];
static uint8_t payload[RADIO_CAPACITY];
static volatile bool tx_done = false;

// Record the radio interrupt without doing work inside the callback.
static void IRAM_ATTR on_tx_done() { tx_done = true; }

// Resolve a configured GPIO to ADC1; reject pins on other ADC units.
static int adc_channel(int pin) {
    for (int c = 0; c < ADC1_CHANNEL_MAX; ++c) {
        gpio_num_t gpio;
        if (adc1_pad_get_io_num(adc1_channel_t(c), &gpio) == ESP_OK && int(gpio) == pin) return c;
    }
    return -1;
}

// Require caller-supplied header codes before capturing real sensor data.
static bool codes_configured() {
    return NODE_ID <= 255u && DATE_CODE <= 31u && BATTERY_CODE <= 7u;
}

// Accept exact driver bandwidth choices so settings are not silently rounded.
static bool bandwidth_configured() {
    const long supported[] = {7800,10400,15600,20800,31250,41700,62500,125000,250000,500000};
    for (long value : supported) if (LORA_BANDWIDTH_HZ == value) return true;
    return false;
}

// Retain exactly 80,000 DMA samples and reject capture loss or timing faults.
static bool capture() {
    int channel = adc_channel(ADC_GPIO);
    if (channel < 0) { Serial.println("ERROR: configure an ADC1 GPIO"); return false; }
    i2s_config_t config = {};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN);
    config.sample_rate = 8000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.use_apll = true;
    QueueHandle_t events = nullptr;
    if (i2s_driver_install(I2S_NUM_0, &config, 32, &events) != ESP_OK) {
        Serial.println("ERROR: I2S ADC DMA install"); return false;
    }
    bool good = i2s_set_adc_mode(ADC_UNIT_1, adc1_channel_t(channel)) == ESP_OK;
    good = good && adc1_config_width(ADC_WIDTH_BIT_12) == ESP_OK;
    good = good && adc1_config_channel_atten(adc1_channel_t(channel), ADC_ATTEN_DB_12) == ESP_OK;
    good = good && i2s_adc_enable(I2S_NUM_0) == ESP_OK;
    const bool adc_enabled = good;
    uint16_t words[256];
    size_t got = 0;
    // Discard startup conversions before measuring the retained 80,000 samples.
    for (int i = 0; good && i < 2; ++i)
        good = i2s_read(I2S_NUM_0, words, sizeof(words), &got, pdMS_TO_TICKS(1000)) == ESP_OK && got == sizeof(words);
    i2s_event_t event;
    while (xQueueReceive(events, &event, 0))
        if (event.type == I2S_EVENT_RX_Q_OVF || event.type == I2S_EVENT_DMA_ERROR) good = false;
    uint32_t start = micros(), count = 0, clipped = 0;
    uint16_t minimum = 4095, maximum = 0;
    while (good && count < SAMPLE_COUNT) {
        size_t wanted = std::min(size_t(256), size_t(SAMPLE_COUNT-count));
        good = i2s_read(I2S_NUM_0, words, wanted*2, &got, pdMS_TO_TICKS(1000)) == ESP_OK && got == wanted*2;
        if (!good) break;
        for (size_t i = 0; i < wanted; ++i) {
            // Restore mono sample order before checking the embedded ADC channel tag.
            uint16_t word = words[ADC_SWAP_ADJACENT ? (i^1u) : i];
            if ((word >> 12) != unsigned(channel)) { good = false; break; }
            uint16_t raw = word & 0x0fff;
            if (raw == 0 || raw == 4095) ++clipped;
            if (raw < minimum) minimum = raw;
            if (raw > maximum) maximum = raw;
            pcm[count/PCM_BLOCK][count%PCM_BLOCK] = int16_t(raw);
            ++count;
        }
        while (xQueueReceive(events, &event, 0))
            if (event.type == I2S_EVENT_RX_Q_OVF || event.type == I2S_EVENT_DMA_ERROR) good = false;
    }
    uint32_t elapsed = micros()-start;
    // Release DMA resources before the later DSP stage uses its working memory.
    if (adc_enabled) i2s_adc_disable(I2S_NUM_0);
    i2s_stop(I2S_NUM_0);
    while (xQueueReceive(events, &event, 0))
        if (event.type == I2S_EVENT_RX_Q_OVF || event.type == I2S_EVENT_DMA_ERROR) good = false;
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.printf("ADC samples=%u duration_us=%u effective_hz=%.3f raw_min=%u raw_max=%u rail_samples=%u\n",
                  count, elapsed, elapsed ? count*1e6/elapsed : 0, minimum, maximum, clipped);
    Serial.println("ADC1 12-bit, 12dB attenuation, raw-code normalization; no voltage calibration");
    // Wall-clock check detects gross clock/rate failures; it is not an oscillator calibration.
    if (elapsed < 9800000 || elapsed > 10200000) good = false;
    if (!good || count != SAMPLE_COUNT) Serial.println("ERROR: invalid capture, timeout, channel mismatch, DMA loss, or sample timing");
    return good && count == SAMPLE_COUNT;
}

// Send only with explicit RF configuration and wait for local TX completion.
static bool send_payload(size_t length) {
    if (!RADIO_ENABLED) { Serial.println("RF disabled: serial payload only"); return true; }
    if (BENCH_MODE && !ALLOW_SYNTHETIC_RF) { Serial.println("Synthetic RF disabled"); return true; }
    if (LORA_SCK < 0 || LORA_MISO < 0 || LORA_MOSI < 0 || LORA_CS < 0 || LORA_RESET < 0 || LORA_DIO0 < 0 ||
        LORA_FREQUENCY_HZ < 137000000 || LORA_FREQUENCY_HZ > 1020000000 || !bandwidth_configured() || LORA_SPREADING_FACTOR < 7 || LORA_SPREADING_FACTOR > 12 ||
        LORA_CODING_RATE_DENOMINATOR < 5 || LORA_CODING_RATE_DENOMINATOR > 8 || LORA_TX_DBM < 2 || LORA_TX_DBM > 20 ||
        LORA_SYNC_WORD < 0 || LORA_SYNC_WORD > 255 || LORA_PREAMBLE_SYMBOLS < 6) {
        Serial.println("ERROR: supply complete, hardware-appropriate RF configuration"); return false;
    }
    // LoRa 0.8.0 truncates writes above 255 bytes, so reject before touching its FIFO.
    if (length > RADIO_CAPACITY) { Serial.println("ERROR: LoRa application payload overflow"); return false; }
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RESET, LORA_DIO0);
    if (!LoRa.begin(LORA_FREQUENCY_HZ)) { Serial.println("ERROR: SX1276 not detected"); return false; }
    LoRa.setSignalBandwidth(LORA_BANDWIDTH_HZ);
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setCodingRate4(LORA_CODING_RATE_DENOMINATOR);
    LoRa.setTxPower(LORA_TX_DBM);
    LoRa.setSyncWord(LORA_SYNC_WORD);
    LoRa.setPreambleLength(LORA_PREAMBLE_SYMBOLS);
    LoRa.enableCrc();
    tx_done = false;
    LoRa.onTxDone(on_tx_done);
    if (!LoRa.beginPacket() || LoRa.write(payload, length) != length || !LoRa.endPacket(true)) {
        LoRa.onTxDone(nullptr);
        Serial.println("ERROR: LoRa packet start/write"); return false;
    }
    uint32_t start = millis();
    while (!tx_done) {
        if (millis()-start > TX_TIMEOUT_MS) {
            LoRa.idle(); LoRa.onTxDone(nullptr);
            Serial.println("ERROR: RF TX timeout"); return false;
        }
        delay(1);
    }
    LoRa.onTxDone(nullptr);
    LoRa.sleep();
    Serial.println("LoRa TX_DONE (receiver delivery is not confirmed)");
    return true;
}

// Let the Arduino task yield between DSP blocks to service the watchdog.
static void cooperate() { delay(1); }

// Run one capture-to-payload cycle and release its PCM buffers afterward.
static void single_capture() {
    if (!BENCH_MODE && !codes_configured()) { Serial.println("ERROR: configure node/date-code/battery-code"); return; }
    // Smaller chunks avoid requiring one contiguous 160 KB heap allocation.
    for (int b = 0; b < 4; ++b) {
        pcm[b] = static_cast<int16_t *>(heap_caps_malloc(PCM_BLOCK*sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!pcm[b]) {
            Serial.println("ERROR: not enough SRAM for capture");
            for (int j = 0; j < b; ++j) { free(pcm[j]); pcm[j] = nullptr; }
            return;
        }
    }
    bool valid = true;
    if (BENCH_MODE) {
        Serial.println("SYNTHETIC BENCH: 500Hz, 0.2 full-scale; example node=42 date-code=11 battery-code=7");
        for (int i = 0; i < SAMPLE_COUNT; ++i)
            pcm[i/PCM_BLOCK][i%PCM_BLOCK] = int16_t(std::lrint(6553.0*std::sin(2.0*PI*500*i/8000)));
    } else valid = capture();
    if (valid) {
        float scale;
        uint32_t start = micros();
        int status = compute_mfcc(const_cast<const int16_t *const *>(pcm), BENCH_MODE ? 32768.0 : 2048.0,
                                  &workspace, features, quantized, &scale, nullptr, cooperate);
        Serial.printf("DSP status=%d time_us=%u scale=%.9g\n", status, micros()-start, double(scale));
        if (status == PIPELINE_OK) {
            size_t length = 0;
            uint8_t schedule;
            start = micros();
            status = make_payload(quantized, scale, BENCH_MODE ? 42 : NODE_ID, BENCH_MODE ? 11 : DATE_CODE,
                                  BENCH_MODE ? 7 : BATTERY_CODE, payload, sizeof(payload), &length, &schedule);
            Serial.printf("ENC status=%d bytes=%u schedule=%u time_us=%u\n", status, unsigned(length), schedule, micros()-start);
            if (status == PIPELINE_OK) {
                Serial.print("PAYLOAD_HEX=");
                for (size_t i = 0; i < length; ++i) Serial.printf("%02x", payload[i]);
                Serial.println();
                send_payload(length);
            } else Serial.println("ERROR: payload rejected; no truncation, fallback or RF send");
        }
    }
    for (auto &block : pcm) { free(block); block = nullptr; }
    Serial.printf("heap_free=%u heap_min=%u largest_block=%u workspace=%u\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)), unsigned(sizeof(workspace)));
}

// Start serial diagnostics without automatically sampling or transmitting.
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("15x13 sensing -> 6-byte-header Rice payload. Send 'c' for one capture.");
    Serial.println(BENCH_MODE ? "Synthetic bench mode; no physical samples claimed." : "Physical ADC capture mode.");
}

// Each received c command requests one measurement; no schedule is imposed.
void loop() {
    if (Serial.available() && Serial.read() == 'c') single_capture();
    delay(10);
}
