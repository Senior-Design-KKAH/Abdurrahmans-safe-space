# Half-demo: minimal end-to-end implementation

## Task and priority

Implement the complete working demonstration in one go:

```text
10-second audio in ESP32 flash
  -> streaming log-mel preprocessing
  -> on-ESP int8 ML inference
  -> mean probability score
  -> LoRa packet
  -> aggregating ESP32 cluster head
  -> Wi-Fi HTTP POST
  -> local Python receiver
  -> score printed and stored in SQLite
```

Deliver functioning code and short instructions for building, configuring, flashing, running, and checking this entire path. Do not stop at scaffolding, placeholder callbacks, or firmware that builds but cannot process and forward a score. There are no implementation phases or intermediate approval gates.

All new work belongs in `half-demo/`. Leave `Khalid/`, `Kareem/`, `Haitham/`, `sensing/`, and the original datasets unchanged. Copy only the source needed for this demo and retain its licenses. Do not use Haitham's backend or dashboard yet.

`sensing-to-payload-implementation.md` is a reference for **simplicity and code style only**. Do not import its acquisition, MFCC, compression, packet, or hardware requirements. `main.pdf` supplies no technical requirements for this implementation.

## Simplicity is a hard requirement

Prioritize the smallest understandable working implementation. This is a student prototype, not a production IoT platform.

- Use plain C/C++, fixed-size arrays, small functions, and brief useful comments. Keep application code in a few focused files. Reuse the existing model, preprocessing, and communication implementation rather than redesigning them.
- Keep dependencies to the ESP32 framework, TensorFlow Lite Micro required by the model, and Kareem's existing LoRa driver. Use built-in Wi-Fi/HTTP facilities. The receiver must use only Python's standard library, including `http.server`, `struct`, `zlib`, and `sqlite3`.
- Use one compatible firmware build with sender/head roles. The existing ML example uses ESP-IDF and the communication code uses Arduino; Arduino as an IDF component is acceptable. Choose a normally supported matching framework/component combination and record the versions in the README. Do not force the previous IDF 5.3.6 / Arduino 3.1.3 combination if it needs SDK source exclusions or feature hacks.
- Use normal installed build tools. Do not create a custom SDK/Python downloader, private toolchain installation, dependency-management framework, or elaborate bootstrap scripts. Do not insist that unused Wi-Fi libraries be entirely absent from the sender; simply do not start Wi-Fi there.
- Put role/node ID, LoRa settings, Wi-Fi credentials, server URL, and bench values in a short configuration header/template. No configuration service, JSON configuration files, or persistent settings management. Do not commit real credentials.
- Use simple Serial messages. No logging framework, class hierarchy, plugin system, generic transport abstraction, or unnecessary wrappers.
- Keep verification small and direct. No exhaustive campaign, whole-dataset hashing, generated inventories, schema framework, filesystem probes, or sprawling reports.
- Do not add persistent head outboxes, filesystem provisioning, replay after reboot, clock synchronization, TLS setup, dashboards, OTA, deep sleep, battery acquisition, or live audio acquisition.
- Simplicity does not excuse incorrect features, buffer overflow, blocking radio service, corrupt packets, silently lost queue entries, or false success reports.

A suitable deliverable is one firmware project with sender/head roles and the necessary copied ML/radio sources, the embedded clip, `receiver.py`, a few direct checks, and a concise `README.md`. Avoid extra directories and helper programs unless they serve the running demo.

## Reuse these existing pieces

- **Khalid:** `firmware/logmel/` and the committed model in `firmware/esp32_stream1s/main/generated/model_data.cc/.h`. Use its streaming frontend and TFLite Micro setup. The existing example repeats a one-second fixture; replace that behavior with the actual ten-second clip in the new copy.
- **Kareem:** the communication API, result codec, protocol, TDMA, Selective Repeat ARQ, and patched LoRa driver. Preserve its deferred IRQ servicing and useful retry/duplicate handling. Connect the application instead of recreating the radio protocol.
- **Haitham:** no runtime dependency or copied backend code for this demo.

The selected model is the single int8 `stream1s_w8_d4`, 35,344 bytes, SHA-256 `22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46`. Do not retrain it or introduce an ensemble. Inspect actual source before copying; older comments may describe a previous model or test status.

## Sensing-node behavior

