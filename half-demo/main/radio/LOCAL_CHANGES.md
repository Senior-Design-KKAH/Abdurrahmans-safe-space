# Local ESP32 deferred-interrupt adaptation

Base: Sandeep Mistry LoRa0.8.0; original MIT license retained. This copy belongs only to the new test project; the original supplied reference archive is untouched.

Observed physical failure: cluster head rebooted with `assert failed: xQueueSemaphoreTake ... xTaskGetSchedulerState ...`, during packets/storage. Upstream onDio0Rise invoked SPI transactions directly in GPIO interrupt context; those transactions use ESP32 task mutexes.

Opt-in API:

- `setDeferredInterrupts(true)` before attaching callbacks.
- ISR captures `millis()` and sets a pending flag; no SPI or mutex work occurs there.
- `pollIrq()` from the main loop snapshots the pending event and performs the original IRQ/SPI/FIFO/callback logic in task context.
- `irqTimestamp()` returns the captured edge time during a callback. The application uses it for TxDone/RxDone epochs.

Default mode remains upstream-compatible when deferred mode is not enabled. The main firmware enables it for all three roles. Call pollIrq regularly; a long flash/model/logging stall can still lose radio packets, to be handled by bounded ARQ. This is not an unlimited interrupt-event queue or a substitute for correct DIO0 wiring. Receive-state diagnostics report register flags and the GPIO26 level while unsynchronized.

Half-demo copy: deprecated Arduino B111/B1000 aliases replaced with identical C++ binary literals for Arduino 3.x. Deferred IRQ behavior unchanged.
