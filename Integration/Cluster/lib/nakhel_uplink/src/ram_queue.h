// A fixed-size UplinkQueue in RAM. Its contents are lost on a reboot or power cut.
#pragma once

#include <cstddef>
#include <mutex>

#include "uplink_queue.h"

namespace nakhel {

template <size_t Capacity>
class RamQueue : public UplinkQueue {
 public:
  // Returns false when full and keeps the older records, so nothing already queued is overwritten.
  bool push(const UplinkRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == Capacity) {
      ++dropped_;
      return false;
    }
    slots_[(head_ + size_) % Capacity] = record;
    ++size_;
    return true;
  }

  bool peek(UplinkRecord& out) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) return false;
    out = slots_[head_];
    return true;
  }

  void remove() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) return;
    head_ = (head_ + 1) % Capacity;
    --size_;
  }

  size_t count() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
  }

  // How many records push() has refused because the queue was full.
  size_t dropped() {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

 private:
  std::mutex mutex_;
  UplinkRecord slots_[Capacity];
  size_t head_ = 0;
  size_t size_ = 0;
  size_t dropped_ = 0;
};

}  // namespace nakhel
