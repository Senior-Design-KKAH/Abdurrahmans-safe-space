#include <Arduino.h>
#include <atomic>
#include "Config.h"
#include "application_integration.h"
#include "communication_api.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if NODE_ID == 0
#include <WiFi.h>
#include <HTTPClient.h>
struct Upload { uint8_t wire[rpw::DataPacketBytes]; };
static QueueHandle_t uploads=nullptr;
static std::atomic<bool> uploadHalted{false};

bool queueUpload(const rpw::Packet& packet) {
    Upload item;
    return uploads && rpw::serialize(packet,item.wire,sizeof item.wire)==sizeof item.wire &&
           xQueueSend(uploads,&item,0)==pdTRUE;
}

static void uploader(void*) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(DEMO_WIFI_SSID,DEMO_WIFI_PASSWORD);
    for(;;) {
        if(uploadHalted.load()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        Upload item;
        if(xQueuePeek(uploads,&item,pdMS_TO_TICKS(200))!=pdTRUE) continue;
        if(WiFi.status()!=WL_CONNECTED) {
            Serial.println("UPLINK Wi-Fi disconnected; RAM packet pending, retry in 3s");
            WiFi.reconnect(); vTaskDelay(pdMS_TO_TICKS(3000)); continue;
        }
        WiFiClient client; HTTPClient http;
        http.setConnectTimeout(1500); http.setTimeout(2000);
        http.setReuse(false);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        bool stored=false, permanent=false;
        int status=-1;
        if(http.begin(client,DEMO_SERVER_URL)) {
            http.addHeader("Content-Type","application/octet-stream");
            status=http.POST(item.wire,sizeof item.wire);
            if(status==200 || status==201) {
                // Bound both response size and total read time. Do not allocate a String body.
                if(http.getSize()==int(sizeof item.wire)) {
                    uint8_t echo[sizeof item.wire]; unsigned got=0;
                    auto* stream=http.getStreamPtr();
                    const uint32_t start=millis();
                    while(stream && got<sizeof echo && millis()-start<2000) {
                        int available=stream->available();
                        if(available>0) {
                            unsigned n=unsigned(available);
                            if(n>sizeof echo-got) n=sizeof echo-got;
                            const int read=stream->read(echo+got,n);
                            if(read>0) got+=unsigned(read);
                        } else if(!stream->connected()) break;
                        else vTaskDelay(1);
                    }
                    stored=got==sizeof echo && !memcmp(echo,item.wire,sizeof echo);
                }
                if(!stored) Serial.println("UPLINK invalid/missing exact echo; packet remains pending");
            } else if(status>=300 && status<500 && status!=408 && status!=429) {
                permanent=true;
            }
        }
        http.end();
        if(stored) {
            Upload removed;
            // This task is the only consumer; producers never alter the front entry.
            if(xQueueReceive(uploads,&removed,0)!=pdTRUE || memcmp(removed.wire,item.wire,sizeof item.wire)) {
                Serial.println("FATAL upload queue invariant"); uploadHalted.store(true); continue;
            }
            Serial.printf("UPLINK COMMITTED status=%d node=%u token=%08lx measurement=%lu bits=%08lx pending=%u\n",
                          status,item.wire[3],(unsigned long)rpw::get32(item.wire+8),
                          (unsigned long)rpw::get32(item.wire+16),
                          (unsigned long)rpw::get32(item.wire+26),unsigned(uxQueueMessagesWaiting(uploads)));
        } else {
            Serial.printf("UPLINK %s status=%d node=%u token=%08lx id=%lu packet retained%s\n",permanent?"PERMANENT ERROR":"RETRY",
                          status,item.wire[3],(unsigned long)rpw::get32(item.wire+8),
                          (unsigned long)rpw::get32(item.wire+16),permanent?"; fix configuration/conflict, type r to resume":"; retry in 3s");
            if(permanent) uploadHalted.store(true);
            else vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}
void initializeApplication() {
    uploads=xQueueCreate(cfg::QueueCapacity,sizeof(Upload));
    if(!uploads || xTaskCreatePinnedToCore(uploader,"uploader",6144,nullptr,1,nullptr,0)!=pdPASS) {
        Serial.println("FATAL uploader initialization; no acceptance possible");
        uploads=nullptr;
    }
    Serial.println("HEAD: ACK means accepted in volatile RAM. Pending uploads lost on restart. r resumes halted uplink.");
}
void serviceApplication() {
    for(unsigned i=0;i<16 && Serial.available();i++) {
        if(Serial.read()=='r') { uploadHalted.store(false); Serial.println("UPLINK resume requested"); }
    }
}
#else
#include "inference.h"
static QueueHandle_t commands=nullptr;
static std::atomic<bool> busy{true}, retained{false}, modelReady{false};
static float pendingScore=0;
static uint32_t tracking[cfg::QueueCapacity]={};

static const char* submitName(communication::SubmitResult value) {
    using communication::SubmitResult;
    switch(value) {
        case SubmitResult::Queued:return "Queued";
        case SubmitResult::InvalidValue:return "InvalidValue";
        case SubmitResult::QueueFull:return "QueueFull";
        case SubmitResult::NotReady:return "NotReady";
        case SubmitResult::WrongRole:return "WrongRole";
        case SubmitResult::IdExhausted:return "IdExhausted";
    }
    return "Unknown";
}
static void checkDelivery() {
    for(auto& id:tracking) if(id) {
        auto status=communication::deliveryStatus(id);
        if(status.state==communication::DeliveryState::Delivered || status.state==communication::DeliveryState::Failed) {
            Serial.printf("LORA measurement=%lu %s\n",(unsigned long)id,
                          status.state==communication::DeliveryState::Delivered?"DELIVERED to head RAM (computer persistence separate)":"FAILED unconfirmed (head may have received it)");
            id=0;
        }
    }
}
static void submitPending() {
    uint32_t* slot=nullptr;
    for(auto& id:tracking) if(!id) { slot=&id; break; }
    if(!slot) { Serial.println("SUBMISSION tracking full; result retained, type r later"); return; }
    uint32_t id=0;
    const auto outcome=communication::submitInferenceResult(pendingScore,cfg::BenchDay,cfg::BenchBatteryPercent,&id);
    Serial.printf("SUBMISSION %s measurement=%lu day=%u battery=SIMULATED %.1f%% code=%u\n",
                  submitName(outcome),(unsigned long)id,cfg::BenchDay,
                  double(cfg::BenchBatteryPercent),ml::batteryCode(cfg::BenchBatteryPercent));
    if(outcome==communication::SubmitResult::Queued) { *slot=id; retained.store(false); }
    else Serial.println("Result retained; type r for explicit submission retry. No new result was queued.");
}
static void inferenceWorker(void*) {
    if(!initializeInference()) {
        Serial.println("FATAL model initialization failed; c disabled"); vTaskDelete(nullptr); return;
    }
    modelReady.store(true); busy.store(false);
    Serial.println("SENDER ready. Type c for one ten-second run; r retries only an unqueued result.");
    for(;;) {
        checkDelivery();
        char command;
        if(xQueueReceive(commands,&command,pdMS_TO_TICKS(50))!=pdTRUE) continue;
        if(command=='c') {
            float mean=0;
            if(inferClip(mean)) {
                pendingScore=mean; retained.store(true);
                Serial.printf("MEAN windows=10 probability=%.9f bits=%08lx\n",double(mean),(unsigned long)ml::floatBits(mean));
                checkDelivery(); submitPending();
            } else Serial.println("RUN FAILED: no score submitted");
        } else if(retained.load()) { checkDelivery(); submitPending(); }
        busy.store(false);
    }
}
void initializeApplication() {
    commands=xQueueCreate(1,sizeof(char));
    if(!commands || xTaskCreatePinnedToCore(inferenceWorker,"inference",8192,nullptr,1,nullptr,0)!=pdPASS)
        Serial.println("FATAL inference task creation; c disabled");
}
void serviceApplication() {
    for(unsigned i=0;i<16 && Serial.available();i++) {
        char command=Serial.read();
        if(command!='c' && command!='r') continue;
        if(!modelReady.load()) { Serial.println("MODEL not ready"); continue; }
        if(busy.exchange(true)) { Serial.println("BUSY: command rejected"); continue; }
        if((command=='c' && retained.load()) || (command=='r' && !retained.load())) {
            Serial.println(command=='c'?"Unqueued result retained; type r first":"No unqueued result to retry");
            busy.store(false); continue;
        }
        if(xQueueSend(commands,&command,0)!=pdTRUE) {
            Serial.println("Command queue unavailable; command rejected"); busy.store(false);
        }
    }
}
#endif
