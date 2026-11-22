// Builds the JSON request bodies by hand: no heap, and a fixed worst-case size.
#pragma once

#include <cstddef>
#include <cstdint>

#include "records.h"

namespace nakhel {

// Fits any record with every value at its longest; a test proves it.
constexpr size_t kBodyBytes = 512;
constexpr size_t kIsoBytes = 21;  // "2026-10-24T18:00:04Z" plus NUL

void formatIso8601(int64_t epochS, char out[kIsoBytes]);
// Accepts YYYY-MM-DDTHH:MM:SS, then optional fraction, then Z. Returns false for anything else.
bool parseIso8601Utc(const char* text, int64_t& epochS);

// Each returns the body's length, or 0 if the record can never be valid: a confidence outside 0 to 1
// or a bad model version would be refused by the server, so it is caught here. Optional values
// that are out of range are left out, because a bad RSSI reading shouldn't cost a good measurement.
size_t buildMeasurement(const Measurement& m, char* out, size_t capacity);
size_t buildNodeHealth(const NodeHealth& h, char* out, size_t capacity);
size_t buildClusterHealth(const ClusterHealth& h, char* out, size_t capacity);

}  // namespace nakhel
