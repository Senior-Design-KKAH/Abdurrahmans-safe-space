#include "uplink.h"

#include <cstdio>
#include <cstdlib>

namespace nakhel {
namespace {

constexpr const char* kMeasurementPath = "/api/v1/ingest/measurements";
constexpr const char* kNodeHealthPath = "/api/v1/ingest/node-health";
constexpr const char* kClusterHealthPath = "/api/v1/ingest/cluster-health";

}  // namespace

Uplink::Uplink(HttpTransport& transport, UplinkQueue& queue, UplinkPlatform& platform, UplinkConfig config)
    : transport_(transport), queue_(queue), platform_(platform), config_(config), retryDelayMs_(config.firstRetryMs) {}

uint32_t Uplink::step(uint64_t nowMs) {
  if (keyRefused_) return config_.keyRefusedPollMs;
  if (!platform_.linkReady()) return config_.linkPollMs;

  // Checked first so that a long backlog can't starve it and raise a false cluster_offline alert.
  if (nowMs >= nextHealthMs_) {
    nextHealthMs_ = nowMs + config_.clusterHealthIntervalMs;
    sendClusterHealth();
    return 0;
  }
  if (nowMs < nextAttemptMs_) {
    const uint64_t untilRetry = nextAttemptMs_ - nowMs;
    const uint64_t untilHealth = nextHealthMs_ - nowMs;
    return static_cast<uint32_t>(untilRetry < untilHealth ? untilRetry : untilHealth);
  }
  if (!queue_.peek(record_)) return config_.idlePollMs;
  sendHead(nowMs);
  return 0;
}

void Uplink::sendClusterHealth() {
  ClusterHealth health;
  health.timestampS = platform_.epochSeconds();
  health.uptimeS = platform_.uptimeSeconds();
  health.bufferedCount = static_cast<uint32_t>(queue_.count());
  health.hasRssi = platform_.uplinkRssi(health.rssiDbm);

  const size_t length = buildClusterHealth(health, body_, sizeof(body_));
  char iso[kIsoBytes];
  formatIso8601(health.timestampS, iso);

  // Not retransmitted: the next reading replaces this one and is more useful than an old copy.
  response_ = HttpResponse();
  transport_.post(kClusterHealthPath, body_, length, response_);
  const Classified result = classify(response_, -1, iso);
  if (result.verdict == Verdict::KeyRefused) {
    refuseKey();
  } else if (result.verdict == Verdict::Ack) {
    ++stats_.healthSent;
    checkClock(response_.body);
  } else {
    ++stats_.healthFailed;
  }
}

void Uplink::sendHead(uint64_t nowMs) {
  const bool isMeasurement = record_.kind == UplinkRecord::Kind::Measurement;
  const size_t length = isMeasurement ? buildMeasurement(record_.measurement, body_, sizeof(body_))
                                      : buildNodeHealth(record_.nodeHealth, body_, sizeof(body_));
  if (length == 0) {
    reject("invalid_locally");
    return;
  }

  const int nodeId = isMeasurement ? record_.measurement.nodeId : record_.nodeHealth.nodeId;
  const int64_t timestampS = isMeasurement ? record_.measurement.timestampS : record_.nodeHealth.timestampS;
  char iso[kIsoBytes];
  formatIso8601(timestampS, iso);

  response_ = HttpResponse();
  transport_.post(isMeasurement ? kMeasurementPath : kNodeHealthPath, body_, length, response_);
  const Classified result = classify(response_, nodeId, iso);

  switch (result.verdict) {
    case Verdict::Ack:
      queue_.remove();
      if (result.ack == AckKind::Stored) {
        ++stats_.stored;
      } else {
        ++stats_.duplicates;
      }
      retryDelayMs_ = config_.firstRetryMs;
      nextAttemptMs_ = 0;
      {
        char line[80];
        snprintf(line, sizeof(line), "delivered node %d at %s (%s)", nodeId, iso, result.ack == AckKind::Stored ? "stored" : "duplicate");
        platform_.log(line);
      }
      checkClock(response_.body);
      break;
    case Verdict::Rejected:
      reject(result.errorCode);
      break;
    case Verdict::KeyRefused:
      refuseKey();
      break;
    case Verdict::Retry:
      scheduleRetry(nowMs);
      break;
  }
}

void Uplink::reject(const char* reason) {
  platform_.onRejected(record_, reason);
  queue_.remove();
  ++stats_.rejected;
  // A rejection proves the link works, so the next record needn't wait.
  retryDelayMs_ = config_.firstRetryMs;
  nextAttemptMs_ = 0;
  char line[64];
  snprintf(line, sizeof(line), "rejected a record: %s", reason);
  platform_.log(line);
}

void Uplink::scheduleRetry(uint64_t nowMs) {
  ++stats_.retries;
  // Up to 20% jitter, so cluster heads recovering from the same outage don't all retry together.
  const uint32_t jitter = retryDelayMs_ / 5 == 0 ? 0 : platform_.random() % (retryDelayMs_ / 5);
  nextAttemptMs_ = nowMs + retryDelayMs_ + jitter;
  char line[64];
  snprintf(line, sizeof(line), "send failed (status %d), retrying in %lu ms", response_.status,
           static_cast<unsigned long>(retryDelayMs_ + jitter));
  platform_.log(line);
  const uint64_t doubled = static_cast<uint64_t>(retryDelayMs_) * 2;
  retryDelayMs_ = doubled > config_.maxRetryMs ? config_.maxRetryMs : static_cast<uint32_t>(doubled);
}

void Uplink::checkClock(const char* responseBody) {
  char text[32];
  int64_t serverS = 0;
  if (!jsonString(responseBody, "server_time", text, sizeof(text)) || !parseIso8601Utc(text, serverS)) return;
  const int64_t drift = platform_.epochSeconds() - serverS;
  if (drift > config_.clockDriftWarnS || drift < -config_.clockDriftWarnS) {
    char line[64];
    snprintf(line, sizeof(line), "warning: clock is %ld s off server time", static_cast<long>(drift));
    platform_.log(line);
  }
}

void Uplink::refuseKey() {
  keyRefused_ = true;
  platform_.log("API key refused: sending stopped, buffer kept. Install a valid key and restart.");
}

}  // namespace nakhel
