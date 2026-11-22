# Edge integration plan

The Edge ESP32 takes one ten-second recording from the piezo on a manual trigger, runs Khalid's model locally, and sends one result over LoRa to the cluster head ESP32. The cluster head's role in this plan ends at decoding and acknowledging that result.

```text
Serial c, onboard BOOT, or EN/RESET with the latch fitted → 10 s piezo capture
  → 80,000 mono PCM samples at 8 kHz
  → ten log-mel windows → ten int8 CNN outputs → one float32 mean
  → 6-byte result → LoRa DATA → cluster head decode and acknowledgment
```

## Implementation rules

- Keep dependencies to the minimum needed for capture, inference, and LoRa. Reuse the existing code and tools wherever they fit.
- Use straightforward code with a clear path from trigger to transmission. Every line and every file should serve a specific purpose in the firmware, build, or verification.
- Keep checks focused on what can fail on the board or across the radio link. Add helpers and separate files only when they make the code easier to understand or verify.

## 1. Start one measurement on demand

- In the serial monitor at 115200 baud, `c` starts one measurement. Discard extra commands received while the node is busy. Return to idle after success or failure.
- On battery, switch on with BOOT released and wait for Edge to initialize. One press of BOOT (GPIO0) starts one measurement. A held button does not repeat, and presses during a run are ignored. Use EN/RESET as a trigger only with the optional latch fitted.
- Check reset reasons for cold power-on and EN on the actual board first. Espressif marks `ESP_RST_EXT` as inapplicable on the classic ESP32, so the firmware needs a reliable way to distinguish the two events. If they report the same reason, add a small external reset-event latch read through a spare GPIO. The latch powers up clear, arms after the first successful boot, captures a subsequent EN press while the supply stays up, and is cleared by firmware before the measurement starts. Verify cold starts, power cycling, EN presses, and brownouts before enabling automatic capture from that latch. [ESP-IDF reset reasons](https://github.com/espressif/esp-idf/blob/v5.5.3/components/esp_system/include/esp_system.h)

## 2. Capture ten seconds from the piezo

Use the supplied circuit. The WANGCL piezo module feeds the MCP602E buffer through an AC coupling capacitor, is biased near 1.65 V, and reaches **GPIO34 / ADC1_CH6** through the 1 kΩ series resistor. Verify idle bias, signal swing, noise, and clipping on the board. The diagram shows an input high-pass stage and no explicit anti-alias low-pass stage. Check the response near and above 4 kHz and add analog filtering if needed. Khalid's analysis found the model particularly sensitive to the 3–4 kHz band, so measure what the capture chain does there. [Khalid's model analysis](../../Khalid/docs/ESP32_DEV_DAY_REPORT.md)

- Use the working ESP-IDF 5.5.3 and Arduino-ESP32 3.3.7 build as the starting point. This supports Khalid's TensorFlow Lite Micro runtime and the Arduino LoRa library used in `LoRa/Sender`. Build the combined dependencies early and pin the versions that pass. [Existing ESP32 build](../../half-demo/README.md) · [LoRa sender configuration](../../LoRa/Sender/platformio.ini)
- Reuse the sensing prototype's capture checks and diagnostics. Its older I2S ADC driver can request 8 kHz directly. On the newer ESP-IDF baseline, start with ADC1 continuous DMA at **24 kHz** and use a low-pass decimation filter to produce **8 kHz** PCM. Verify the actual sampling clock and filter response. Account for filter startup and delay before counting the first retained sample. [Capture reference](../../sensing/src/main.cpp) · [ESP32 continuous ADC sampling limits](https://github.com/espressif/esp-idf/blob/v5.5.3/components/soc/esp32/include/soc/soc_caps.h)
- Remove the measured ADC bias and convert samples to signed 16-bit PCM using fixed scaling. Keep the gain consistent between captures. Check sample order, duration, count, and ADC rail hits. Log min/max, clipping, and dropped samples.
- Keep capture draining DMA independently while inference runs. Start with two one-second PCM buffers of 16,000 bytes each, plus DMA and filter state. Pass each completed buffer to the model task while capture fills the other. If a buffer overruns, a DMA read fails, or the recording is incomplete, abandon the measurement without sending a score.

## 3. Run Khalid's model

1. Reuse Khalid's C log-mel front end, coefficient tables, and embedded `stream1s_w8_d4` model. Initialize TensorFlow Lite Micro with a 64 KiB tensor arena and read quantization parameters from the model tensors. [Model and front-end files](../../Khalid/firmware/README.md)
2. Feed ten distinct, consecutive windows of 8,000 samples. For each window, generate the **59 × 64 int8 log-mel tensor** and invoke the model once. The deployed graph already includes the training normalization. Use Khalid's log-mel pipeline directly after PCM conversion. [Front-end API](../../Khalid/firmware/logmel/logmel.h)
3. Convert each `int8` output with `(q - zero_point) * scale`. Average all ten scores with a `float32` accumulator. Send the resulting finite value in `[0, 1]`, including low scores. An inference error invalidates the measurement. Treat the result as a model score pending calibration on the real sensor chain. [Inference example](../../Khalid/firmware/esp32_stream1s/main/main.cc)

