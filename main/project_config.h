#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

/* The processing stream contains exactly 10 seconds at nominal 8 kHz. */
#define ACOUSTIC_SAMPLE_RATE_HZ 8000
#define ACOUSTIC_DURATION_SECONDS 10
#define ACOUSTIC_SAMPLE_COUNT (ACOUSTIC_SAMPLE_RATE_HZ * ACOUSTIC_DURATION_SECONDS)
/* Connect the MCP6002 output to ADC1 channel 6 (GPIO34) by default. */
#define ADC_CAPTURE_UNIT ADC_UNIT_1
#define ADC_CAPTURE_CHANNEL ADC_CHANNEL_6
#define ADC_CAPTURE_ATTENUATION ADC_ATTEN_DB_12
#define ADC_CAPTURE_BIT_WIDTH 12
/* ESP32 continuous ADC requires at least 20 kHz; downsample 32 kHz to 8 kHz. */
#define ADC_CAPTURE_RATE_HZ 32000
#define ADC_DECIMATION_FACTOR (ADC_CAPTURE_RATE_HZ / ACOUSTIC_SAMPLE_RATE_HZ)
#define ADC_DECIMATION_TAPS 127
#define ADC_DECIMATION_CUTOFF_HZ 3200.0f
#define ADC_DMA_FRAME_BYTES 1024
#define ADC_DMA_POOL_BYTES 16384
#define ADC_READ_TIMEOUT_MS 1000

/* Smooth power over 100 ms; disable gating to process every valid capture. */
#define POWER_WINDOW_SAMPLES (ACOUSTIC_SAMPLE_RATE_HZ / 10)
#define POWER_FLOOR 1.0e-12f
#define ENABLE_SNR_GATE 0
#define SNR_THRESHOLD_DB 4.0f
#define NODE_ID 1

/* Sample counts: 25 ms frames and 10 ms steps at 8 kHz. */
#define MFCC_FRAME_LENGTH 200
#define MFCC_FRAME_STEP 80
#define MFCC_FFT_SIZE 256
#define MFCC_MEL_FILTERS 26
#define MFCC_COEFFICIENTS 13
#define MFCC_RETAINED_FRAMES 15
#define MFCC_MEL_LOW_HZ 100.0f
#define MFCC_MEL_HIGH_HZ 2500.0f
#define MFCC_LOG_FLOOR 1.0e-12f
#define MFCC_FULL_FRAME_COUNT (1 + (ACOUSTIC_SAMPLE_COUNT - MFCC_FRAME_LENGTH) / MFCC_FRAME_STEP)

/* Wire sizes include all 182 deltas, exception records, and the final CRC. */
#define MFCC_DELTA_COUNT ((MFCC_RETAINED_FRAMES - 1) * MFCC_COEFFICIENTS)
#define MFCC_PACKED_DELTA_BYTES ((MFCC_DELTA_COUNT + 1) / 2)
#define PAYLOAD_HEADER_BYTES (15 + MFCC_COEFFICIENTS)
#define PAYLOAD_CRC_BYTES 2
#define PAYLOAD_FIXED_BYTES (PAYLOAD_HEADER_BYTES + MFCC_PACKED_DELTA_BYTES + PAYLOAD_CRC_BYTES)
#define PAYLOAD_MAX_BYTES (PAYLOAD_FIXED_BYTES + 2 * MFCC_DELTA_COUNT)
#define PAYLOAD_VERSION 1
#define PAYLOAD_FLAG_SNR_PASSED 0x01u
#define PAYLOAD_FLAG_EXCEPTIONS 0x02u

/* Reject configuration changes that invalidate the filter or packet format. */
_Static_assert(ACOUSTIC_SAMPLE_RATE_HZ == 8000, "Bandpass coefficients require 8 kHz");
_Static_assert(ACOUSTIC_SAMPLE_COUNT == 80000, "Capture must contain 80000 samples");
_Static_assert(ADC_CAPTURE_RATE_HZ % ACOUSTIC_SAMPLE_RATE_HZ == 0, "Integer decimation required");
_Static_assert(ADC_DECIMATION_FACTOR >= 1, "Invalid ADC rate");
_Static_assert(ADC_DECIMATION_TAPS > 1 && ADC_DECIMATION_TAPS % 2 == 1, "FIR must have odd length");
_Static_assert(MFCC_FRAME_LENGTH > 1 && MFCC_FRAME_LENGTH <= MFCC_FFT_SIZE, "Invalid frame length");
_Static_assert((MFCC_FFT_SIZE & (MFCC_FFT_SIZE - 1)) == 0, "FFT size must be a power of two");
_Static_assert(MFCC_FRAME_STEP > 0 && MFCC_FULL_FRAME_COUNT >= MFCC_RETAINED_FRAMES, "Invalid frame step");
_Static_assert(MFCC_MEL_FILTERS >= MFCC_COEFFICIENTS, "Insufficient Mel filters");
_Static_assert(MFCC_RETAINED_FRAMES == 15 && MFCC_COEFFICIENTS == 13, "Protocol requires 15 x 13");
_Static_assert(MFCC_DELTA_COUNT == 182 && MFCC_PACKED_DELTA_BYTES == 91, "Invalid delta body");
_Static_assert(PAYLOAD_HEADER_BYTES == 28 && PAYLOAD_FIXED_BYTES == 121, "Invalid packet layout");
_Static_assert(PAYLOAD_MAX_BYTES == 485, "Invalid worst-case capacity");

#endif
