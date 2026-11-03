# Half-demo

One ESP32-WROOM-32 sender (ID 1) runs the embedded ten-second clip through the
real int8 model, sends its mean over SX1278 LoRa to an ESP32 head (ID 0), and the
head uploads the binary packet over Wi-Fi to `receiver.py` for SQLite storage.
All runtime sources and embedded assets are here; the sibling projects are not
runtime dependencies. See [NOTICE.md](NOTICE.md) for copied-source provenance.

Use an ordinary **ESP-IDF 5.5.3** installation and its activated ESP-IDF shell
(Python **3.12** tested). The official component manager fetches Arduino-ESP32
**3.3.7** and esp-tflite-micro **1.3.5**; `dependencies.lock` freezes resolved
versions. Arduino 3.3.7 belongs to the IDF 5.5 release line
([release](https://github.com/espressif/arduino-esp32/releases/tag/3.3.7),
[component instructions](https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html)).
No SDK patches, source exclusions or custom toolchain downloader are used.
The receiver and host checks need only standard Python 3 and a native C++ compiler.

## Configure, build, flash

From this directory, in the ESP-IDF shell:

```sh
cp main/config.local.h.example main/config.local.h
# Edit main/config.local.h: 2.4 GHz Wi-Fi SSID/password and computer LAN URL.
# Edit main/Config.h: BenchDay (1..31) before the bench test.
idf.py -B build-sender -DDEMO_NODE_ID=1 -DSDKCONFIG=sdkconfig.sender build
idf.py -B build-head -DDEMO_NODE_ID=0 -DSDKCONFIG=sdkconfig.head build
python -m serial.tools.list_ports
```

`config.local.h` is ignored; keep real credentials there. Example server URL:
`http://192.168.1.100:8080/results`. Replace the IP with the computer's reachable
LAN IP. `localhost` on the head refers to the ESP, not the computer. Allow inbound
TCP 8080 on the computer and ensure both devices can reach each other. USB is
only for flashing, commands and logs.

Both boards: SX1278 SCK18, MISO19, MOSI23, CS27, RESET14, DIO0=26, 3V3/GND;
use a 433 MHz antenna. Radio settings in `main/Config.h`: 433 MHz, SF7,
BW125 kHz, CR4/5, preamble8, explicit header, PHY CRC, sync0x34, TX10 dBm.
The bench battery is **simulated 100%, code 7**, not a measurement.

Replace the example ports below with the enumerated ports (Windows: e.g. COM5).
Flash one board at a time if port identity is unclear, then confirm its `ROLE`
and `Node` startup log. Ports are not permanent board identities.

```sh
idf.py -B build-sender -p /dev/ttyUSB0 -b 115200 flash
idf.py -B build-head -p /dev/ttyUSB1 -b 115200 flash
```

The build uses a conservative **2 MiB flash** configuration and ESP-IDF's
standard single-app partition: factory app at `0x10000`, size `0x100000` (1 MiB).
This also fits a 4 MiB board while using only its first 2 MiB. No filesystem
partition or provisioning is needed. Check actual board flash before flashing:

```sh
python -m esptool --chip esp32 --port /dev/ttyUSB0 flash_id
python -m esptool --chip esp32 --port /dev/ttyUSB1 flash_id
```

Require at least 2 MiB detected flash. Build size checks enforce the
1 MiB app partition; physical flash detection remains a hardware check.

## Run and check

Start the receiver on the computer, then monitors in two other ESP-IDF terminals (exit monitor with Ctrl+]):

```sh
python receiver.py --bind 0.0.0.0 --port 8080 --db results.sqlite3
idf.py -B build-sender -p /dev/ttyUSB0 monitor -b 115200
idf.py -B build-head -p /dev/ttyUSB1 monitor -b 115200
```

Wait for sender `MODEL` and `SENDER ready`. Type **c** in the sender monitor
(no automatic inference on boot). Expect ten `WINDOW` outputs with distinct
sample ranges, one `MEAN ... bits=XXXXXXXX`, one `SUBMISSION Queued` with its
measurement ID, head `HEAD ... bits=XXXXXXXX`, eventual `LORA ... DELIVERED`,
head `UPLINK COMMITTED`, and receiver `STORED`. Low probabilities are sent too.
A second `c` during processing is rejected. If submission cannot queue, the
score stays in RAM: type **r** to retry that unqueued result. It cannot be
submitted twice by `r` after successful queuing. A failed LoRa delivery is
reported as unconfirmed, since a lost ACK can coexist with head acceptance.

Inspect committed records from another terminal:

```sh
python - <<'PY'
import sqlite3
with sqlite3.connect('results.sqlite3') as db:
    for node, token, mid, bits, raw, at in db.execute('SELECT node_id,sender_boot_token,measurement_id,probability_bits,raw_packet,ingested_at FROM results ORDER BY id'):
        print(node, f'{token:08x}', mid, f'{bits:08x}', len(raw), at)
PY
```

