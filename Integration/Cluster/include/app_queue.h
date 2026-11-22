// RAM buffer shared by the LoRa receiver and the uplink task.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ram_queue.h"

constexpr size_t kQueueRecords = 512;  // about 100 bytes of RAM each
constexpr int64_t kClockSetAfterS = 1704067200;  // 2024-01-01: anything earlier means no NTP yet

using AppQueue = nakhel::RamQueue<kQueueRecords>;
extern AppQueue uplinkQueue;
