# Project guide for AI agents

## Purpose and working approach

This repository studies Red Palm Weevil acoustic sensing: recordings → quantized MFCCs → lossless temporal-delta Rice compression → an ESP32-to-LoRa application payload. Offline experiments and firmware are separate implementations with different packet conventions.

- Read the relevant implementation and task document before changing code. Ask about material ambiguities before execution; otherwise proceed within the requested scope.
- Preserve source recordings and existing experiment artifacts. Use a separate directory for a new feature layout or experiment unless the user explicitly requests replacement.
- Keep the repository root tidy. Put implementation, tests, documentation, logs and outputs under the relevant project directory.
- Prefer plain, understandable code and minimal dependencies. Firmware should remain a small Arduino prototype, not an IoT framework.
- Report measured results separately from estimates and unexecuted procedures. Never infer physical sampling, successful flashing, RF delivery or regulatory compliance from a build or host test.
- Keep user-facing summaries concise. Link detailed reports and tables when useful.

## Repository map

| Path | Purpose |
| --- | --- |
| `lab/`, `field/` | Original audio trees, with nested folders; treat as read-only |
| `TreeVibes.xlsx` | Original dataset-associated workbook |
| `mfcc/mfcc_processor.py` | Reusable original 15-frame × 13-coefficient extraction pipeline |
| `mfcc/outputs/` | Verified original lab/field quantized NPZ archives |
| `mfcc/MFCC_USAGE.md`, `MFCC_RUN_SUMMARY.md` | Extraction commands, numeric choices and results |
| `mfcc/schedule_bank/` | Original 15×13 compression optimization, codec, tests and reports |
| `mfcc/schedule_bank/results/` | Balanced-bank experiment, split manifest, metrics and per-record sizes |
| `mfcc/schedule_bank/field_only/` | Field-only banks of sizes 1/2/4/8/16 and comparison tables |
| `mfcc/30x15/` | Isolated 30-frame × 15-coefficient extraction and newly optimized banks |
| `sensing/` | ESP32 firmware, exact six-byte-header payload, Python decoder and verification |
| `bank-selection.md` | Original offline optimization requirements |
| `sensing-to-payload-implementation.md` | Detailed firmware/payload requirements |
| `adaptive-rice-explainer.md` | Background explanation; inspect before treating it as executable requirements |

The extraction task document is `mfcc/mfcc_extraction.md`. Some task documents preserve older requirements followed by updates. The current decisions below and explicit subsequent user instructions resolve those historical differences.

## Dataset and extraction decisions

- Current datasets include **WAV only**: 54,643 lab recordings and 4,154 field recordings, totaling 58,797. Discover `.wav` case-insensitively and recursively.
- There are 800 OGG files in the field tree. SoundFile rejected them as malformed; sampled FFmpeg checks also failed with CRC errors. The user explicitly instructed us to ignore them. Do not silently reintroduce them or treat their exclusion as a WAV-processing failure.
- Preserve distinct relative paths, including the leading `lab/` or `field/`, nested directories and original extension. Do not deduplicate by basename or assume files are directly under the split root.
- Duration rule: at least 20 s → 5–15 s; more than 10 but less than 20 s → centered 10 s; exactly 10 s → whole clip; less than 10 s → whole clip without padding. Cropping happens at the original sample rate. Short clips use temporal bins across their actual duration; insufficient frames for nonempty bins must fail explicitly.
- Convert multichannel audio to mono by floating-point channel mean; resample properly to 8 kHz with anti-alias filtering.
- Reference filtering is SciPy order-4 Butterworth bandpass 100–2,500 Hz in SOS form with `sosfiltfilt`, not a causal streaming filter.
- MFCC: periodic Hann window 200 samples, FFT 256, hop 80, complete uncentered windows, power spectrum, 26 Slaney Mel filters with Slaney area normalization over 100–2,500 Hz, `power_to_db(ref=1, amin=1e-10, top_db=80)`, type-II orthonormal DCT. The 80 dB floor is relative to the entire clip's maximum Mel energy.
- Aggregate short-time vectors by their analysis-window center into equal nonoverlapping temporal bins. Do not resize feature images or take only the first few frames.
- Original layout: 15 bins × coefficients 0–12. Variant: 30 bins × coefficients 0–14. The 30×15 variant re-extracted the WAVs and fitted new 15-parameter schedules; it did not reuse the old 13-parameter schedules.
- Quantize the entire matrix with one stored float32 scale, `max(abs(M))/127`; all-zero matrices use scale 1. Use that stored scale in division, round ties to even, clip to [-127,127], and store signed int8.
- No per-coefficient standardization, cepstral mean normalization, signal squaring before MFCC, activity gating, moving-average detection, SNR or threshold-based dropping. Squared FFT magnitude for the conventional MFCC power spectrum is still required.