Compare the eight hex digits with sender `MEAN`, head `HEAD`, and receiver logs;
all must match exactly, with raw length 34. Repeat a few `c` runs: each gets a
distinct measurement ID and one row, even though the same embedded clip gives
the same score. Stop the receiver, run `c`, confirm head `RETRY` and retained
packet, then restart with the **same database**. The pending packet should be
committed once. A lost success reply is handled as a duplicate on retry.

The head keeps a fixed 32-packet RAM FIFO. Acceptance is queued before the ARQ
bitmap is committed. A full queue rejects new records without overwriting or
ACKing them; sender retry limits still apply. Radio duplicates do not enqueue
again (bounded 256-event history plus active batch duplicate checks). The head
posts only validated DATA, with short connection/read timeouts and 3-second
retry delays. A permanent HTTP error (including identity conflict 409) halts
uploads while retaining the front packet; correct its cause and type **r** on
the head to resume. Invalid success echoes also leave the packet pending.

**LoRa ACK means accepted in head RAM, not persisted on the computer. Pending
records are lost on a head restart.** Sender queues/status and dedup history are
also volatile and bounded. Boot tokens are random 32-bit identities, not global
uniqueness guarantees. Identity reuse with different application bytes is logged
as a conflict; the server returns 409 and keeps its original row. No durable or
unlimited delivery guarantee is claimed. Server ingestion time is UTC storage
time, not audio recording or sender measurement time. HTTP is for a trusted LAN.

## Test only the sensing node (no head or working radio required)

The optional sender-only ML test bypasses LoRa initialization and polling; it
uses the **same embedded clip, frontend, model, inference worker and score API**.
The ordinary sender/head builds leave `DEMO_ML_ONLY` disabled. In the ESP-IDF
shell, from this directory:

```sh
idf.py -B build-ml-test -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=ON -DSDKCONFIG=sdkconfig.ml-test build
idf.py -B build-ml-test -p /dev/ttyUSB0 -b 115200 flash monitor
```

Confirm `ML_ONLY TEST` and `SENDER ready`, then type **c**. This mode queues the
score in volatile RAM and performs no radio transmission; `SUBMISSION Queued`
is only an API check. Reset the board to clear its test queue (32 entries).
Use the ordinary sender build/flash commands above when returning to LoRa tests.

On 3 November 2026 the connected ESP32-D0WD-V3 rev3.1 reported **4 MB flash**.
The normal firmware flashed and verified, but its SX1278 did not respond
(`RegVersion=00`), so the on-board ML test used the explicit mode above. Two
complete runs produced 20 valid window outputs, each matching the host reference
**bit for bit**, and the same mean **0.989062488**, bits **`3f7d3333`**.
Each ten-second clip processed in approximately **4.91 s** including frontend,
inference, task yields and serial reporting. Tensor arena use was **40,652 B**
of 65,536 B reserved. Idle startup, busy-command rejection, distinct IDs 1/2,
and rejection of an already-queued result's `r` retry also passed.

The host comparison used the unchanged C frontend with `LOGMEL_USE_LOGF` and
ai-edge-litert 2.3.0 `BUILTIN_REF` on the identical embedded model/clip. This is a
numerical comparison for this clip; the receiver still needs only Python's
standard library. Actual serial output and reference values are retained in
[hardware-test.log](hardware-test.log). The board used for that ML check was left with the
ML-only build at that time. Later radio testing is recorded below. RF, head reception and Wi-Fi forwarding have not been tested.

## Audio, frontend and wire format

`main/generated/clip.cc`: original
`field/field/test/folder_26/F_20200529180809_1_T26.4.wav`, marked **val** in
`Khalid/outputs/real-training-cuda/full/treevibes-984e68c4e99668c8/splits.csv`.
The first 80,000 samples `[0,80000)` are 8 kHz mono signed-int16 PCM, 160,000
bytes in flash, gain unchanged. No final-holdout audio was opened. Recreate from
the repository root, if needed (the committed array needs no regeneration):

```sh
python half-demo/embed_clip.py "$PWD/field/field/test/folder_26/F_20200529180809_1_T26.4.wav" --splits Khalid/outputs/real-training-cuda/full/treevibes-984e68c4e99668c8/splits.csv --manifest-path field/field/test/folder_26/F_20200529180809_1_T26.4.wav
```

The sender feeds 160-sample flash chunks to Khalid's unchanged streaming
frontend: PCM/32768, periodic Hann512, hop128, 59 frames, power/512, 64 HTK mel
bands, natural log floor1e-10 and model input int8 quantization. Normalization
stays in the model. Each distinct second is inferred once; ten dequantized
probabilities are accumulated and averaged in float32. The 64 KiB tensor arena
is allocated in internal RAM; no whole-waveform RAM copy exists. Inference runs
on a worker task on core0, while the Arduino radio loop runs on core1. On the
head, a separate core0 uploader owns Wi-Fi/HTTP; only the radio loop owns SPI.

