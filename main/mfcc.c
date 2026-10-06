#include "mfcc.h"
#include "signal_processing.h"
#include <math.h>
#include <string.h>

static const float pi = 3.14159265358979323846f;

esp_err_t mfcc_init(mfcc_context_t *context)
{
    if (!context || MFCC_MEL_LOW_HZ < 0.0f || MFCC_MEL_HIGH_HZ > ACOUSTIC_SAMPLE_RATE_HZ / 2.0f ||
        MFCC_MEL_LOW_HZ >= MFCC_MEL_HIGH_HZ) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(context, 0, sizeof(*context));
    for (size_t n = 0; n < MFCC_FRAME_LENGTH; ++n) {
        context->window[n] = 0.54f - 0.46f * cosf(2.0f * pi * n / (MFCC_FRAME_LENGTH - 1));
    }
    for (size_t k = 0; k < MFCC_FFT_SIZE / 2; ++k) {
        float angle = -2.0f * pi * k / MFCC_FFT_SIZE;
        context->twiddle_real[k] = cosf(angle);
        context->twiddle_imaginary[k] = sinf(angle);
    }
    /* Space triangular filter edges evenly on the HTK Mel scale. */
    float low = 2595.0f * log10f(1.0f + MFCC_MEL_LOW_HZ / 700.0f);
    float high = 2595.0f * log10f(1.0f + MFCC_MEL_HIGH_HZ / 700.0f);
    for (size_t m = 0; m < MFCC_MEL_FILTERS + 2; ++m) {
        float mel = low + (high - low) * m / (MFCC_MEL_FILTERS + 1);
        context->mel_edges[m] = 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
    }
    /* Orthonormal DCT-II includes coefficient zero with its own normalization. */
    for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
        float normalization = sqrtf((c == 0 ? 1.0f : 2.0f) / MFCC_MEL_FILTERS);
        for (size_t m = 0; m < MFCC_MEL_FILTERS; ++m) {
            context->dct[c][m] = normalization * cosf(pi * c * (m + 0.5f) / MFCC_MEL_FILTERS);
        }
    }
    return ESP_OK;
}

/* In-place radix-2 FFT, using precomputed complex rotation factors. */
static void fft(mfcc_context_t *context)
{
    /* Bit-reversed ordering prepares the input for successive butterfly stages. */
    for (size_t i = 1, j = 0; i < MFCC_FFT_SIZE; ++i) {
        size_t bit = MFCC_FFT_SIZE >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float temp = context->real[i];
            context->real[i] = context->real[j];
            context->real[j] = temp;
            temp = context->imaginary[i];
            context->imaginary[i] = context->imaginary[j];
            context->imaginary[j] = temp;
        }
    }
    for (size_t length = 2; length <= MFCC_FFT_SIZE; length <<= 1) {
        size_t half = length / 2;
        size_t stride = MFCC_FFT_SIZE / length;
        for (size_t start = 0; start < MFCC_FFT_SIZE; start += length) {
            for (size_t j = 0; j < half; ++j) {
                size_t a = start + j;
                size_t b = a + half;
                float wr = context->twiddle_real[j * stride];
                float wi = context->twiddle_imaginary[j * stride];
                float tr = wr * context->real[b] - wi * context->imaginary[b];
                float ti = wr * context->imaginary[b] + wi * context->real[b];
                context->real[b] = context->real[a] - tr;
                context->imaginary[b] = context->imaginary[a] - ti;
                context->real[a] += tr;
                context->imaginary[a] += ti;
            }
        }
    }
}

