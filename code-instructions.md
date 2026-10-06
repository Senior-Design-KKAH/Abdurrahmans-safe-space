# Codex Task: Implement the ESP32-WROOM-32 Acoustic Processing and First-Order Delta Payload Pipeline in ESP-IDF C

## Objective

Implement the sensing-node firmware path for the RPW acoustic detection project in **ESP-IDF C** for an **ESP32-WROOM-32**.

The firmware must take the signal from the tree-mounted piezoelectric sensor through the complete on-node processing pipeline and finish with a compact, serialized binary payload that is ready to be passed to the LoRa transmission layer.

Do **not** implement the Raspberry Pi, cloud backend, classifier, dashboard, or LoRa receive side in this task.

Do **not** transmit raw audio.

The required path is:

```text
Piezoelectric sensor
-> MCP6002 analog conditioning circuit
-> ESP32 ADC sampling
-> 10-second recording at 8 kHz
-> digital bandpass filtering from 100 Hz to 2500 Hz
-> signal power / moving-average processing and SNR calculation
-> MFCC extraction
-> reduction to exactly 15 time frames x 13 coefficients
-> symmetric INT8 quantization using one scale factor for the full matrix
-> standard first-order delta encoding down each coefficient column
-> 4-bit delta packing
-> exception handling for deltas that do not fit in signed 4 bits
-> final serialized binary payload buffer and payload length
```

The final output of the firmware must be a `uint8_t` payload buffer and its exact byte length, ready to be given to the RFM95W LoRa transmit code later.

---

# Target Platform

Use:

```text
MCU: ESP32-WROOM-32
Framework: ESP-IDF
Language: C
```

Use normal ESP-IDF project structure and APIs appropriate for ESP32-WROOM-32.

Use the current ESP-IDF ADC driver rather than Arduino APIs.

The conditioned analog signal from the MCP6002 is within the ESP32 ADC input range.

Make the ADC unit/channel configurable in one header rather than scattering pin/channel constants through the code.

Do not assume that the piezoelectric sensor directly connects to the ADC. The analog MCP6002 conditioning circuit exists before the ADC.

---

# Important Coding Requirements

The generated implementation must be actual compilable C, not pseudocode.

Include comments in the generated C and header files, but keep them short, useful, and straightforward. Comments should briefly explain what a block, function, important variable, or non-obvious operation is doing. Do not write long tutorial-style comments, excessive documentation, or elaborate explanations inside the source code.

Keep the implementation simple and direct. The code should do exactly what this specification requires and nothing more. Do not add optional features, abstractions, frameworks, extensibility layers, extra diagnostics, or additional functionality unless they are necessary to make the specified pipeline work correctly. Prefer the simplest correct implementation over a more elaborate or highly generalized design.

Keep the implementation modular enough to remain readable, but do not over-engineer the project structure. Use clear function names, file names, types, constants, and concise comments.

Avoid C++ features.

Avoid Arduino APIs.

Avoid dynamic allocation inside tight sample-processing loops.

Prefer fixed-size buffers, reusable workspaces, and bounded memory usage suitable for ESP32 SRAM.

Check return values from ESP-IDF APIs and return meaningful error codes.

Do not silently ignore failures.

---

# Deliverables

Create a complete ESP-IDF component or minimal project containing at least:

```text
main/
    main.c
    acoustic_capture.c
    acoustic_capture.h
    signal_processing.c
    signal_processing.h
    mfcc.c
    mfcc.h
    quantization.c
    quantization.h
    delta_codec.c
    delta_codec.h
    payload.c
    payload.h
    project_config.h
CMakeLists.txt
main/CMakeLists.txt
```

You may slightly reorganize the file structure if there is a strong implementation reason, but keep capture, DSP/MFCC, quantization, delta encoding, and packet serialization logically separated.

At the end, explain briefly:

1. how to select the ADC channel,
2. how to build the project,
3. how to flash it,
4. where the final payload buffer and payload length are produced,
5. the exact packet layout so the receiver can later reconstruct the original quantized 15 x 13 matrix.

Keep this explanation concise. Do not add unrelated setup guides, architectural discussion, or future-feature suggestions.

---

# Signal Acquisition

## Sampling rate

Capture the conditioned piezoelectric signal at:

```text
8000 samples/second
```

## Recording duration

Capture exactly:

```text
10 seconds
```

Therefore the acquisition contains:

```text
80000 samples
```