Model `stream1s_w8_d4`, 35,344 bytes, SHA-256:
`22c373cfc93af09fd3fc5c84f384ea273f7c815dd08ab7100b74be5e238a5a46`.
This demonstration does not establish species identification or field accuracy.

RPW **v5** rejects v4. Header is unchanged: magic/version/type/node (0..3),
run/token/batch/measurement little-endian uint32 (4..19), index/count/attempt/
payload length (20..23). DATA application bytes (24..29) are node,
`(day << 3) | battery_code`, then probability float32 little-endian. CRC32
(30..33) covers the first 30 bytes. Total **34 bytes**. Battery percentage API
uses `floor(percent*7/100+0.5)`. DATA airtime remains 78 ms rounded upward:
34 and 35 bytes occupy the same LoRa symbol count. Existing two TDMA slots,
108 ms cells, 1099 ms slots, 2258 ms frames, admission and selective retries stay.

`POST /results`, `Content-Type: application/octet-stream`, `Content-Length: 34`
sends the complete packet. The receiver validates framing, fields, CRC and
application values. SQLite identity is `(node_id,sender_boot_token,measurement_id)`;
matching six-byte application data returns 200, new committed data 201, conflict
409. Both successes echo the exact **submitted** packet, including retry fields.
The head removes a queued packet only after status 200/201 and exact byte echo.

## Verification status

Run the small native/HTTP checks with Python standard library and a C++ compiler:

```sh
python test/check.py
```

The check compiles the actual portable firmware codec and checks its v5 golden
packet against Python, CRC, float bits, battery encoding, TDMA timing, selective
retry/duplicate handling and queue limits. It checks the selected model hash and
clip sample count, then exercises real HTTP/SQLite new/duplicate/conflict,
invalid packet/request and storage-failure responses. No third-party Python
runtime packages are needed by the receiver or checks.

Verified on 3 November 2026: both ESP32 role builds passed with the versions
above and official ESP-IDF Python dependency constraints. Sender app is 539,712 B;
head app is 870,912 B, both below the 1,048,576 B partition limit. ELF inspection
places the 160,000 B clip and 35,344 B model in flash-mapped rodata. The embedded
crop matches the chosen WAV PCM byte for byte. Native/HTTP checks passed (four
small test groups, including both role APIs); see [verification.log](verification.log).

The connected sensing node has now passed the standalone physical inference
checks above, including exact output/mean comparison with the host reference.
A cluster head was not connected. RF delivery, head Wi-Fi uplink and physical
outage/retry tests remain **unverified**; the SX1278 initialization issue must be
resolved before testing the full path.

A later physical comparison of one ten-second crop from each eligible folder under `field/field/test` is recorded in [folder-comparison.md](folder-comparison.md); sealed folders 29 and 32 remain pending authorization.

## Transmitter-only LoRa bench test (no head)

Normal sender firmware waits for a head beacon/admission before sending DATA.
The explicit TX-only build bypasses that scheduling for a bounded radio test:

```sh
idf.py -B build-radio-test -DDEMO_NODE_ID=1 -DDEMO_ML_ONLY=OFF -DDEMO_LORA_TX_TEST=ON -DSDKCONFIG=sdkconfig.radio-test build
idf.py -B build-radio-test -p /dev/ttyUSB0 -b 115200 flash monitor
```

With the 433 MHz antenna connected, wait for `READY`, then type **t**. The test
sends ten 34-byte ASCII diagnostic packets, with a one-second gap. It uses the
same patched LoRa driver and radio settings: 433 MHz, SF7, BW125 kHz, CR4/5,
explicit header, PHY CRC, preamble8, sync0x34 and 10 dBm; test SPI is 1 MHz.
Radio identity must be stable (`0x12` sixteen times), configuration registers
must match, and each FIFO write must read back exactly before TX starts.
Success requires ten deferred GPIO26/DIO0 `TxDone` callbacks without timeouts.
A one-second timeout logs IRQ/mode/DIO0 state and stops the test. Radio init
failure runs the copied hardware/software SPI diagnostics without attempting TX.

These are diagnostic ASCII bytes, with no ML score or RPW application framing.
`LOCAL_TX` confirms local chip completion; reception and radiated power/antenna
performance need a receiver or RF instrumentation. This mode is sender-only,
mutually exclusive with ML-only mode, and disabled for normal sender/head builds.
Restore the appropriate firmware after a bench test. See [lora-test.log](lora-test.log)
for the actual hardware run and its limitations.

The physical TX-only check on 3 November 2026 completed **10/10 packets at
1 MHz SPI**, with ten FIFO readback matches and ten deferred DIO0 callbacks;
local completion times were 77–78 ms (mean 77.1 ms). A preceding 4 MHz burst
completed three packets and timed out on packet four. This supports using the
slower test clock, but does not establish the cause of the faster-clock failure
or long-term reliability. The pre-existing firmware on the connected board was
backed up and restored after testing; restoration was hash-verified and its
original idle prompt was observed. Details: [lora-test.md](lora-test.md).
