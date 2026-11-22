/* Streaming 1 s log-mel front end; see logmel.h for the feature contract. */
#include "logmel.h"

#include <math.h>
#include <string.h>

#include "logmel_tables.h"

void logmel_init(logmel_stream_t *s, float in_scale, int32_t in_zero_point, int8_t *q_out, float *f_out) {
    memset(s, 0, sizeof(*s));
    s->in_scale = in_scale;
    s->in_zero_point = in_zero_point;
    s->q_out = q_out;
    s->f_out = f_out;
}

int8_t logmel_quantize(float value, float scale, int32_t zero_point) {
    /* Matches NumPy 2: float32 divide and add, then round half to even (the default FE_TONEAREST
     * mode used by nearbyintf). */
    const float q = nearbyintf(value / scale + (float)zero_point);
    return (int8_t)(q < -128.0f ? -128 : (q > 127.0f ? 127 : (int)q));
}

static unsigned bit_reverse9(unsigned v) {
    unsigned r = 0;
    for (int i = 0; i < 9; ++i) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

/* In-place iterative radix-2 complex FFT, forward (e^{-i...}), 512 points. On the ESP32 this can be
 * replaced by esp-dsp's dsps_fft2r_fc32 (or a real-input FFT) once parity is re-checked. */
static void fft512(logmel_real *re, logmel_real *im) {
    for (unsigned size = 2; size <= LOGMEL_FFT; size <<= 1) {
        const unsigned half = size >> 1, step = LOGMEL_FFT / size;
        for (unsigned start = 0; start < LOGMEL_FFT; start += size) {
            for (unsigned k = 0; k < half; ++k) {
                const logmel_real wr = LOGMEL_COS[k * step], wi = -LOGMEL_SIN[k * step];
                const unsigned j = start + k, l = j + half;
                const logmel_real tr = wr * re[l] - wi * im[l];
                const logmel_real ti = wr * im[l] + wi * re[l];
                re[l] = re[j] - tr;
                im[l] = im[j] - ti;
                re[j] += tr;
                im[j] += ti;
            }
        }
    }
}

static void compute_frame(logmel_stream_t *s) {
    const uint32_t base = s->frame * LOGMEL_HOP;
    for (unsigned j = 0; j < LOGMEL_FFT; ++j) {
        const logmel_real x = (logmel_real)s->ring[(base + j) & (LOGMEL_FFT - 1)] * (logmel_real)(1.0 / 32768.0);
        const unsigned r = bit_reverse9(j);
        s->re[r] = x * (logmel_real)LOGMEL_WINDOW[j];
        s->im[r] = 0;
    }
    fft512(s->re, s->im);
    for (unsigned k = 0; k < LOGMEL_BINS; ++k)
        s->power[k] = (s->re[k] * s->re[k] + s->im[k] * s->im[k]) / (logmel_real)LOGMEL_FFT;
    const size_t row = (size_t)s->frame * LOGMEL_BANDS;
    for (unsigned b = 0; b < LOGMEL_BANDS; ++b) {
        const float *w = LOGMEL_MEL_W + LOGMEL_BAND_OFFSET[b];
        const logmel_real *p = s->power + LOGMEL_BAND_START[b];
        logmel_real acc = 0;
        for (unsigned i = 0; i < LOGMEL_BAND_LEN[b]; ++i) acc += (logmel_real)w[i] * p[i];
        const logmel_real floor_value = (logmel_real)1e-10;
#ifdef LOGMEL_USE_LOGF  /* single-precision log: much cheaper on the ESP32 (no soft-float double) */
        const float v = logf((float)(acc > floor_value ? acc : floor_value));
#else
        const float v = (float)log(acc > floor_value ? acc : floor_value);
#endif
        if (s->f_out) s->f_out[row + b] = v;
        s->q_out[row + b] = logmel_quantize(v, s->in_scale, s->in_zero_point);
    }
}

size_t logmel_push(logmel_stream_t *s, const int16_t *pcm, size_t n, int *window_ready) {
    *window_ready = 0;
    for (size_t i = 0; i < n; ++i) {
        s->ring[s->pos & (LOGMEL_FFT - 1)] = pcm[i];
        ++s->pos;
        if (s->frame < LOGMEL_FRAMES && s->pos == s->frame * LOGMEL_HOP + LOGMEL_FFT) {
            compute_frame(s);
            ++s->frame;
        }
        if (s->pos == LOGMEL_WINDOW_SAMPLES) { /* samples 7936..7999 belong to no frame, as in Python */
            s->pos = 0;
            s->frame = 0;
            *window_ready = 1;
            return i + 1;
        }
    }
    return n;
}
