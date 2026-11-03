Source provenance and licenses

- `main/logmel/` and `main/generated/model_data.*` are copied from Khalid's
  `firmware/logmel/` and `firmware/esp32_stream1s/main/generated/`. The frontend,
  tables, selected trained model and quantization are unchanged. No standalone
  license file accompanied these source files in the supplied Khalid tree.
- `main/radio.cpp`, `communication_api.*`, `communication_internal.h`,
  `CommunicationLog.h`, `MLResult.h`, `ResultQueue.h`, `RPWProtocol.h`, `TDMA.h`,
  and `SelectiveRepeatARQ.h` derive from Kareem's communication subsystem.
  Local changes implement v5 six-byte application encoding and volatile RAM
  acceptance; TDMA, admission, deferred IRQ and selective retries are retained.
  No separate project license accompanied these communication files.
- `main/radio/LoRa.*` is Sandeep Mistry's LoRa 0.8.0 with Kareem's deferred IRQ
  patch. Its MIT license is retained in `main/radio/LICENSE`, with patch notes
  in `main/radio/LOCAL_CHANGES.md`.
- `main/generated/clip.cc` contains the first ten seconds, unchanged signed-int16
  PCM, of the TreeVibes development recording identified in README.md. Dataset
  provenance is separate from the LoRa source license; no new license for that
  recording or the supplied model is asserted here.
- ESP-IDF (Apache-2.0), Arduino-ESP32 (LGPL-2.1), TFLite Micro (Apache-2.0) and
  their component-managed dependencies retain their own upstream licenses in
  `managed_components/` and the installed framework. They are fetched by the
  normal component manager, rather than copied into the demo source.

`main/lora_tx_test.cpp` reuses SPI diagnostic routines from Kareem/tools/radio_diagnostic/src/main.cpp; the test uses the retained patched LoRa driver and license above.
