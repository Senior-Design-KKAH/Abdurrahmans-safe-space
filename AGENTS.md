# Sensing firmware: guide for AI agents

## Scope and purpose

This repository implements an ESP32-WROOM-32 piezoelectric sensing prototype:

**10-second capture → bandpass → 15×13 MFCCs → signed int8 quantization → temporal delta/ZigZag/Rice coding → serial payload output → optional LoRa transmission.**

All paths and commands in this guide are relative to this repository's root. Keep work focused on the files here. The firmware does not perform classification, cloud integration, activity detection, deep sleep, or scheduled measurements.

Read the relevant source before editing. Ask about material ambiguities; otherwise implement the requested work. Prefer small, plain functions and minimal dependencies. Add concise one-line comments above functions and around non-obvious processing steps. Keep user-facing summaries short and distinguish measured results from estimates.

## File map

| Path | Purpose |
| --- | --- |
| `platformio.ini` | ESP32 target, pinned framework/library versions and compiler flags |
| `src/main.cpp` | Manual capture flow, ADC DMA, serial diagnostics and optional radio transmission |
| `src/dsp.cpp` | Forward/backward filtering, FFT, MFCC aggregation and quantization call |
| `src/payload.cpp` | Quantization, fixed Rice bank, schedule selection and binary packet builder |
| `include/config.h` | Capture mode, actual ADC pin, unsigned header codes and RF configuration |
| `include/pipeline.h` | Shared constants, status codes, bounded workspace and C-compatible function declarations |
| `include/dsp_tables.h` | Checked-in filter, window, Mel, DCT and FFT constants |
| `host/decoder.py` | Standard-library packet decoder and independent Python reference encoder |
| `host/verify.py` | Native C++ build, generated tests, DSP comparisons and recording packet round-trips |
| `host/generate_tables.py` | Helper for regenerating the checked-in DSP constants |
| `PROTOCOL.md` | Exact wire format and a fully decoded generated example |
| `VERIFICATION.md` | Recorded build/test results, numerical tolerances and verification limitations |
| `BENCH_TEST.md` | Physical test procedure; a procedure is not proof that a test ran |
| `README.md` | Build, configuration, usage and implementation explanation |
| `results/` | Build/verification logs, JSON reports, golden vectors, example payload and compiled image |

`results/example_payload.bin` and `.json` describe the same example. `golden_vectors.json` contains generated protocol test vectors. `verification.json` records numerical and packet checks; `build_verification.json` records the configuration/build snapshot and source hashes. Those snapshots can predate later configuration changes.

## Current configuration and operation

At the time of this guide's update, `include/config.h` contains:

- `BENCH_MODE=false`: physical ADC capture is selected.
- `ADC_GPIO=34`, `ADC_SWAP_ADJACENT=true`.
- Unsigned header codes: node ID **187**, date code **2**, battery code **7**.
- `RADIO_ENABLED=false`, `ALLOW_SYNTHETIC_RF=false`.
- LoRa wiring and RF settings remain unset.

Always inspect `config.h`; do not assume earlier documentation about synthetic defaults describes the current build. Preserve the user's chosen pin and header codes when making unrelated changes.

Open Serial at **115200 baud** and send lowercase **`c`** for one measurement. Capture retains 80,000 samples at a requested 8,000 samples/second. DSP runs afterward, then the sketch prints `ENC status`, payload length, selected schedule and **`PAYLOAD_HEX=...`**. Serial payload output works without a radio module. No measurement runs automatically at startup.

Synthetic bench mode generates a labeled 500 Hz waveform instead of sampling. It uses explicit example header codes, not operational telemetry. Synthetic RF requires its separate configuration switch. Date and battery fields are caller-supplied codes; there is no implemented calendar, battery-voltage or percentage mapping.

## Acquisition and analog input

Capture uses ADC1 through the pinned classic ESP32 I2S DMA driver, with APLL requested at 8 kHz, 12-bit ADC codes and `ADC_ATTEN_DB_12`. Two startup DMA buffers are discarded. The retained samples must have the expected ADC channel tags; timeouts, DMA errors/overruns, insufficient samples and gross duration faults reject the capture.

