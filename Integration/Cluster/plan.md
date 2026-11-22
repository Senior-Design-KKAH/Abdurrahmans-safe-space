# Cluster plan · Receive and forward one sensing node’s results

One cluster head ESP32 listens for one Edge ESP32 over LoRa. Run one measurement at a time. The first phase receives, logs, and acknowledges the result. The second phase queues each accepted result and forwards it over Wi-Fi to the existing NAKHEL server. The combined firmware builds from `Integration/Cluster`. TDMA and multiple sensing nodes are later work.

## Implementation rules

Keep dependencies minimal. Use the existing PlatformIO setup, Arduino ESP32 framework, and LoRa library. Reuse the radio and protocol code already shared with Edge. Keep the receiver flow easy to follow, with a clear purpose for every line and file.

## Receive and acknowledge

1. **Listen.** Use the receive loop and session handling in the LoRa benchmark as the reference. Match the Edge's current radio profile and result protocol version. Keep the cluster head listening for the next session after a completed session or a timeout. Review the existing Cluster draft against this plan when implementation begins. [Original receiver](../../LoRa/Receiver/src/main.cpp) · [Shared radio profile](../link/profile.h)
2. **Open one session.** Validate `START` and reply with `READY` using the existing handshake. Expect one packet, six application bytes, and a window size of one. Match the session ID on subsequent frames. [Shared protocol](../link/protocol.h)
3. **Decode the result.** Expect a 13-byte DATA frame with the existing seven-byte header, sequence zero, and the six-byte payload below. Validate the frame, day, and finite score before accepting it. Print the session ID, node ID, day, battery code, score, and its exact float bits at 115200 baud.
4. **Handle retries.** Keep the accepted payload in RAM for the session. A repeat with the same session, sequence, and payload leaves the original result available for acknowledgment and produces no second result line. Reject a conflicting payload under the same session and sequence and retain the first result.
5. **Complete the exchange.** Reply to `POLL` with bitmap bit 0 set once the result is accepted. Reply to `FINISH` with `REPORT`, and replay that report if the sender repeats `FINISH`. Let a stalled session time out so the next measurement can start. The acknowledgment confirms receipt by the cluster head.

| Application byte | Field | Encoding |
| --- | --- | --- |
| 0 | Node ID | Unsigned byte. The first Edge node uses `1`. |
| 1, bits 7–3 | Day | Day of month, 1–31. Edge currently sends a dummy day. |
| 1, bits 2–0 | Battery | Level code, 0–7. Edge currently sends dummy code `5`. |
| 2–5 | Model score | Little-endian IEEE 754 `float32` in `[0, 1]`. |

Example payload `01 1D 00 00 40 3F` decodes to node 1, day 3, battery code 5, and score 0.75 with float bits `3F400000`.

## LoRa acceptance checks

1. **Decode.** Check the example payload against the expected fields and float bits. Reject a wrong frame length or version, a nonzero sequence, day zero, and a non-finite or out-of-range score.
2. **Retry and recover.** Check that a retransmitted result produces one result line, a repeated `POLL` returns the same acknowledgment, and a repeated `FINISH` returns the same report. Check that a stalled session releases the receiver for the next `START`.
3. **Test the two boards.** Start the cluster head first, then trigger one Edge measurement with `c`. Compare the Edge's `PAYLOAD_HEX` and score bits with the cluster head's decoded fields and score bits. Check that the cluster head prints one result, Edge confirms receipt, and the next manual capture starts a new session.

## Forward to the existing NAKHEL server

1. **Combine the two paths in Cluster.** Keep the working receiver and shared `Integration/link` code. Include Haitham’s `nakhel_uplink` library under `Cluster/lib/`, the RAM queue and transport headers under `Cluster/include/`, and the ESP32 transport under `Cluster/src/`. Build from these local files so another checkout is unnecessary. Reuse its Wi-Fi and time setup. Use one firmware entry point and one LoRa receive loop. Keep HTTP requests and retries in the uplink task so they cannot block LoRa replies. Keep dependencies minimal and make each file’s role clear. [Included uploader](lib/nakhel_uplink/src/uplink.cpp)
2. **Create one record per accepted LoRa session.** At the first valid DATA frame that can be queued, capture the synchronized Cluster UTC receive time. Use it for `timestamp` and `received_at` for this milestone. This is a proxy for recording time because Edge currently sends a dummy day. Map the received node ID to `node_id` and the unchanged float score to `confidence`. Set `model_version` to the deployed model identifier, initially `stream1s_w8_d4`, and include the radio RSSI and SNR. Keep the dummy day and battery code in the serial log. Confirm how the score should be interpreted before using bench results for infestation decisions. [Measurement contract](https://github.com/HaithamAyyash/rpw-dashboard/blob/f507389028de4478f681dd2dd7ea276771696e42/docs/contracts/cluster_head_to_cloud.md)
3. **Queue before acknowledging.** Mark DATA accepted and log one `RESULT` only after the RAM queue accepts it. A repeated DATA frame in the same session reuses the existing acceptance state and never creates a second record. Return `Busy` to a new `START` if the clock has not synchronized or the queue is full. If insertion fails after `READY`, leave the bitmap bit clear and report no acceptance unless a later retry queues the result. Once the clock is synchronized, keep accepting during a Wi-Fi outage while the queue has room. The RAM queue loses pending records on reboot or power loss.
4. **Send and track delivery.** Use Haitham’s existing `POST /api/v1/ingest/measurements` flow. Keep each queued record’s timestamp fixed across retries, since the server deduplicates by node and timestamp. A matching `201 stored` or `200 duplicate` confirms server receipt. Preserve the sender’s handling of transient errors, permanent record rejection, and a refused key. Log the LoRa session, node ID, timestamp, and queue outcome at handoff so the uplink log can be matched by node and timestamp. A LoRa acknowledgment means queued on Cluster. Server confirmation comes separately.

### Setup for the existing server

- Confirm the ESP32 can reach the existing server over Wi-Fi and that the deployment accepts `confidence` and `model_version` measurements. Use HTTPS with a trusted root certificate for regular use. For a short HTTP bench test, use a dedicated test key and leave the root CA empty.
- Use a designated bench cluster and register or verify LoRa node `1` under its cluster API key. Check how alerts are configured for that bench node before sending real scores. Keep Wi-Fi credentials and the key in local, ignored firmware configuration.
- Check that the clock synchronizes and the firmware has enough free memory for the queue and TLS connection. Preserve the current Edge packet, radio settings, and upload-port assignments.

### Forwarding acceptance checks

1. **Build and exercise the handoff.** Build the combined Cluster firmware. Verify record mapping from the example payload, a single queue insertion across duplicate DATA frames, and no positive LoRa acknowledgment when insertion fails. Check that an unsynchronized clock or a full queue produces a clear busy result and that a new measurement works after recovery.
2. **Trace one live measurement.** Start Cluster, wait for clock synchronization, and send `c` on Edge. Match the Edge score bits to Cluster’s `RESULT`, then match the queued node ID, timestamp, score, and model version to one stored record on the existing server. Check LoRa and server acknowledgments separately.
3. **Exercise recovery.** After clock synchronization, interrupt Wi-Fi, send a measurement, and verify Cluster queues it. Restore Wi-Fi and confirm the record arrives once with its original timestamp. Use a controlled test of a lost HTTP response to verify that the resend returns `200 duplicate` and clears the queue without a second stored measurement.