Both NPZs contain exactly `mfcc_q`, `scale`, `path`, with matching row indices. Shapes are `(N,15,13)` or `(N,30,15)`, `(N,)`, `(N,)`; dtypes are int8, float32, Unicode. Always load with `allow_pickle=False`. Reconstruction is `mfcc_q[i].astype(float32) * scale[i]`. Original paths must stay aligned with both arrays. Failures must be explicit; publish a split atomically only when its requested recordings validate.

## Compression experiments and comparisons

- Delta-code along time within each coefficient: `int16(Q[t,c]) - int16(Q[t-1,c])`. Never flatten before differencing and never subtract in int8.
- Preserve the complete first frame as signed bytes. Original layout has 182 deltas and a 104-bit first frame; 30×15 has 435 deltas and a 120-bit first frame.
- ZigZag: `2*d` for nonnegative d, otherwise `-2*d-1`. Rice: quotient zeros then one, followed by k MSB-first remainder bits. Symbols are time-major; zero-pad only the final byte.
- Original experiments compare bank sizes 1/2/4/8/16 and lab-only, field-only, and balanced objectives. Balanced gives lab and field equal total weight, regardless of recording counts.
- Schedules use k=0..8. Training uses seeded iterative assignment/update and four restarts. Validation chooses restarts and the smallest bank within 0.5 bytes of the best validation mean. Test data must not derive schedules or decide hyperparameters. These are local optimization results, not a proof of global optimality.
- Reuse `mfcc/schedule_bank/results/split_manifest.csv` for matched comparisons. Lab uses heuristic blocks of 100 consecutive IDs within class because no parent/session mapping exists. Field uses recording dates, holding out all original test dates and other clips from those dates. Grouping does not prove true session/device independence.
- Original split counts: lab 32,729 train / 10,957 validation / 10,957 test; field 1,434 train / 427 validation / 2,293 test. No path or declared group crosses stages.
- Experimental codecs support plain Rice and a separately evaluated one-bit raw fallback. Their assumed metadata prefix is **8 bytes**: version, application type, uint16 sequence and float32 scale.
- Original raw baseline is 195 bytes; variant raw baseline is 450. With fallback flag and padding, caps are 196 and 451 bytes respectively, before the assumed metadata.
- Say whether results use all recordings or only held-out rows. All recordings were processed even when summary tables show only test rows. Do not compare cohort changes as compression gains.
- Percentiles use empirical inverse CDF (`method='inverted_cdf'`). Report byte padding/index/mode/header costs consistently. NPZ container compression is not Rice compression performance.
- Preserve row-level measurements for plotting. Useful files:
  - `mfcc/schedule_bank/results/recording_size_comparison.csv`: every original recording under balanced B2 and field-only B1/2/4/8/16.
  - `mfcc/schedule_bank/field_only/percentile_comparison.csv` and `lab_percentile_comparison.csv`: original held-out field/lab statistics for those same banks.
  - `mfcc/30x15/results/recording_size_comparison.csv`: every variant recording under newly trained banks.
  - `mfcc/30x15/results/{lab,field}_percentile_comparison.csv`: variant held-out statistics.
  - `mfcc/30x15/results/comparison_15x13_vs_30x15.csv`: old/new held-out statistics together.

