#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Config.h>
#include <RPWProtocol.h>
#include <TDMA.h>
#include <SelectiveRepeatARQ.h>
#include <ResultQueue.h>
#include <communication_internal.h>
#include <application_integration.h>
#include <CommunicationLog.h>
using namespace rpw;
static_assert(cfg::SF==7&&cfg::Bandwidth==125000&&cfg::CodingRate==5&&cfg::Preamble==8&&cfg::Guard==30,"Update TDMA formula/tests before changing radio timing");
uint32_t bootId,runId=0,epoch=0,frameNo=0,nextBeacon=1000,txStarted=0;
volatile bool txDone=false,rxPending=false;
volatile uint32_t txAt=0,rxAt=0;
volatile unsigned rxLen=0;
uint8_t rxBuffer[255];bool txBusy=false,synced=false,beaconPending=false,frameActive=false;
uint32_t rxEvents=0,ackEvents=0,decodeFailures=0,uniqueRecords=0,duplicates=0,originals=0,retries=0;
unsigned txBytes=0;
void onTx(){txAt=LoRa.irqTimestamp();txDone=true;}
void onRx(int n){if(rxPending)return;unsigned j=0;while(LoRa.available()&&j<255)rxBuffer[j++]=LoRa.read();rxLen=j;rxAt=LoRa.irqTimestamp();rxPending=true;}
bool send(const Packet&p){
 if(txBusy)return false;
 uint8_t b[255];unsigned n=serialize(p,b);if(!n)return false;
 LoRa.idle();if(!LoRa.beginPacket())return false;LoRa.write(b,n);txDone=false;txBusy=true;txBytes=n;txStarted=millis();LoRa.endPacket(true);return true;
}
void serviceTx(){
 if(txBusy&&!txDone&&millis()-txStarted>tdma::airtime(txBytes)+200){
  LoRa.idle();txBusy=false;beaconPending=false;frameActive=false;synced=false;
  LOG_ERROR("TX_TIMEOUT frame abandoned; delivery unknown\n");LoRa.receive();
 }
 if(txBusy&&txDone){txBusy=false;LoRa.receive();}
}
Packet packet(uint8_t type,uint8_t node,uint32_t token,uint32_t batch,unsigned count){Packet p;p.type=type;p.node=node;p.run=runId;p.token=token;p.batch=batch;p.count=count;return p;}
#if NODE_ID == 0
arq::Receiver receivers[2];bool ackSent[2]={},reportedTimeout[2]={};uint32_t grantAt[2]={};
struct Seen {uint32_t token=0,id=0;uint8_t node=0,app[ApplicationBytes]={};};
Seen history[cfg::StorageRecords];unsigned historyNext=0;
void receive(const Packet&p,uint32_t at){
 if(!p.node||p.node>2||p.run!=runId||!frameActive||!tdma::uplink(p.node,at-epoch))return;
 auto&r=receivers[p.node-1];uint32_t off=at-epoch-tdma::base(p.node);
 if(p.type==Ready){if(off>=tdma::cell())return;
  bool fresh=r.token!=p.token||r.batch!=p.batch;
  if(r.grant(p.token,p.batch,p.count)){if(fresh){grantAt[p.node-1]=at;reportedTimeout[p.node-1]=false;}LOG_INFO("GRANT Node=%u token=%08lx batch=%lu count=%u\n",p.node,(unsigned long)p.token,(unsigned long)p.batch,p.count);}return;}
 if(r.token!=p.token||r.batch!=p.batch||r.count!=p.count)return;
 if(p.type==Finish){if(off>=tdma::cell())return;LOG_INFO("FIN Node=%u batch=%lu sender=%s received=%02x expected=%02x\n",p.node,(unsigned long)p.batch,p.body[0]?"COMPLETE":"INCOMPLETE",r.bits,arq::mask(r.count));return;}
 if(p.type!=Data)return;
 unsigned cell=off/tdma::cell();if(cell!=unsigned(p.index)+1)return;
 rxEvents++;
 ml::Result result;
 if(!measurement(p,result)){decodeFailures++;LOG_ERROR("ML_RESULT validation FAIL; not acknowledged\n");return;}
 bool seen=false;
 for(const auto& h:history) if(h.node==p.node&&h.token==p.token&&h.id==p.measurement){
  if(memcmp(h.app,p.body,ApplicationBytes)){LOG_ERROR("IDENTITY_CONFLICT not acknowledged\n");return;}
  seen=true;break;
 }
 uint32_t digest=crc32(p.body,p.len);unsigned status=r.classify(p.index,p.measurement,digest);
 if(!status){LOG_ERROR("ARQ conflict/invalid record not acknowledged\n");return;}
 if(status==1){
  if(!seen){if(!queueUpload(p)){LOG_ERROR("UPLOAD_QUEUE_FULL/unavailable not acknowledged\n");return;}auto&h=history[historyNext++%cfg::StorageRecords];h.node=p.node;h.token=p.token;h.id=p.measurement;memcpy(h.app,p.body,ApplicationBytes);uniqueRecords++;
}
  else{status=2;duplicates++;}
  r.commit(p.index,p.measurement,digest);
 }else duplicates++;
 LOG_INFO("HEAD node=%u token=%08lx id=%lu day=%u bench_battery=%u/7 p=%.9f bits=%08lx duplicate=%u RAM=accepted\n",
 p.node,(unsigned long)p.token,(unsigned long)p.measurement,result.day,result.battery,
 double(result.probability),(unsigned long)ml::floatBits(result.probability),status==2);

}
#else
uint32_t batchId=1,batchStarted=0;arq::Window window;
bool admitted=false,finishPending=false;uint8_t finishOK=0,terminalBitmap=0;unsigned cellIndex=0;uint32_t finishedFrame=0;
void startBatch(){
 unsigned count=communication::queueDepth();
 if(!window.count&&count&&!finishPending){LOG_INFO("QUEUE depth=%u starting batch=%lu\n",count,(unsigned long)batchId);window=arq::Window(count>8?8:count);admitted=false;batchStarted=millis();communication::internal::markInFlight(window.count);}
}
void receive(const Packet&p,uint32_t at){
 if(p.type==Beacon){
  if(runId!=p.run){runId=p.run;window.bits=0;window.attempt=0;admitted=false;}
  frameNo=get32(p.body);epoch=at;synced=true;cellIndex=0;
  unsigned off=4+(NODE_ID-1)*8;
  admitted=get32(p.body+off)==bootId&&get32(p.body+off+4)==batchId;
  LOG_INFO("SYNC frame=%lu batch=%lu admitted=%u slot=%u\n",(unsigned long)frameNo,(unsigned long)batchId,admitted,NODE_ID);return;
 }
 uint32_t off=at-epoch,base=tdma::base(NODE_ID);
 if(synced&&admitted&&matchingAck(p,NODE_ID,runId,bootId,batchId,window.count)&&off>=base+9*tdma::cell()+30&&off<base+tdma::slot()){
  if(window.ack(p.body[0]))LOG_INFO("ACK batch=%lu bitmap=%02x\n",(unsigned long)batchId,window.bits);
 }
}
#endif
void pollRadio(){if(txBusy||!rxPending)return;uint8_t b[255];unsigned n;uint32_t at;
 noInterrupts();n=rxLen;at=rxAt;memcpy(b,rxBuffer,n);rxPending=false;interrupts();Packet p;if(parse(p,b,n)){LOG_DEBUG("LINK RSSI=%d LoRa_SNR=%.2f\n",LoRa.packetRssi(),double(LoRa.packetSnr()));receive(p,at);}else{decodeFailures++;LOG_ERROR("TRANSPORT integrity/framing FAIL\n");}
}
uint8_t radioRegister(uint8_t address,uint32_t hz){
 SPI.beginTransaction(SPISettings(hz,MSBFIRST,SPI_MODE0));digitalWrite(cfg::CS,LOW);
 SPI.transfer(address&0x7f);uint8_t value=SPI.transfer(0);digitalWrite(cfg::CS,HIGH);SPI.endTransaction();return value;
}
bool initializeRadio(){
 if(LoRa.begin(cfg::Frequency))return true;
 LOG_INFO("RADIO_DIAG Node=%u pins SCK=%d MISO=%d MOSI=%d CS=%d RESET=%d DIO0=%d expected_RegVersion=12\n",NODE_ID,cfg::SCK,cfg::MISO,cfg::MOSI,cfg::CS,cfg::RESET,cfg::DIO0);
 for(uint32_t hz:{8000000UL,1000000UL,100000UL}){
  LOG_INFO("RADIO_DIAG SPI_Hz=%lu RegVersion=",(unsigned long)hz);
  for(unsigned i=0;i<8;i++){LOG_INFO("%02x ",radioRegister(0x42,hz));delay(5);}
  LOG_INFO("OpMode=%02x\n",radioRegister(0x01,hz));
 }
 LoRa.setSPIFrequency(1000000);
 if(LoRa.begin(cfg::Frequency)){LOG_INFO("RADIO_RECOVERED using 1MHz SPI\n");return true;}
 LOG_ERROR("FATAL SX1278 init; no valid radio identity on configured SPI pins (including 1MHz retry)\n");
 return false;
}
void setup(){
 Serial.setTxBufferSize(1024);Serial.begin(115200);
 Serial.printf("ROLE %s Node=%u 115200 baud\n",NODE_ID==0?"HEAD":"SENDER",NODE_ID);
 bootId=esp_random();if(!bootId)bootId=1;
#if DEMO_ML_ONLY
 communication::internal::initialize();
 Serial.println("ML_ONLY TEST: LoRa disabled; submissions stay in volatile RAM, no radio delivery is tested.");
 initializeApplication();
 return;
#endif
 SPI.begin(cfg::SCK,cfg::MISO,cfg::MOSI,cfg::CS);LoRa.setPins(cfg::CS,cfg::RESET,cfg::DIO0);
 if(!initializeRadio()){while(true)delay(1000);}
 LoRa.setSpreadingFactor(cfg::SF);LoRa.setSignalBandwidth(cfg::Bandwidth);LoRa.setCodingRate4(cfg::CodingRate);LoRa.setPreambleLength(cfg::Preamble);LoRa.setSyncWord(cfg::SyncWord);LoRa.setTxPower(cfg::Power);LoRa.enableCrc();
 LoRa.setDeferredInterrupts(true);LoRa.onReceive(onRx);LoRa.onTxDone(onTx);LoRa.receive();
 LOG_INFO("IRQ setup GPIO%u level=%d flags=%02x deferred_SPI=YES\n",cfg::DIO0,digitalRead(cfg::DIO0),radioRegister(0x12,1000000));
 LOG_INFO("RPW ML v5 Node=%u radio OK production token=%08lx DATA=34 application=6 cell=%lu slot=%lu frame=%lu ms\n",NODE_ID,(unsigned long)bootId,(unsigned long)tdma::cell(),(unsigned long)tdma::slot(),(unsigned long)tdma::frame());
#if NODE_ID == 0
 runId=bootId;
#else
 communication::internal::initialize();
#endif
 initializeApplication();
}
void loop(){
#if DEMO_ML_ONLY
 serviceApplication();delay(1);return;
#endif
 LoRa.pollIrq();serviceTx();pollRadio();uint32_t now=millis();
 serviceApplication();now=millis();
#if NODE_ID == 0
 for(unsigned i=0;i<2;i++)if(receivers[i].token&&!reportedTimeout[i]&&now-grantAt[i]>=cfg::BatchTimeout&&receivers[i].bits!=arq::mask(receivers[i].count)){
  reportedTimeout[i]=true;LOG_INFO("BATCH_TIMEOUT INCOMPLETE Node=%u batch=%lu bitmap=%02x\n",i+1,(unsigned long)receivers[i].batch,receivers[i].bits);
 }
 if(!txBusy&&tdma::due(now,nextBeacon)){
  Packet p;p.type=Beacon;p.run=runId;p.len=20;put32(p.body,++frameNo);
  for(unsigned i=0;i<2;i++){put32(p.body+4+i*8,receivers[i].token);put32(p.body+8+i*8,receivers[i].batch);ackSent[i]=false;}
  frameActive=false;if(send(p)){beaconPending=true;nextBeacon=now+tdma::frame()+tdma::airtime(48)+500;}
 }
 if(beaconPending&&!txBusy){beaconPending=false;epoch=txAt;frameActive=true;nextBeacon=epoch+tdma::frame();LOG_INFO("BEACON frame=%lu\n",(unsigned long)frameNo);}
 if(frameActive&&!txBusy)for(unsigned i=0;i<2;i++){
  uint32_t t=epoch+tdma::base(i+1)+9*tdma::cell()+30;
  if(!ackSent[i]&&tdma::due(now,t)){ackSent[i]=true;
   if(!tdma::due(now,t+15)&&receivers[i].token){auto&r=receivers[i];Packet p=packet(Ack,i+1,r.token,r.batch,r.count);p.body[0]=r.bits;p.len=1;
    ackEvents++;send(p);
    LOG_INFO("ACK Node=%u batch=%lu bitmap=%02x\n",i+1,(unsigned long)r.batch,r.bits);
   }break;
  }
 }
#else
 // Low-rate diagnostics only while unsynchronized; never sends outside TDMA.
#if RPW_LOG_LEVEL >= 2
 static uint32_t lastRxDiagnostic=0;
 if(RPW_LOG_LEVEL>=2&&!synced&&!txBusy&&now-lastRxDiagnostic>=5000){
  lastRxDiagnostic=now;
  uint8_t mode=radioRegister(0x01,1000000),irq=radioRegister(0x12,1000000),mapping=radioRegister(0x40,1000000);
  int dio=digitalRead(cfg::DIO0);
  LOG_DEBUG("RX_DIAG Node=%u waiting_beacon OpMode=%02x IRQ=%02x DIO0_GPIO%d=%d Mapping=%02x\n",NODE_ID,mode,irq,cfg::DIO0,dio,mapping);
 }
#endif
 startBatch();if(window.count&&!finishPending&&now-batchStarted>=cfg::BatchTimeout){finishOK=0;finishPending=true;terminalBitmap=window.bits;communication::internal::reportBatchOutcome(window.count,terminalBitmap);LOG_INFO("BATCH_TIMEOUT INCOMPLETE; reporting at next synchronized control cell\n");}
 if(!synced||txBusy){delay(1);return;}
 if(tdma::due(now,epoch+tdma::frame())){synced=false;return;}
 uint32_t base=epoch+tdma::base(NODE_ID);
 if(cellIndex<=8){uint32_t t=base+cellIndex*tdma::cell();if(tdma::due(now,t)){
  unsigned index=cellIndex++;if(tdma::due(now,t+15)){delay(1);return;}
  if(index==0&&finishPending){Packet p=packet(Finish,NODE_ID,bootId,batchId,window.count);p.len=1;p.body[0]=finishOK;
   if(send(p)){finishPending=false;communication::internal::finishBatch(window.count,terminalBitmap);window=arq::Window();batchId++;admitted=false;}return;}
  if(!window.count)return;
  if(index==0&&!admitted){send(packet(Ready,NODE_ID,bootId,batchId,window.count));return;}
  if(index&&admitted&&!finishPending&&window.missing(index-1)){
   ml::QueuedRecord r;if(!communication::internal::copyRecord(index-1,r))return;Packet p=packet(Data,NODE_ID,bootId,batchId,window.count);p.index=index-1;p.measurement=r.id;p.attempt=window.attempt;p.len=ApplicationBytes;memcpy(p.body,r.app,ApplicationBytes);
   if(send(p)){if(window.attempt)retries++;else originals++;LOG_INFO("[Sender Node %u] TX Measurement=%lu Sequence=%lu TDMA_Slot=%u batch=%lu index=%u attempt=%u bytes=%u\n",NODE_ID,(unsigned long)p.measurement,(unsigned long)p.measurement,NODE_ID,(unsigned long)batchId,p.index,p.attempt,p.len+28);}
  }
 }}
 if(tdma::due(now,base+tdma::slot())&&finishedFrame!=frameNo){finishedFrame=frameNo;
  if(admitted&&window.count&&!finishPending){if(window.complete()||window.exhausted()){
   finishOK=window.complete();finishPending=true;terminalBitmap=window.bits;communication::internal::reportBatchOutcome(window.count,terminalBitmap);unsigned delivered=__builtin_popcount(unsigned(window.bits));
   LOG_INFO("BATCH %s batch=%lu count=%u delivered=%u completed_measurement_delivery=%.2f%% duration_ms=%lu originals=%lu retries=%lu\n",finishOK?"COMPLETE":"INCOMPLETE",(unsigned long)batchId,window.count,delivered,100.0*delivered/window.count,(unsigned long)(now-batchStarted),(unsigned long)originals,(unsigned long)retries);
  }else window.advance();}
 }
#endif
 delay(1);
}
