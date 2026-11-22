#include "payload.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace nakhel {
namespace {

// The server's limit for a stored 32-bit float (rpw-dashboard: web/lib/schemas/ingest.ts).
constexpr float kRealMax = 3.4e38F;

// Howard Hinnant's civil-date algorithms. Used instead of gmtime because time_t is 32 bits on some
// ESP32 toolchains.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

// Appends to a fixed buffer and remembers if anything didn't fit.
class Writer {
 public:
  Writer(char* out, size_t capacity) : out_(out), capacity_(capacity) {}

  void add(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    if (failed_) return;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(out_ + length_, capacity_ - length_, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity_ - length_) {
      failed_ = true;
      return;
    }
    length_ += static_cast<size_t>(written);
  }

  size_t finish() const { return failed_ ? 0 : length_; }

 private:
  char* out_;
  size_t capacity_;
  size_t length_ = 0;
  bool failed_ = false;
};

bool validReal(float value) { return std::isfinite(value) && std::fabs(value) <= kRealMax; }
bool validRssi(int value) { return value >= -200 && value <= 20; }
bool validConfidence(float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; }

// Same rule as the server. Also keeps the hand-built JSON safe, since nothing here needs escaping.
bool validModelVersion(const char* text) {
  if (text[0] == '\0') return false;
  for (size_t i = 0; i < kModelVersionBytes; ++i) {
    const char c = text[i];
    if (c == '\0') return true;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return false;  // no terminator
}

}  // namespace

void formatIso8601(int64_t epochS, char out[kIsoBytes]) {
  int64_t days = epochS / 86400;
  int64_t rest = epochS % 86400;
  if (rest < 0) {
    rest += 86400;
    --days;
  }
  int64_t year = 0;
  unsigned month = 0;
  unsigned day = 0;
  civilFromDays(days, year, month, day);
  snprintf(out, kIsoBytes, "%04d-%02u-%02uT%02d:%02d:%02dZ", static_cast<int>(year), month, day,
           static_cast<int>(rest / 3600), static_cast<int>(rest / 60 % 60), static_cast<int>(rest % 60));
}

bool parseIso8601Utc(const char* text, int64_t& epochS) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int consumed = 0;
  if (sscanf(text, "%4d-%2u-%2uT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &consumed) != 6) {
    return false;
  }
  const char* tail = text + consumed;
  if (*tail == '.') {
    ++tail;
    while (*tail >= '0' && *tail <= '9') ++tail;
  }
  if (tail[0] != 'Z' || tail[1] != '\0') return false;
  epochS = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
  return true;
}

size_t buildMeasurement(const Measurement& m, char* out, size_t capacity) {
  if (!validConfidence(m.confidence) || !validModelVersion(m.modelVersion)) return 0;
  char iso[kIsoBytes];
  formatIso8601(m.timestampS, iso);
  Writer writer(out, capacity);
  writer.add("{\"node_id\":%u,\"timestamp\":\"%s\",\"confidence\":%.7g,\"model_version\":\"%s\"", m.nodeId, iso,
             static_cast<double>(m.confidence), m.modelVersion);
  if (m.receivedAtS != 0) {
    formatIso8601(m.receivedAtS, iso);
    writer.add(",\"received_at\":\"%s\"", iso);
  }
  if (m.hasSnr && validReal(m.snrDb)) writer.add(",\"snr_db\":%.7g", static_cast<double>(m.snrDb));
  if (m.hasRssi && validRssi(m.rssiDbm)) writer.add(",\"rssi_dbm\":%d", m.rssiDbm);
  writer.add("}");
  return writer.finish();
}

size_t buildNodeHealth(const NodeHealth& h, char* out, size_t capacity) {
  char iso[kIsoBytes];
  formatIso8601(h.timestampS, iso);
  Writer writer(out, capacity);
  writer.add("{\"node_id\":%u,\"timestamp\":\"%s\"", h.nodeId, iso);
  if (h.hasBattery) writer.add(",\"battery_mv\":%lu", static_cast<unsigned long>(h.batteryMv));
  if (h.hasRssi && validRssi(h.rssiDbm)) writer.add(",\"rssi_dbm\":%d", h.rssiDbm);
  if (h.hasUptime) writer.add(",\"uptime_s\":%lu", static_cast<unsigned long>(h.uptimeS));
  writer.add("}");
  return writer.finish();
}

size_t buildClusterHealth(const ClusterHealth& h, char* out, size_t capacity) {
  char iso[kIsoBytes];
  formatIso8601(h.timestampS, iso);
  Writer writer(out, capacity);
  writer.add("{\"timestamp\":\"%s\",\"uptime_s\":%lu,\"uplink_type\":\"wifi\",\"buffered_count\":%lu", iso,
             static_cast<unsigned long>(h.uptimeS), static_cast<unsigned long>(h.bufferedCount));
  if (h.hasRssi && validRssi(h.rssiDbm)) writer.add(",\"rssi_dbm\":%d", h.rssiDbm);
  writer.add("}");
  return writer.finish();
}

}  // namespace nakhel
