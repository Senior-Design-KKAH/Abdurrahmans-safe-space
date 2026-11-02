# Verification report

## What actually ran

The ESP32-WROOM-32 target compiled and linked successfully with PlatformIO. The firmware was **not flashed or executed on a physical ESP32**. No ADC capture, oscilloscope measurement, radio transmission, receiver delivery, regional compliance assessment, on-device latency measurement or heap high-water reading occurred. No USB/ACM serial target was used. The following results are host verification and cross-compilation results only. The current header uses unsigned node/date/battery fields of 8/5/3 bits; sender and receiver must both use this updated unversioned format.

Build: PlatformIO Core 6.1.19, Espressif32 7.1.3, Arduino package 4.20017.260907+sha.dcc1105b, xtensa-esp32 GCC 8.4.0+2021r2-patch5, LoRa 0.8.0. Static RAM: **44,068/327,680 bytes**. Linked flash use: **314,685/1,310,720 bytes**. These are the default synthetic/RF-disabled build's sizes; enabling configured physical/radio paths can increase code and runtime resources. The complete build output is `results/firmware_build.log`; `results/firmware.bin` is that exact synthetic-mode application image.

The native C++ DSP workspace is 20,568 bytes. The acquisition buffers additionally require 160,000 runtime heap bytes in four 40,000-byte chunks, plus I2S driver/DMA resources, runtime stacks, and core allocations. DMA storage is nominally eight 256-sample 16-bit buffers (4,096 bytes) plus descriptors/queues. The driver is torn down before DSP; no full float waveform is allocated. The image's static RAM count already includes the DSP workspace. Allocation is checked at runtime; actual minimum free heap, largest block, capture/DSP/encoding latency and RF timing still require the board.

## Native packet verification

The actual C++ firmware encoder was built as a shared library and called on every original dataset row. Its emitted bytes were independently generated and decoded by Python. All 195 signed coefficients and all 32 reconstruction-scale bits matched. Source NPZ SHA-256 digests were unchanged. Host test header values (node 42, date-code 11, battery-code 7) were explicitly synthetic. No rows failed or were excluded. The input is the original 15x13 WAV-only archives, not the separate 30x15 experiment.

| Source | Tested/successful | Mean bytes | Median | p95 | p99 | Max | Failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| lab | 54,643/54,643 | 52.386710 | 52 | 55 | 57 | 74 | 0 |
| field | 4,154/4,154 | 71.064035 | 69 | 90 | 99 | 123 | 0 |

These sizes include the six fixed header bytes, three index bits, all first-frame bits, all Rice bits and terminal padding. No raw fallback exists. They exclude the radio library/PHY envelope. Quantiles use empirical inverse CDF. All 58,797 recordings were tested; unlike earlier held-out compression tables, this table is descriptive for the complete datasets.

## Boundary and format checks

A hand-derived zero-matrix golden packet is 43 bytes with header `2a5f3f800000`, 107 initial zero stream bits, 182 unary one bits and seven terminal zero pad bits. It matches the C++ and Python encoders. Truncation was tested at all 43 byte cuts. Extra bytes, nonzero padding, all-zero/no-terminator streams and nonpositive/nonfinite scale headers were rejected. Generated temporal patterns exercised all eight automatically selected indices, and signed first-frame tests include -128/-127/-1/0/127. Single-column alternating patterns verify both ±254 and the wider int8 boundary ±255 without arithmetic overflow. All-column alternating ±127 requires 5,947 bytes and is rejected before any radio write. Seven valid unsigned-header boundary cases and eleven invalid-range/type cases verify the 8/5/3-bit fields, including date 31, battery 7, node 255, negative inputs and fractional inputs. Small output capacity and invalid header codes are rejected. Floating feature tests verify all-zero scale=1, nonfinite rejection and negative/positive halfway ties-to-even. Nine reusable golden vectors are saved in `results/golden_vectors.json`.

## DSP compatibility

Nine identical raw PCM vectors were processed by a **native build of the shared firmware C++ DSP** and by the actual Python reference: silence, a 500 Hz tone, a mid-biased ADC-code tone, seeded noise, edge/center impulses, and four real cropped WAV inputs. Filtered waveform maximum absolute error was **0**; aggregated MFCC maximum absolute error was **5.11590769747e-13**. All quantized coefficients, float32 scale bits and final payload bytes matched exactly for these vectors.

The tolerances fixed in the executable test are filtered-waveform absolute error <=1e-10 and floating MFCC absolute error <=1e-7, with zero allowed quantized differences and exact scale/payload bits. Double SOS coefficients/states and the same padding/initialization explain the small waveform error; FFT/DCT and reversed aggregation order can differ by floating-point rounding. The chosen tolerances are far below ordinary quantization steps for these inputs, but do not prove equality at every possible halfway boundary. These are algorithmic host checks, not measurements of the ESP32 libm or physical ADC.

