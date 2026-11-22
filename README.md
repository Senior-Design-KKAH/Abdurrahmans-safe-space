# Edge and Cluster bench guide

Keep implementation straightforward. Use only dependencies needed for capture, inference, and the link. Every line and file should have a clear role in the firmware, build, or verification.

The Edge ESP32 captures a ten-second piezo recording and sends one model score with its node ID, dummy day, and dummy battery code `5`. Cluster acknowledges it over LoRa after queueing it, then forwards it to the existing NAKHEL server. See the [Edge plan](Edge/plan.md) for the payload and the [Cluster plan](Cluster/plan.md) for the server handoff.

## Build and flash

Edge's PlatformIO project uses ESP-IDF **5.5.3**. Its component manifest pins Arduino-ESP32 **3.3.7** and Espressif TensorFlow Lite Micro **1.3.5**. Cluster keeps the original LoRa receiver's PlatformIO toolchain. Both use Sandeep Mistry LoRa **0.8.0**. All project sources, including the uploader and HTTP transport, are included here. PlatformIO installs the pinned frameworks and third-party dependencies. Sibling project folders are unnecessary for either build.

Edge enables Arduino's selective compilation in `sdkconfig.defaults`, with SPI as its only optional Arduino library. The upstream Arduino manifest still downloads and builds transitive components, including RainMaker and Insights, so the PlatformIO certificate-embedding workaround remains necessary. Edge's own sender uses a single-result retry loop and the shared protocol, radio profile, and driver. Its model code depends only on the inference and preprocessing components.

Open `Integration/Edge` as a PlatformIO project, or run from that folder:

```sh
pio run
pio run -t upload --upload-port EDGE_PORT
pio device monitor --port EDGE_PORT
```

Use Python **3.12** for PlatformIO when setting up Edge's ESP-IDF environment. With Python 3.15, the pinned Python dependencies failed to install during the first build.

Before using Cluster’s server forwarding, create the local, ignored `Integration/Cluster/include/secrets.h` if it is missing. Define `NAKHEL_WIFI_SSID`, `NAKHEL_WIFI_PASSWORD`, `NAKHEL_SERVER_URL`, `NAKHEL_API_KEY`, and `NAKHEL_ROOT_CA` as C++ strings. Use the server base URL without a trailing slash. Verify that node `1` is registered under the key and check the bench node’s alert settings before sending scores. For a short bench test over HTTP, leave `NAKHEL_ROOT_CA` empty and use a dedicated test key that you can revoke afterward. HTTP sends the key and measurements unencrypted. For HTTPS, supply the PEM root certificate. Empty required values cause Cluster to print a configuration message and reply `Busy` to Edge.

Cluster builds the included `lib/nakhel_uplink/` library and `src/esp32_transport.cpp`. A checkout without `secrets.h` builds with empty configuration and prints setup instructions at startup. Create the file with the following definitions and fill in your own settings.

```cpp
#pragma once
#define NAKHEL_WIFI_SSID ""
#define NAKHEL_WIFI_PASSWORD ""
#define NAKHEL_SERVER_URL "http://31.97.105.159:6767"
#define NAKHEL_API_KEY ""
#define NAKHEL_ROOT_CA ""
```

From `Integration/Cluster`:

```sh
pio run
pio run -t upload --upload-port CLUSTER_PORT
pio device monitor --port CLUSTER_PORT
```

Replace the port names with the ports of the identified boards. Both consoles run at 115200 baud. The native ESP-IDF build is also available from Edge in an activated ESP-IDF **5.5.3** shell. The known ten-second development clip is included in `Edge/main/fixture/`. To enable it, build with `idf.py -D EDGE_MODEL_CHECK=ON build` and send `t` on the Edge console. It exercises the model without ADC or radio and should print mean bits `3f7d3333`. The normal build has no fixture dependency. [Reference run](../half-demo/hardware-test.log)

## Wire the bench setup

