#pragma once
#include <stdint.h>

namespace profile {
constexpr int sck = 18, miso = 19, mosi = 23, nss = 27, reset = 14, dio0 = 26;
constexpr uint32_t spiHz = 4000000;
constexpr uint32_t frequencyHz = 433920000;
constexpr uint32_t bandwidthHz = 125000;
constexpr int txDbm = 10;
constexpr int spreadingFactor = 7;
constexpr int codingRate = 5;
constexpr uint16_t preamble = 8;
constexpr uint8_t syncWord = 0x12;
constexpr int dutyPermille = 100;

static_assert(spreadingFactor >= 7 && spreadingFactor <= 9, "Supported SF: 7 through 9");
static_assert(bandwidthHz == 125000, "Experiments require 125 kHz bandwidth");
static_assert(txDbm == 10, "Experiments require 10 dBm transmit power");
static_assert(codingRate >= 5 && codingRate <= 8, "Coding rate is 4/5 through 4/8");
static_assert(preamble >= 8 && preamble <= 32, "Use a preamble of 8 to 32 symbols");
static_assert(frequencyHz >= 410000000 && frequencyHz <= 525000000, "Outside XL1278-SMT band");
static_assert(dutyPermille >= 10 && dutyPermille <= 1000, "Duty pacing: 1% through 100%");

constexpr uint32_t symbolUs = (1000000ULL << spreadingFactor) / bandwidthHz;
constexpr uint64_t turnaroundUs = 2 * symbolUs + 2000;
constexpr uint64_t dataGapUs = 2000;
inline unsigned payloadGroups(unsigned bytes) {
    const unsigned numerator = 8 * bytes + 28 + 16 - 4 * spreadingFactor;
    const unsigned denominator = 4 * spreadingFactor;
    return (numerator + denominator - 1) / denominator;
}
inline uint64_t airUs(unsigned bytes) {
    return uint64_t(4 * (preamble + 8 + codingRate * payloadGroups(bytes)) + 17)
        * symbolUs / 4;
}
constexpr uint64_t pacedInterval(uint64_t air) {
    return (air * 1000 + dutyPermille - 1) / dutyPermille;
}
}
