#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "Config.h"

// Explicit bench mode; ASCII diagnostic bytes, no ML result, TDMA or ACK claim.
static constexpr unsigned PacketBytes=34;
static bool ready=false, active=false, sending=false;
static unsigned startedCount=0, completedCount=0;
static uint32_t nextAt=0, startedAt=0;
static volatile bool done=false;
static volatile uint32_t doneAt=0;
static char payload[PacketBytes+1];

static uint8_t regRead(uint8_t address) {
    SPI.beginTransaction(SPISettings(cfg::RadioTestSPI,MSBFIRST,SPI_MODE0));
    digitalWrite(cfg::CS,LOW); SPI.transfer(address&0x7f);
    const uint8_t value=SPI.transfer(0);
    digitalWrite(cfg::CS,HIGH); SPI.endTransaction(); return value;
}
static void regWrite(uint8_t address,uint8_t value) {
    SPI.beginTransaction(SPISettings(cfg::RadioTestSPI,MSBFIRST,SPI_MODE0));
    digitalWrite(cfg::CS,LOW); SPI.transfer(address|0x80); SPI.transfer(value);
    digitalWrite(cfg::CS,HIGH); SPI.endTransaction();
}
static void onDone() { doneAt=LoRa.irqTimestamp(); done=true; }

// Derived from Kareem/tools/radio_diagnostic; never drives MISO as an output.
static uint8_t softRead(uint8_t address) {
    digitalWrite(cfg::SCK,LOW); digitalWrite(cfg::CS,LOW); delayMicroseconds(50);
    for(int b=7;b>=0;--b) {
        digitalWrite(cfg::MOSI,((address&0x7f)>>b)&1);
        delayMicroseconds(50); digitalWrite(cfg::SCK,HIGH);
        delayMicroseconds(50); digitalWrite(cfg::SCK,LOW);
    }
    digitalWrite(cfg::MOSI,LOW); uint8_t value=0;
    for(unsigned b=0;b<8;++b) {
        delayMicroseconds(50); digitalWrite(cfg::SCK,HIGH);
        delayMicroseconds(50); value=(value<<1)|digitalRead(cfg::MISO);
        digitalWrite(cfg::SCK,LOW);
    }
    digitalWrite(cfg::CS,HIGH); return value;
}
static void diagnoseSPI() {
    const uint32_t speeds[]={cfg::RadioTestSPI,1000000,100000};
    for(uint32_t hz:speeds) {
        LoRa.setSPIFrequency(hz);
        Serial.printf("INIT_RETRY SPI=%lu result=%d\n",(unsigned long)hz,LoRa.begin(cfg::Frequency));
        Serial.printf("HARD_SPI %lu RegVersion=",(unsigned long)hz);
        for(unsigned i=0;i<8;++i) {
            SPI.beginTransaction(SPISettings(hz,MSBFIRST,SPI_MODE0));
            digitalWrite(cfg::CS,LOW); SPI.transfer(0x42);
            const uint8_t value=SPI.transfer(0);
            digitalWrite(cfg::CS,HIGH); SPI.endTransaction(); Serial.printf("%02x ",value);
        }
        Serial.println();
    }
    SPI.end(); pinMode(cfg::SCK,OUTPUT);pinMode(cfg::MOSI,OUTPUT);pinMode(cfg::CS,OUTPUT);
    digitalWrite(cfg::CS,HIGH);digitalWrite(cfg::SCK,LOW);
    pinMode(cfg::RESET,OUTPUT);digitalWrite(cfg::RESET,LOW);delay(10);digitalWrite(cfg::RESET,HIGH);delay(25);
    const uint8_t modes[]={INPUT,INPUT_PULLUP,INPUT_PULLDOWN};
    const char* names[]={"INPUT","PULLUP","PULLDOWN"};
    for(unsigned m=0;m<3;++m) {
        pinMode(cfg::MISO,modes[m]);delay(5);Serial.printf("SOFT_SPI %s RegVersion=",names[m]);
        for(unsigned i=0;i<16;++i)Serial.printf("%02x ",softRead(0x42));
        Serial.println();
    }
    pinMode(cfg::MISO,INPUT);
    Serial.println("FAIL radio identity/SPI initialization; no transmission attempted. Check wiring/power/module.");
}
void setup() {
    Serial.setTxBufferSize(1024);Serial.begin(115200);delay(300);
    Serial.println("LORA_TX_ONLY TEST sender=1; no receiver/ACK/TDMA; no automatic TX. Type t for ten packets.");
    Serial.printf("PINS SCK=%d MISO=%d MOSI=%d CS=%d RESET=%d DIO0=%d SPI=%lu\n",
                  cfg::SCK,cfg::MISO,cfg::MOSI,cfg::CS,cfg::RESET,cfg::DIO0,(unsigned long)cfg::RadioTestSPI);
    SPI.begin(cfg::SCK,cfg::MISO,cfg::MOSI,cfg::CS);
    LoRa.setPins(cfg::CS,cfg::RESET,cfg::DIO0);LoRa.setSPIFrequency(cfg::RadioTestSPI);
    if(!LoRa.begin(cfg::Frequency)) { diagnoseSPI();return; }
    unsigned good=0;
    Serial.print("IDENTITY RegVersion=");
    for(unsigned i=0;i<16;++i) { const uint8_t v=regRead(0x42);good+=v==0x12;Serial.printf("%02x ",v); }
    Serial.printf("matches=%u/16\n",good);
    if(good!=16) { Serial.println("FAIL unstable radio identity; no TX");return; }
    LoRa.setSpreadingFactor(cfg::SF);LoRa.setSignalBandwidth(cfg::Bandwidth);
    LoRa.setCodingRate4(cfg::CodingRate);LoRa.setPreambleLength(cfg::Preamble);
    LoRa.setSyncWord(cfg::SyncWord);LoRa.setTxPower(cfg::Power);LoRa.enableCrc();
    const uint32_t frf=(uint32_t(regRead(0x06))<<16)|(uint32_t(regRead(0x07))<<8)|regRead(0x08);
    const uint16_t preamble=(uint16_t(regRead(0x20))<<8)|regRead(0x21);
    Serial.printf("READBACK Frf=%06lx Modem1=%02x Modem2=%02x Modem3=%02x preamble=%u sync=%02x PA=%02x mode=%02x\n",
                  (unsigned long)frf,regRead(0x1d),regRead(0x1e),regRead(0x26),preamble,regRead(0x39),regRead(0x09),regRead(0x01));
    const uint32_t expectedFrf=uint32_t((uint64_t(cfg::Frequency)<<19)/32000000);
    if(frf!=expectedFrf || regRead(0x1d)!=0x72 || (regRead(0x1e)&0xf4)!=0x74 ||
       preamble!=8 || regRead(0x39)!=0x34 || regRead(0x09)!=0x88 || (regRead(0x01)&0x87)!=0x81) {
        Serial.println("FAIL configuration readback; no TX");return;
    }
    LoRa.setDeferredInterrupts(true);LoRa.onTxDone(onDone);
    ready=true;
    Serial.println("READY 433MHz SF7 BW125 CR4/5 explicit PHY_CRC preamble8 sync34 TX10dBm; payload=34 ASCII bytes. Type t.");
}
void loop() {
    if(!ready) { delay(20);return; }
    LoRa.pollIrq();
    if(sending && done) {
        sending=false;completedCount++;
        Serial.printf("TX_DONE packet=%u bytes=%u elapsed_ms=%lu deferred_DIO0_callback=PASS FIFO=PASS (reception unverified)\n",
                      completedCount,PacketBytes,(unsigned long)(doneAt-startedAt));
        nextAt=millis()+cfg::RadioTestGapMs;
        if(completedCount==cfg::RadioTestPackets) {
            active=false;LoRa.idle();
            Serial.printf("PASS LOCAL_TX started=%u completed=%u timeouts=0; no receiver/ACK evidence. Waiting for t.\n",startedCount,completedCount);
        }
    }
    if(sending && millis()-startedAt>1000) {
        const uint8_t irq=regRead(0x12),mode=regRead(0x01);
        LoRa.idle();sending=false;active=false;ready=false;
        Serial.printf("FAIL TX_TIMEOUT packet=%u IRQ=%02x mode=%02x DIO0=%d; hardware_TxDone=%s callback missing; no delivery claim\n",
                      startedCount,irq,mode,digitalRead(cfg::DIO0),(irq&0x08)?"SET":"NOT_SET");
        Serial.printf("TIMEOUT_REGS version=%02x sync=%02x FRF=%02x%02x%02x Modem1=%02x Modem2=%02x PA=%02x\n",
                      regRead(0x42),regRead(0x39),regRead(0x06),regRead(0x07),regRead(0x08),regRead(0x1d),regRead(0x1e),regRead(0x09));
        return;
    }
    if(Serial.available() && Serial.read()=='t') {
        if(active) Serial.println("BUSY test running; t rejected");
        else { startedCount=completedCount=0;active=true;nextAt=millis();Serial.println("START LOCAL_TX count=10 gap_ms=1000 payload=diagnostic ASCII, not an ML result"); }
    }
    if(active && !sending && int32_t(millis()-nextAt)>=0) {
        const int n=snprintf(payload,sizeof payload,"half-demo TX-only packet %02u",startedCount+1);
        if(n<0 || unsigned(n)>PacketBytes) { ready=false;Serial.println("FAIL payload bounds");return; }
        memset(payload+n,'.',PacketBytes-unsigned(n));payload[PacketBytes]=0;
        LoRa.idle();regWrite(0x12,0xff);done=false;
        if(!LoRa.beginPacket() || LoRa.write(reinterpret_cast<uint8_t*>(payload),PacketBytes)!=PacketBytes || regRead(0x22)!=PacketBytes) {
            ready=false;Serial.println("FAIL FIFO write/length; no TX");return;
        }
        regWrite(0x0d,0);
        bool same=true;
        for(unsigned i=0;i<PacketBytes;++i) if(regRead(0x00)!=uint8_t(payload[i])) same=false;
        regWrite(0x0d,0);
        if(!same) { ready=false;Serial.println("FAIL FIFO readback; no TX");return; }
        startedCount++;startedAt=millis();sending=true;
        LoRa.endPacket(true);
        const uint8_t launchedMode=regRead(0x01);
        Serial.printf("TX_START packet=%u bytes=%u mode=%02x payload=%s\n",startedCount,PacketBytes,launchedMode,payload);

    }
    delay(1);
}