The ADC acquisition must be driven by an ESP-IDF mechanism that can sustain an approximately uniform 8 kHz sample stream without relying on `vTaskDelay()` timing for individual samples.

Use the ESP-IDF ADC continuous/digital controller facilities that are supported by ESP32-WROOM-32.

Convert raw ADC samples to a signed processing representation by removing the measured or calculated DC midpoint before filtering.

Keep the raw ADC representation and DSP representation clearly separated.

The implementation does not need to create an actual RIFF/WAV file in normal firmware operation. The ESP32 is acquiring the same underlying audio/vibration sample stream directly from the sensor. Treat the 10-second ADC capture as the audio input to the processing pipeline.

---

# Digital Bandpass Filter

Apply a digital bandpass filter that preserves approximately:

```text
100 Hz to 2500 Hz
```

at an 8 kHz sampling rate.

Use a stable embedded implementation appropriate for the ESP32.

A cascade of biquad IIR sections is acceptable and preferred over a very long FIR if it substantially reduces memory and CPU requirements.

Use floating point for the first implementation unless fixed point is clearly simpler and demonstrably correct.

Keep the filter coefficients in one clearly identifiable location so they can later be replaced after bench calibration.

Process the signal without requiring a second 80000-sample floating-point copy if a streaming or in-place design can reasonably avoid it.

---

# Power Envelope and SNR Processing

After filtering:

```text
power[n] = filtered[n] * filtered[n]
```

Apply a moving average with a:

```text
100 ms window
```

At 8 kHz this is:

```text
800 samples
```

Implement the moving average using a running sum rather than summing all 800 values again for every output sample.

Calculate the project's SNR-style diagnostic from the smoothed power envelope.

The firmware should expose the calculated SNR in dB for logging and packet metadata.

For this implementation, keep the SNR threshold configurable with a default of:

```text
4.0 dB
```

Provide a configuration switch such as:

```text
ENABLE_SNR_GATE
```

When enabled, MFCC extraction and payload generation may be skipped when the SNR does not pass the configured threshold.

When disabled, every valid 10-second capture must continue to MFCC extraction. This is useful for bench testing and comparison with the earlier dataset analysis.

Default it in `project_config.h` and make the choice obvious.

---

# MFCC Extraction

The project representation uses:

```text
13 MFCC coefficients per time frame
```

The implementation must perform MFCC extraction locally on the ESP32.

Use a conventional MFCC pipeline suitable for an 8 kHz signal:

```text
short-time framing
windowing
FFT
power spectrum
Mel filter bank
log energies
DCT-II
13 output coefficients
```

Use an approximately:

```text
10 ms frame step
```

A practical deterministic starting configuration is acceptable, for example:

```text
frame length: 25 ms
frame step:   10 ms
FFT size:     256
Mel filters:  26
MFCC count:   13
```

If another nearby conventional configuration is used, keep all MFCC parameters centralized in `project_config.h`.

The output before temporal reduction must be ordered as:

```text
rows    = time frames
columns = 13 MFCC coefficients
```

Do not transpose the matrix unintentionally.

Avoid storing the full approximately 1000 x 13 MFCC matrix if it is unnecessary. Since only 15 representative frames are retained, design the processing so memory usage remains reasonable on the ESP32.

---

# Reduction to Exactly 15 x 13

The final floating-point MFCC representation must be exactly:

```text
15 time frames x 13 coefficients
```

Reduce the complete ordered MFCC sequence to exactly 15 representative frames using this rule:

1. Divide the full ordered MFCC frame sequence into 15 contiguous groups that cover the full 10-second recording.
2. From each group, select the MFCC frame closest to the temporal middle of that group.
3. Preserve chronological order.
4. Do not average the frames in each group.
5. Do not randomly sample frames.

The output must be:

```c
float mfcc_15x13[15][13];
```

or an equivalent contiguous representation.

---

# INT8 Quantization

Quantize the complete 15 x 13 MFCC matrix using one global scale factor for that matrix.

The quantized matrix is:

```text
15 x 13 signed INT8
```

Use symmetric quantization.

Use the positive magnitude limit of signed INT8 for scale calculation:

```text
127
```

A suitable definition is:

```text
max_abs = maximum absolute MFCC value over all 195 entries
scale = max_abs / 127.0
q = round(value / scale)
q = clamp(q, -127, 127)
```

For an all-zero matrix, handle the scale safely and produce an all-zero quantized matrix.

