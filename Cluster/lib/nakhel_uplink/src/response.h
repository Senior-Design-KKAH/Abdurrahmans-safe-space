// Decides what a server answer means for the record that was sent.
#pragma once

#include <cstddef>

#include "transport.h"

namespace nakhel {

enum class Verdict {
  Ack,        // the server has it: remove from the buffer
  Rejected,   // the server refuses it for good: remove and report, because retrying can't help
  KeyRefused, // bad or revoked key: stop sending and keep the buffer
  Retry,      // anything else: keep it and try again later
};

enum class AckKind { None, Stored, Duplicate };

struct Classified {
  Verdict verdict = Verdict::Retry;
  AckKind ack = AckKind::None;
  char errorCode[32] = {};  // the server's error.code, when it sent one
};

// Removing data is only safe on a verdict that really came from our server. A captive portal or proxy
// can answer 200, 401 or 400 with its own page, so a status code alone is never enough:
// - Ack needs status "stored" or "duplicate", and the node and timestamp that were sent.
// - Rejected and KeyRefused need one of the error codes in the contract.
// expectedNodeId is negative for cluster health, whose answer has no node_id.
Classified classify(const HttpResponse& response, int expectedNodeId, const char* expectedTimestamp);

// Finds "key":"value" anywhere in text. Returns false if it's missing or doesn't fit.
bool jsonString(const char* text, const char* key, char* out, size_t capacity);

}  // namespace nakhel
