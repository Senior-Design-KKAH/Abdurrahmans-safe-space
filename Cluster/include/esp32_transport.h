#pragma once

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "transport.h"

// One POST at a time over a kept-alive connection, because a new TLS handshake for every record is
// slow on an ESP32. Only used from the uplink task.
class Esp32Transport : public nakhel::HttpTransport {
 public:
  Esp32Transport(const char* baseUrl, const char* apiKey, const char* rootCa);
  void post(const char* path, const char* body, size_t length, nakhel::HttpResponse& response) override;

 private:
  const char* baseUrl_;
  bool secure_;
  String authorization_;
  WiFiClient plain_;
  WiFiClientSecure tls_;
  HTTPClient http_;
};
