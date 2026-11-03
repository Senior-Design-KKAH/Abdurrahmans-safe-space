# Agent handoff: half-demo

Last updated: 3 November 2026, after the sensing-node ML and transmitter-only
hardware checks. This is a repository handoff, not a claim that the complete
physical pipeline has passed. Recheck connected hardware and current source;
ports, installed firmware, temporary files and build caches can change.

## Read first and respect scope

Read [integration.md](integration.md), [README.md](README.md),
[verification.log](verification.log), and the affected source before changes.
For hardware work, also read [hardware-test.log](hardware-test.log),
[lora-test.md](lora-test.md), and [lora-test.log](lora-test.log).
For recording comparisons, read [folder-comparison.md](folder-comparison.md).
Later physical results supersede earlier statements saying hardware was absent.

- Keep new demo work in `half-demo/` unless the user explicitly expands scope.
  Preserve `Khalid/`, `Kareem/`, `Haitham/`, `sensing/`, original audio, the split
  manifests/lock, and `TreeVibes.xlsx`. They are reference sources, not runtime
  dependencies. Retain copied-source provenance/licenses in [NOTICE.md](NOTICE.md).
- Implement requested work to completion with the authorization already given.
  Do not create new approval gates for routine reversible work. Documentation-only
  requests do not authorize another flash, reset, transmission, or data evaluation.
- Keep the student prototype small: plain C/C++, fixed buffers, focused functions,
  simple Serial output, normal installed tools. No production framework, SDK hacks,
  custom downloader/toolchain installer, persistent outbox, filesystem provisioning,
  dashboard, live acquisition, OTA, TLS deployment, or unrelated model training.
- `sensing-to-payload-implementation.md` is only a simplicity/style reference.
  Do not import its MFCC, acquisition, compression, packet or hardware requirements.
  `main.pdf` supplies no technical requirements for this demo.
- Do not publish credentials, raw flash backups, local Wi-Fi configuration or build
  caches. Use ignored `main/config.local.h` for credentials; copy its example only
  if needed and do not overwrite an existing local configuration.
- Preserve existing user changes. This workspace has not exposed a usable Git
  repository to earlier commands; a failed `git status` is not proof of a clean tree.

## Objective, architecture and source map

```text
80,000 int16 PCM samples in sensing ESP32 flash
  -> ten distinct 1-second streaming log-mel windows
  -> ten on-ESP int8 model invocations
  -> one float32 mean
  -> communication API -> RPW v5 LoRa DATA
  -> ESP32 head accepts full packet into RAM before ACK
  -> independent Wi-Fi HTTP uploader -> receiver.py -> committed SQLite row
```

Sender ID is 1, head ID is 0; both target ESP32-WROOM-32. Codec and TDMA retain
node-2/two-slot support, but this demo does not build a second sensing-node role.
No cluster head or RF receiver was connected during the recorded hardware tests.
USB supplies flashing, commands and logs; it is never the normal data relay.

| File(s) | Responsibility |
|---|---|
| `main/Config.h`, `config.local.h.example` | Pins/radio settings, bench day/battery, Wi-Fi and server URL |
| `main/inference.cpp`, `inference.h` | Model initialization, flash chunks, frontend and ten-window mean |
| `main/logmel/` | Unchanged copied C frontend and coefficient tables |
| `main/generated/model_data.cc/.h` | Committed selected model |
| `main/generated/clip.cc/.h`, `embed_clip.py` | Committed ten-second PCM and development-WAV crop helper |
| `main/radio.cpp` | Normal radio initialization, deferred polling, head admission/acceptance, TDMA/ARQ |
| `main/communication_api.*`, `communication_internal.h` | Thread-safe result submission, queue/status lifecycle |
| `main/MLResult.h`, `ResultQueue.h`, `RPWProtocol.h`, `TDMA.h`, `SelectiveRepeatARQ.h` | Portable codecs, immutable records, framing, timing, retries |
| `main/application.cpp`, `application_integration.h` | Sender worker/commands; head RAM queue and independent uploader |
| `main/radio/LoRa.*` | Vendored LoRa 0.8.0 with deferred GPIO IRQ patch; MIT license retained |
| `main/lora_tx_test.cpp` | Explicit standalone, bounded TX-only diagnostic, selected at build time |
| `receiver.py` | Python standard-library binary HTTP receiver, validation, dedup/conflicts and SQLite |
| `test/check.py`, `packet_check.cpp`, `api_check.cpp` | Small native C++ and real HTTP/SQLite checks |

