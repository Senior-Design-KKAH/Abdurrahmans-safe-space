#include <WiFi.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <esp_random.h>
#include <esp_sntp.h>
#include <time.h>

#include "app_queue.h"
#include "esp32_transport.h"
#include "radio.h"
#include "uplink.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define NAKHEL_WIFI_SSID ""
#define NAKHEL_WIFI_PASSWORD ""
#define NAKHEL_SERVER_URL ""
#define NAKHEL_API_KEY ""
#define NAKHEL_ROOT_CA ""
#endif

using namespace wire;

AppQueue uplinkQueue;
static std::atomic<bool> clockSynced{false};
static constexpr char modelVersion[] = "stream1s_w8_d4";
static_assert(sizeof(modelVersion) <= nakhel::kModelVersionBytes, "Model version must fit the uplink record");

class ClusterPlatform : public nakhel::UplinkPlatform {
public:
    bool linkReady() override { return WiFi.status() == WL_CONNECTED && epochSeconds() > kClockSetAfterS; }
    int64_t epochSeconds() override { return clockSynced.load() ? time(nullptr) : 0; }
    uint32_t uptimeSeconds() override { return nowUs() / 1000000; }
    bool uplinkRssi(int16_t& rssiDbm) override {
        if (WiFi.status() != WL_CONNECTED) return false;
        rssiDbm = WiFi.RSSI();
        return true;
    }
    uint32_t random() override { return esp_random(); }
    void log(const char* line) override { Serial.printf("[uplink] %s\n", line); }
};

static ClusterPlatform platform;

static void uplinkTask(void* context) {
    auto& uplink = *static_cast<nakhel::Uplink*>(context);
    uint32_t lastReconnect = millis();
    for (;;) {
        if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect >= 30000) {
            lastReconnect = millis();
            WiFi.reconnect();
        }
        const uint32_t waitMs = uplink.step(nowUs() / 1000);
        delay(waitMs == 0 ? 1 : std::min(waitMs, uint32_t(1000)));
    }
}

