#pragma once
#include <stddef.h>
#include <stdint.h>

constexpr int SAMPLE_COUNT = 80000;
constexpr int PCM_BLOCK = 20000;
constexpr int FILTER_BLOCK = 1024;
constexpr int FILTER_PAD = 27;
constexpr int EXTENDED_COUNT = SAMPLE_COUNT + 2 * FILTER_PAD;
constexpr int CHECKPOINTS = (EXTENDED_COUNT + FILTER_BLOCK - 1) / FILTER_BLOCK;
constexpr size_t RADIO_CAPACITY = 255;

enum Status { PIPELINE_OK = 0, INVALID_FEATURES = 1, INVALID_SCALE = 2,
              INVALID_HEADER = 3, BUFFER_OVERFLOW = 4, PAYLOAD_TOO_LARGE = 5 };

// Reuse bounded scratch buffers across filter replay and both MFCC traversals.
struct Workspace {
    double checkpoints[CHECKPOINTS][4][2];
    double block[FILTER_BLOCK];
    double ring[200];
    double real[256], imaginary[256];
    double sums[15][13];
    int counts[15];
};

extern "C" {
size_t dsp_workspace_bytes();
int quantize_features(const double *features, int8_t *q, float *scale);
int make_payload(const int8_t *q, float scale, uint32_t node, uint32_t date, uint32_t battery,
                 uint8_t *out, size_t capacity, size_t *required, uint8_t *selected);
int compute_mfcc(const int16_t *const *pcm, double divisor, Workspace *work,
                 double *features, int8_t *q, float *scale, double *filtered,
                 void (*cooperate)());
}

