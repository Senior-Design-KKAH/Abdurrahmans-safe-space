#include <communication_internal.h>
#include <Config.h>
#ifdef RPW_HOST_TEST
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
struct HostMutex { CRITICAL_SECTION cs; HostMutex(){InitializeCriticalSection(&cs);} ~HostMutex(){DeleteCriticalSection(&cs);} };
static HostMutex apiMutex;
struct Guard { Guard(){EnterCriticalSection(&apiMutex.cs);} ~Guard(){LeaveCriticalSection(&apiMutex.cs);} };
#else
#include <mutex>
static std::mutex apiMutex;
struct Guard { Guard(){apiMutex.lock();} ~Guard(){apiMutex.unlock();} };
#endif
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
static portMUX_TYPE apiMutex=portMUX_INITIALIZER_UNLOCKED;
struct Guard { Guard(){portENTER_CRITICAL(&apiMutex);} ~Guard(){portEXIT_CRITICAL(&apiMutex);} };
#endif
namespace communication {
static ml::ResultQueue<cfg::QueueCapacity> records;
static DeliveryStatus states[cfg::QueueCapacity*2];
static unsigned stateNext=0;
#if NODE_ID != 0
static uint32_t nextId=1;
#endif
static bool ready=false;
static void state(uint32_t id, DeliveryState value) {
 for(auto& s:states)if(s.measurementId==id){s.state=value;return;}
 auto& s=states[stateNext++%(cfg::QueueCapacity*2)];s.measurementId=id;s.state=value;
}
SubmitResult submitInferenceResult(float p,uint8_t day,float battery,uint32_t* id) {
 if(id)*id=0;
#if NODE_ID == 0
 (void)p;(void)day;(void)battery;return SubmitResult::WrongRole;
#else
 if(!ml::validProbability(p)||day<1||day>31||!isfinite(battery)||battery<0||battery>100)
  return SubmitResult::InvalidValue;
 Guard lock;
 if(!ready)return SubmitResult::NotReady;
 if(!nextId)return SubmitResult::IdExhausted;
 if(records.full())return SubmitResult::QueueFull;
 uint32_t assigned=nextId;
 if(!records.push(assigned,NODE_ID,day,ml::batteryCode(battery),p))return SubmitResult::InvalidValue;
 ++nextId;state(assigned,DeliveryState::Queued);if(id)*id=assigned;
 return SubmitResult::Queued;
#endif
}
DeliveryStatus deliveryStatus(uint32_t id) {
 Guard lock;
 for(const auto& s:states)if(id&&s.measurementId==id)return s;
 DeliveryStatus unknown;unknown.measurementId=id;return unknown;
}
unsigned queueDepth(){Guard lock;return records.size();}
namespace internal {
void initialize(){Guard lock;ready=true;}
bool copyRecord(unsigned index,ml::QueuedRecord& out) {
 Guard lock;const auto* r=records.at(index);if(!r)return false;out=*r;return true;
}
void markInFlight(unsigned count){Guard lock;for(unsigned i=0;i<count;i++){const auto*r=records.at(i);if(r)state(r->id,DeliveryState::InFlight);}}
void reportBatchOutcome(unsigned count,uint8_t bitmap){
 Guard lock;if(count>records.size())return;
 for(unsigned i=0;i<count;i++)state(records.at(i)->id,(bitmap&(1u<<i))?DeliveryState::Delivered:DeliveryState::Failed);
}
void finishBatch(unsigned count,uint8_t bitmap){
 Guard lock;if(count>records.size())return;
 for(unsigned i=0;i<count;i++)state(records.at(i)->id,(bitmap&(1u<<i))?DeliveryState::Delivered:DeliveryState::Failed);
 records.pop(count);
}
}
}