Store the scale as IEEE-754 float32 in the packet so the cloud side can approximately dequantize using:

```text
mfcc_float ~= q * scale
```

The delta codec operates only on the quantized INT8 matrix.

Use a wider signed integer type for subtraction so INT8 arithmetic cannot overflow.

---

# Standard First-Order Delta Encoding

The quantized matrix orientation is:

```text
x[time_frame][coefficient]
x[15][13]
```

Perform standard first-order delta encoding independently down each coefficient column.

For:

```text
t = 1 ... 14
c = 0 ... 12
```

calculate:

```text
delta[t][c] = x[t][c] - x[t - 1][c]
```

Do not flatten all 195 coefficients and then subtract neighboring flattened values.

The first frame remains the original 13 signed INT8 coefficients:

```text
x[0][0] ... x[0][12]
```

There are exactly:

```text
14 x 13 = 182 deltas
```

Serialize delta locations in row-major order:

```text
linear delta index 0  = delta for frame 1, coefficient 0
linear delta index 1  = delta for frame 1, coefficient 1
...
linear delta index 12 = delta for frame 1, coefficient 12
linear delta index 13 = delta for frame 2, coefficient 0
...
linear delta index 181 = delta for frame 14, coefficient 12
```

The mapping is:

```text
index = (t - 1) * 13 + c
```

This index fits in one byte because it ranges from 0 to 181.

---

# Signed 4-Bit Delta Representation

A delta fits directly when it lies in:

```text
-8 through +7
```

Encode normal deltas as signed 4-bit two's-complement nibbles.

Examples:

```text
 0 -> 0000
 1 -> 0001
 7 -> 0111
-1 -> 1111
-8 -> 1000
```

Pack two delta nibbles into each byte.

Use this nibble ordering consistently:

```text
first delta index  -> high nibble
second delta index -> low nibble
```

Therefore the 182 delta positions require exactly:

```text
91 bytes
```

in the packed delta body.

---

# Oversized Delta Exception Design

If a delta lies outside:

```text
-8 ... +7
```

then:

1. Put nibble `0000` in that delta's normal location in the 91-byte packed delta body.
2. Add an exception entry to the packet header/exception table.
3. The exception entry must identify the delta location.
4. Store the actual absolute quantized coefficient value at that location, not the full delta.

Use this exception record:

```c
typedef struct {
    uint8_t delta_index;
    int8_t coefficient_value;
} mfcc_delta_exception_t;
```

For exception index `i`, convert the delta index back to the matrix location with:

```text
t = (i / 13) + 1
c = i % 13
```

The stored `coefficient_value` is:

```text
x[t][c]
```

This design is intentional.

An original coefficient fits in one signed byte, while the mathematical delta between two INT8 values can require more than one signed byte if represented generally.

During decoding:

```text
if the current delta index appears in the exception table:
    x[t][c] = exception.coefficient_value
else:
    x[t][c] = x[t - 1][c] + decoded_4bit_delta
```

A normal zero delta also has nibble `0000`, but this is not ambiguous because exception positions are explicitly listed in the exception table.

Do not reserve or remap any other 4-bit value.

The representation must reconstruct the quantized 15 x 13 matrix exactly.

---

# Final Binary Packet Format

Build one contiguous binary payload.

Do not use JSON.

Do not serialize integers using compiler-dependent packed C structs directly onto the wire.

Serialize each field explicitly into the byte buffer so byte ordering and layout are deterministic.

Use big-endian network byte order for multi-byte integer fields.

Serialize the float32 scale by copying its IEEE-754 32-bit representation and transmitting those four bytes in big-endian order.

Use this packet layout:

```text
Offset    Size          Field
------    ----          -----
0         1             packet format version
1         1             flags
2         2             node ID, uint16
4         4             UTC Unix timestamp, uint32
8         2             SNR in signed fixed point, int16
10        4             MFCC quantization scale, float32
14        1             exception count, uint8
15        13            first MFCC frame, 13 x int8
28        2*N           exception table
28+2*N    91            packed 4-bit first-order delta body
119+2*N   2             CRC-16
```

where:

```text
N = exception count
```

The nominal fixed portion before exceptions is 28 bytes.

The packet size is therefore:

```text
121 + 2*N bytes
```

including the CRC.

Check this calculation in code rather than trusting the written number blindly.

If the implementation arrives at a different mathematically correct fixed byte count because of an identified layout mistake in this specification, stop and clearly explain the discrepancy before silently changing the protocol.