Normal Arduino radio loop runs on core 1; inference/uploader worker runs on core
0 according to role. Only the radio task owns LoRa/SPI. Never put inference,
HTTP, filesystem access, callbacks, heap allocation or long waits under the
communication queue critical section or in the GPIO ISR. Keep queue snapshots
copied across tasks; do not retain internal record pointers.

## Model, audio and application invariants

- Single int8 **stream1s_w8_d4**, **35,344 bytes**, SHA-256
  `22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46`.
  No ensemble, retraining or model substitution without a new user request.
  Older source comments can refer to `stream1s_w8`; trust the actual model/hash.
- Current committed clip is the first ten seconds of
  `../field/field/test/folder_26/F_20200529180809_1_T26.4.wav`, assigned **val**
  in Khalid's locked split. It is 8 kHz mono signed-int16 PCM, exactly 80,000
  samples/160,000 bytes, unchanged gain, stored in flash. It was restored after
  the multi-folder comparison and physically rerun to confirm the original mean.
- Feed flash in 160-sample chunks; do not copy the whole waveform into RAM.
  Each inference uses a consecutive distinct 8,000-sample window; never repeat
  the old one-second fixture ten times or substitute synthetic predictions.
- Preserve PCM/32768, periodic Hann512, FFT512, hop128, 59 frames, power/512,
  64 HTK mel bands, natural log floor1e-10 and input int8 rounding/quantization.
  Build defines `LOGMEL_USE_LOGF`. Normalization is already in the model graph.
  No bandpass, MFCC, gain normalization, SNR gate or infestation threshold.
- Derive quantization from actual tensors. Current input scale/zero are
  approximately `0.0945814773440361 / 115`; output is `0.00390625 / -128`.
  These are sanity references, not replacements for runtime tensor metadata.
- Check schema, arena/tensor allocation, invocation errors, int8 tensor sizes,
  positive finite scales and finite output probabilities in [0,1]. Internal
  tensor arena reservation is 65,536 bytes; measured use was 40,652 bytes.
- Type **c** at 115200 baud to start; there is no automatic startup inference.
  Reject another `c` while busy. Print ten scores, their sample ranges, one mean
  and its bits, submission outcome/ID and eventual LoRa status when available.
- Average all ten dequantized probabilities with float32 accumulation. Submit
  every valid mean, including low values. `0.989062488` represents 98.9062488%
  when displayed as a percentage; transmit the float fraction without decimal
  reserialization. A model score is not a verified calibrated infestation chance.
- Preserve the public `submitInferenceResult(probability, day, batteryPercent, &id)`
  contract. Queued is not Delivered. If submission fails, retain that score for
  explicit **r** retry; prevent a successful score from being submitted twice.
  Failed LoRa delivery means unconfirmed: the head may have received a packet
  whose ACK was lost. IDs/status/history and queues are bounded volatile RAM.
- Bench day must be manually set to 1..31 before testing (currently 3). USB bench
  battery is explicitly **simulated 100%, code 7**. No real battery/date acquisition.

## Wire format and server contract: do not regress to Kareem v4

Current normal protocol is **RPW v5**. Reject v4. DATA is **34 bytes**, comprising
a 24-byte transport header, six application bytes and four CRC32 bytes.

| Packet offset | Field |
|---|---|
| 0..3 | Magic `0xa7`, version `5`, DATA type `3`, transport node ID |
| 4..7 | Head/session run ID, uint32 little-endian |
| 8..11 | Random sender boot token, uint32 little-endian |
| 12..15 | Batch ID, uint32 little-endian |
| 16..19 | Measurement ID, uint32 little-endian |
| 20..23 | Index, count, attempt, application length (`6`) |
| 24 | Application node ID, matching transport node |
| 25 | `(day << 3) | battery_code` |
| 26..29 | IEEE-754 float32 probability, little-endian |
| 30..33 | CRC32 of first 30 bytes, little-endian |

Nodes 1/2, day 1..31, battery 0..7, finite probability [0,1]; nonzero run, boot
token, batch and measurement; count 1..8, index<count and attempt<5. Percentage
API battery rounding is `floor(percent*7/100+0.5)`. Do not restore the old seven
application bytes, 4-bit battery or 35-byte DATA layout. Change C++/Python codecs,
checks and vectors together if the user requests a future version.

Keep admission, both TDMA slots, ACK masks, partial batches of 1..8 records,
selective missing-record retries, duplicate/conflict checks and bounded attempts.
At current radio settings, 34-byte DATA occupies the same symbol count as old
35-byte DATA: rounded airtime78 ms, cell108 ms, slot1099 ms, frame2258 ms.
Normal sender waits for a real head beacon/admission before DATA. A missing
head must not be diagnosed by silently bypassing normal TDMA.