Target one ESP32-WROOM-32 sensing node, ID 1, and one identical ESP32 head, ID 0. A second sensing node is future work; retain the existing two-slot TDMA behavior without implementing a separate two-node demonstration now.

Embed one real ten-second clip as a constant signed-int16 PCM array in flash: **8 kHz, mono, 80,000 samples, 160,000 bytes**. Select a suitable existing development recording marked `train` or `val` in Khalid's split manifest, with at least ten seconds available. Use the first ten seconds, preserve its gain, and record the source path/crop in the README. A directory named `test` is not itself the split assignment; do not open sealed final-holdout recordings. A small Python standard-library conversion helper is sufficient for a compatible PCM WAV.

Verify that the built image fits the actual board's flash/partition. Read PCM from flash in chunks; do not copy the full waveform into RAM. No speaker, sensor, ADC, or physical audio playback is involved.

Typing `c` at 115200 baud starts one complete run. There is no automatic startup inference. Process ten consecutive, distinct one-second windows using Khalid's exact frontend: 512-point FFT, hop 128, 59 frames, 64 HTK log-mel bands, and the model's input quantization. Keep its PCM scaling, window, mel weights, log convention, and embedded normalization unchanged. Do not add the old bandpass, MFCC extraction, gain normalization, or SNR gate.

Run the model once per window, dequantize each output, and average all ten probabilities using float32 accumulation. Submit **one mean score** through the communication API. Transmit every valid completed score, including low scores; do not apply an infestation threshold. The samples need not be paced in real time.

Print the ten window scores, final mean and its float32 bits, submission outcome, and eventual LoRa delivery status. Reject a second `c` while processing. If submission cannot be queued, report it clearly and retain that result for a simple explicit retry rather than silently losing it or submitting it twice.

Inference must not run inside the radio polling loop or its critical section. Use only the small number of FreeRTOS tasks necessary to keep radio servicing responsive. Check model allocation/inference errors and finite [0,1] output values.

## Radio and packet

Use SX1278 modules with the confirmed configuration:

| Setting | Value |
| --- | --- |
| SPI pins | SCK18, MISO19, MOSI23, CS27 |
| Control pins | RESET14, DIO0=26 |
| Radio | 433 MHz, SF7, BW125 kHz, CR4/5 |
| Other settings | Preamble 8, explicit header, PHY CRC, sync 0x34, TX 10 dBm |

Correct Kareem's application layout in the new copy:

| Application byte | Meaning |
| --- | --- |
| 0 | Node ID, 1 initially; existing codec supports 1 and 2. |
| 1 | Day in bits 7–3; battery code in bits 2–0. |
| 2–5 | Probability, IEEE-754 float32, little-endian. |

Pack byte 1 as `(day << 3) | battery_code`. Day remains 1–31 under the existing day-of-month API; battery code is 0–7. Configure a clearly labeled bench day value manually before the test. The USB-powered bench battery value is explicitly simulated 100%, code 7. Keep the public percentage API if reused, changing its quantization to `floor(percent * 7 / 100 + 0.5)`.

The application payload is **6 bytes**. Keep Kareem's 24-byte transport header and four-byte CRC32: **34 bytes total**, excluding LoRa PHY overhead. Use protocol version **5** on both boards to distinguish this correction from v4. Update both codecs, packet-length checks, diagnostics, and the few associated test vectors together. Reject old v4 packets.

Keep the existing TDMA, admission, ACK masks, batches, and selective retries. Recheck airtime-derived timing for 34-byte DATA packets; a one-byte reduction does not necessarily change airtime. Do not replace the patched driver with an upstream version that performs SPI in the GPIO ISR.

## Cluster-head behavior

The head receives and validates the existing protocol packets, decodes/logs the score, and forwards accepted DATA packets. It does not run inference, average results, or change probability values.

Use a small fixed RAM upload queue, for example 32 entries. Queue the **complete validated 34-byte DATA packet** before acknowledging acceptance over LoRa. Exact radio duplicates must not create another queued event. If the queue is full, do not overwrite entries or claim acceptance; report the condition and let the existing sender retry limits apply.

Replace the copied head's filesystem-storage dependency with this RAM acceptance path for the demo. No LittleFS, custom storage partition, or flash queue is needed. A LoRa ACK now means accepted into the head's RAM, **not persisted on the computer**. Pending records are lost on a head restart; document that limitation plainly.

