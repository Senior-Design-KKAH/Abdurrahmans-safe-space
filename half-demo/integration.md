# Half-demo: current status and continuation handoff

**Updated 3 November 2026, after implementation, physical sensing-node tests,
transmitter-only tests, and the GitHub publication.** Read this section before
acting on the original implementation request preserved below. The source is
implemented; do not restart the project or substitute scaffolding. The complete
physical sender -> head -> Wi-Fi -> SQLite path still needs validation.

Start with [AGENTS.md](AGENTS.md) for agent instructions and the source map,
[README.md](README.md) for operating commands, and [verification.log](verification.log)
for the executed checks. Do not flash/reset hardware, transmit, open protected
data, or retrain merely to update documentation.

## Completed implementation and its boundaries

The normal project now contains the complete intended code path:

```text
Embedded real ten-second PCM -> unchanged streaming log-mel -> real int8 model
  -> one float32 mean -> communication API -> RPW v5 LoRa DATA
  -> head RAM acceptance before ACK -> independent Wi-Fi HTTP uploader
  -> standard-library receiver.py -> validated, committed SQLite result
```

- One ESP-IDF project, with sender ID1/head ID0 and explicit standalone test
  modes. Source, model and embedded clip are all in this folder. No sibling
  backend or audio dataset is needed to build/run the committed demo clip.
- Existing TDMA, admission, partial batches, selective retries, ACK masks and
  useful duplicate/conflict handling were retained. The patched LoRa driver
  services SPI/IRQ callbacks in task context, not in the GPIO ISR.
- `c` starts one ten-window model run; no inference runs automatically at boot.
  Busy commands are rejected. All valid means, including low scores, are
  submitted. An unqueued result is retained for explicit `r` retry; successful
  submission cannot be duplicated with `r`.
- Only the radio task owns LoRa/SPI. Inference and head networking run separately
  from radio polling and outside the communication queue critical section.
- Corrected application codec: six bytes, `(day << 3) | battery_code`, a 3-bit
  battery code and IEEE-754 float32 probability in little-endian order. Normal
  **RPW v5 DATA is 34 bytes**: header24 + application6 + CRC32(4). Reject v4.
  Percentage battery API uses `floor(percent*7/100+0.5)`.
- Head accepts the complete validated packet into a fixed 32-entry RAM upload
  queue before ARQ commit/ACK. Duplicates do not create another queued event;
  a full queue does not overwrite or acknowledge a new entry. History is bounded.
- Head POSTs the exact 34 bytes, `Content-Type: application/octet-stream`, to
  the computer's LAN `http://...:8080/results`. The independent uploader retains
  the front packet until status200/201 and an exact echo. It handles connection
  failures, timeouts, truncated/null response streams and delayed retries.
  Permanent errors halt uploads with the packet retained; head `r` resumes.
- `receiver.py` uses only Python's standard library. It validates framing,
  header/application fields, CRC and finite score range; stores the raw packet,
  application bytes, exact probability bits and UTC ingestion time in SQLite.
  Identity `(node_id, sender_boot_token, measurement_id)` is unique. Committed
  new row ->201; matching duplicate ->200; different application data under
  that identity ->409 preserving the original. Success echoes the submitted
  packet, even when a retry's transport metadata differs from the original row.
- **Queued is not Delivered. LoRa ACK/Delivered means head RAM acceptance, not
  computer persistence.** Pending head uploads and sender state are volatile.
  Reboots lose pending state; no durable/unlimited or globally unique delivery
  guarantee is claimed. USB is only for power/flashing/commands/logs.

## Verified results: distinguish software, inference and RF evidence

| Area | What actually passed | What it does not establish |
|---|---|---|
| Builds | Normal sender, head, ML-only and TX-only variants compiled | Physical head or complete system operation |
| Host checks | Four small native/HTTP/SQLite test groups; actual codec/CRC/bits, battery, queue/status, TDMA/ARQ, commit/duplicate/conflict/invalid/storage-failure paths | Reception or Wi-Fi from a physical head |
| Original clip on ESP | Two ten-window runs; all 20 output scores and both means matched the host reference exactly | Diagnostic accuracy across unseen trees/devices |
| Seven-folder ESP comparison | All 70 one-second outputs and seven float32 means matched the host reference | False-positive performance or a calibrated infestation probability |
| Local LoRa TX, SPI1 MHz | 10/10 34-byte packets completed; ten FIFO readbacks and ten deferred DIO0 TxDone callbacks; 77–78 ms, mean77.1 ms | Reception, ACKs, RF power, range or antenna efficiency |
| Local LoRa TX, SPI4 MHz | Only three completed before packet4 timed out (IRQ00, mode80, DIO0 low) | Reliable operation at 4 MHz or a proven cause of failure |

