#include <Arduino.h>
#include "esp_random.h"
#include "esp_system.h"
#include "edge.h"
#include "radio.h"
#ifdef EDGE_MODEL_CHECK
#include "clip.h"
#endif

using namespace wire;

static constexpr uint8_t nodeId = 1;
static constexpr unsigned dummyDay = 3;
static constexpr unsigned dummyBatteryCode = 5;
static constexpr unsigned dataRetries = 4, controlRetries = 4;
static constexpr int bootButtonPin = 0;
static constexpr unsigned buttonDebounceMs = 25;
static constexpr int resetPendingPin = 32, resetClearPin = 33, resetArmPin = 25;
static Radio radio;
static bool modelReady, radioReady, resetTrigger, bootButtonHeld;

static bool bootPressed() {
    const bool pressed = digitalRead(bootButtonPin) == LOW;
    if (pressed == bootButtonHeld) return false;
    delay(buttonDebounceMs);
    if ((digitalRead(bootButtonPin) == LOW) != pressed) return false;
    bootButtonHeld = pressed;
    return pressed;
}

template<class Match>
static bool exchange(const Frame& request, Type replyType, unsigned replySize, Match match) {
    Frame response;
    for (unsigned attempt = 0; attempt <= controlRetries; ++attempt) {
        if (!radio.send(request)) {
            Serial.printf("LoRa TX failed: frame=%u attempt=%u\n", request.type(), attempt + 1);
            continue;
        }
        const uint64_t deadline = nowUs() + replyWaitUs(request.type(), replySize);
        while (nowUs() < deadline) {
            if (radio.read(response) && response.type() == replyType &&
                response.size == replySize && response.id() == request.id() && match(response))
                return true;
            delay(1);
        }
    }
    return false;
}

static void sendResult(const uint8_t (&payload)[resultBytes]) {
    if (!radioReady) radioReady = radio.begin();
    if (!radioReady) {
        Serial.println("Radio initialization failed");
        return;
    }
    uint32_t session;
    do { session = esp_random(); } while (!session);
    Serial.printf("LoRa session=%08lx\n", (unsigned long)session);

    Frame request;
    resultSettings.write(request, session);
    unsigned status = InvalidStart;
    const bool ready = exchange(request, Ready, readyLen, [&](const Frame& frame) {
        status = frame.bytes[5];
        return status <= Busy;
    });
    if (!ready || status != Accepted) {
        if (!ready) Serial.println("No READY from cluster. DATA was not sent.");
        else Serial.printf("Cluster declined START, status=%u. DATA was not sent.\n", status);
        return;
    }

    const uint64_t started = nowUs();
    SendCounts counts;
    bool confirmed = false, bitmapReceived = false;
    for (unsigned attempt = 0; attempt <= dataRetries; ++attempt) {
        const Type type = attempt == 0 ? Initial : Retry;
        Frame data;
        writeData(data, type, session, payload);
        if (type == Initial) ++counts.initial;
        else ++counts.retries;
        if (!radio.send(data)) {
            ++counts.failures;
            if (type == Retry) ++counts.retryFailures;
        }

        Frame poll;
        poll.set(Poll, session, pollLen);
        put(poll.bytes + header, 0, 2);
        bitmapReceived = exchange(poll, Bitmap, bitmapLen, [&](const Frame& reply) {
            if (get(reply.bytes + header, 2) != 0) return false;
            const uint32_t received = get(reply.bytes + dataHeader, 4);
            if (received > 1) return false;
            confirmed = received == 1;
            return true;
        });
        if (!bitmapReceived || confirmed) break;
    }

    counts.dataElapsedUs = nowUs() - started;
    counts.write(request, session);
    Delivery report;
    const bool reported = exchange(request, Report, reportLen, [&](const Frame& frame) {
        return report.read(frame, resultSettings.count) && report.unique >= unsigned(confirmed);
    });
    if (reported && report.unique) Serial.println("DELIVERED to cluster (final report)");
    else if (confirmed) Serial.println("DELIVERED to cluster (bitmap ACK, no final report)");
    else if (reported) Serial.println("NOT DELIVERED: cluster reported no result");
    else Serial.println("DELIVERY UNCONFIRMED: no positive ACK or final report");
    Serial.printf("DATA initial=%lu retries=%lu TX_failed=%lu bitmap_reply=%s\n",
                  (unsigned long)counts.initial, (unsigned long)counts.retries,
                  (unsigned long)counts.failures, bitmapReceived ? "yes" : "timeout");
}

