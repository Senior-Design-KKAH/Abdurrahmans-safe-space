#pragma once
#include <cmath>
#include <cstring>
#include <limits>
#include <stdint.h>
#include "profile.h"

namespace wire {
constexpr unsigned maxFrame = 255;
constexpr unsigned header = 5;
constexpr unsigned dataHeader = header + 2;
constexpr unsigned startLen = 16;
constexpr unsigned readyLen = 6;
constexpr unsigned pollLen = 7;
constexpr unsigned bitmapLen = 11;
constexpr unsigned finishLen = 29;
constexpr unsigned reportLen = 31;
constexpr uint8_t version = 0x20;
constexpr unsigned resultBytes = 6;

enum Type : uint8_t { Start = 1, Ready, Initial, Retry, Poll, Bitmap, Finish, Report };
enum Status : uint8_t { Accepted, ProfileMismatch, InvalidStart, Busy };

inline void put(uint8_t* out, uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; ++i) {
        out[i] = uint8_t(value);
        value >>= 8;
    }
}

inline uint64_t get(const uint8_t* in, unsigned size) {
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= uint64_t(in[i]) << (8 * i);
    return value;
}

struct Frame {
    uint8_t bytes[maxFrame];
    unsigned size = 0;

    void set(Type type, uint32_t id, unsigned length) {
        size = length;
        bytes[0] = version | type;
        put(bytes + 1, id, 4);
    }
    unsigned type() const {
        return size >= header && (bytes[0] & 0xf0) == version ? bytes[0] & 0x0f : 0;
    }
    uint32_t id() const { return size >= header ? get(bytes + 1, 4) : 0; }
};

inline uint32_t profileTag() {
    const uint32_t values[] = {profile::frequencyHz, profile::bandwidthHz,
        profile::spreadingFactor, profile::codingRate, profile::preamble,
        profile::syncWord, profile::dutyPermille, profile::txDbm};
    uint32_t hash = 2166136261U;
    for (uint32_t value : values)
        for (unsigned i = 0; i < 4; ++i, value >>= 8)
            hash = (hash ^ uint8_t(value)) * 16777619U;
    return hash;
}

struct Settings {
    unsigned count;
    unsigned payload;
    unsigned window;
    int power;

    constexpr Settings(unsigned count = 0, unsigned payload = 0, unsigned window = 0, int power = 0)
        : count(count), payload(payload), window(window), power(power) {}

    bool sameAs(const Settings& other) const {
        return count == other.count && payload == other.payload &&
            window == other.window && power == other.power;
    }
    void write(Frame& frame, uint32_t id) const {
        frame.set(Start, id, startLen);
        put(frame.bytes + 5, count, 4);
        frame.bytes[9] = payload;
        frame.bytes[10] = window;
        frame.bytes[11] = power;
        put(frame.bytes + 12, profileTag(), 4);
    }
    void read(const Frame& frame) {
        count = get(frame.bytes + 5, 4);
        payload = frame.bytes[9];
        window = frame.bytes[10];
        power = frame.bytes[11];
    }
};

// The result session carries exactly one measurement, at sequence zero.
constexpr Settings resultSettings{1, resultBytes, 1, profile::txDbm};

inline bool validScore(float score) {
    return std::isfinite(score) && score >= 0.0f && score <= 1.0f;
}

inline uint32_t scoreBits(float score) {
    static_assert(sizeof(float) == sizeof(uint32_t) && std::numeric_limits<float>::is_iec559,
                  "The link requires IEEE 754 binary32");
    uint32_t bits;
    std::memcpy(&bits, &score, sizeof(bits));
    return bits;
}

inline bool encodeResult(uint8_t (&payload)[resultBytes], uint8_t node,
                         unsigned day, unsigned battery, float score) {
    if (day < 1 || day > 31 || battery > 7 || !validScore(score)) return false;
    payload[0] = node;
    payload[1] = uint8_t((day << 3) | battery);
    put(payload + 2, scoreBits(score), 4);
    return true;
}

struct Result {
    uint8_t node, day, battery;
    float score;
};