---

# Packet Fields

## Packet version

Use:

```text
version = 1
```

## Flags

Use one byte.

Define at least:

```text
bit 0 = SNR gate passed
bit 1 = exception table present
bits 2 through 7 = reserved, set to zero
```

## Node ID

Use:

```text
uint16_t
```

Make the node ID configurable.

## Timestamp

Use:

```text
uint32_t Unix UTC seconds
```

The payload-building API should accept the timestamp as an argument rather than assuming network time is already implemented.

## SNR

Do not put a floating-point SNR value into the packet.

Store SNR in signed hundredths of a dB:

```text
snr_q = round(snr_db * 100)
```

Use:

```text
int16_t
```

For example:

```text
4.37 dB -> 437
```

Clamp safely if required.

## Scale

Store the single global MFCC quantization scale as:

```text
float32
```

## Exception count

Use:

```text
uint8_t
```

The theoretical maximum is 182, so it fits in one byte.

## First frame

Store the original:

```text
13 x int8_t
```

values from:

```text
x[0][0 ... 12]
```

## Exception table

Each entry is exactly two bytes on the wire:

```text
byte 0: delta_index, uint8
byte 1: coefficient_value, signed int8 raw byte representation
```

Keep exception entries in ascending delta-index order.

## Packed delta body

Always store exactly:

```text
91 bytes
```

for the 182 nibble positions.

For an oversized delta, its corresponding nibble must be `0000`.

## CRC

Append CRC-16 over every preceding byte in the application payload.

Use a clearly named standard CRC variant and implement the same deterministic algorithm for future receiver-side decoding.

Use CRC-16/CCITT-FALSE unless there is already an existing project CRC implementation that must be matched.

Parameters:

```text
poly    = 0x1021
init    = 0xFFFF
refin   = false
refout  = false
xorout  = 0x0000
```

Store the CRC in big-endian order.

This application CRC is separate from any CRC provided internally by the LoRa radio.

---

# Maximum Payload Handling

Before writing the output packet, compute the exact required byte length:

```text
fixed fields
+ 2 bytes per exception
+ 91 packed delta bytes
+ CRC
```

Never write beyond the caller-provided payload buffer.

Return an error if the buffer is too small.

Also return the calculated payload length to the caller.

Define a worst-case application buffer large enough for 182 exceptions.

Do not assume that this worst-case packet is necessarily suitable for one LoRa transmission. This task only produces the serialized application payload. LoRa fragmentation, regional payload restrictions, radio settings, and transmission scheduling are outside the scope of this implementation.

Log the actual payload size and exception count so the team can measure the compression behavior later.

---

# Required Encoder API

Expose an API conceptually equivalent to:

```c
esp_err_t encode_mfcc_payload(
    const int8_t mfcc[15][13],
    float scale,
    float snr_db,
    uint16_t node_id,
    uint32_t timestamp,
    bool snr_gate_passed,
    uint8_t *payload,
    size_t payload_capacity,
    size_t *payload_length
);
```

The exact signature may be adjusted slightly if required for good C design, but the same information must be supplied and the same result must be produced.

---

# Minimal Verification Requirement

Do not build a separate production decoder, host-side test framework, random-test generator, or other extra tooling as part of this task.

Only include the minimum validation needed while developing the encoder to confirm that:

1. first-order subtraction uses a wider integer type and does not overflow,
2. signed 4-bit values are packed correctly,
3. oversized deltas create the correct exception entry and `0000` placeholder,
4. the calculated payload length matches the bytes actually serialized,
5. the final payload buffer stays within its declared capacity.

Keep any such checks simple and directly related to the required firmware path.

---

# Memory and Runtime Requirements

The implementation is intended for ESP32-WROOM-32, so avoid designs that unnecessarily retain multiple full 10-second floating-point buffers.

The raw 10-second capture alone contains 80000 samples.

Design with bounded memory in mind.

A reasonable approach is to:

1. acquire samples into a suitably compact raw buffer,
2. process filtering and envelope calculations in streaming form or in manageable blocks,
3. compute MFCC frames sequentially,
4. retain only the 15 selected MFCC frames rather than every MFCC frame,
5. quantize only after all 15 representative frames are known so one global scale can be calculated,
6. reuse scratch buffers for FFT and Mel computations.

If a full 80000-sample raw buffer is unnecessary, avoid it.

If PSRAM is not available, the firmware must still be designed for ordinary ESP32-WROOM-32 memory constraints.

