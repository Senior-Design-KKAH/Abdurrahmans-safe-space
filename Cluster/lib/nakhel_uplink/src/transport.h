// How the ARQ engine reaches the server. ESP32: src/esp32_transport.cpp. Tests: fakes.
#pragma once

#include <cstddef>

namespace nakhel {

constexpr size_t kResponseBytes = 1024;

struct HttpResponse {
  int status = 0;                   // HTTP status; 0 or negative means no answer arrived
  char body[kResponseBytes] = {};   // NUL-terminated; longer answers are cut off, which is safe
};

class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  // One POST. Must time out by itself: 5 s to connect and 15 s to read, per the contract.
  virtual void post(const char* path, const char* body, size_t length, HttpResponse& response) = 0;
};

}  // namespace nakhel