No cluster head or RF receiver was connected during these physical tests.
The full normal-protocol sender/head exchange, physical head Wi-Fi uploader,
computer ingestion from that head, and physical outage/retry behavior remain
**unverified**. The original program's “no boards available” condition was
superseded by the recorded ML and local TX checks, not by a full-system test.

Evidence: [hardware-test.log](hardware-test.log),
[folder-comparison.md](folder-comparison.md),
[folder-comparison.json](folder-comparison.json),
[folder-comparison.csv](folder-comparison.csv),
[folder-comparison.log](folder-comparison.log),
[lora-test.md](lora-test.md), and [lora-test.log](lora-test.log).

## Frozen model, restored clip and score interpretation

- Single int8 **stream1s_w8_d4**, **35,344 bytes**, SHA-256
  `22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46`.
  It was reused without retraining or an ensemble.
- The committed/restored waveform is the first ten seconds of
  `field/field/test/folder_26/F_20200529180809_1_T26.4.wav`, assigned **val**
  in Khalid's split manifest. It is 80,000 signed-int16 samples, 160,000 bytes,
  8 kHz mono, original gain, in flash. Read it in 160-sample chunks; do not buffer
  the whole waveform or repeat a one-second fixture ten times.
- Preserve PCM/32768, periodic Hann512, FFT512, hop128, 59 frames, 64 HTK log-mel
  bands, power/512, natural log floor1e-10 and tensor-derived int8 quantization.
  Embedded normalization stays in the graph. No MFCC, bandpass, gain normalization,
  SNR gate or decision threshold was added. Ten dequantized outputs are averaged
  with float32 accumulation.
- Original clip mean: **0.989062488**, exact float32 bits **`3f7d3333`**, displayed
  as **98.91%** after rounding. This is the fraction that a normal score packet
  would carry. Measured ten-second-clip processing time was approximately
  **4.91 s**; tensor arena use **40,652 B** of **65,536 B** reserved.
- Host comparison used the unchanged compiled C frontend with `LOGMEL_USE_LOGF`
  and ai-edge-litert2.3.0 `BUILTIN_REF` on identical model/PCM bytes. Report parity
  only for these actual tested outputs; these optional host tools are not runtime
  dependencies of the receiver or ESP firmware.
- A high model score is not a verified, calibrated real-world chance of infestation
  or an infestation-severity percentage. The waveform and firmware were restored
  after the folder comparison, and a physical rerun reconfirmed `3f7d3333`.

## Folder comparison and protected data

One alphabetically first **available**, manifest-listed recording with at least
10 seconds was selected per eligible folder, without score-based selection.
The first 10 seconds were flashed/run on the ESP, with unchanged gain. Actual
filenames and per-window values are in the comparison files linked above.

| Folder | Manifest assignment | ESP mean | Workbook AUDIO / CONFIRMATION / VISUAL |
|---|---|---:|---|
| 26 | val | 98.91% | 1 / YES / B |
| 27 | val | 91.13% | 1 / YES / B |
| 28 | train | 95.27% | 1 / YES / B |
| 29 | sealed test | Not evaluated | 1 / YES / B |
| 30 | val | 91.05% | 1 / YES / B |
| 31 | train | 95.20% | 1 / YES / B |
| 32 | sealed test | Not evaluated | 1 / YES / B |
| 33 | train | 96.64% | 1 / YES / B |
| 34 | val | 97.77% | 1 / YES / B |

The seven selected means average **95.14%**, median **95.27%**, range
**91.05–98.91%**. The workbook source is `TreeVibes.xlsx`, `Sheet1!E27:H35`;
visual legend `J4:K4` defines B/Β as “exit tunnels, low infestation.” These are
positive folder-level acoustic labels with folder-level confirmation. Earlier
conversation wording that confirmation was unavailable was too broad: the
workbook does say YES. Per-clip/species confirmation and calibrated probabilities
are separate questions. With no negative-labelled folders and only one crop per
folder, do not claim overall accuracy, specificity, false-positive rates or
independent evaluation; the original train/val assignments are mixed here.

`difficult_cases` was explicitly excluded. **Folders29 and32 remain unopened and
unscored**. A question about exposing their sealed holdout scores was left pending;
the user moved on to other tasks without authorizing it. Do not infer permission
from subsequent radio, documentation or GitHub work. Check actual split membership
before opening audio; a directory named `test` can contain train/val recordings.
The locked manifest is at:

