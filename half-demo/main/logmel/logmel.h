/*
 * Streaming 1 s log-mel front end for the stream1s_w8 TreeVibes model (portable C99).
 *
 * Reproduces scripts/grouped_cv_campaign.py window_features() log-mel for one 8000-sample window
 * at 8 kHz: 59 frames at hop 128, periodic Hann 512, |rFFT|^2 / 512, 64 HTK mel bands, natural log
 * floored at 1e-10. Samples are int16 PCM scaled by 1/32768 (soundfile's PCM_16 convention).
 * The model input is int8: q = round_half_even(logmel / scale + zero_point), saturated, which is
 * the export's float32 NumPy arithmetic; the Keras graph already folds training normalisation.
 *
 * Memory: no 1 s or 10 s audio buffer. State keeps a 512-sample int16 ring (1 KiB), FFT scratch
 * (4 KiB) and a 257-bin power row (~1 KiB); the int8 output (59 x 64 = 3,776 B) can point straight
 * at the TFLM input tensor. Constant tables (window, twiddles, sparse mel weights) are in flash.
 */
#ifndef LOGMEL_H
#define LOGMEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOGMEL_SAMPLE_RATE 8000
#define LOGMEL_WINDOW_SAMPLES 8000
#define LOGMEL_FFT 512
#define LOGMEL_HOP 128
#define LOGMEL_FRAMES 59
#define LOGMEL_BANDS 64
#define LOGMEL_BINS (LOGMEL_FFT / 2 + 1)
#define LOGMEL_OUTPUT_COUNT (LOGMEL_FRAMES * LOGMEL_BANDS)

/* Arithmetic type; float on the ESP32 (single-precision FPU). -DLOGMEL_REAL=double is for host
 * diagnosis only. */
#ifndef LOGMEL_REAL
#define LOGMEL_REAL float
#endif
typedef LOGMEL_REAL logmel_real;

typedef struct {
    int16_t ring[LOGMEL_FFT];      /* last 512 samples of the current 1 s window */
    uint32_t pos;                  /* samples consumed in the current window, 0..7999 */
    uint32_t frame;                /* next frame to compute, 0..59 */
    float in_scale;                /* model input quantisation scale */
    int32_t in_zero_point;         /* model input zero point */
    int8_t *q_out;                 /* LOGMEL_OUTPUT_COUNT int8 values (row-major frame x band) */
    float *f_out;                  /* optional LOGMEL_OUTPUT_COUNT floats; NULL to skip */
    logmel_real re[LOGMEL_FFT];    /* FFT scratch */
    logmel_real im[LOGMEL_FFT];
    logmel_real power[LOGMEL_BINS];
} logmel_stream_t;

/* q_out must hold LOGMEL_OUTPUT_COUNT bytes; f_out may be NULL. */
void logmel_init(logmel_stream_t *s, float in_scale, int32_t in_zero_point, int8_t *q_out, float *f_out);

/* Feed up to n samples. Frames are computed as soon as their 512 samples have arrived. Returns the
 * number of samples consumed; it stops early and sets *window_ready = 1 when the 8000th sample of a
 * window is consumed, so the caller can run inference before outputs are overwritten. */
size_t logmel_push(logmel_stream_t *s, const int16_t *pcm, size_t n, int *window_ready);

/* Saturating int8 quantisation used for every output value (exposed for tests). */
int8_t logmel_quantize(float value, float scale, int32_t zero_point);

#ifdef __cplusplus
}
#endif

#endif
