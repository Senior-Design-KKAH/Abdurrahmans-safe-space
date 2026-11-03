#include <communication_internal.h>
#include <cassert>
#include <cstdio>
using namespace communication;
int main() {
    uint32_t id=99;
#if NODE_ID == 0
    assert(submitInferenceResult(.5f,3,100,&id)==SubmitResult::WrongRole && id==0);
    assert(queueDepth()==0);
#else
    assert(submitInferenceResult(.5f,3,100,&id)==SubmitResult::NotReady && id==0);
    internal::initialize();
    assert(submitInferenceResult(NAN,3,100)==SubmitResult::InvalidValue);
    assert(submitInferenceResult(.5f,0,100)==SubmitResult::InvalidValue);
    for(unsigned i=0;i<32;i++) {
        assert(submitInferenceResult(.5f,3,100,&id)==SubmitResult::Queued && id==i+1);
    }
    assert(submitInferenceResult(.75f,3,100,&id)==SubmitResult::QueueFull && id==0);
    ml::QueuedRecord record;
    assert(internal::copyRecord(0,record));
    assert(record.id==1 && record.app[1]==((3<<3)|7));
    internal::markInFlight(1);
    assert(deliveryStatus(1).state==DeliveryState::InFlight);
    internal::reportBatchOutcome(1,1);
    assert(deliveryStatus(1).state==DeliveryState::Delivered && queueDepth()==32);
    internal::finishBatch(1,1);
    assert(submitInferenceResult(.75f,3,100,&id)==SubmitResult::Queued && id==33);
    assert(internal::copyRecord(31,record));
    ml::Result result;
    assert(record.id==33 && ml::decode(record.app,6,result));
    assert(ml::floatBits(result.probability)==0x3f400000);
#endif
    puts("PASS submission validation, role, queue saturation/retry and delivery state");
}
