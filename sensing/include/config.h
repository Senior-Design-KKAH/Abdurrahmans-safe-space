#pragma once
#include <stdint.h>

// Safe default: synthetic waveform, serial output only, capture on command 'c'.
constexpr bool BENCH_MODE = false;
constexpr bool RADIO_ENABLED = false;
constexpr bool ALLOW_SYNTHETIC_RF = false;

// Supply the actual ADC1 GPIO and operational codes before disabling bench mode.
constexpr int ADC_GPIO = 34;
constexpr bool ADC_SWAP_ADJACENT = true; // Classic ESP32 16-bit mono RX word order; verify with a ramp on the bench.
// Caller-supplied unsigned header codes for this node.
constexpr uint32_t NODE_ID = 187;   // Unsigned 0..255 on the wire.
constexpr uint32_t DATE_CODE = 2;   // Unsigned opaque 0..31 code; calendar mapping is unassigned.
constexpr uint32_t BATTERY_CODE = 7; // Unsigned opaque 0..7 code; not a percentage.

// No wiring or regional RF settings have been inferred from the dataset.
constexpr int LORA_SCK = -1;
constexpr int LORA_MISO = -1;
constexpr int LORA_MOSI = -1;
constexpr int LORA_CS = -1;
constexpr int LORA_RESET = -1;
constexpr int LORA_DIO0 = -1;
constexpr long LORA_FREQUENCY_HZ = 0;
constexpr long LORA_BANDWIDTH_HZ = 0;
constexpr int LORA_SPREADING_FACTOR = 0;
constexpr int LORA_CODING_RATE_DENOMINATOR = 0;
constexpr int LORA_TX_DBM = -100;
constexpr int LORA_SYNC_WORD = -1;
constexpr int LORA_PREAMBLE_SYMBOLS = 0;
constexpr uint32_t TX_TIMEOUT_MS = 30000;

