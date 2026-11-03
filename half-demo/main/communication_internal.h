#pragma once
// Communication owner only; applications use communication_api.h.
#include <communication_api.h>
#include <ResultQueue.h>
namespace communication { namespace internal {
void initialize();
bool copyRecord(unsigned index, ml::QueuedRecord& out);
void markInFlight(unsigned count);
void reportBatchOutcome(unsigned count, uint8_t acknowledgedBitmap);
void finishBatch(unsigned count, uint8_t acknowledgedBitmap);
} }