| PCM input | Filter max absolute error | MFCC max absolute error | Int8 mismatches | Scale error | Payload match |
| --- | ---: | ---: | ---: | ---: | --- |
| `silence` | 0 | 4.54747e-13 | 0 | 0 | True |
| `500Hz` | 0 | 1.81188e-13 | 0 | 0 | True |
| `biased_adc_730Hz` | 0 | 4.83169e-13 | 0 | 0 | True |
| `seeded_noise` | 0 | 2.84217e-14 | 0 | 0 | True |
| `edge_and_center_impulses` | 0 | 5.11591e-13 | 0 | 0 | True |
| `lab/lab/clean/clean_1.wav` | 0 | 1.7053e-13 | 0 | 0 | True |
| `lab/lab/infested/infested_33744.wav` | 0 | 1.7053e-13 | 0 | 0 | True |
| `field/field/test/difficult_cases/F_20200218001001_19_T8.6.wav` | 0 | 1.7053e-13 | 0 | 0 | True |
| `field/field/train/clean/folder_10/F_20200321111418_10_T38.6.wav` | 0 | 1.13687e-13 | 0 | 0 | True |

The DSP uses a matched forward/backward SOS filter, not a causal substitution. It matches the existing 27-sample odd padding, SciPy SOS steady-state initial conditions, 200-sample periodic Hann, 256-point FFT, 80-sample hops, 26 normalized Slaney Mel filters, power spectrum, 10log10 reference=1/floor=1e-10/global 80dB dynamic range, orthonormal type-II DCT and coefficients 0..12. All 998 complete frames are assigned by window centers to 15 chronological equal bins; none are arbitrarily truncated or resized. DC bias is removed before the bandpass; the comparisons feed the identical normalized/DC-removed inputs into both implementations. There is no causal mode because matched zero-phase filtering fits the bounded replay workspace.

Native host DSP timings are retained in the machine-readable report (about 0.03 seconds per vector on this host); they are not ESP32 timings. MCU double-precision math can be much slower and platform libm differences can change near-boundary quantization. The cooperative block yields and SRAM checks are implemented, but on-device timing, watchdog behavior, heap peaks and numerical agreement must be measured physically.

## Comparison with earlier compression charts

The Rice symbol order and fixed bank equal the earlier field-only eight-schedule bank, but the earlier charts excluded an assumed eight-byte metadata prefix and their fallback format included an extra one-bit raw/Rice flag. This protocol has six actual header bytes and no mode flag. On a recording using Rice in both, the new application size is old reported MFCC size plus 5 or 6 bytes depending on terminal padding; it is not old size plus 8. Comparisons also need the same all-recording versus held-out subset. No settings were changed to force chart equality.

| Complete source set | Earlier field-B8 MFCC mean (no 8B prefix, with mode bit) | New complete application mean | Difference |
| --- | ---: | ---: | ---: |
| lab | 46.511630 | 52.386710 | 5.875080 |
| field | 65.195234 | 71.064035 | 5.868801 |

## Open hardware/protocol decisions

Actual ADC1 GPIO, MCP6002 gain/bias/protection/anti-alias filter, ADC input operating range/calibration, LoRa SPI/reset/DIO0 wiring, module/antenna band, and locally permitted radio settings are not known. They remain explicit configuration and bench items. The ADC raw-count normalization is deterministic but not a voltage/acoustic calibration; physical gain can shift MFCC coefficient 0 and the classifier distribution. Unsigned date (5 bits, 0..31) and battery (3 bits, 0..7) code semantics remain opaque and unresolved. Node ID is unsigned 8-bit (0..255); wider unsigned C++ API inputs are range-checked before packing. Physical confirmation of mono DMA adjacent-sample ordering and an external sample-rate check remain required. Default synthetic mode and disabled RF avoid silently inventing any of those choices.

Use `BENCH_TEST.md` to record physical observations separately. The sender only reports TX_DONE after DIO0; it never claims receiver acknowledgment. An oversize packet is reported, not fragmented or reformatted. The variable-length explicit-header LoRa path supports SF7–12; SF6 would need a new agreed arrangement and is rejected. No LoRa airtime or duty-cycle claim is made.

## Reproduce

```bash
pio run -d sensing
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python sensing/host/verify.py
python sensing/host/decoder.py --file sensing/results/example_payload.bin
```

All created paths are under `sensing/`; original preprocessing scripts, datasets, optimized banks and source WAVs were not edited. `README.md` covers the pinned dependencies and configuration. `PROTOCOL.md` fully decodes the actual generated example. `results/verification.json` and the logs retain the exact results.
