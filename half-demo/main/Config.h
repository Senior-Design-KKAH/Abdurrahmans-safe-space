#pragma once
#include <stdint.h>
// CMake sets NODE_ID from -DDEMO_NODE_ID=1 (sender) or 0 (head).
#ifndef NODE_ID
#define NODE_ID 1
#endif
static_assert(NODE_ID == 0 || NODE_ID == 1, "Demo roles: head=0, sender=1");
// Optional sender-only bench build; the normal integration leaves this disabled.
#ifndef DEMO_ML_ONLY
#define DEMO_ML_ONLY 0
#endif
static_assert(!DEMO_ML_ONLY || NODE_ID == 1, "ML-only test requires the sender");
namespace cfg {
constexpr long Frequency=433000000, Bandwidth=125000;
constexpr uint8_t SF=7, CodingRate=5, SyncWord=0x34, Power=10;
constexpr uint16_t Preamble=8;
constexpr int SCK=18, MISO=19, MOSI=23, CS=27, RESET=14, DIO0=26;
constexpr unsigned Guard=30, Attempts=5, QueueCapacity=32, StorageRecords=256;
constexpr uint32_t BatchTimeout=120000;
// Standalone radio test only: ten diagnostic packets, one-second gap.
constexpr uint32_t RadioTestSPI=1000000;
constexpr unsigned RadioTestPackets=10, RadioTestGapMs=1000;
// Manually set day-of-month before the bench run. USB battery is SIMULATED.
constexpr uint8_t BenchDay=3;
constexpr float BenchBatteryPercent=100.0f;
static_assert(BenchDay>=1 && BenchDay<=31, "Set a valid bench day");
}
// Copy config.local.h.example to config.local.h; never commit credentials.
#if __has_include("config.local.h")
#include "config.local.h"
#else
#define DEMO_WIFI_SSID "YOUR_WIFI_SSID"
#define DEMO_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define DEMO_SERVER_URL "http://192.168.1.100:8080/results"
#endif