Inspect actual JSON `schedules`, array contents and wire specifications instead of assuming compatibility from a filename. The original selected balanced B2, field-specific banks, 30×15 banks and fixed firmware bank are different configurations.

## Firmware source of truth and constraints

The sensing firmware intentionally returns to **15×13**, with the exact ordered eight-entry bank from `sensing-to-payload-implementation.md`. Definitions are in `sensing/src/payload.cpp` and `sensing/host/decoder.py`; the full wire specification is `sensing/PROTOCOL.md`. Do not substitute the selected balanced bank, reorder the eight schedules, use 30×15 features, or add a raw fallback without an explicitly agreed protocol change.

The actual sensing application payload is:

```text
byte 0: unsigned node ID (8 bits, 0..255)
byte 1: bits 7..3 unsigned date code (0..31), bits 2..0 unsigned battery code (0..7)
bytes 2–5: IEEE754 float32 scale, big-endian
byte 6 onward: 3-bit schedule index, 104-bit first frame, 182 Rice symbols
final bits only: zero padding
length = 6 + ceil((3 + 104 + Rice_bits)/8)
```

The current 5-bit-date/3-bit-battery layout supersedes the earlier 4+4 layout. Encoder and decoder must be updated together; the unversioned wire format cannot distinguish old packets automatically. There is no version byte, sequence number, mode flag or extra length field in this format. Its **6-byte header** must not be confused with the experiments' **8-byte metadata assumption**. The eight-entry index costs 3 bits. Selection minimizes packed bytes, then total bits, then schedule index. LoRa 0.8.0 supports at most 255 application bytes and can truncate writes; the encoder rejects oversize packets first and reports their exact required length. No fragmentation or automatic fallback is implemented. Radio/PHY overhead and airtime are separate.

Firmware files:

- `sensing/src/main.cpp`: one-shot manual capture, ADC checks, serial diagnostics, optional transmission.
- `sensing/src/dsp.cpp`: matched forward/backward SOS, fixed FFT/MFCC and binning.
- `sensing/src/payload.cpp`: ties-to-even quantization and bounded packet encoding.
- `sensing/include/config.h`: hardware/RF/caller codes; default synthetic bench mode and RF disabled.
- `sensing/include/dsp_tables.h`: generated reference constants; reproduce with `host/generate_tables.py` and verify after changes.
- `sensing/host/decoder.py`: standalone standard-library decoder and independent reference packet builder.
- `sensing/host/verify.py`: native C++ build, boundary/golden tests, DSP comparison, all-recording packet round-trips.

The acquisition implementation uses ADC1 through the pinned classic ESP32 I2S DMA driver, requesting 8 kHz and retaining 80,000 samples. It checks sample tags, DMA overruns/timeouts and gross elapsed-time failures. Do not replace it with uncontrolled `analogRead`/delay sampling. Check mono adjacent-sample ordering and rate on the actual board before accepting physical results.

Four 40,000-byte PCM chunks avoid one large 160 KB allocation. A 20,568-byte workspace uses filter-state checkpoints/block replay to implement the reference's 27-sample odd padding and forward/backward filtering without allocating a full floating waveform. Two spectral traversals implement the clip-global 80 dB floor. Preserve that numerical behavior if refactoring; causal filtering is not equivalent. No tight-loop allocations or heavy DSP/ML dependencies are needed.

### Open decisions and verified status