inline bool decodeData(const Frame& frame, Result& result) {
    if ((frame.type() != Initial && frame.type() != Retry) ||
        frame.size != dataHeader + resultBytes || !frame.id() ||
        get(frame.bytes + header, 2) != 0) return false;
    const uint8_t* payload = frame.bytes + dataHeader;
    const uint32_t bits = get(payload + 2, 4);
    std::memcpy(&result.score, &bits, sizeof(bits));
    result.node = payload[0];
    result.day = payload[1] >> 3;
    result.battery = payload[1] & 7;
    return result.day != 0 && validScore(result.score);
}

inline void writeData(Frame& frame, Type type, uint32_t id,
                      const uint8_t (&payload)[resultBytes]) {
    frame.set(type, id, dataHeader + resultBytes);
    put(frame.bytes + header, 0, 2);
    std::memcpy(frame.bytes + dataHeader, payload, resultBytes);
}

struct Signal {
    int16_t minRssi = 0, maxRssi = 0;
    int8_t minSnr = 0, maxSnr = 0;
    int32_t sumRssi = 0, sumSnr = 0;

    void add(unsigned prior, int rssi, int snrQ4) {
        if (!prior || rssi < minRssi) minRssi = rssi;
        if (!prior || rssi > maxRssi) maxRssi = rssi;
        if (!prior || snrQ4 < minSnr) minSnr = snrQ4;
        if (!prior || snrQ4 > maxSnr) maxSnr = snrQ4;
        sumRssi += rssi;
        sumSnr += snrQ4;
    }
};

struct Delivery {
    uint32_t initial = 0, unique = 0, duplicates = 0, rejected = 0;
    Signal signal;

    void write(Frame& frame, uint32_t id) const {
        frame.set(Report, id, reportLen);
        put(frame.bytes + 5, initial, 3);
        put(frame.bytes + 8, unique, 3);
        put(frame.bytes + 11, duplicates, 3);
        put(frame.bytes + 14, rejected, 3);
        put(frame.bytes + 17, uint16_t(signal.minRssi), 2);
        put(frame.bytes + 19, uint16_t(signal.maxRssi), 2);
        frame.bytes[21] = uint8_t(signal.minSnr);
        frame.bytes[22] = uint8_t(signal.maxSnr);
        put(frame.bytes + 23, uint32_t(signal.sumRssi), 4);
        put(frame.bytes + 27, uint32_t(signal.sumSnr), 4);
    }

    bool read(const Frame& frame, unsigned requested) {
        initial = get(frame.bytes + 5, 3);
        unique = get(frame.bytes + 8, 3);
        duplicates = get(frame.bytes + 11, 3);
        rejected = get(frame.bytes + 14, 3);
        signal.minRssi = int16_t(get(frame.bytes + 17, 2));
        signal.maxRssi = int16_t(get(frame.bytes + 19, 2));
        signal.minSnr = int8_t(frame.bytes[21]);
        signal.maxSnr = int8_t(frame.bytes[22]);
        signal.sumRssi = int32_t(get(frame.bytes + 23, 4));
        signal.sumSnr = int32_t(get(frame.bytes + 27, 4));
        return initial <= unique && unique <= requested;
    }
};

struct SendCounts {
    uint32_t initial = 0, retries = 0, failures = 0, retryFailures = 0;
    uint64_t dataElapsedUs = 0;

    void write(Frame& frame, uint32_t id) const {
        frame.set(Finish, id, finishLen);
        put(frame.bytes + 5, initial, 4);
        put(frame.bytes + 9, retries, 4);
        put(frame.bytes + 13, failures, 4);
        put(frame.bytes + 17, retryFailures, 4);
        put(frame.bytes + 21, dataElapsedUs, 8);
    }
    bool read(const Frame& frame, unsigned requested) {
        initial = get(frame.bytes + 5, 4);
        retries = get(frame.bytes + 9, 4);
        failures = get(frame.bytes + 13, 4);
        retryFailures = get(frame.bytes + 17, 4);
        dataElapsedUs = get(frame.bytes + 21, 8);
        return initial <= requested && retries <= 15 * requested &&
            retryFailures <= retries && retryFailures <= failures &&
            failures - retryFailures <= initial;
    }
};
}
