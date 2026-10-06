# ESP32 acoustic node

Minimal ESP-IDF C project for ESP32-WROOM-32. One boot records a nominal
10 seconds, then produces a binary MFCC payload. No radio or raw-audio
transmission is implemented. The ADC interface follows ESP-IDF 5.4.2 headers;
ESP-IDF is required to build the firmware.

Edit `main/project_config.h` to select `ADC_CAPTURE_CHANNEL` and attenuation.
The default is ADC1 channel 6 (GPIO34), connected to the **MCP6002 output**.
ESP32 DMA capture supports ADC1; selecting ADC2 returns an error. Node ID,
MFCC parameters, `ENABLE_SNR_GATE` (default `0`), and the 4.0 dB threshold
are also in this header.

With ESP-IDF installed and its environment activated, build and flash from
the project root:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace the serial port with the board's port. `app_main()` calls
`run_measurement(0)`; replace `0` with supplied UTC Unix seconds when a clock
is available. Timestamp zero means the epoch placeholder in this prototype.
`encode_mfcc_payload()` independently accepts any caller-supplied timestamp.

For missing ESP-IDF headers, activate the SDK's `export.sh` and run
`idf.py reconfigure` from the project root. Configure your editor to use
`build/compile_commands.json`, which supplies the SDK and generated-header
include paths. Firmware requires the actual ESP-IDF headers; the minimal
`tests/include/esp_err.h` is only for host checks.

The checked-in `.vscode/c_cpp_properties.json` uses the ESP-IDF SDK and
Xtensa compiler installed under `${userHome}/.platformio/packages`, plus
the generated compiler database. If VS Code retains old include errors,
run **C/C++: Reset IntelliSense Database** from the Command Palette.

On success, `measurement_payload` and `measurement_payload_length` in
`main/main.c` remain valid after `app_main()` returns. Length zero means the
measurement failed or the enabled SNR gate skipped it. The encoder in
`main/payload.h` can also write directly to a future radio layer's buffer.
On insufficient capacity it returns `ESP_ERR_INVALID_SIZE`, reports the
required length, and leaves the buffer untouched. Other invalid arguments
return length zero when the length pointer is valid.

## Signal processing choices