The head queues the complete validated 34-byte packet before committing ARQ
acceptance/ACK. Upload queue capacity is 32; duplicate history is bounded to 256.
Exact duplicates do not enqueue again. Full/unavailable queue must leave new
records unacknowledged, never overwrite entries or claim acceptance. Identity
conflicts must remain visible.

Uploader: POST exactly 34 raw bytes to the computer's reachable LAN
`http://...:8080/results`, `Content-Type: application/octet-stream`. No JSON,
audio, feature payloads, USB relay, `localhost` ESP URL or Haitham backend.
Use short timeouts/delayed retries; current connect timeout is 1.5 s, reads2 s,
retry delay3 s. Guard a null `HTTPClient::getStreamPtr()` after a truncated reply.
Only status200/201 plus an exact 34-byte echo permits removing the front queue
entry. Permanent errors halt uploads and retain the packet; head **r** resumes
once the cause is corrected. A missing/incorrect echo remains pending.

Receiver uses only Python's standard library. SQLite event identity is
`(node_id, sender_boot_token, measurement_id)`, enforced unique. New row returns
201 only after commit; matching application bytes return200 even if retry,
batch/session metadata differ. Both successes echo the exact submitted packet.
Different six-byte application data under the same identity returns409 and
preserves the original row. Invalid/storage requests return non-success.
Store raw packet, application data, exact float bits, score and UTC ingestion
time. Ingestion time is server storage time, not source recording/measurement time.

**LoRa ACK means head RAM acceptance; it does not mean computer persistence.**
Head restart loses pending uploads. No reboot recovery, durable/unlimited delivery,
global uniqueness or exactly-once radio guarantee is claimed. Boot token is only
32 random bits. Keep these limitations explicit.

## Build tools, roles and commands

Use normal **ESP-IDF 5.5.3**, **Arduino-ESP32 component 3.3.7**,
**esp-tflite-micro 1.3.5**, Python **3.12** and Xtensa GCC14.2.0.
`main/idf_component.yml` pins direct dependencies; retain `dependencies.lock`.
Official IDF5.5 Python constraints were used, including esptool4.12.0.
Python3.14 could not install a pinned build dependency. No custom SDK patches or
source exclusions are needed. A PlatformIO build hit a generated dependency-file
issue; the supported deliverable uses native `idf.py`/CMake/Ninja, not a new PIO
configuration or a private downloader.

Run commands **from `half-demo/`** in an activated ESP-IDF shell. Keep relative
build directories within this folder; calling `idf.py -C ... -B relative-path`
from the parent previously created a disposable build outside scope.
Always set both test flags explicitly when switching modes: CMake caches persist.

```sh
# Normal sensing node and head
idf.py -B build-sender -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.sender build
idf.py -B build-head -DDEMO_NODE_ID=0 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.head build

# Model test without head or working radio; type c after SENDER ready
idf.py -B build-ml-test -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=ON -DDEMO_LORA_TX_TEST=OFF -DSDKCONFIG=sdkconfig.ml-test build

# Local radio TX test without a head; type t after READY
idf.py -B build-radio-test -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=ON -DSDKCONFIG=sdkconfig.radio-test build

# Enumerate first; replace example port with the verified board
python -m serial.tools.list_ports
python -m esptool --chip esp32 --port /dev/ttyUSB0 flash_id
idf.py -B build-ml-test -p /dev/ttyUSB0 -b 115200 flash monitor

# Host checks and local receiver
python test/check.py
python receiver.py --bind 0.0.0.0 --port 8080 --db results.sqlite3
```

Monitor baud115200; exit IDF monitor with Ctrl+]. Flash baud115200 is conservative;
460800 worked for later flashes. Check success and flash hash verification, and
retry conservatively if serial programming fails. Close monitors before flashing.
The standard single-app partition is at `0x10000`, size `0x100000` (1 MiB), in a
conservative 2 MiB image configuration. Tested ESP32s reported 4 MiB physical
flash; verify each target rather than assuming that. Recorded normal sender/head
images were 539,712 / 870,912 bytes, both fitting the app partition.