static void measure() {
    if (!modelReady) {
        Serial.println("Model unavailable");
        return;
    }
    float mean;
    if (!captureAndScore(mean) || !validScore(mean)) {
        Serial.println("Capture or inference failed. No result sent.");
        return;
    }
    Serial.printf("MEAN score=%.9f bits=%08lx\n", double(mean), (unsigned long)scoreBits(mean));
    uint8_t payload[resultBytes];
    if (!encodeResult(payload, nodeId, dummyDay, dummyBatteryCode, mean)) return;
    Serial.print("PAYLOAD_HEX=");
    for (uint8_t byte : payload) Serial.printf("%02X", unsigned(byte));
    Serial.println();
    sendResult(payload);
}

#ifdef EDGE_MODEL_CHECK
static void checkModel() {
    float sum = 0.0f;
    for (unsigned i = 0; i < windowCount; ++i) {
        float score;
        if (!scoreWindow(kClipPcm + i * samplesPerWindow, score)) {
            Serial.println("MODEL CHECK failed during inference");
            return;
        }
        sum += score;
        Serial.printf("FIXTURE window=%u bits=%08lx\n", i + 1, (unsigned long)scoreBits(score));
        delay(1);
    }
    const uint32_t meanBits = scoreBits(sum / float(windowCount));
    Serial.printf("MODEL CHECK mean_bits=%08lx %s\n", (unsigned long)meanBits,
                  meanBits == 0x3f7d3333 ? "matches reference" : "MISMATCH");
}
#endif

void setup() {
    Serial.begin(115200);
    pinMode(bootButtonPin, INPUT_PULLUP);
    pinMode(resetArmPin, OUTPUT);
    digitalWrite(resetArmPin, LOW);
    pinMode(resetPendingPin, INPUT_PULLDOWN);
    const bool pending = digitalRead(resetPendingPin) == HIGH;
    const esp_reset_reason_t reason = esp_reset_reason();
    resetTrigger = pending && (reason == ESP_RST_POWERON || reason == ESP_RST_EXT);
    // Pulse the external latch's active-low clear before acting on its event.
    pinMode(resetClearPin, OUTPUT_OPEN_DRAIN);
    digitalWrite(resetClearPin, LOW);
    delay(1);
    digitalWrite(resetClearPin, HIGH);
    Serial.printf("BOOT reset_reason=%d latch=%d trigger=%d\n", int(reason), pending, resetTrigger);
    modelReady = initializeModel();
    // Arm after the external power-on supervisor has released both latches.
    if (modelReady) {
        delay(350);
        digitalWrite(resetArmPin, HIGH);
    }
    bootButtonHeld = digitalRead(bootButtonPin) == LOW;
    Serial.println(modelReady ? "Edge ready. Press BOOT or send c to capture." : "Model initialization failed.");
#ifdef EDGE_MODEL_CHECK
    Serial.println("Send t for the fixture check without capture or radio.");
#endif
}

void loop() {
    const int command = Serial.read();
    if (bootPressed() || resetTrigger || command == 'c') {
        resetTrigger = false;
        measure();
        while (Serial.available()) Serial.read();
        // Ignore a press that began while Edge was busy, even if the button is still held.
        bootButtonHeld = digitalRead(bootButtonPin) == LOW;
        Serial.println("Edge idle");
    }
#ifdef EDGE_MODEL_CHECK
    else if (command == 't') {
        checkModel();
        while (Serial.available()) Serial.read();
        bootButtonHeld = digitalRead(bootButtonPin) == LOW;
    }
#endif
    delay(1);
}
