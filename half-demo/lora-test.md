# Transmitter-only LoRa check — 3 November 2026

The connected ESP32/SX1278 was tested without a cluster head or receiver.
The firmware already installed on the board identified the radio as `0x12`
and used 9-byte synthetic packets, sync0x12 and 4 MHz SPI. Its `B 10 1000`
command required a receiver's START handshake and returned `START unconfirmed;
no DATA sent`. A dedicated, explicitly selected TX-only build was therefore
used to test local transmission without that handshake or TDMA admission.

| Check | Result |
|---|---|
| Wiring | SCK18, MISO19, MOSI23, CS27, RESET14, DIO0=26 |
| Radio identity | `0x12`, 16/16 matching reads |
| Frequency register | `0x6c4000` = configured 433 MHz |
| Modem readback | Config1 `0x72`, Config2 `0x74`, Config3 `0x04` |
| Other readback | Preamble8, sync0x34, PA `0x88` (configured 10 dBm), standby `0x81` |
| Payload | 34 ASCII diagnostic bytes; no ML/RPW result framing |
| RF settings | SF7, BW125 kHz, CR4/5, explicit header, PHY CRC |
| First burst, SPI4 MHz | Three TxDone callbacks; packet four timed out after 1 s, IRQ `00`, mode `80`, DIO0 low |
| Repeat, SPI1 MHz | **10/10 completed**, no timeouts |
| FIFO integrity at SPI1 MHz | **10/10 exact 34-byte readback matches** before starting TX |
| Interrupt servicing at SPI1 MHz | **10/10 GPIO26 TxDone callbacks**, using the patched deferred-IRQ driver |
| Local completion time at SPI1 MHz | **77–78 ms**, mean **77.1 ms** |
| Spacing | At least 1000 ms after each completed packet |
| Receiver/ACK/range/RF power | Unverified; no receiver or RF measurement equipment was connected |

One intermediate 1 MHz attempt was stopped by an overly early immediate-mode
assertion. That assertion was removed because the asynchronous start call may
return before the mode readback changes. This attempt is excluded from the
result above; the final repeat used TxDone plus the same one-second timeout as
the first burst. Its ten successful callbacks are retained in the raw log.

The successful repeat establishes local chip completion under these bench
conditions. It does not establish reception, antenna efficiency, calibrated RF
power, range or durable protocol delivery. The 4 MHz failure is a remaining
reliability concern; this small test does not prove its cause. Normal sender
TDMA/admission and driver behavior were not changed. Only the standalone test's
SPI clock is set to 1 MHz in `main/Config.h`.

A private 4 MiB backup of the board's complete pre-test flash was captured in
`/tmp` before the temporary test firmware was installed. The complete original image was restored, flash-hash verified, and its original startup/idle prompt was observed after reset.
The original diagnostic firmware sends no automatic test on boot.

Reproduction commands and manual `t` trigger are in [README.md](README.md).
Raw UART evidence, including failed and successful attempts: [lora-test.log](lora-test.log).