Test flags are sender-only and mutually exclusive. ML-only queues scores but
never services radio; its 32-entry volatile queue fills without a head, so reset
between extended tests. TX-only selects `lora_tx_test.cpp` instead of normal
application sources. It sends **ASCII diagnostics**, with no ML/RPW framing, ten
34-byte packets with 1000 ms gaps. It requires identity/configuration/FIFO
readback and deferred DIO0 TxDone; timeout stops the test. Neither test mode is
an end-to-end demo and neither may weaken normal sender admission.

`build-radio-test/` initially received copied verified binaries from a temporary
TX-mode build in `build-ml-test/`; it may lack a complete CMake project/cache.
Build from source before relying on `idf.py flash/monitor` there, or use a fresh
build directory. The last recorded `build-ml-test` cache was restored to
ML-only=ON, TX-test=OFF. Never assume a cached binary matches its directory name;
inspect flags, rebuild, and identify the flashed role from boot output.

On this laptop, `idf.py` was not globally on PATH. If an ordinary activated IDF
shell is unavailable, these **already installed** tools were used (verify paths):

```sh
export IDF_PATH="$HOME/.platformio/packages/framework-espidf"
export IDF_TOOLS_PATH="$HOME/.platformio/penv"
export IDF_PYTHON_ENV_PATH="$HOME/.platformio/penv/.espidf-5.5.3"
export ESP_ROM_ELF_DIR="$HOME/.platformio/packages/tool-esp-rom-elfs"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$IDF_PATH/tools:$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin:$PATH"
idf.py --version
```

The original unpinned system `pio` uses Python3.14; do not delete/recreate global
IDF environments or disable dependency checks as a first troubleshooting step.
Use installed Python3.12 and official constraints. `/tmp/half-demo-*` scripts,
LiteRT installation, reference shared library and backups are session artifacts,
not required project dependencies and not guaranteed to survive a restart.

## Hardware state, evidence and remaining work

The user last supplied **only one sensing board**, with no cluster head/receiver.
Serial port `/dev/ttyUSB0` was used, but different ESP32 identities appeared
between the ML and later radio sessions. Re-identify the target; never use a
port name alone as board identity or restore a backup to a different board.

| SX1278 signal | ESP32 GPIO |
|---|---:|
| SCK / MISO / MOSI | 18 / 19 / 23 |
| CS / RESET / DIO0 | 27 / 14 / 26 |
| Supply | 3V3 and common ground |

Keep 433 MHz RF, SF7, BW125 kHz, CR4/5, preamble8, explicit header, PHY CRC,
sync0x34 and configured TX10 dBm. **SPI1/4 MHz are ESP-to-radio bus clocks;
433 MHz is the wireless frequency.** Do not conflate them or remap pins based on
guesswork. Use a suitable attached antenna for transmit checks.

Verified on 3 November 2026:

- All four build variants compiled; four small native/HTTP/SQLite test groups
  passed. Host checks cover the actual codec/CRC/float bits, battery, queue/API
  status/limits, TDMA/ARQ and real HTTP commit/duplicate/conflict/invalid/storage
  responses. They do not prove hardware reception or actual head uploading.
- Initial ML board booted the model successfully in ML-only mode after its radio
  init returned RegVersion00. Two complete runs (20 windows) matched the host
  reference output bits. Original clip mean **0.989062488**, bits **3f7d3333**,
  approximately **4.91 s/run**, tensor arena **40,652/65,536 B**. Idle startup,
  busy `c` rejection, IDs1/2, and no duplicate submission on `r` also passed.
- Seven development-folder clips were physically inferred: folders26,27,28,30,
  31,33,34. All **70 window outputs and seven means** matched the host reference.
  Mean scores were **98.91%, 91.13%, 95.27%, 91.05%, 95.20%, 96.64%, 97.77%**,
  respectively. Overall selected-clip mean95.14%, median95.27%. This is a small
  descriptive sample, with one unchanged-gain ten-second crop per folder.
- Later connected radio responded with RegVersion12 in 16/16 reads. Dedicated
  TX-only test at **1 MHz SPI** completed **10/10 34-byte packets**, with ten
  exact FIFO readbacks and ten deferred GPIO26/DIO0 TxDone callbacks, **77–78 ms**
  per packet (mean77.1 ms). At **4 MHz**, only three completed before packet4
  timed out (IRQ00, mode80, DIO0 low). Root cause is unproven; do not claim all
  SPI speeds or long-term reliability passed.
- Only **test** SPI was set to 1 MHz (`cfg::RadioTestSPI`). Normal `radio.cpp`
  still starts with the driver's default 8 MHz and retries initialization at
  1 MHz. Do not claim normal runtime was changed or validated at 1 MHz.