`ADC_SWAP_ADJACENT` handles the selected 16-bit mono DMA ordering. Confirm sample order and actual sample rate on hardware before changing this flag or upgrading the ADC driver. The elapsed-time acceptance window of 9.8–10.2 seconds detects gross faults; its reported effective rate is not an external clock calibration.

The intended analog front end supplies appropriate high-impedance buffering/amplification, DC bias, protection and anti-alias filtering before GPIO34. A bare piezo is not a properly conditioned ADC source. Digital filtering cannot repair analog clipping or prior aliasing. Input normalization removes the recording's mean and divides raw ADC codes by 2048; it is not calibrated voltage or acoustic amplitude. Synthetic signed PCM uses divisor 32768.

**User-reported bench observation:** with the piezo directly connected to GPIO34 near a 1.5 kHz speaker, the serial output reported 80,000 samples, 10.094504 seconds, raw min/max 0/367, and 75,896 rail samples. Since the upper rail was never reached, 94.87% of the samples were exactly zero. DSP took about 20.81 seconds, encoding 351 microseconds, and the payload was 56 bytes. Treat this as a reported physical observation, not a formally validated frequency-response test. Input conditioning remains unresolved; do not interpret mostly-zero MFCC coefficients as evidence of absent sound or insect activity.

## DSP behavior to preserve

- Process the filtered, unsquared 10-second waveform; no SNR, power-envelope gating or threshold-based dropping.
- Use a fourth-order Butterworth 100–2,500 Hz bandpass in four SOS sections, forward and backward with 27-sample odd edge extension and steady-state initialization. A causal IIR is not numerically equivalent.
- Use periodic Hann windows of 200 samples, a 256-point FFT, an 80-sample hop, no centered/padded analysis frames, and a power spectrum.
- Apply 26 Slaney Mel filters with area normalization over 100–2,500 Hz. Log power uses reference 1, minimum energy 1e-10 and an 80 dB floor relative to the entire capture's maximum Mel energy.
- Apply orthonormal type-II DCT and retain coefficients 0–12. Average the 998 complete analysis frames into 15 equal chronological bins using each window's center. Do not resize, interpolate or simply truncate to 15 frames.
- Quantize the complete 15×13 floating matrix with one stored IEEE754 float32 scale: `max(abs(M))/127`, or 1 for an all-zero matrix. Divide by that stored scale, round ties to even including negative halfway cases, and clip to signed int8 [-127,127]. No per-coefficient normalization or cepstral mean subtraction is applied.

The 160,000-byte PCM capture is allocated in four 40,000-byte chunks. The fixed DSP workspace is 20,568 bytes. Checkpoint/replay filtering avoids storing a full floating waveform. Two spectral traversals implement the clip-global log floor. Keep allocations outside tight processing loops and check failures explicitly.

The current implementation uses double precision and repeated filter/FFT work to preserve the reference behavior within the available RAM. Roughly 21 seconds of DSP has been reported on the physical board. Any performance changes must be measured and checked numerically; a faster implementation must not silently change filtering, Mel normalization, rounding or payload semantics.

## Exact payload contract

| Position | Field | Encoding |
| --- | --- | --- |
| Byte 0 | Node ID | Unsigned 8-bit, 0–255 |
| Byte 1 bits 7–3 | Date code | Unsigned 5-bit, 0–31 |
| Byte 1 bits 2–0 | Battery code | Unsigned 3-bit, 0–7 |
| Bytes 2–5 | Reconstruction scale | IEEE754 float32, big-endian |
| From byte 6 | Schedule index | 3 bits, immediately followed by data |
| Next | Complete first MFCC frame | 13 signed int8 values, 104 bits |
| Next | Temporal deltas | 182 Rice-coded ZigZag integers, time-major |
| End only | Padding | 0–7 zero bits to finish the final byte |

Byte 1 is `(date << 3) | battery`. Current node/date/battery values produce initial bytes **`bb 17`**. C++ header APIs use unsigned `uint32_t` inputs and validate wire ranges before narrowing; Python requires integer, nonnegative, in-range values.

All bits are MSB-first, with no padding between fields. The header is **six bytes**. There is no version byte, sequence number, length field, raw-mode flag, fallback or fragmentation. The current 5+3 packing replaced an older 4+4 layout; both endpoints must agree because old packets cannot be identified automatically.

