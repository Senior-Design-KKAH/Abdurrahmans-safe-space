#pragma once
#include <Arduino.h>
#include <LoRa.h>
#include <SPI.h>
#include <esp_timer.h>
#include "protocol.h"

inline uint64_t nowUs() { return esp_timer_get_time(); }

inline void waitUntil(uint64_t deadline) {
    while (nowUs() < deadline) delay(1);
}

class Radio {
public:
    int rssi = 0, snrQ4 = 0;

    bool begin() {
        SPI.begin(profile::sck, profile::miso, profile::mosi, profile::nss);
        LoRa.setPins(profile::nss, profile::reset, profile::dio0);
        LoRa.setSPIFrequency(profile::spiHz);
        pinMode(profile::dio0, INPUT);
        initialized = LoRa.begin(profile::frequencyHz);
        if (!initialized) return false;
        LoRa.setSpreadingFactor(profile::spreadingFactor);
        LoRa.setSignalBandwidth(profile::bandwidthHz);
        LoRa.setCodingRate4(profile::codingRate);
        LoRa.setPreambleLength(profile::preamble);
        LoRa.setSyncWord(profile::syncWord);
        LoRa.enableCrc();
        LoRa.disableInvertIQ();
        LoRa.setTxPower(profile::txDbm);
        LoRa.receive();
        return true;
    }

    bool send(const wire::Frame& frame, uint64_t earliest = 0) {
        waitUntil(earliest > nextTx ? earliest : nextTx);
        return transmit(frame);
    }

    bool read(wire::Frame& frame) {
        // Read RX_DONE from the radio over SPI, so a DIO0 wiring fault cannot hide a packet.
        if (!initialized || !(irq() & 0x40)) return false;
        const int length = LoRa.parsePacket();
        frame.size = 0;
        if (length > 0 && length <= int(wire::maxFrame)) {
            rssi = LoRa.packetRssi();
            snrQ4 = int(LoRa.packetSnr() * 4);
            while (frame.size < unsigned(length)) {
                const int byte = LoRa.read();
                if (byte < 0) break;
                frame.bytes[frame.size++] = byte;
            }
        }
        LoRa.receive();
        return length > 0 && frame.size == unsigned(length);
    }

private:
    uint64_t nextTx = 0;
    bool initialized = false;

    static uint8_t irq(uint8_t clear = 0) {
        SPI.beginTransaction(SPISettings(profile::spiHz, MSBFIRST, SPI_MODE0));
        digitalWrite(profile::nss, LOW);
        SPI.transfer(clear ? 0x92 : 0x12);
        const uint8_t flags = SPI.transfer(clear);
        digitalWrite(profile::nss, HIGH);
        SPI.endTransaction();
        return flags;
    }

    bool transmit(const wire::Frame& frame) {
        if ((!initialized && !begin()) || !frame.size || frame.size > wire::maxFrame)
            return false;
        if (!LoRa.beginPacket() || LoRa.write(frame.bytes, frame.size) != frame.size) {
            begin();
            return false;
        }
        const uint64_t air = profile::airUs(frame.size);
        // LoRa 0.8.0 has no bounded TX-done wait. Poll its IRQ flag after async TX.
        irq(0xff);
        const uint64_t start = nowUs();
        LoRa.endPacket(true);
        nextTx = start + profile::pacedInterval(air);
        const uint64_t deadline = start + air + 4 * profile::turnaroundUs + 20000;
        while (!(irq() & 0x08)) {
            if (nowUs() >= deadline) {
                begin();
                return false;
            }
            delay(1);
        }
        irq(0x08);
        LoRa.receive();
        const uint64_t gapEnd = nowUs() + profile::dataGapUs;
        if (nextTx < gapEnd) nextTx = gapEnd;
        return true;
    }
};

inline uint64_t replyWaitUs(unsigned requestType, unsigned replySize) {
    // A repeated request can arrive before the peer may retransmit its previous reply.
    const unsigned precedingSize = requestType == wire::Start ? wire::reportLen : replySize;
    return profile::pacedInterval(profile::airUs(precedingSize)) + profile::airUs(replySize)
        + 3 * profile::turnaroundUs + 10000;
}