`Khalid/outputs/real-training-cuda/full/treevibes-984e68c4e99668c8/splits.csv`

Preserve the original dataset, workbook, manifest/lock and model. No alarm or
transport threshold has been calibrated or applied in this demo. Do not start
training, a whole-dataset scan or final evaluation as a side effect
of continuing this integration. `embed_clip.py` intentionally accepts only train/val.

## Radio settings, SPI finding and last observed board state

Pins are unchanged: **SCK18, MISO19, MOSI23, CS27, RESET14, DIO0=26**, 3V3/common
ground. Normal/test RF settings are **433 MHz**, SF7, BW125 kHz, CR4/5, preamble8,
explicit header, PHY CRC, sync**0x34**, configured TX**10 dBm**.

**SPI1/4 MHz describe the ESP32-to-radio bus clock, not the 433 MHz wireless
frequency.** `cfg::RadioTestSPI=1000000` affects only the standalone test. Normal
`radio.cpp` still starts with the driver's default 8 MHz and falls back to 1 MHz
on initialization failure. Its runtime has not been changed/validated at 1 MHz.
The 4 MHz timeout is an unresolved reliability issue; slower-clock success does
not prove the cause. Revisit the normal SPI setting deliberately before claiming
reliable normal-protocol operation on this hardware.

The TX-only test sends **34-byte ASCII diagnostics**, not ML results or RPW DATA.
It bypasses admission only in its explicit test build and does not wait for ACKs.
Normal sender correctly waits for a real head beacon/admission before DATA.
One intermediate test incorrectly demanded TX mode immediately after async start
and was excluded; mode may still read standby81 immediately. The final test waits
for deferred TxDone with a one-second timeout.

At the end of the radio test, the connected board's **pre-existing diagnostic
firmware was restored**, full-flash hash verified, and its original idle prompt
observed. It was **not left running half-demo ML-only or TX-only firmware**.
Its startup advertised 9-byte synthetic packets, sync0x12, SPI4 MHz and `H` help;
`B 10 1000` waited for a receiver START handshake and sent no DATA without one.
It does not match the normal half-demo 34-byte/sync0x34 contract.

Only one sensing board was supplied for each test; different ESP32 identities
appeared between ML and radio sessions. Historical port `/dev/ttyUSB0` is not a
permanent board identity. Re-enumerate and inspect the actual target/boot role.
ESP32-D0WD-V3 rev3.1 boards reported 4 MiB physical flash. A private recovery
image existed at `/tmp/half-demo-pre-radio-test-flash.bin`; it may be gone in a
new session, may contain device settings, and must not be published or restored
to a different board. Restoration already completed; do not repeat it by default.

## Build, configuration and short verification commands

Verified stack: **ESP-IDF5.5.3 + Arduino-ESP32 component3.3.7 + esp-tflite-micro1.3.5**,
Python**3.12**, Xtensa GCC14.2.0, official IDF5.5 Python constraints/esptool4.12.0.
Keep `main/idf_component.yml` and `dependencies.lock`. Native `idf.py`/CMake/Ninja
is the supported path. Python3.14 hit a pinned dependency incompatibility; a PIO
attempt hit generated dependency-file handling. No SDK patches, source exclusions
or custom toolchain downloader were needed.

Run from **`half-demo/`** in an activated ESP-IDF shell. Both test flags are
sender-only and mutually exclusive; specify them explicitly because CMake caches
persist. Local `build-radio-test/` originally held copied flash artifacts rather
than a complete build tree; the fresh `build-radio-only` example avoids that cache.

```sh
# Normal roles
idf.py -B build-sender -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.sender build
idf.py -B build-head -DDEMO_NODE_ID=0 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.head build

# No head/radio needed: wait for SENDER ready, then c
idf.py -B build-ml-test -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=ON -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.ml-test build

# No receiver needed: wait for READY, then t (ten diagnostic packets)
idf.py -B build-radio-only -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=ON -DSDKCONFIG=sdkconfig.radio-test build

# Identify target first; replace example port and build directory appropriately
python -m serial.tools.list_ports
python -m esptool --chip esp32 --port /dev/ttyUSB0 flash_id
idf.py -B build-ml-test -p /dev/ttyUSB0 -b 115200 flash monitor

# Standard-library receiver and small host/native checks
python test/check.py
python receiver.py --bind 0.0.0.0 --port 8080 --db results.sqlite3
```