Preserve the exact ordered **eight-entry, 13-parameter bank** in `src/payload.cpp` and `host/decoder.py`. Never replace or reorder it as an incidental optimization. Choose the lowest final packed byte cost, then lowest total bit count, then lowest index.

Store the first frame directly. For t=1..14 and c=0..12, compute `d = int16(Q[t,c]) - int16(Q[t-1,c])`, then ZigZag `u = 2*d` if nonnegative, otherwise `-2*d-1`. Never difference adjacent coefficients or subtract in int8. Emit symbols with time outermost and coefficient innermost. Rice writes `u >> k` zeros, a one terminator, then k remainder bits MSB-first.

Application length is `6 + ceil((3 + 104 + Rice_bits)/8)` bytes. The encoder rejects lengths above 255 before writing/transmitting and reports the exact required size. The alternating ±127 stress case requires 5,947 bytes and must fail explicitly. Radio/PHY overhead is separate from application bytes. The decoder must validate scale, bounds, unary syntax, exact symbol count and terminal padding, while preserving all 195 coefficients and all scale bits.

## Build and local tools

Run from this repository's root:

```bash
pio run
pio run -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
python host/decoder.py --file results/example_payload.bin
python host/decoder.py --hex HEX_DIGITS
```

Uploading affects the connected board; do it as part of the requested hardware task. A successful build is not proof of successful sampling or RF delivery. The saved example has synthetic header codes and is not necessarily the current physical node's payload.

`platformio.ini` pins Espressif32 **7.1.3**, Arduino framework package **4.20017.260907+sha.dcc1105b**, and **LoRa 0.8.0**, targeting `esp32dev`. SPI and the ADC/I2S driver come with the Arduino core. Keep dependencies minimal and do not casually upgrade the framework while assuming unchanged DMA behavior. Compiler flags disable fast math and floating-point contraction for reproducibility.

The decoder uses only Python's standard library. The native verification helper uses g++, NumPy, SciPy, SoundFile and librosa. **The full `host/verify.py` and `host/generate_tables.py` workflows currently require external reference/data inputs that are not included in this repository.** They cannot complete from an isolated checkout alone. Check their actual imports and file access before running them; `--limit` does not remove those dependencies. The checked-in DSP tables allow firmware builds without regenerating constants. Golden vectors and the standalone decoder remain available locally. Do not claim full verification passed if external inputs were unavailable, and do not silently replace reference checks with fabricated data.

## Verification, hardware status and change discipline

- `VERIFICATION.md` and the saved JSON/logs record successful cross-compilation, 58,797 packet round-trips with exact scale bits, and nine native shared-DSP PCM comparisons with matching quantized matrices and payloads. These are historical test results, not automatic evidence about modified code or a newly configured build.
- The native numerical tolerances are waveform absolute error <=1e-10 and MFCC absolute error <=1e-7, with zero quantized mismatches and exact scale/payload bits on the checked vectors. Native C++ agreement does not guarantee identical ESP32 libm behavior for every input.
- A user has since reported physical ADC/DSP execution as described above. RF remains disabled and over-the-air receipt is unverified. Do not describe the entire physical signal chain as validated.
- LoRa pins, module/antenna band and permitted radio settings remain unspecified. Never invent a regional band. The implemented variable-length explicit-header path supports SF7–12 and CRC; SF6 needs a separately agreed arrangement. `TX_DONE` confirms local completion only, not receiver acknowledgment.
- Ask for missing circuit/wiring details when they affect the requested task. Do not use software filtering to conceal a missing bias, damaged input, clipping or poor mechanical coupling.
- Protocol/encoder changes require cross-language round-trips, malformed/oversize tests and a firmware build. DSP changes require identical-input comparisons and measured timing. Preserve the user's hardware configuration during unrelated work.
- Keep firmware as a few readable C++ functions. Do not introduce a framework, class hierarchy, scheduler, cloud layer or large DSP/ML dependency without a concrete requirement.
- Keep requested source, reports, golden vectors and useful example outputs. `.pio/`, `__pycache__/` and native shared libraries are reproducible build/cache artifacts. Do not remove unrelated files or overwrite configuration/results without task scope.
- This is its own Git repository. Inspect its status before Git operations; commit/push only the requested changes, preserve existing history, and do not force-push or include untracked editor files incidentally.
