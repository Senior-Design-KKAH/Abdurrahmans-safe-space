#include "esp32_transport.h"

#include <cstring>

namespace {

constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr uint16_t kReadTimeoutMs = 15000;

// Takes the answer from HTTPClient, which handles chunked encoding and keeps the connection reusable.
// Keeps what fits and discards the rest, so an unexpected page from a proxy can't exhaust the heap.
class BoundedSink : public Stream {
 public:
  BoundedSink(char* out, size_t capacity) : out_(out), capacity_(capacity) {}

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t room = capacity_ - 1 - length_;
    const size_t kept = size < room ? size : room;
    memcpy(out_ + length_, data, kept);
    length_ += kept;
    out_[length_] = '\0';
    return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

 private:
  char* out_;
  size_t capacity_;
  size_t length_ = 0;
};

}  // namespace

Esp32Transport::Esp32Transport(const char* baseUrl, const char* apiKey, const char* rootCa)
    : baseUrl_(baseUrl), secure_(strncmp(baseUrl, "https://", 8) == 0), authorization_(String("Bearer ") + apiKey) {
  if (secure_) {
    tls_.setCACert(rootCa);
    tls_.setHandshakeTimeout(kReadTimeoutMs / 1000);
  }
  http_.setReuse(true);
  http_.setConnectTimeout(kConnectTimeoutMs);
  http_.setTimeout(kReadTimeoutMs);
}

void Esp32Transport::post(const char* path, const char* body, size_t length, nakhel::HttpResponse& response) {
  const String url = String(baseUrl_) + path;
  const bool ready = secure_ ? http_.begin(tls_, url) : http_.begin(plain_, url);
  if (!ready) {
    response.status = 0;
    return;
  }
  http_.addHeader("Authorization", authorization_);
  http_.addHeader("Content-Type", "application/json");

  // POST takes a non-const pointer but only reads from it.
  response.status = http_.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(body)), length);
  if (response.status > 0) {
    // If the connection drops mid-answer the body stays incomplete, and that counts as no ACK.
    BoundedSink sink(response.body, sizeof(response.body));
    http_.writeToStream(&sink);
  }
  http_.end();
}
