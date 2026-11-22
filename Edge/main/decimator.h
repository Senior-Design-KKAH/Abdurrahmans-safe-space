#pragma once
#include <stdint.h>

class Decimator {
public:
    static constexpr unsigned inputRate = 24000;
    static constexpr unsigned ratio = 3;

    // Wait for a complete FIR window, then keep every third output.
    bool push(int16_t centered, int16_t& pcm) {
        history[next] = centered;
        if (++next == taps) next = 0;
        if (filled < taps) {
            if (++filled < taps) return false;
        }
        if (phase++ != 0) {
            if (phase == ratio) phase = 0;
            return false;
        }

        int32_t sum = 0;
        unsigned pos = next;
        for (unsigned i = 0; i < taps; ++i) {
            sum += coefficients[i] * int32_t(history[pos]);
            if (++pos == taps) pos = 0;
        }
        // Q15 FIR output multiplied by 16 to map 12-bit counts to int16 PCM.
        const int32_t scaled = (sum + (sum >= 0 ? 1024 : -1024)) / 2048;
        pcm = scaled > 32767 ? 32767 : scaled < -32768 ? -32768 : int16_t(scaled);
        return true;
    }

private:
    static constexpr unsigned taps = 121;
    // Q15, unity DC gain. Blackman-windowed sinc with a 3.75 kHz cutoff at 24 kHz.
    static constexpr int16_t coefficients[taps] = {
            0,     0,     0,     0,    -1,    -1,     1,     2,     2,    -1,
           -5,    -6,     0,     9,    12,     3,   -13,   -21,   -10,    16,
           34,    22,   -17,   -50,   -41,    13,    69,    70,     0,   -88,
         -109,   -26,   104,   160,    69,  -111,  -222,  -136,   103,   292,
          232,   -71,  -369,  -366,     0,   447,   551,   129,  -522,  -811,
         -356,   588,  1213,   783,  -639, -1989, -1811,   671,  4797,  8663,
        10242,  8663,  4797,   671, -1811, -1989,  -639,   783,  1213,   588,
         -356,  -811,  -522,   129,   551,   447,     0,  -366,  -369,   -71,
          232,   292,   103,  -136,  -222,  -111,    69,   160,   104,   -26,
         -109,   -88,     0,    70,    69,    13,   -41,   -50,   -17,    22,
           34,    16,   -10,   -21,   -13,     3,    12,     9,     0,    -6,
           -5,    -1,     2,     2,     1,    -1,    -1,     0,     0,     0,
            0,
    };
    int16_t history[taps]{};
    unsigned next = 0, filled = 0, phase = 0;
};