- Actual ADC1 pin, MCP6002 gain/bias/protection/anti-aliasing, LoRa wiring, module/antenna band and permitted regional settings are unknown. Configuration placeholders deliberately remain unset. Do not infer them from dataset paths or fabricate a regional band.
- Node ID, date code and battery code use unsigned integer APIs, with wire ranges 0..255, 0..31 and 0..7 respectively. Date/battery meanings remain opaque; no calendar, percentage or voltage mapping is agreed. Synthetic example codes are labeled and are not actual telemetry.
- ADC uses 12-bit raw codes at the pinned core's `ADC_ATTEN_DB_12` setting, removes the recording mean, and normalizes by 2048. No calibrated acoustic amplitude or ADC voltage transfer is claimed. Analog gain/nonlinearity can affect model inputs; digital filtering cannot prevent prior analog aliasing.
- As of the recorded implementation verification: ESP32 cross-compilation passed; all 58,797 original matrices round-tripped with exact scale bits; nine native shared-DSP PCM comparisons had zero quantized differences and matching payloads. These native results are not execution on the ESP32.
- Physical flashing, ADC sampling, on-device DSP accuracy/latency/heap peaks, and over-the-air transmission/reception remain untested. `sensing/BENCH_TEST.md` is an unexecuted procedure. `TX_DONE` indicates local radio completion, not receiver acknowledgment.
- `sensing/VERIFICATION.md` and `sensing/results/verification.json` record measurements; `results/build_verification.json` records build/source hashes. Rerun relevant checks after code changes and update reports rather than assuming old measurements still apply.

## Reproducible commands

Run from the repository root unless a command changes directories. Existing scientific dependencies are pinned in `mfcc/requirements-mfcc.txt`; schedule-bank-only dependencies are also declared locally. `NUMBA_CACHE_DIR` must be writable when installed libraries are read-only. Commands do not imply permission to overwrite existing outputs outside the user's requested scope.

```bash
# Original extraction/tests; extraction writes the supplied output directory.
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python -m pytest -q -p no:cacheprovider mfcc/test_mfcc_processor.py
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 python mfcc/mfcc_processor.py . mfcc/outputs --workers 8 --wav-only

# Original bank tests and experiment: defaults resolve relative to the script.
cd mfcc/schedule_bank
PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python -m pytest -q -p no:cacheprovider test_schedule_bank.py
PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python experiment.py
PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python field_bank.py
cd ../..

# 30x15 variant, with original archives preserved.
cd mfcc/30x15
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python -m pytest -q -p no:cacheprovider test_variant.py
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python extract.py ../.. outputs --workers 8 --wav-only
PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python analyze.py
cd ../..

# Sensing: compile, native verification, standalone decoder.
pio run -d sensing
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python sensing/host/verify.py
python sensing/host/decoder.py --file sensing/results/example_payload.bin
```

Firmware build packages are pinned in `sensing/platformio.ini`: Espressif32 7.1.3, Arduino package `4.20017.260907+sha.dcc1105b`, LoRa 0.8.0. Do not casually upgrade the ADC driver/framework while claiming unchanged capture semantics. Use `sensing/README.md` for flash/serial instructions after hardware configuration. Host Python code is not firmware runtime code.

## Change and verification discipline

- Use focused tests for the affected pipeline; preserve quantized matrices and float32 scale bits where losslessness is required.
- A firmware encoder or layout change requires cross-language packet checks, malformed/oversize tests and a firmware build. DSP changes require identical-PCM reference comparisons, not merely shape tests.
- Changes to training, grouping or selection require a new reproducible experiment and clear provenance. Do not tune against already observed test results and then describe that test as untouched for the new decision.
- Keep concise comments where they explain non-obvious firmware/DSP/hardware choices; preserve the existing style elsewhere.
- Avoid introducing extra frameworks or unnecessary generated artifacts. `__pycache__`, `.pytest_cache`, native `.so` files and PlatformIO `.pio/` are reproducible/disposable; retain source, pinned dependencies, reports, golden vectors and requested data outputs. Prevent caches during lightweight checks when practical.
- Do not move or delete unrelated source assets, root task documents, hidden environment directories, or datasets as routine cleanup.
