// The boundary between the LoRa side and the uplink.
#pragma once

#include <cstddef>

#include "records.h"

namespace nakhel {

// ARQ depends on peek() and remove() being separate: a record stays in the buffer until the server
// has acknowledged it, so a timeout or crash mid-send can't lose it.
//
// The uplink calls these from its own task, so an implementation must be safe against the LoRa task
// adding records at the same time.
class UplinkQueue {
 public:
  virtual ~UplinkQueue() = default;

  // Copies the oldest record into `out` without removing it. Returns false if the queue is empty.
  // Repeated calls must return the same record until remove() is called.
  virtual bool peek(UplinkRecord& out) = 0;

  // Removes the record most recently returned by peek().
  virtual void remove() = 0;

  virtual size_t count() = 0;
};

}  // namespace nakhel