- **Piezo.** The supplied bias and buffer circuit drives GPIO34. Edge requests 24 kHz ADC1 DMA, reorders adjacent 16-bit samples for the classic ESP32 mono FIFO, and filters and downsamples to 8 kHz. Verify the order and sample clock with a known signal on the actual board. The digital filter is about −0.2 dB at 3.4 kHz and −21 dB at 4 kHz. Measure the piezo and circuit response before interpreting model scores. [ESP32 I2S FIFO behavior](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2s.html)
- **Power.** USB power is sufficient for the serial capture and LoRa test. For a battery-powered run with serial logs, use a 3.3 V USB-to-UART adapter with TX, RX, and ground only. Use one power source for the DevKit. [DevKit power options](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html#power-supply-options)
- **LoRa.** On both ESP32s, connect SX1278 SCK/MISO/MOSI/NSS/RESET/DIO0 to GPIO18/19/23/27/14/26 and connect 3.3 V, ground, and the 433 MHz antenna. Both roles compile the same [radio profile](link/profile.h). The integration uses the benchmark README's 10% pacing baseline. The benchmark's current source is set to 90%. [LoRa benchmark](../LoRa/README.md)

### Onboard buttons on battery

Power on Edge with BOOT released. Once the firmware has initialized, press and release BOOT to capture and send one measurement. A held button produces one capture, and a press during capture or transmission is ignored. Press BOOT again after the current run finishes. Holding BOOT while powering up or pressing EN enters the ESP32’s download mode. [ESP32 boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)

### Optional EN/RESET trigger

The original ESP32 uses the same reset reason for cold power-up and EN on typical DevKit boards. Triggering with EN requires a reset latch signal. With GPIO32 unwired, cold boot and EN both leave the firmware idle, ready for BOOT or serial `c`. [ESP-IDF reset reasons](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_system/include/esp_system.h)

Use 3.3 V logic powered while EN is held low. One simple arrangement uses two D flip-flops and a supply supervisor:

| Signal | Connection |
| --- | --- |
| Arm state | Tie the first flip-flop D high. Clock it on the rising edge of **GPIO25**. Add a pull-down so the clock stays low while the ESP32 is in reset. Edge raises GPIO25 once the model is ready and the supervisor has had 350 ms to release. |
| Reset event | Feed the arm flip-flop Q into the second flip-flop D. Clock the second flip-flop on a falling edge of EN, using a high-impedance Schmitt inverter into a rising-edge clock. Connect its Q to **GPIO32**. |
| Clear | Use an active-low power supervisor to clear both flip-flops on supply startup or collapse. The supervisor should release within the 350 ms arming wait. Also clear the event flip-flop when **GPIO33** goes low. Combine those two clear inputs with an AND gate or equivalent isolated logic. Keep the arm flip-flop's clear independent of GPIO33. Use a pull-up on GPIO33 for its open-drain clear pulse. Tie unused preset inputs high. |

GPIO32 should stay low at cold boot, go high on EN after the first boot arms the latch, and return low when Edge consumes the event. Verify those levels and the printed `BOOT` reason before testing untethered. A full power cycle should clear both flip-flops.

## Run and verify

1. Start the Cluster monitor and wait for both `Cluster ready for Edge results` and `[uplink] Clock synchronized`.
2. Type `c` on Edge. Check `ADC samples=80000`, ten `WINDOW` lines, and one `MEAN`. The capture rejects DMA errors, missed buffers, and a gross timing error. Check the voltage and clipping diagnostics.
3. Compare `PAYLOAD_HEX` and the Edge `MEAN` bits with Cluster's single `RESULT … queued` line. Edge confirms delivery to the Cluster queue. Match the node and UTC timestamp to `[uplink] delivered node … (stored)` or `(duplicate)` and verify one record on the server. Compare the server score at the uplink’s JSON precision of seven significant digits.
4. Disconnect Edge from the laptop and power it from the battery with BOOT released. Confirm cold power-on leaves it idle, then press BOOT once. Check for one new Cluster result with dummy battery code `5`. Hold BOOT through a run to check that it does not repeat, then release and press again for one more result. If the optional EN latch is fitted, check that EN also starts one run.
5. After Cluster’s clock synchronizes, interrupt Wi-Fi and trigger one measurement. Restore Wi-Fi and verify that the queued result arrives once with its original UTC timestamp. Keep Cluster powered during the interruption because its queue is in RAM.

Both firmware builds passed, including the optional Edge fixture build. The host reference matched all ten fixture window scores and mean bits `3f7d3333`. A simulated link ran both firmware roles through packet loss, duplicate frames, missing acknowledgments, and retry exhaustion. There were no ESP32 boards connected for the live capture, battery, reset latch, and over-the-air checks above.

The model and log-mel sources come from Khalid's ESP32 firmware. The radio, framing, and retry logic are adapted from the LoRa sender and receiver. The vendored LoRa library retains its [MIT license](Edge/main/lora/LICENSE). Cluster's included uploader and HTTP transport come from [Haitham's firmware](https://github.com/HaithamAyyash/rpw-dashboard/tree/f507389028de4478f681dd2dd7ea276771696e42/firmware/cluster_head). The optional Edge fixture is copied from the original half-demo.
