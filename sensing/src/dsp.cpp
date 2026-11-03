#include "pipeline.h"
#include "dsp_tables.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Expose the native workspace size so host tests allocate it with correct alignment.
size_t dsp_workspace_bytes() { return sizeof(Workspace); }

// Advance one sample through the four transposed-direct-form SOS sections.
static double step(double value, double state[4][2]) {
    for (int s = 0; s < 4; ++s) {
        double next = SOS[s][0] * value + state[s][0];
        state[s][0] = SOS[s][1] * value - SOS[s][4] * next + state[s][1];
        state[s][1] = SOS[s][2] * value - SOS[s][5] * next;
        value = next;
    }
    return value;
}

// Match SciPy steady-state initialization using the first sample of the pass.
static void initial_state(double state[4][2], double first) {
    for (int s = 0; s < 4; ++s)
        for (int j = 0; j < 2; ++j) state[s][j] = ZI[s][j] * first;
}

// Read chunked PCM, remove the recording bias, and convert to normalized units.
static double sample(const int16_t *const *pcm, int index, double mean, double divisor) {
    return (double(pcm[index / PCM_BLOCK][index % PCM_BLOCK]) - mean) / divisor;
}

// Synthesize the odd-reflected edge samples used by reference sosfiltfilt.
static double extended(const int16_t *const *pcm, int index, double mean, double divisor) {
    if (index < FILTER_PAD)
        return 2 * sample(pcm, 0, mean, divisor) - sample(pcm, FILTER_PAD-index, mean, divisor);
    index -= FILTER_PAD;
    if (index >= SAMPLE_COUNT)
        return 2 * sample(pcm, SAMPLE_COUNT-1, mean, divisor) - sample(pcm, 2*SAMPLE_COUNT-2-index, mean, divisor);
    return sample(pcm, index, mean, divisor);
}

// Convert one complete Hann-windowed frame into 26 log-power Mel energies.
static void spectrum(Workspace *w, int start, double energies[26]) {
    for (int i = 0; i < 256; ++i) {
        w->real[i] = i < 200 ? w->ring[(start+i)%200] * WINDOW[i] : 0;
        w->imaginary[i] = 0;
    }
    // Bit-reversal ordering prepares the in-place radix-2 FFT.
    for (unsigned i = 1, j = 0; i < 256; ++i) {
        unsigned bit = 128;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(w->real[i], w->real[j]);
    }
    // Only the fixed 256-point complex FFT needed for this MFCC is implemented.
    for (int length = 2; length <= 256; length *= 2)
        for (int base = 0; base < 256; base += length)
            for (int j = 0; j < length/2; ++j) {
                int a = base+j, b = a+length/2, k = j*256/length;
                double real = COS[k]*w->real[b] - SIN[k]*w->imaginary[b];
                double imaginary = SIN[k]*w->real[b] + COS[k]*w->imaginary[b];
                w->real[b] = w->real[a]-real;
                w->imaginary[b] = w->imaginary[a]-imaginary;
                w->real[a] += real;
                w->imaginary[a] += imaginary;
            }
    for (int k = 0; k <= 128; ++k)
        w->real[k] = w->real[k]*w->real[k] + w->imaginary[k]*w->imaginary[k];
    for (int m = 0; m < 26; ++m) {
        double energy = 0;
        for (int k = 0; k <= 128; ++k) energy += MEL[m][k] * w->real[k];
        energies[m] = 10 * std::log10(std::max(energy, 1e-10));
    }
}

// Filter the full capture, average MFCCs into 15 bins, then quantize all 195 values.
int compute_mfcc(const int16_t *const *pcm, double divisor, Workspace *w,
                 double *features, int8_t *q, float *scale, double *filtered,
                 void (*cooperate)()) {
    if (!pcm || !w || !features || !q || !scale || !std::isfinite(divisor) || divisor <= 0) return INVALID_FEATURES;
    for (int i = 0; i < 4; ++i) if (!pcm[i]) return INVALID_FEATURES;
    int64_t total = 0;
    for (int i = 0; i < SAMPLE_COUNT; ++i) total += pcm[i/PCM_BLOCK][i%PCM_BLOCK];
    double mean = double(total) / SAMPLE_COUNT;
    double state[4][2];
    initial_state(state, extended(pcm, 0, mean, divisor));
    double last = 0;
    // Checkpoints replay the forward filter a block at a time: no 320KB float waveform.
    for (int j = 0; j < EXTENDED_COUNT; ++j) {
        if (j % FILTER_BLOCK == 0) {
            std::memcpy(w->checkpoints[j/FILTER_BLOCK], state, sizeof(state));
            if (cooperate) cooperate();
        }
        last = step(extended(pcm, j, mean, divisor), state);
        if (!std::isfinite(last)) return INVALID_FEATURES;
    }
    std::memset(w->sums, 0, sizeof(w->sums));
    std::memset(w->counts, 0, sizeof(w->counts));
    double maximum_db = -100;
    // First traversal finds the global 80dB log floor; second aggregates exact bins.
    for (int pass = 0; pass < 2; ++pass) {
        // The reverse filter starts from the final forward-filtered sample.
        initial_state(state, last);
        for (int block = CHECKPOINTS-1; block >= 0; --block) {
            int start = block*FILTER_BLOCK;
            int length = std::min(FILTER_BLOCK, EXTENDED_COUNT-start);
            double forward[4][2];
            // Replay only this forward block before processing its samples backward.
            std::memcpy(forward, w->checkpoints[block], sizeof(forward));
            for (int j = 0; j < length; ++j)
                w->block[j] = step(extended(pcm, start+j, mean, divisor), forward);
            for (int j = length-1; j >= 0; --j) {
                double value = step(w->block[j], state);
                if (!std::isfinite(value)) return INVALID_FEATURES;
                // Strip the virtual filter padding before handling real capture samples.
                int i = start+j-FILTER_PAD;
                if (i < 0 || i >= SAMPLE_COUNT) continue;
                if (filtered && pass == 1) filtered[i] = value;
                w->ring[i%200] = value;
                // Analyze only complete 200-sample windows on the 80-sample hop grid.
                if (i > SAMPLE_COUNT-200 || i%80 != 0) continue;
                double logs[26];
                spectrum(w, i, logs);
                if (pass == 0) {
                    for (double x : logs) {
                        if (!std::isfinite(x)) return INVALID_FEATURES;
                        if (x > maximum_db) maximum_db = x;
                    }
                } else {
                    // Bin by window center while retaining chronological output rows.
                    int bin = ((i+100)*15) / SAMPLE_COUNT;
                    ++w->counts[bin];
                    for (int c = 0; c < 13; ++c) {
                        double feature = 0;
                        for (int m = 0; m < 26; ++m) feature += DCT[c][m] * std::max(logs[m], maximum_db-80);
                        w->sums[bin][c] += feature;
                    }
                }
            }
            if (cooperate) cooperate();
        }
    }
    for (int bin = 0; bin < 15; ++bin) {
        if (w->counts[bin] == 0) return INVALID_FEATURES;
        for (int c = 0; c < 13; ++c) features[bin*13+c] = w->sums[bin][c] / w->counts[bin];
    }
    return quantize_features(features, q, scale);
}