The [ESP32 ADC capabilities](https://github.com/espressif/esp-idf/blob/v5.4.2/components/soc/esp32/include/soc/soc_caps.h)
specify a 20 kHz minimum continuous sampling rate. The driver therefore
requests 32 kHz, filters with a 127-tap Blackman-windowed sinc lowpass at
3.2 kHz, and retains every fourth result: exactly 80,000 unsigned 12-bit
samples at nominal 8 kHz. The FIR is initialized with the first input value
and adds about 2 ms group delay. Actual sample timing depends on the ADC
clock divider and must be measured on the board. DMA loss, invalid channel
results, and read timeouts reject the measurement rather than accepting gaps.

The capture mean supplies the measured DC midpoint. DSP uses signed floats
normalized by 2048. The two Butterworth biquads in
`signal_processing.c:bandpass_coefficients` implement a 100 Hz highpass and
a 2500 Hz lowpass at 8 kHz, with approximately -3 dB corners.

The specification gives no exact SNR estimator. This implementation uses
the minimum complete 800-sample moving-average power window as the noise
baseline and the mean of all complete power windows as total power:
`10 * log10(max(mean, 1e-12) / max(minimum, 1e-12))`.
The first 799 incomplete windows are excluded; silence yields 0 dB. This
is an envelope-based diagnostic, not a separately measured physical SNR.
Bit 0 records `snr_db >= SNR_THRESHOLD_DB`, even when gating is disabled.

MFCC uses 200-sample Hamming frames, 80-sample steps, a zero-padded 256-point
FFT, power divided by FFT size, 26 triangular HTK Mel filters spanning
100–2500 Hz, natural log with a `1e-12` floor, and orthonormal DCT-II
coefficients 0–12. No pre-emphasis, liftering, or frame averaging is applied.
There are 998 complete frames. Group `g` spans indices
`floor(g*998/15)` through `floor((g+1)*998/15)-1`; midpoint ties select the
earlier frame. Retained indices are
`32, 99, 165, 232, 298, 365, 431, 498, 564, 631, 697, 764, 830, 897, 964`.
After the SNR gate, the filter is reset and replayed across the same capture;
only these selected frames require FFT/MFCC computation. This is equivalent
to computing every frame and then selecting those rows.

Global symmetric quantization uses `max_abs/127`, nearest rounding with
ties away from zero, and clamping to `[-127,127]`. All-zero input uses
scale 1 and all-zero coefficients. Extremely tiny scales are bounded by
`FLT_MIN` to avoid division underflow on the MCU.

## Packet layout

All multibyte fields are big-endian. Signed bytes use two's complement.
`N` is the exception count, from 0 through 182.

| Offset | Bytes | Field |
| --- | ---: | --- |
| 0 | 1 | Version = 1 |
| 1 | 1 | Flags: bit 0 SNR passed, bit 1 exceptions present; other bits zero |
| 2 | 2 | Node ID, uint16 |
| 4 | 4 | UTC Unix seconds, uint32 |
| 8 | 2 | SNR × 100, rounded and saturated to int16 |
| 10 | 4 | Global scale, IEEE-754 float32 bits |
| 14 | 1 | Exception count N |
| 15 | 13 | First quantized frame |
| 28 | 2N | Ascending `(delta_index, absolute_int8_value)` records |
| 28+2N | 91 | Packed deltas, even index in high nibble |
| 119+2N | 2 | CRC-16/CCITT-FALSE over preceding bytes |

Total length is **121+2N bytes**, maximum **485 bytes**. The code derives
this from the field sizes and verifies the final write position.
CRC parameters: polynomial `0x1021`, initial `0xFFFF`, no input/output
reflection, final XOR zero; `123456789` gives `0x29B1`.

To reconstruct, load row 0. Iterate delta index `i=0..181` with
`t=i/13+1`, `c=i%13`. If an exception names `i`, set `x[t][c]` to its
absolute signed value; its body nibble is zero. Otherwise sign-extend
the nibble from `[-8,7]` and add it to `x[t-1][c]` in a wider integer.
An ordinary zero nibble without an exception is a legitimate zero delta.
Approximate dequantization is `x[t][c] * scale`.

## Memory and checks

Application static workspaces are about **13 KB**: MFCC 6,652 bytes,
power envelope about 3,232 bytes, capture about 1,800 bytes, floating MFCC
matrix 780 bytes, quantized matrix 195 bytes, and payload 485 bytes.
The 160,000-byte raw buffer is allocated once from internal SRAM and freed
after processing; there are no allocations inside sample-processing loops
and no full float recording. Runtime ADC storage adds a 16,384-byte pool,
about 5,120 DMA bytes, and driver bookkeeping; the main task stack is
6,144 bytes. Total application buffers plus those known runtime buffers
are approximately **201 kB (196 KiB)**, excluding ESP-IDF system tasks and overhead.
PSRAM is not required. Runtime allocation avoids ESP32's
[static DRAM limit](https://docs.espressif.com/projects/esp-idf/en/v5.1.3/esp32/api-guides/memory-types.html#dram-data-ram).

Run the small deterministic C checks without ESP-IDF:

```sh
cmake -S tests -B /tmp/rpw-acoustic-tests
cmake --build /tmp/rpw-acoustic-tests
ctest --test-dir /tmp/rpw-acoustic-tests --output-on-failure
```

These compile the actual DSP and encoder sources against a minimal test-only
`esp_err.h`. They check INT8 extremes, nibble ordering and boundaries,
exception placeholders and exact matrix reconstruction, packet fields/CRC,
capacity guards, quantization, filter response, power-window behavior,
FFT against a direct DFT, and chronological frame selection. There is no
production decoder or random-test framework.

Host checks and header/syntax checks for all seven firmware sources passed
with the installed ESP-IDF 5.5.3 SDK and ESP32 compiler. A full firmware
image build remains unverified: the installed SDK Python environment is
incomplete (including missing `cryptography`). Physical sampling/timing
validation still requires an attached board.
