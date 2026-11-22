// Stop-and-wait ARQ over HTTPS: one record in flight, removed from the buffer only on acknowledgement,
// and resent with exponential backoff until then. The HTTP answer is the ACK, and the server's
// (node, timestamp) identity makes retransmissions safe: a lost ACK leads to a retry, answered 200
// duplicate. The order of records is kept.
#pragma once

#include <cstddef>
#include <cstdint>

#include "payload.h"
#include "records.h"
#include "response.h"
#include "transport.h"
#include "uplink_queue.h"

namespace nakhel {

struct UplinkConfig {
  uint32_t firstRetryMs = 5000;
  uint32_t maxRetryMs = 600000;
  uint32_t idlePollMs = 200;
  uint32_t linkPollMs = 1000;
  uint32_t keyRefusedPollMs = 60000;
  uint32_t clusterHealthIntervalMs = 300000;  // 15 minutes of silence raises cluster_offline
  int32_t clockDriftWarnS = 60;
};

// The outside world, supplied by the firmware or by a test.
class UplinkPlatform {
 public:
  virtual ~UplinkPlatform() = default;
  // Connected, and the clock has been set: timestamps and TLS certificate checks depend on it.
  virtual bool linkReady() = 0;
  virtual int64_t epochSeconds() = 0;
  virtual uint32_t uptimeSeconds() = 0;
  virtual bool uplinkRssi(int16_t& rssiDbm) = 0;
  virtual uint32_t random() = 0;
  virtual void log(const char* line) = 0;
  // A record the server refused for good, or that could never be valid. The firmware has no rejected
  // table like the Pi's, so this is where to keep it if the team wants one.
  virtual void onRejected(const UplinkRecord& record, const char* reason) {
    (void)record;
    (void)reason;
  }
};

struct UplinkStats {
  uint32_t stored = 0;
  uint32_t duplicates = 0;
  uint32_t rejected = 0;
  uint32_t retries = 0;
  uint32_t healthSent = 0;
  uint32_t healthFailed = 0;
};

class Uplink {
 public:
  Uplink(HttpTransport& transport, UplinkQueue& queue, UplinkPlatform& platform, UplinkConfig config = {});

  // Does at most one send. Returns how many ms to wait before calling again. nowMs must not wrap.
  uint32_t step(uint64_t nowMs);

  bool keyRefused() const { return keyRefused_; }
  const UplinkStats& stats() const { return stats_; }

 private:
  void sendClusterHealth();
  void sendHead(uint64_t nowMs);
  void reject(const char* reason);
  void scheduleRetry(uint64_t nowMs);
  void checkClock(const char* responseBody);
  void refuseKey();

  HttpTransport& transport_;
  UplinkQueue& queue_;
  UplinkPlatform& platform_;
  UplinkConfig config_;
  UplinkStats stats_;

  // Members, not locals: together they are about 1.6 KB, which is a lot for a task's stack.
  UplinkRecord record_;
  char body_[kBodyBytes] = {};
  HttpResponse response_;

  uint64_t nextAttemptMs_ = 0;
  uint64_t nextHealthMs_ = 0;
  uint32_t retryDelayMs_ = 0;
  bool keyRefused_ = false;
};

}  // namespace nakhel