static void startUplink() {
    const char* url = NAKHEL_SERVER_URL;
    const bool https = std::strncmp(url, "https://", 8) == 0;
    const bool http = std::strncmp(url, "http://", 7) == 0;
    const size_t urlLength = std::strlen(url);
    if (!NAKHEL_WIFI_SSID[0] || !NAKHEL_API_KEY[0] || (!https && !http) ||
        urlLength <= (https ? 8u : 7u) || url[urlLength - 1] == '/') {
        Serial.println("[uplink] Configure include/secrets.h with Wi-Fi, server URL, and API key");
        return;
    }
    if (https && !NAKHEL_ROOT_CA[0]) {
        Serial.println("[uplink] HTTPS requires NAKHEL_ROOT_CA in include/secrets.h");
        return;
    }
    if (http) Serial.println("[uplink] Bench HTTP sends the API key and measurements unencrypted");
    static Esp32Transport transport(url, NAKHEL_API_KEY, NAKHEL_ROOT_CA);
    static nakhel::Uplink uplink(transport, uplinkQueue, platform);
    if (xTaskCreatePinnedToCore(uplinkTask, "nakhel_uplink", 16384, &uplink, 1, nullptr, 0) != pdPASS) {
        Serial.println("[uplink] Could not start uplink task");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(NAKHEL_WIFI_SSID, NAKHEL_WIFI_PASSWORD);
    sntp_set_time_sync_notification_cb([](timeval* utc) {
        if (utc->tv_sec > kClockSetAfterS && !clockSynced.exchange(true))
            Serial.println("[uplink] Clock synchronized");
    });
    configTime(0, 0, "pool.ntp.org", "time.google.com");
}

static Radio radio;
enum class SessionState { Idle, Receiving, Finished };
static SessionState state = SessionState::Idle;
static uint32_t session;
static uint64_t lastActivity;
static Delivery delivery;
static uint8_t accepted[resultBytes];
static const uint64_t idleLimit = 3 * profile::pacedInterval(profile::airUs(reportLen)) + 2000000;

static void reply(const Frame& response, uint64_t receivedAt) {
    if (!radio.send(response, receivedAt + profile::turnaroundUs))
        Serial.println("Reply TX failed");
}

static void handleStart(const Frame& request, uint64_t receivedAt) {
    if (request.size != startLen || !request.id()) return;
    Settings settings;
    settings.read(request);
    Status status = Accepted;
    if (get(request.bytes + 12, 4) != profileTag()) status = ProfileMismatch;
    else if (!settings.sameAs(resultSettings)) status = InvalidStart;
    else if (request.id() == session && state != SessionState::Receiving) status = InvalidStart;
    else if (request.id() != session && state == SessionState::Receiving) status = Busy;
    else if (state != SessionState::Receiving && platform.epochSeconds() <= kClockSetAfterS) {
        status = Busy;
        Serial.println("[lora] Busy: waiting for clock synchronization");
    } else if (state != SessionState::Receiving && uplinkQueue.count() >= kQueueRecords) {
        status = Busy;
        Serial.println("[lora] Busy: uplink queue full");
    } else {
        if (state != SessionState::Receiving) {
            session = request.id();
            state = SessionState::Receiving;
            delivery = Delivery{};
            Serial.printf("[lora] START accepted: session=%08lx\n", (unsigned long)session);
        }
        lastActivity = receivedAt;
    }
    Frame response;
    response.set(Ready, request.id(), readyLen);
    response.bytes[5] = status;
    reply(response, receivedAt);
}

static void handleData(const Frame& frame, uint64_t receivedAt) {
    Result result{};
    const uint8_t* payload = frame.bytes + dataHeader;
    if (!decodeData(frame, result) ||
        (delivery.unique && std::memcmp(accepted, payload, resultBytes) != 0)) {
        if (delivery.rejected < 0xffffff) ++delivery.rejected;
        Serial.println("Rejected invalid or conflicting DATA");
        return;
    }
    lastActivity = receivedAt;
    if (delivery.unique) {
        if (delivery.duplicates < 0xffffff) ++delivery.duplicates;
        return;
    }

    nakhel::UplinkRecord record;
    auto& measurement = record.measurement;
    measurement.timestampS = platform.epochSeconds();
    if (measurement.timestampS <= kClockSetAfterS) {
        Serial.println("[lora] DATA not queued: clock unavailable");
        return;
    }
    measurement.receivedAtS = measurement.timestampS;
    measurement.nodeId = result.node;
    measurement.confidence = result.score;
    std::memcpy(measurement.modelVersion, modelVersion, sizeof(modelVersion));
    measurement.hasRssi = measurement.hasSnr = true;
    measurement.rssiDbm = radio.rssi;
    measurement.snrDb = radio.snrQ4 / 4.0f;
    if (!uplinkQueue.push(record)) {
        Serial.printf("[lora] DATA not queued: session=%08lx node=%u queue full\n",
                      (unsigned long)session, unsigned(result.node));
        return;
    }

    std::memcpy(accepted, payload, resultBytes);
    delivery.signal.add(0, radio.rssi, radio.snrQ4);
    delivery.unique = 1;
    delivery.initial = frame.type() == Initial;
    char timestamp[nakhel::kIsoBytes];
    nakhel::formatIso8601(measurement.timestampS, timestamp);
    Serial.printf("RESULT session=%08lx node=%u day=%u battery=%u score=%.9f bits=%08lx rssi=%d snr=%.2f timestamp=%s queued\n",
                  (unsigned long)session, unsigned(result.node), unsigned(result.day),
                  unsigned(result.battery), double(result.score), (unsigned long)scoreBits(result.score),
                  radio.rssi, double(measurement.snrDb), timestamp);
}

void setup() {
    Serial.begin(115200);
    startUplink();
    while (!radio.begin()) {
        Serial.println("Radio initialization failed. Retrying.");
        delay(2000);
    }
    Serial.println("Cluster ready for Edge results");
}

void loop() {
    Frame frame;
    const bool received = radio.read(frame);
    const uint64_t receivedAt = nowUs();
    // Expire an abandoned session before responding to the next START.
    if (state == SessionState::Receiving && receivedAt - lastActivity > idleLimit) {
        state = SessionState::Idle;
        Serial.printf("Session %08lx timed out, accepted=%lu\n",
                      (unsigned long)session, (unsigned long)delivery.unique);
    }
    if (received) {
        const unsigned type = frame.type();
        if (type == Start) handleStart(frame, receivedAt);
        else if (state != SessionState::Idle && frame.id() == session) {
            if ((type == Initial || type == Retry) && state == SessionState::Receiving) {
                handleData(frame, receivedAt);
            } else if (type == Poll && frame.size == pollLen &&
                       get(frame.bytes + header, 2) == 0) {
                lastActivity = receivedAt;
                Frame response;
                response.set(Bitmap, session, bitmapLen);
                put(response.bytes + header, 0, 2);
                put(response.bytes + dataHeader, delivery.unique, 4);
                reply(response, receivedAt);
            } else if (type == Finish && frame.size == finishLen) {
                SendCounts attempts;
                if (attempts.read(frame, resultSettings.count)) {
                    state = SessionState::Finished;
                    Frame response;
                    delivery.write(response, session);
                    reply(response, receivedAt);
                }
            }
        }
    }
    delay(1);
}
