#include "signal_processing.h"
#include <float.h>
#include <math.h>
#include <string.h>

/* Butterworth HP 100 Hz then LP 2500 Hz at 8 kHz; b0, b1, b2, a1, a2. */
static const float bandpass_coefficients[2][5] = {
    {0.9459768560f, -1.8919537120f, 0.9459768560f, -1.8890330794f, 0.8948743446f},
    {0.4181633458f, 0.8363266915f, 0.4181633458f, 0.4629380253f, 0.2097153578f}
};

void bandpass_reset(bandpass_context_t *context)
{
    memset(context, 0, sizeof(*context));
}

float bandpass_process(bandpass_context_t *context, float sample)
{
    /* Transposed direct form II uses two state values per section. */
    for (size_t section = 0; section < 2; ++section) {
        const float *b = bandpass_coefficients[section];
        float output = b[0] * sample + context->z1[section];
        context->z1[section] = b[1] * sample - b[3] * output + context->z2[section];
        context->z2[section] = b[2] * sample - b[4] * output;
        sample = output;
    }
    return sample;
}

void power_reset(power_context_t *context)
{
    memset(context, 0, sizeof(*context));
    context->minimum = FLT_MAX;
}

void power_process(power_context_t *context, float filtered_sample)
{
    float power = filtered_sample * filtered_sample;
    /* Replace the oldest power value in constant time; double limits drift. */
    context->sum += (double)power - context->power[context->position];
    context->power[context->position] = power;
    context->position = (context->position + 1) % POWER_WINDOW_SAMPLES;
    ++context->samples;
    /* Exclude partial startup windows from the SNR statistics. */
    if (context->samples >= POWER_WINDOW_SAMPLES) {
        float envelope = (float)fmax(context->sum / POWER_WINDOW_SAMPLES, 0.0);
        context->envelope_sum += envelope;
        context->minimum = fminf(context->minimum, envelope);
        ++context->envelopes;
    }
}

esp_err_t power_snr_db(const power_context_t *context, float *snr_db)
{
    if (!context || !snr_db) {
        return ESP_ERR_INVALID_ARG;
    }
    if (context->envelopes == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The quietest full window is the noise baseline; the floor handles silence. */
    float mean = (float)(context->envelope_sum / context->envelopes);
    *snr_db = 10.0f * log10f(fmaxf(mean, POWER_FLOOR) /
                             fmaxf(context->minimum, POWER_FLOOR));
    return isfinite(*snr_db) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t signal_measure_snr(power_context_t *context, const uint16_t *raw,
                             size_t count, float dc_midpoint, float *snr_db)
{
    if (!context || !raw || !snr_db || count != ACOUSTIC_SAMPLE_COUNT || !isfinite(dc_midpoint)) {
        return ESP_ERR_INVALID_ARG;
    }
    bandpass_context_t filter;
    bandpass_reset(&filter);
    power_reset(context);
    for (size_t i = 0; i < count; ++i) {
        /* Convert unsigned 12-bit counts to a signed processing sample. */
        float centered = ((float)raw[i] - dc_midpoint) / 2048.0f;
        power_process(context, bandpass_process(&filter, centered));
    }
    return power_snr_db(context, snr_db);
}