No actual build/flash/test above was rerun for this documentation update. Earlier
four-variant builds and host/hardware checks are the evidence recorded here.
Monitor baud is115200; exit with Ctrl+]. Images use conservative 2 MiB flash
configuration and a standard 1 MiB factory app partition at0x10000. Recorded normal
sender/head sizes were **539,712 / 870,912 B** and fit. Check the actual board's flash.

Credentials/URL belong in ignored `main/config.local.h`, copied from its example
without overwriting existing local settings. Set the manual bench day (currently3)
before testing. USB battery is simulated100%, code7. Head URL must be the computer's
reachable LAN address; `localhost` on the ESP is not the computer. ML-only mode
queues in volatile RAM without draining radio; reset between extended tests when
its 32-entry queue fills. `r` retries an unqueued sender result, or resumes a halted
head uploader according to role. It is not a resend command for an already queued
result.

On this laptop `idf.py` was not globally on PATH. [AGENTS.md](AGENTS.md) records
an activation recipe using already installed tools under `$HOME/.platformio/`.
Use normal installed tools; do not recreate/download a private SDK. Temporary
reference libraries, LiteRT installation and capture scripts under `/tmp` are
optional session artifacts, not required project files or guaranteed future tools.

## GitHub publication and workspace layout

Repository: https://github.com/Senior-Design-KKAH/Abdurrahmans-safe-space
(default branch **master**). Initial reorganization/publish commit:
**`2ee91edeecf2151abb4f1e65153454c5fb72b768`**. This handoff update follows that
commit; inspect the latest branch head rather than treating that SHA as permanent.

```text
repository/
  sensing/     # every file previously in that GitHub repository, preserved
  half-demo/   # 51 project files at initial publication, honoring .gitignore
```

The user explicitly chose **project files only**. Build caches, managed dependency
caches, generated local SDK configurations, `__pycache__`, real credentials and
private flash backups were not uploaded. `AGENTS.md`, the embedded model/clip,
source, lockfile, reports and normal test logs were included. The original remote
project was preserved byte-for-byte as the `sensing/` subtree, with commit history
retained. Do **not** repeat the initial relocation on a later update; that would
incorrectly nest the already reorganized repository inside another `sensing/`.

The earlier publish used an isolated temporary Git repository and verified that
all local files stayed unchanged. For this handoff refresh, only local
`integration.md` is being edited and synchronized to the matching GitHub file;
other local files and cloud project contents remain unchanged. The original local workspace has
not exposed a usable Git repository; do not initialize/reorganize it just to push.
Use an isolated checkout, preserve concurrent remote commits, and push normally.

A fresh GitHub clone does **not** contain the local workspace's `Khalid/`,
`Kareem/`, `Haitham/`, original `field/` recordings or `TreeVibes.xlsx`. The cloud
`sensing/` is the archived previous remote project, not automatically a copy of
the local `sensing/` directory. Historical source/audio paths in reports refer to
the original workspace. The embedded clip/model and committed demo source are
self-contained; do not assume external training data transfers with Git.

## Concrete next steps for a new session

1. Read this handoff and `AGENTS.md`, inspect current source/settings, then identify
   actual hardware and installed firmware. The last user setup had no head or
   receiver. Do not infer one is now present or reuse a stale port/backup blindly.
2. Resolve/validate the normal sender's SPI operating clock on the target board;
   only the standalone 1 MHz run has clean ten-packet evidence. Preserve deferred
   IRQ servicing and normal TDMA/admission. Do not treat ASCII TX-test packets as
   proof of normal RPW v5 delivery.
3. When a correctly wired head is available and hardware work is requested, build
   and flash normal sender/head roles with both test flags OFF. Configure matching
   v5/sync0x34 RF settings, bench metadata, head Wi-Fi and the computer LAN URL.
4. Start `receiver.py`; type `c` on the sender. Confirm ten windows, one mean,
   one head acceptance and one committed SQLite row. Compare the **exact float32
   bits** printed by sender/head and stored as `probability_bits` with raw length34.
5. Repeat runs to check distinct measurement IDs and one stored row per event.
   Stop/restart the receiver using the same database and verify pending head RAM
   records retry without duplicate rows. A LoRa ACK is still only RAM acceptance;
   head reboot recovery remains outside scope.
6. Keep model/split/clip fidelity and scientific limits intact. Exposing sealed
   folders29/32 is a separate, still-pending user decision. Update the evidence
   and handoff with actual new results and failures, not assumed success.

---

## Original implementation requirements (preserved verbatim)

The following is the original task/specification. Its imperative wording records
the requested deliverable; it does not mean the completed source/tests above are
missing or should be discarded. Continue the existing implementation.

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