static esp_err_t compute_ring(mfcc_context_t *context, const float *frame, size_t oldest,
                               float coefficients[MFCC_COEFFICIENTS])
{
    /* Read circular history chronologically and apply the Hamming window. */
    for (size_t n = 0; n < MFCC_FRAME_LENGTH; ++n) {
        float sample = frame[(oldest + n) % MFCC_FRAME_LENGTH];
        if (!isfinite(sample)) {
            return ESP_ERR_INVALID_ARG;
        }
        context->real[n] = sample * context->window[n];
    }
    /* Zero-pad to the FFT size and start with a purely real input. */
    memset(context->real + MFCC_FRAME_LENGTH, 0,
           (MFCC_FFT_SIZE - MFCC_FRAME_LENGTH) * sizeof(float));
    memset(context->imaginary, 0, sizeof(context->imaginary));
    fft(context);
    /* Retain the nonnegative-frequency power spectrum for the Mel filters. */
    for (size_t k = 0; k <= MFCC_FFT_SIZE / 2; ++k) {
        float re = context->real[k];
        float im = context->imaginary[k];
        context->spectrum[k] = (re * re + im * im) / MFCC_FFT_SIZE;
    }
    /* Sum triangular Mel bands, then floor energies before taking natural logs. */
    float log_energy[MFCC_MEL_FILTERS];
    for (size_t m = 0; m < MFCC_MEL_FILTERS; ++m) {
        float left = context->mel_edges[m];
        float center = context->mel_edges[m + 1];
        float right = context->mel_edges[m + 2];
        float energy = 0.0f;
        for (size_t k = 0; k <= MFCC_FFT_SIZE / 2; ++k) {
            float frequency = (float)k * ACOUSTIC_SAMPLE_RATE_HZ / MFCC_FFT_SIZE;
            if (frequency > left && frequency < right) {
                float weight = frequency <= center ? (frequency - left) / (center - left) :
                                                     (right - frequency) / (right - center);
                energy += weight * context->spectrum[k];
            }
        }
        log_energy[m] = logf(fmaxf(energy, MFCC_LOG_FLOOR));
        if (!isfinite(log_energy[m])) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    /* Project log band energies onto the first 13 DCT-II coefficients. */
    for (size_t c = 0; c < MFCC_COEFFICIENTS; ++c) {
        float value = 0.0f;
        for (size_t m = 0; m < MFCC_MEL_FILTERS; ++m) {
            value += context->dct[c][m] * log_energy[m];
        }
        if (!isfinite(value)) {
            return ESP_ERR_INVALID_STATE;
        }
        coefficients[c] = value;
    }
    return ESP_OK;
}

esp_err_t mfcc_compute_frame(mfcc_context_t *context, const float *frame,
                             float coefficients[MFCC_COEFFICIENTS])
{
    if (!context || !frame || !coefficients) {
        return ESP_ERR_INVALID_ARG;
    }
    return compute_ring(context, frame, 0, coefficients);
}

size_t mfcc_representative_index(size_t group)
{
    if (group >= MFCC_RETAINED_FRAMES) {
        return SIZE_MAX;
    }
    /* Integer boundaries partition every complete frame into contiguous groups. */
    size_t start = group * MFCC_FULL_FRAME_COUNT / MFCC_RETAINED_FRAMES;
    size_t end = (group + 1) * MFCC_FULL_FRAME_COUNT / MFCC_RETAINED_FRAMES;
    return start + (end - start - 1) / 2;
}

esp_err_t mfcc_reduce_capture(mfcc_context_t *context, const uint16_t *raw,
                              size_t count, float dc_midpoint,
                              float output[MFCC_RETAINED_FRAMES][MFCC_COEFFICIENTS])
{
    if (!context || !raw || !output || count != ACOUSTIC_SAMPLE_COUNT || !isfinite(dc_midpoint)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Reset filtering so this pass matches the one used for SNR. */
    bandpass_context_t filter;
    bandpass_reset(&filter);
    size_t retained = 0;
    size_t next = mfcc_representative_index(0);
    for (size_t n = 0; n < count; ++n) {
        float centered = ((float)raw[n] - dc_midpoint) / 2048.0f;
        context->ring[n % MFCC_FRAME_LENGTH] = bandpass_process(&filter, centered);
        /* Only complete frames on the configured step can be selected. */
        if (n + 1 >= MFCC_FRAME_LENGTH && (n + 1 - MFCC_FRAME_LENGTH) % MFCC_FRAME_STEP == 0) {
            size_t frame_index = (n + 1 - MFCC_FRAME_LENGTH) / MFCC_FRAME_STEP;
            /* Unselected frames need no FFT; the selected coefficients are identical. */
            if (retained < MFCC_RETAINED_FRAMES && frame_index == next) {
                esp_err_t err = compute_ring(context, context->ring, (n + 1) % MFCC_FRAME_LENGTH,
                                               output[retained]);
                if (err != ESP_OK) {
                    return err;
                }
                next = mfcc_representative_index(++retained);
            }
        }
    }
    return retained == MFCC_RETAINED_FRAMES ? ESP_OK : ESP_ERR_INVALID_STATE;
}
