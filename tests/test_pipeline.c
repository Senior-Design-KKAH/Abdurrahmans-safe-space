#include "delta_codec.h"
#include "mfcc.h"
#include "payload.h"
#include "quantization.h"
#include "signal_processing.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint16_t raw[ACOUSTIC_SAMPLE_COUNT];
static mfcc_context_t mfcc;
static power_context_t power;
static const double pi = 3.14159265358979323846;

static int signed_byte(uint8_t byte)
{
    return byte < 128 ? byte : (int)byte - 256;
}

static void verify_packet(const int8_t matrix[15][13], float snr, bool passed, unsigned expected_exceptions)
{
    uint8_t guarded[PAYLOAD_MAX_BYTES + 2];
    memset(guarded, 0xa5, sizeof(guarded));
    uint8_t *packet = guarded + 1;
    size_t required = 121 + 2 * expected_exceptions;
    size_t length = 0;
    assert(encode_mfcc_payload(matrix, 0.5f, snr, 0x1234, 0x89abcdef, passed,
                               packet, required - 1, &length) == ESP_ERR_INVALID_SIZE);
    assert(length == required);
    for (size_t i = 0; i < sizeof(guarded); ++i) {
        assert(guarded[i] == 0xa5);
    }
    assert(encode_mfcc_payload(matrix, 0.5f, snr, 0x1234, 0x89abcdef, passed,
                               packet, required, &length) == ESP_OK);
    assert(length == required);
    assert(guarded[0] == 0xa5 && guarded[length + 1] == 0xa5);
    assert(packet[0] == 1);
    assert(packet[1] == ((passed ? 1 : 0) | (expected_exceptions ? 2 : 0)));
    assert(packet[2] == 0x12 && packet[3] == 0x34);
    assert(packet[4] == 0x89 && packet[5] == 0xab && packet[6] == 0xcd && packet[7] == 0xef);
    int snr_q = (packet[8] << 8) | packet[9];
    if (snr_q >= 32768) {
        snr_q -= 65536;
    }
    int expected_snr = (int)fmax(INT16_MIN, fmin(INT16_MAX, round((double)snr * 100.0)));
    assert(snr_q == expected_snr);
    assert(packet[10] == 0x3f && packet[11] == 0 && packet[12] == 0 && packet[13] == 0);
    assert(packet[14] == expected_exceptions);
    assert(memcmp(packet + 15, matrix[0], 13) == 0);
    uint16_t crc = payload_crc16_ccitt_false(packet, length - 2);
    assert(packet[length - 2] == (uint8_t)(crc >> 8) && packet[length - 1] == (uint8_t)crc);
    const uint8_t *body = packet + 28 + 2 * expected_exceptions;
    int reconstructed[15][13];
    for (size_t c = 0; c < 13; ++c) {
        reconstructed[0][c] = signed_byte(packet[15 + c]);
    }
    size_t exception = 0;
    for (size_t i = 0; i < 182; ++i) {
        size_t t = i / 13 + 1;
        size_t c = i % 13;
        unsigned nibble = (body[i / 2] >> (i % 2 == 0 ? 4 : 0)) & 15;
        int delta = (int)matrix[t][c] - (int)matrix[t - 1][c];
        if (delta < -8 || delta > 7) {
            assert(exception < expected_exceptions && packet[28 + 2 * exception] == i);
            assert(nibble == 0);
            reconstructed[t][c] = signed_byte(packet[29 + 2 * exception++]);
        } else {
            assert(nibble == ((unsigned)delta & 15));
            int decoded_delta = nibble < 8 ? (int)nibble : (int)nibble - 16;
            reconstructed[t][c] = reconstructed[t - 1][c] + decoded_delta;
        }
        assert(reconstructed[t][c] == matrix[t][c]);
    }
    assert(exception == expected_exceptions);
}

