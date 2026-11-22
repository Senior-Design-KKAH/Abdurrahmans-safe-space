#include "response.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nakhel {
namespace {

const char* skipSpace(const char* p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
  return p;
}

// The position just after `"key":`, or nullptr.
const char* findValue(const char* text, const char* key) {
  char quoted[32];
  const int length = snprintf(quoted, sizeof(quoted), "\"%s\"", key);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(quoted)) return nullptr;
  for (const char* at = strstr(text, quoted); at != nullptr; at = strstr(at + 1, quoted)) {
    const char* p = skipSpace(at + length);
    if (*p == ':') return skipSpace(p + 1);
  }
  return nullptr;
}

bool jsonInt(const char* text, const char* key, long& out) {
  const char* p = findValue(text, key);
  if (p == nullptr) return false;
  char* end = nullptr;
  out = strtol(p, &end, 10);
  return end != p;
}

bool oneOf(const char* value, const char* const* options, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (strcmp(value, options[i]) == 0) return true;
  }
  return false;
}

Classified classifySuccess(const HttpResponse& response, int expectedNodeId, const char* expectedTimestamp) {
  Classified result;
  char status[16];
  if (!jsonString(response.body, "status", status, sizeof(status))) return result;
  const bool stored = response.status == 201 && strcmp(status, "stored") == 0;
  const bool duplicate = response.status == 200 && strcmp(status, "duplicate") == 0;
  if (!stored && !duplicate) return result;

  char timestamp[32];
  if (!jsonString(response.body, "timestamp", timestamp, sizeof(timestamp))) return result;
  if (strcmp(timestamp, expectedTimestamp) != 0) return result;
  if (expectedNodeId >= 0) {
    long nodeId = -1;
    if (!jsonInt(response.body, "node_id", nodeId) || nodeId != expectedNodeId) return result;
  }
  result.verdict = Verdict::Ack;
  result.ack = stored ? AckKind::Stored : AckKind::Duplicate;
  return result;
}

}  // namespace

bool jsonString(const char* text, const char* key, char* out, size_t capacity) {
  const char* p = findValue(text, key);
  if (p == nullptr || *p != '"') return false;
  ++p;
  const char* end = strchr(p, '"');
  if (end == nullptr) return false;
  const size_t length = static_cast<size_t>(end - p);
  if (length >= capacity) return false;
  memcpy(out, p, length);
  out[length] = '\0';
  return true;
}

Classified classify(const HttpResponse& response, int expectedNodeId, const char* expectedTimestamp) {
  if (response.status == 200 || response.status == 201) {
    return classifySuccess(response, expectedNodeId, expectedTimestamp);
  }

  Classified result;
  char code[sizeof(result.errorCode)];
  if (!jsonString(response.body, "code", code, sizeof(code))) return result;
  memcpy(result.errorCode, code, sizeof(code));

  static const char* const kBadRequest[] = {"invalid_json", "invalid_body", "timestamp_in_future", "timestamp_too_old"};
  static const char* const kTooLarge[] = {"body_too_large"};
  static const char* const kUnknownNode[] = {"unknown_node"};
  static const char* const kUnauthorized[] = {"unauthorized"};

  if ((response.status == 400 && oneOf(code, kBadRequest, 4)) || (response.status == 413 && oneOf(code, kTooLarge, 1)) ||
      (response.status == 422 && oneOf(code, kUnknownNode, 1))) {
    result.verdict = Verdict::Rejected;
  } else if (response.status == 401 && oneOf(code, kUnauthorized, 1)) {
    result.verdict = Verdict::KeyRefused;
  }
  return result;
}

}  // namespace nakhel