- An intermediate test incorrectly demanded TX mode immediately after async
  start and was excluded. Immediate mode readback can still be standby81;
  assess deferred **TxDone plus timeout**, not that premature assertion.

Last observed board firmware was **restored to its pre-existing diagnostic**,
not left running half-demo ML or TX-only firmware. Its boot log showed 9-byte
synthetic packets, sync0x12 and SPI4 MHz; it waits for serial input and sends no
automatic test. `H` prints help; `B 10 1000` returned START unconfirmed and sent
no DATA without a receiver. This firmware differs from the 34-byte/sync0x34
half-demo contract. Identify it before testing; do not assume `c` or `t` works.

A private complete 4 MiB backup was saved at
`/tmp/half-demo-pre-radio-test-flash.bin`. Restoration was flash-hash verified
and the original idle prompt observed. This is historical recovery context,
not an instruction to flash that image again. It may contain device settings;
keep it private and never commit it. After any authorized temporary firmware
replacement, preserve/restore the correct target's prior state when appropriate
and report exactly which firmware remains installed.

Still **unverified**: reception by another radio, ACK/admission across two boards,
head queue acceptance under physical RF loss, head Wi-Fi HTTP forwarding,
physical outage/retry behavior, RF range/power/antenna efficiency and the complete
embedded-audio-to-SQLite hardware path. A local TxDone callback proves none of
those. Next physical integration work needs the correctly flashed ESP32 head,
matching v5/sync0x34 settings, actual Wi-Fi/LAN credentials and the local receiver.
Compare sender/head/server float32 bits and committed row identity; exercise a
brief receiver outage using the same SQLite database. Do not claim reboot recovery.

## Dataset boundaries and scientific interpretation

A folder called `test` is not the split assignment. Check the locked manifest
before opening audio or features:

`../Khalid/outputs/real-training-cuda/full/treevibes-984e68c4e99668c8/splits.csv`

- Of `../field/field/test/folder_26` through `folder_34`, folders26/27/30/34 are
  val, folders28/31/33 are train, and **folders29/32 are sealed test**.
- `difficult_cases` was explicitly excluded from the user's comparison request.
  Folders29/32 were **not opened/scored**; a question about exposing their holdout
  scores was left pending and the user changed tasks without answering it.
  Do not infer permission from later LoRa/documentation requests. Preserve the
  sealed data unless the user explicitly authorizes that evaluation. If scope
  conflicts, explain the existing restriction and exact folders, and continue
  independent development work while clarification is pending.
- `embed_clip.py` intentionally accepts only train/val recordings. It crops the
  first 80,000 samples without resampling, channel conversion or gain changes.
  Protect the original committed clip before temporary multi-file tests. Restore
  its exact bytes, force a rebuild after restoration (avoid preserved old mtimes
  making a stale object appear up to date), flash only when authorized, and
  verify the expected original mean when applicable.
- Never select a recording after looking at its score. The comparison selected
  the alphabetically first available manifest-listed recording of at least ten
  seconds per eligible folder. Actual filenames, crop hashes, window scores,
  workbook joins and float bits are in `folder-comparison.json/.csv/.log`.
- `TreeVibes.xlsx`, `Sheet1!E27:H35`, marks **all nine folders** AUDIO=1,
  CONFIRMATION=YES, VISUAL=B; visual legend `J4:K4` says exit tunnels/low infestation.
  Comments describe urban trees near an alley; folder26 adds a waveguide.
  Earlier conversation wording that confirmation was unavailable was too broad:
  the workbook does provide folder-level confirmation. Preserve that distinction
  from per-recording or species-specific verified ground truth.
- High scores agree with these positive acoustic labels; they are not infestation
  severity percentages or verified calibrated real-world probabilities. No
  negative folders were sampled, so false-positive performance, specificity,
  overall diagnostic accuracy and calibration cannot be established here.
  Train/val membership does not by itself establish exported-model training
  membership or independent evaluation. Do not manufacture thresholds, lineage,
  statistical independence or broad performance claims from this small sample.
- Host reference used ai-edge-litert2.3.0 `BUILTIN_REF` plus the unchanged compiled
  C frontend with `LOGMEL_USE_LOGF`. These optional verification tools are not
  receiver/firmware runtime dependencies. Keep reported parity limited to the
  actual tested clips and outputs; do not claim universal feature/model parity.

Keep evidence small and direct. Do not start training/tuning, whole-dataset
scans/hashing or a new final-evaluation campaign as a side effect of demo work.
Report actual checks, failures, limitations and installed firmware accurately;
update the relevant handoff/report when meaningful state changes.