Report approximate static buffer sizes after implementation.

---

# Runtime Flow

The normal firmware control path should be clearly structured as:

```text
initialize ADC
initialize DSP/MFCC context
capture 10 seconds at 8 kHz
remove ADC DC offset / center signal
bandpass filter
compute power envelope and SNR
if ENABLE_SNR_GATE and SNR does not pass:
    stop this measurement cleanly
else:
    extract MFCC sequence
    select exactly 15 representative frames
    quantize 15 x 13 matrix to INT8 using one global scale
    first-order delta encode per coefficient column
    build exception table
    pack 182 nibbles into 91 bytes
    serialize final versioned packet
    append CRC
    expose payload pointer/buffer and exact byte length
```

At the end of one successful run, log at least:

```text
SNR dB
quantization scale
exception count
payload length
```

Do not add hexadecimal payload dumps or other extra debugging output unless needed to diagnose a problem during development.

---

# Important Distinction From the Earlier Dataset Analysis

The earlier offline TreeVibes analysis loaded `.wav` files from storage. This firmware does not need to read a `.wav` file from a filesystem during normal operation.

The ESP32 obtains the acoustic/vibration data directly through:

```text
piezoelectric sensor -> MCP6002 -> ADC
```

The resulting 10-second 8 kHz sample stream is the firmware equivalent of the offline `.wav` input.

The same final representation must still be produced:

```text
15 x 13 MFCC
-> global INT8 quantization
-> standard first-order temporal delta encoding
```

---

# Scope Boundaries

Do not implement these items yet:

```text
RFM95W SPI driver
actual LoRa transmission
TDMA slot scheduling
Raspberry Pi receiver
MQTT
cloud backend
machine-learning inference
web dashboard
deep sleep scheduling
battery monitoring
solar charging logic
```

However, make the final payload interface easy for the future LoRa function to consume, for example:

```c
const uint8_t *payload;
size_t payload_length;
```

Do not couple the codec to a particular LoRa library.

---

# Implementation Style Constraint

This is a focused prototype implementation. Do not add anything simply because it may be useful later. If a feature is not explicitly required by this document or necessary for the required signal-processing and payload-generation path, leave it out.

The source code should be easy for an undergraduate engineering team to read and modify. Favor straightforward C over clever abstractions. Use concise comments to explain the important processing steps without turning the code into a tutorial.

---

# Acceptance Criteria

The task is complete only when all of the following are true:

- ESP-IDF C is used.
- The code targets ESP32-WROOM-32.
- The analog input is acquired from the ESP32 ADC at 8 kHz.
- One measurement covers 10 seconds.
- The signal is digitally filtered to the project band of approximately 100 to 2500 Hz.
- The 100 ms power moving average and SNR diagnostic are implemented.
- 13 MFCC coefficients are extracted over time.
- The sequence is reduced to exactly 15 chronological representative frames.
- The resulting matrix is exactly 15 x 13.
- One global float32 scale is used to quantize the whole matrix to signed INT8.
- First-order deltas are calculated vertically through time for each coefficient independently.
- Exactly 182 first-order deltas are produced.
- Signed 4-bit range is treated as -8 through +7.
- In-range deltas are stored directly as two's-complement 4-bit nibbles.
- Two deltas are packed into each byte.
- The packed delta body is exactly 91 bytes.
- Oversized deltas place `0000` in their normal body location.
- Each oversized delta produces an exception-table entry containing its delta index and the absolute INT8 coefficient value.
- Legitimate zero deltas remain valid and are distinguished from exceptions by the exception table.
- The first 13 original coefficients are retained.
- The packet carries version, flags, node ID, timestamp, SNR, scale, exception count, first frame, exception table, packed deltas, and CRC.
- Serialization is explicit and endian-stable.
- The codec never relies on packed-struct memory layout for the wire format.
- A decoder reconstructs the original quantized 15 x 13 matrix exactly.
- Edge-case tests pass.
- The final result is a `uint8_t` payload plus exact payload length, ready for the future LoRa layer.
- No comments are present inside generated C or header source files.

---

# Final Instruction

Implement this fully rather than only describing how it could be implemented.

Produce the source files, build configuration, and tests.

If an ESP-IDF API detail differs by installed ESP-IDF version, inspect the installed headers/examples and adapt the implementation to the available API while preserving the required behavior and packet format.

Do not change the encoding protocol or matrix dimensions to simplify the implementation.