Khalid's board test reported about **429 ms** of feature extraction and inference per one-second window and about **40 KiB** of tensor arena in use. Measure latency, buffer headroom, and memory again on the integrated Edge build. [Khalid's bench results](../../Khalid/firmware/README.md)

## 4. Battery field

The current bench firmware sends dummy battery code `5` on every result. The design below is for a future version with a measured battery level.

- Sense the switched, protected battery voltage **before the XL6009 boost converter**. Use the protected system ground as the divider return. The 5 V point marked for a meter is downstream of the regulator.
- Use **GPIO35 / ADC1_CH7** for the battery sense. Start with a 47 kΩ / 47 kΩ divider and a 100 nF capacitor from the ADC pin to ground. With a 4.2 V cell, the divider gives about 2.1 V at GPIO35. Use the appropriate ADC attenuation, average settled readings, apply ADC calibration and the divider ratio, and compare the result against a multimeter. [DevKit ADC pinout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html) · [Espressif ADC calibration](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc/adc_calibration.html)
- Read the battery once per trigger before starting audio DMA so the GPIO35 one-shot measurement and GPIO34 continuous capture do not compete for ADC1. Take the reading while the Edge board is powered and the radio is idle.
- Use an initial voltage scale of **3,300 mV → 0** and **4,200 mV → 7**. Round to the nearest level with `round(7 × clamp((battery_mV - 3300) / 900.0, 0, 1))`. Verify the endpoints against the actual pack and the boost converter's usable range. This is a voltage level code. A disconnected or invalid reading blocks transmission and produces a clear diagnostic. Serial testing can still capture and score with the battery input disconnected.

## 5. Encode and send the result

| Payload offset | Field | Encoding |
| --- | --- | --- |
| 0 | Node ID | Unsigned byte, configurable. Use `1` for the first Edge node. |
| 1, bits 7–3 | Day of month | `1–31`. Use configurable dummy day `3` initially. |
| 1, bits 2–0 | Battery level | `0–7`. The bench firmware sends dummy code `5`. |
| 2–5 | Ten-second model score | IEEE 754 `float32`, little-endian, range `[0, 1]`. |

Pack byte 1 as `(day << 3) | battery_code`. For node `1`, day `3`, battery code `5`, and score `0.75`, the payload is `01 1D 00 00 40 3F`.

- Use [the LoRa sender](../../LoRa/Sender/src/main.cpp) for the radio setup, START/READY handshake, data transfer, bitmap acknowledgments, bounded retries, and FINISH/REPORT exchange. Use one packet and a window size of one per measurement. The **7-byte LoRa DATA header + 6-byte result = 13-byte DATA frame** before LoRa physical-layer overhead. Keep the existing matched radio profiles, hardware CRC, and transmit pacing.
- Replace the sender's generated test bytes with the encoded result. Retain the same six bytes for every retry of that measurement. Give the result protocol its own version so the benchmark and result firmwares cannot accidentally start a session with each other. [Existing frame and byte encoding](../../LoRa/Sender/include/protocol.h)
- The matching [LoRa receiver](../../LoRa/Receiver/src/main.cpp) needs a small change before a physical test. Replace generated-byte validation with decoding of the six-byte result. Verify the session, sequence, frame size, day and score before marking the packet received. Report each accepted result once and acknowledge valid retries through the existing bitmap. Log the score bits for comparison with the Edge log. An acknowledgment confirms receipt by the cluster head.
- At the Edge, report confirmed delivery, retry exhaustion, or unconfirmed delivery. A lost final report may leave an earlier bitmap acknowledgment as the available evidence. Do not trigger a new recording to retry a packet.

## Bench acceptance

1. **Model parity.** Feed known development PCM into the integrated model path and compare all ten window outputs and the mean with the reference. The included ten-second fixture in `main/fixture/` has recorded mean bits `0x3F7D3333`. The optional build uses that local copy. [Reference run](../../half-demo/hardware-test.log)
2. **Capture and power.** Use a known signal to check sample ordering, rate, filter response, and clipping. Confirm exactly 80,000 retained samples and zero capture loss with inference running. On battery power, verify that the boost supply holds up during capture and transmit.
3. **Two-board link.** Start the cluster head and trigger a live recording with serial `c`. Compare the Edge's six payload bytes and float bits against the cluster head's decoded result. Confirm one accepted result per trigger, correct handling of retries and duplicate frames, and a clear status when the cluster head is unavailable.
4. **Battery-only run.** Disconnect Edge from the laptop and power it from the battery with BOOT released. Confirm cold power-on stays idle, then press BOOT and check that Cluster receives exactly one result with dummy battery code `5`. Check that holding BOOT produces only one result and that releasing it and pressing again starts a new run. If the optional latch is fitted, test EN separately. [DevKit power options](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html#power-supply-options)