static void test_codec(void)
{
    assert(payload_crc16_ccitt_false((const uint8_t *)"123456789", 9) == 0x29b1);
    int8_t matrix[15][13] = {{0}};
    verify_packet(matrix, 4.37f, true, 0);
    static const int8_t edge_deltas[13] = {-8, 7, -1, 0, 1, -7, 6, -2, 2, -6, 5, -3, 3};
    for (size_t t = 1; t < 15; ++t) {
        for (size_t c = 0; c < 13; ++c) {
            matrix[t][c] = (int8_t)(matrix[t - 1][c] + (t % 2 ? edge_deltas[c] : -edge_deltas[c]));
        }
    }
    verify_packet(matrix, -4.37f, false, 7);
    mfcc_delta_encoding_t encoding;
    assert(delta_encode_mfcc(matrix, &encoding) == ESP_OK);
    assert(encoding.packed[0] == 0x87 && encoding.packed[1] == 0xf0);
    for (size_t t = 0; t < 15; ++t) {
        for (size_t c = 0; c < 13; ++c) {
            matrix[t][c] = t % 2 ? INT8_MAX : INT8_MIN;
        }
    }
    verify_packet(matrix, FLT_MAX, true, 182);
    verify_packet(matrix, -FLT_MAX, false, 182);
    memset(matrix, 0, sizeof(matrix));
    matrix[1][0] = 8;
    matrix[1][1] = -9;
    matrix[2][0] = 8;
    matrix[2][1] = -9;
    for (size_t t = 3; t < 15; ++t) {
        matrix[t][0] = 8;
        matrix[t][1] = -9;
    }
    matrix[14][12] = 127;
    verify_packet(matrix, 0.005f, false, 3);
    uint8_t packet[PAYLOAD_MAX_BYTES];
    size_t length = 99;
    assert(encode_mfcc_payload(matrix, NAN, 0, 0, 0, false, packet, sizeof(packet), &length) == ESP_ERR_INVALID_ARG);
    assert(length == 0);
    assert(encode_mfcc_payload(matrix, 1, INFINITY, 0, 0, false, packet, sizeof(packet), &length) == ESP_ERR_INVALID_ARG);
    assert(encode_mfcc_payload(matrix, 0, 0, 0, 0, false, packet, sizeof(packet), &length) == ESP_ERR_INVALID_ARG);
    assert(encode_mfcc_payload(NULL, 1, 0, 0, 0, false, packet, sizeof(packet), &length) == ESP_ERR_INVALID_ARG);
    assert(encode_mfcc_payload(matrix, 1, 0, 0, 0, false, NULL, 0, &length) == ESP_ERR_INVALID_ARG);
    assert(encode_mfcc_payload(matrix, 1, 0, 0, 0, false, packet, sizeof(packet), NULL) == ESP_ERR_INVALID_ARG);
}

static void test_quantization(void)
{
    float input[15][13] = {{0}};
    int8_t output[15][13];
    float scale;
    assert(quantize_mfcc(input, output, &scale) == ESP_OK && scale == 1.0f);
    for (size_t t = 0; t < 15; ++t) {
        for (size_t c = 0; c < 13; ++c) {
            assert(output[t][c] == 0);
        }
    }
    input[0][0] = 127.0f;
    input[14][12] = -127.0f;
    input[1][0] = 0.5f;
    input[1][1] = -0.5f;
    assert(quantize_mfcc(input, output, &scale) == ESP_OK && scale == 1.0f);
    assert(output[0][0] == 127 && output[14][12] == -127);
    assert(output[1][0] == 1 && output[1][1] == -1);
    input[7][5] = NAN;
    assert(quantize_mfcc(input, output, &scale) == ESP_ERR_INVALID_ARG);
    input[7][5] = INFINITY;
    assert(quantize_mfcc(input, output, &scale) == ESP_ERR_INVALID_ARG);
    memset(input, 0, sizeof(input));
    input[0][0] = FLT_MAX;
    assert(quantize_mfcc(input, output, &scale) == ESP_OK && isfinite(scale));
    assert(output[0][0] == 127);
    input[0][0] = FLT_MIN;
    assert(quantize_mfcc(input, output, &scale) == ESP_OK && scale > 0);
    assert(quantize_mfcc(NULL, output, &scale) == ESP_ERR_INVALID_ARG);
}

static float filter_gain(float frequency)
{
    bandpass_context_t filter;
    bandpass_reset(&filter);
    double input_power = 0, output_power = 0;
    for (size_t n = 0; n < 16000; ++n) {
        float input = (float)sin(2 * pi * frequency * n / ACOUSTIC_SAMPLE_RATE_HZ);
        float output = bandpass_process(&filter, input);
        assert(isfinite(output));
        if (n >= 8000) {
            input_power += input * input;
            output_power += output * output;
        }
    }
    return (float)sqrt(output_power / input_power);
}

static void test_signal(void)
{
    assert(filter_gain(20) < 0.05f);
    assert(filter_gain(500) > 0.95f);
    assert(filter_gain(3500) < 0.1f);
    power_reset(&power);
    float snr;
    assert(power_snr_db(&power, &snr) == ESP_ERR_INVALID_STATE);
    for (size_t n = 0; n < POWER_WINDOW_SAMPLES; ++n) {
        power_process(&power, 1);
    }
    assert(power.envelopes == 1 && power.minimum == 1);
    assert(power_snr_db(&power, &snr) == ESP_OK && fabsf(snr) < 1e-5f);
    power_process(&power, 0);
    assert(fabs(power.sum - 799) < 1e-6);
    for (size_t n = 1; n < POWER_WINDOW_SAMPLES; ++n) {
        power_process(&power, 0);
    }
    assert(fabs(power.sum) < 1e-6 && power.minimum == 0);
    for (size_t n = 0; n < ACOUSTIC_SAMPLE_COUNT; ++n) {
        raw[n] = 2048;
    }
    assert(signal_measure_snr(&power, raw, ACOUSTIC_SAMPLE_COUNT, 2048, &snr) == ESP_OK);
    assert(snr == 0 && power.envelopes == ACOUSTIC_SAMPLE_COUNT - POWER_WINDOW_SAMPLES + 1);
    assert(signal_measure_snr(&power, raw, 1, 2048, &snr) == ESP_ERR_INVALID_ARG);
}

