#pragma once
#include "MLResult.h"

namespace ml {
struct QueuedRecord { uint8_t app[PayloadBytes] = {}; uint32_t id = 0; };

// Serialized bytes are frozen on insertion, including acquisition day and probability.
template<unsigned Capacity> class ResultQueue {
    static_assert(Capacity >= 8, "Queue must hold an ARQ window");
    QueuedRecord records_[Capacity];
    unsigned head_ = 0, count_ = 0;
public:
    unsigned size() const { return count_; }
    bool full() const { return count_ == Capacity; }
    const QueuedRecord* at(unsigned index) const {
        return index < count_ ? &records_[(head_+index)%Capacity] : nullptr;
    }
    bool push(uint32_t id, unsigned node, unsigned day, unsigned battery, float probability) {
        if (!id || full()) return false;
        QueuedRecord next;
        if (!encode(node, day, battery, probability, next.app, sizeof next.app)) return false;
        next.id = id;
        records_[(head_+count_)%Capacity] = next;
        ++count_;
        return true;
    }
    bool pop(unsigned count) {
        if (count > count_) return false;
        head_ = (head_+count)%Capacity;
        count_ -= count;
        return true;
    }
};
}
