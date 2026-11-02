# Sensing to payload

This is the requested **15x13** ESP32-WROOM-32 prototype, with the exact eight-schedule bank and **six-byte header** in `../sensing-to-payload-implementation.md`. It leaves the datasets and the separate 30x15 experiment unchanged. Runtime firmware consists of three small C++ files: `main.cpp` for one-shot DMA capture and optional LoRa transport, `dsp.cpp` for DSP, and `payload.cpp` for quantization/encoding. No scheduler, cloud, Wi-Fi, classifier, SNR, activity gating, or raw fallback is added.

## Build and host verification

Run these commands from the repository root:

```bash
pio run -d sensing
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python sensing/host/verify.py
python sensing/host/decoder.py --file sensing/results/example_payload.bin
```

PlatformIO Core 6.1.19 was used. `platformio.ini` pins Espressif32 7.1.3, its Arduino-ESP32 2.0.17-based package `4.20017.260907+sha.dcc1105b`, and LoRa 0.8.0. This supported PlatformIO package exposes the classic ESP32 I2S built-in ADC DMA driver, which can request 8 kHz directly. It is intentionally not migrated to the different modern continuous-ADC API whose ESP32 minimum is 20 kHz. Core/driver versions are pinned because sampling APIs differ. The only added Arduino library is LoRa; SPI and ADC/I2S facilities come with the core. Host verification uses g++ plus the scientific dependencies already pinned in `../mfcc/requirements-mfcc.txt`; the decoder itself uses only Python's standard library.

`verify.py` builds a native shared library from the actual firmware DSP and encoder, runs generated boundary/golden tests, compares nine identical PCM inputs with the existing Python pipeline, and round-trips all 58,797 stored original matrices. `--limit 100` is available for a quick subset and is clearly identified in the resulting report. Full verification is the default. Tables were generated once from the existing reference; to reproduce them:

```bash
NUMBA_CACHE_DIR=/tmp/treevibes-numba-cache PYTHONDONTWRITEBYTECODE=1 OPENBLAS_NUM_THREADS=1 python sensing/host/generate_tables.py
```

The table generator reads the current reference implementation; changing that reference or regenerating tables with different dependencies requires repeating numerical verification. The checked-in table header is sufficient for building the firmware; Python is not a firmware dependency.

## Configure, flash, and use

Edit `sensing/include/config.h`. The checked-in configuration is **synthetic bench mode, RF disabled**. It uses a generated 500 Hz waveform and explicitly labeled example node/date/battery codes. No actual ADC or radio wiring is assumed.

