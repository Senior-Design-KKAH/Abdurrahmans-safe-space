#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits>

namespace ml {
constexpr unsigned PayloadBytes = 6;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "ML results require IEEE-754 binary32");

struct Result {
    uint8_t node = 0, day = 0, battery = 0;
    float probability = 0;
};

inline uint32_t floatBits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}
inline float fromBits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
inline bool validProbability(float value) {
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}
inline unsigned batteryCode(double percentage) {
    if (!isfinite(percentage)) percentage = 0;
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    return unsigned(floor(percentage * 7.0 / 100.0 + 0.5));
}
inline double batteryPercent(unsigned code) { return code * 100.0 / 7.0; }

// No output is written unless every field and the complete output capacity is valid.
inline bool encode(unsigned node, unsigned day, unsigned battery, float probability,
                   uint8_t* out, unsigned capacity) {
    if (!out || capacity < PayloadBytes || node < 1 || node > 2 || day < 1 ||
        day > 31 || battery > 7 || !validProbability(probability)) return false;
    out[0] = uint8_t(node);
    out[1] = uint8_t(((day & 31u) << 3) | (battery & 7u));
    const uint32_t bits = floatBits(probability);
    for (unsigned i = 0; i < 4; ++i) out[2+i] = uint8_t(bits >> (8*i));
    return true;
}
inline bool decode(const uint8_t* body, unsigned length, Result& out) {
    if (!body || length != PayloadBytes || body[0] < 1 || body[0] > 2) return false;
    Result result;
    result.node = body[0];
    result.day = (body[1] >> 3) & 31u;
    result.battery = body[1] & 7u;
    uint32_t bits = 0;
    for (unsigned i = 0; i < 4; ++i) bits |= uint32_t(body[2+i]) << (8*i);
    result.probability = fromBits(bits);
    if (!result.day || !validProbability(result.probability)) return false;
    out = result;
    return true;
}

}