Run the Wi-Fi HTTP uploader separately from radio servicing. Only the radio task owns LoRa/SPI. Use short network timeouts and simple delayed retries for connection failures/5xx responses. Keep the same queued packet until server storage is confirmed. Report permanent request errors rather than silently discarding data or endlessly hiding the failure.

## Local receiver and forwarding contract

Use a minimal direct-binary HTTP interface instead of the previous JSON envelope. The head already has the correct packet, so forwarding it directly avoids an ESP JSON library, decimal float rounding, extra metadata, and duplicate serialization logic.

- The head connects by Wi-Fi directly to the computer's LAN address, such as `http://192.168.x.x:8080/results`. USB is never a data relay. Both devices need network reachability; `localhost` on an ESP is not the computer.
- `receiver.py` listens on a configurable address/port, default port 8080, using Python's standard HTTP server. This is plain HTTP on a trusted local bench network, not a public deployment. No Docker, web framework, dashboard, authentication system, or external Python package is required.
- `POST /results` sends `Content-Type: application/octet-stream` and exactly the **34 raw packet bytes**. Do not send control packets, raw audio, MFCCs, or a JSON probability.
- The receiver validates the packet's length, magic, version, DATA type, payload length, relevant header fields, CRC32, matching transport/application node IDs, valid day, and finite probability in [0,1]. Use the copied protocol as the decoding reference; do not invent offsets or byte order.
- Print the node, event identity, day, test battery code, probability, and probability bits. Store them with the raw packet and server ingestion time in one SQLite database. Server time is when storage occurs, not the source WAV date or an exact sender measurement time. No clock synchronization is needed.
- Identify events by `(node_id, sender_boot_token, measurement_id)` for this single-head demo. Add a SQLite unique constraint. An identical result resent with the same identity is a duplicate; differing retry/batch/session transport fields do not make it a new measurement. If that identity carries different six-byte application data, return `409` and preserve the original row.
- Return `201` only after committing a new row, or `200` for a matching duplicate. For either success, echo the exact submitted **34 bytes** as the response body. The head checks the status and exact echo before removing its queued record. This preserves the score bits and avoids a JSON response parser.
- Return a clear non-success response for invalid requests or storage failure. Bound request/body reads, timeouts, and response sizes. Network timeouts or server failures leave the head record pending for retry. A server commit followed by a lost reply is resolved by the duplicate path on retry.

The boot token is the existing random 32-bit transport identity, not a globally unique guarantee. Conflicting identity reuse must be visible. No durable or unlimited delivery guarantee is claimed.

## Verification and deliverables

Implement the whole path before handing it back. Use a few direct host checks for packet layout/CRC, float preservation, receiver duplicate/conflict handling, and invalid-input rejection. Existing portable communication tests can be reused where convenient; do not build a new testing framework or expand into exhaustive campaigns.

Provide these practical checks and their exact commands in a short README:

1. Build and flash the sender and head using ordinary supported tools. Configure the radio pins, bench day, Wi-Fi, and computer URL. Identify boards by their logged role, not permanent USB-port assumptions.
2. Start `receiver.py` and monitor both boards at 115200 baud.
3. Type `c`. Confirm ten actual one-second model outputs, one mean, one corresponding head reception, and one committed server row.
4. Compare the final float32 bits printed by the sender, decoded by the head, and stored by the receiver. They must match exactly. If an existing host reference for the selected clip is available, compare inference against it; do not claim host/model parity without actually checking it.
5. Repeat a few runs and verify distinct measurement IDs and one stored row per run. Briefly stop/restart the receiver and confirm pending RAM entries retry without duplicate rows. Do not require reboot recovery.

Keep normal logs and a concise statement of what was tested. If boards are unavailable, still finish the code, build it, run the small host checks, and supply exact hardware-run instructions; state that physical inference/RF/uplink checks remain unverified. Do not substitute fake predictions for the embedded model or claim a successful hardware pipeline from compilation alone.

The finished deliverable must let the user run **embedded audio -> on-ESP model -> LoRa -> ESP32 head -> Wi-Fi -> local score storage** without writing missing integration code. Live acquisition, batteries, two-node testing, stronger outage recovery, and an updated dashboard can be added later; none is required now.