For physical capture set `BENCH_MODE=false`, provide the actual `ADC_GPIO` on ADC1 and operational node ID/date code/battery code, and initially keep `RADIO_ENABLED=false`. Node ID, date and battery use unsigned integer APIs with wire ranges 0..255, 0..31 and 0..7 respectively. Byte 1 packs date in bits 7..3 and battery in bits 2..0. Date and battery meanings remain opaque; no calendar, voltage, or percentage mapping exists. Out-of-range unsigned configuration sentinels keep operational codes unset until supplied. Actual ADC input is 12-bit at maximum attenuation (`ADC_ATTEN_DB_12`, the pinned core's name for the former 11 dB setting). Raw codes are processed linearly, with the recording mean removed in double precision and divided by 2048; no eFuse/millivolt calibration or physical amplitude calibration is claimed. Synthetic PCM uses divisor 32768. These units affect MFCC coefficient 0, so analog gain and model compatibility must be checked with the real front end.

Capture uses I2S0 ADC1 DMA, APLL requested at 8 kHz, eight 256-sample DMA buffers, ADC channel-tag checks, and RX-overrun/error events. Two startup buffers are discarded; then exactly 80,000 retained samples are required. Raw rail counts, minimum/maximum, elapsed read time and effective sample rate are printed. The 9.8–10.2 second wall-clock acceptance check detects gross rate failures; it does not prove crystal accuracy. `ADC_SWAP_ADJACENT` handles classic ESP32 16-bit mono word ordering. Its pinned-driver setting must be checked with an asymmetric ramp/step on the actual board before accepting waveform timing; do not port the driver or change mono layout without repeating this test. A failed read, wrong channel, overrun, invalid timing or failed allocation stops that capture without RF transmission. Clipping is reported and quiet signals are processed; no activity threshold exists.

For RF supply every SPI/CS/reset/DIO0 pin, module-compatible permitted frequency, bandwidth, spreading factor, coding-rate denominator, TX power, sync word and preamble length; then enable `RADIO_ENABLED`. There is no default regional band. Supported explicit-header settings here are SF7–12, coding-rate denominator 5–8, power 2–20 dBm, and bandwidths 7800/10400/15600/20800/31250/41700/62500/125000/250000/500000 Hz. Chip-range validation is not proof that the particular module, antenna or local rules permit a setting. Both endpoints must agree on all radio settings; payload CRC is enabled. SF6 is rejected because it would require a separate fixed-length implicit-header arrangement. Synthetic RF remains blocked unless `ALLOW_SYNTHETIC_RF=true` is deliberately selected for a labeled bench test.

```bash
pio run -d sensing -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

Send `c` over Serial for one capture. The sketch prints timing, clipping, scale, encoding status, application byte length, schedule and `PAYLOAD_HEX=...`. Copy the hex after that label into `python sensing/host/decoder.py --hex HEX_DIGITS`. The radio path checks the exact length before calling the driver's potentially truncating write, uses DIO0 TX-done with a timeout, and checks the returned write count. `TX_DONE` confirms local transmission completion only, not reception. There is no automatic repetition or wake schedule; a future scheduler can call the existing `single_capture` boundary.

## RAM and numerical choice

The 160,000-byte raw waveform is allocated once per capture in four 40,000-byte chunks, not as a single large contiguous allocation. Allocation failure is explicit. The 20,568-byte fixed DSP workspace is static. No allocations occur inside sampling/DSP loops, and the DMA driver is freed before DSP. Other firmware/static core RAM and runtime stacks/driver buffers still count; actual heap diagnostics must be read on the board.

A direct 80,000-element float buffer would add 320,000 bytes, so the implementation uses a small checkpoint/replay algorithm instead. It reproduces SciPy's 27-sample odd extension and SOS steady-state initialization. A forward traversal records filter states every 1,024 samples. Backward traversals regenerate one forward block at a time and feed reversed 200-sample analysis windows directly into MFCC extraction. One traversal finds the global log-energy maximum; a second applies the reference 80 dB floor and accumulates bins. Double precision and precomputed reference tables keep numerical differences small. This trades repeated filtering/FFTs for bounded memory without substituting a causal filter or quantizing the filtered waveform. Cooperative yields between blocks prevent a long DSP operation from monopolizing the Arduino task.

See [PROTOCOL.md](PROTOCOL.md) for the exact format and fully decoded real code-generated example, [BENCH_TEST.md](BENCH_TEST.md) for unexecuted physical tests, and [VERIFICATION.md](VERIFICATION.md) for measured host/build results and limitations. Reproducible outputs are in `results/`; `.pio/` is PlatformIO's disposable build/dependency directory.

## API sources checked

The pinned [PlatformIO manifest](https://github.com/platformio/platform-espressif32/blob/v7.1.3/platform.json) defines the framework package. The [Espressif I2S documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/i2s.html) describes ADC DMA and its clock controls. The separate [ESP32 continuous-ADC limits](https://github.com/espressif/esp-idf/blob/v5.5.2/components/soc/esp32/include/soc/soc_caps.h) explain the 20 kHz lower bound of that newer path. [LoRa 0.8.0](https://github.com/sandeepmistry/arduino-LoRa/blob/0.8.0/src/LoRa.cpp) sets its maximum application packet to 255 bytes. No physical or RF verification is inferred from those APIs.
