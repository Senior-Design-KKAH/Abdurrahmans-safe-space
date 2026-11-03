#include "pipeline.h"
#include <cmath>
#include <cstring>
#include <limits>

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Protocol requires IEEE754 binary32");

// This ordered bank is part of the wire protocol shared with the decoder.
static const uint8_t BANK[8][13] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0}, {1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0}, {2,1,0,0,0,0,0,0,0,0,0,0,0},
    {2,1,1,1,0,0,0,0,0,0,0,0,0}, {2,2,1,1,1,1,1,1,0,0,0,0,0},
    {3,2,2,1,1,1,1,1,1,1,1,0,0}, {4,1,1,1,1,1,0,0,0,0,0,0,0}
};

// Map signed temporal differences to unsigned values without int8 overflow.
static unsigned zigzag(int value) {
    return value >= 0 ? 2u * value : unsigned(-2 * value - 1);
}

// Use one stored float32 scale for every coefficient in the recording.
int quantize_features(const double *features, int8_t *q, float *scale) {
    if (!features || !q || !scale) return INVALID_FEATURES;
    double maximum = 0;
    for (int i = 0; i < 195; ++i) {
        if (!std::isfinite(features[i])) return INVALID_FEATURES;
        if (std::fabs(features[i]) > maximum) maximum = std::fabs(features[i]);
    }
    *scale = maximum == 0 ? 1.0f : float(maximum / 127.0);
    if (!std::isfinite(*scale) || *scale <= 0) return INVALID_SCALE;
    for (int i = 0; i < 195; ++i) {
        double x = features[i] / double(*scale);
        double lower = std::floor(x), fraction = x - lower;
        // NumPy rint: halfway values, including negatives, round to even.
        double rounded = lower + (fraction > .5 || (fraction == .5 && std::fmod(std::fabs(lower), 2.0) == 1.0));
        if (rounded < -127) rounded = -127;
        if (rounded > 127) rounded = 127;
        q[i] = int8_t(rounded);
    }
    return PIPELINE_OK;
}

// Select the cheapest schedule and pack the fixed header plus lossless MFCC data.
int make_payload(const int8_t *q, float scale, uint32_t node, uint32_t date, uint32_t battery,
                 uint8_t *out, size_t capacity, size_t *required, uint8_t *selected) {
    if (!q || !required || !selected) return INVALID_FEATURES;
    if (!std::isfinite(scale) || scale <= 0) return INVALID_SCALE;
    if (node > 255u || date > 31u || battery > 7u) return INVALID_HEADER;
    uint32_t best = UINT32_MAX;
    unsigned choice = 0;
    // Bit count breaks byte ties; ascending iteration also breaks equal-bit ties.
    for (unsigned b = 0; b < 8; ++b) {
        uint32_t bits = 107;
        for (int t = 1; t < 15; ++t)
            for (int c = 0; c < 13; ++c) {
                unsigned u = zigzag(int(q[t*13+c]) - int(q[(t-1)*13+c]));
                bits += (u >> BANK[b][c]) + 1 + BANK[b][c];
            }
        if (bits < best) { best = bits; choice = b; }
    }
    *required = 6 + (best + 7) / 8;
    *selected = uint8_t(choice);
    // Report the exact required size before writing into any caller buffer.
    if (*required > RADIO_CAPACITY) return PAYLOAD_TOO_LARGE;
    if (!out || capacity < *required) return BUFFER_OVERFLOW;
    std::memset(out, 0, *required);
    out[0] = uint8_t(node);
    // Byte 1 holds the unsigned date in bits 7..3 and battery in bits 2..0.
    out[1] = uint8_t((date << 3) | battery);
    // Copy IEEE754 bits directly so network byte order preserves the scale exactly.
    uint32_t scale_bits;
    std::memcpy(&scale_bits, &scale, 4);
    for (int i = 0; i < 4; ++i) out[2+i] = uint8_t(scale_bits >> (24-8*i));
    size_t position = 48;
    // Append fields MSB-first without inserting alignment between them.
    auto put = [&](uint32_t value, unsigned count) {
        for (int bit = int(count)-1; bit >= 0; --bit, ++position)
            out[position/8] |= uint8_t(((value >> bit) & 1u) << (7-position%8));
    };
    put(choice, 3);
    // The complete first frame is sent directly so temporal deltas can be inverted.
    for (int c = 0; c < 13; ++c) put(uint8_t(q[c]), 8);
    for (int t = 1; t < 15; ++t)
        for (int c = 0; c < 13; ++c) {
            unsigned u = zigzag(int(q[t*13+c]) - int(q[(t-1)*13+c]));
            unsigned k = BANK[choice][c];
            position += u >> k; // Output was zeroed: unary zeros need no writes.
            put(1, 1);
            put(u & ((1u << k)-1), k);
        }
    return position <= *required * 8 ? PIPELINE_OK : BUFFER_OVERFLOW;
}