static void test_mfcc(void)
{
    assert(mfcc_init(&mfcc) == ESP_OK);
    float frame[MFCC_FRAME_LENGTH];
    for (size_t n = 0; n < MFCC_FRAME_LENGTH; ++n) {
        frame[n] = (float)(0.6 * sin(2 * pi * 500 * n / 8000) + 0.2 * cos(2 * pi * 1700 * n / 8000));
    }
    float coefficients[13];
    assert(mfcc_compute_frame(&mfcc, frame, coefficients) == ESP_OK);
    for (size_t k = 0; k <= MFCC_FFT_SIZE / 2; ++k) {
        double re = 0, im = 0;
        for (size_t n = 0; n < MFCC_FRAME_LENGTH; ++n) {
            double value = frame[n] * (0.54 - 0.46 * cos(2 * pi * n / (MFCC_FRAME_LENGTH - 1)));
            double angle = -2 * pi * k * n / MFCC_FFT_SIZE;
            re += value * cos(angle);
            im += value * sin(angle);
        }
        double expected = (re * re + im * im) / MFCC_FFT_SIZE;
        assert(fabs(mfcc.spectrum[k] - expected) < 1e-5 * fmax(1, expected));
    }
    static const size_t indices[15] = {32, 99, 165, 232, 298, 365, 431, 498, 564, 631, 697, 764, 830, 897, 964};
    for (size_t i = 0; i < 15; ++i) {
        assert(mfcc_representative_index(i) == indices[i]);
    }
    assert(mfcc_representative_index(15) == SIZE_MAX);
    for (size_t n = 0; n < ACOUSTIC_SAMPLE_COUNT; ++n) {
        float frequency = 300.0f + 120.0f * (n / 5334);
        raw[n] = (uint16_t)lround(2048 + 800 * sin(2 * pi * frequency * n / 8000));
    }
    float reduced[15][13];
    assert(mfcc_reduce_capture(&mfcc, raw, ACOUSTIC_SAMPLE_COUNT, 2048, reduced) == ESP_OK);
    bandpass_context_t filter;
    bandpass_reset(&filter);
    size_t group = 0;
    for (size_t n = 0; n < ACOUSTIC_SAMPLE_COUNT; ++n) {
        float value = bandpass_process(&filter, ((float)raw[n] - 2048) / 2048);
        if (group < 15) {
            size_t start = indices[group] * MFCC_FRAME_STEP;
            if (n >= start && n < start + MFCC_FRAME_LENGTH) {
                frame[n - start] = value;
            }
            if (n == start + MFCC_FRAME_LENGTH - 1) {
                assert(mfcc_compute_frame(&mfcc, frame, coefficients) == ESP_OK);
                for (size_t c = 0; c < 13; ++c) {
                    assert(fabsf(reduced[group][c] - coefficients[c]) < 1e-5f);
                }
                ++group;
            }
        }
    }
    assert(group == 15);
    int8_t quantized[15][13];
    float scale, snr;
    assert(signal_measure_snr(&power, raw, ACOUSTIC_SAMPLE_COUNT, 2048, &snr) == ESP_OK);
    assert(quantize_mfcc(reduced, quantized, &scale) == ESP_OK);
    uint8_t packet[PAYLOAD_MAX_BYTES];
    size_t length;
    assert(encode_mfcc_payload(quantized, scale, snr, 1, 0, snr >= 4, packet, sizeof(packet), &length) == ESP_OK);
    assert(length == (size_t)121 + 2 * packet[14]);
    memset(frame, 0, sizeof(frame));
    assert(mfcc_compute_frame(&mfcc, frame, coefficients) == ESP_OK);
    assert(fabs(coefficients[0] - sqrt(26) * log(MFCC_LOG_FLOOR)) < 0.001);
    for (size_t c = 1; c < 13; ++c) {
        assert(fabsf(coefficients[c]) < 0.001f);
    }
    frame[0] = NAN;
    assert(mfcc_compute_frame(&mfcc, frame, coefficients) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_codec();
    test_quantization();
    test_signal();
    test_mfcc();
    printf("Codec and DSP checks passed; MFCC workspace: %zu bytes; power workspace: %zu bytes\n",
           sizeof(mfcc_context_t), sizeof(power_context_t));
    return 0;
}
