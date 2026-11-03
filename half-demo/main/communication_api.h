#pragma once
#include <stdint.h>
#include <MLResult.h>
namespace communication {
enum class SubmitResult : uint8_t { Queued, InvalidValue, QueueFull, NotReady, WrongRole, IdExhausted };
enum class DeliveryState : uint8_t { Unknown, Queued, InFlight, Delivered, Failed };
struct DeliveryStatus { uint32_t measurementId=0; DeliveryState state=DeliveryState::Unknown; };
// Task context only, never ISR. Queued does NOT mean delivered. ID is zero on failure.
// Valid day=1..31, probability=0..1, battery=0..100; floating values must be finite.
SubmitResult submitInferenceResult(float probability, uint8_t dayOfMonth,
                                  float batteryPercentage, uint32_t* id=nullptr);
DeliveryStatus deliveryStatus(uint32_t measurementId);
unsigned queueDepth();
struct ReceivedResult {
 ml::Result result; uint32_t measurementId=0, senderBootToken=0;
};
}
