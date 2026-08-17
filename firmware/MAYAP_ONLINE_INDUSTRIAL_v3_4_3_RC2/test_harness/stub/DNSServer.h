#pragma once
#include <Arduino.h>
enum class DNSReplyCode { NoError = 0, FormError, ServerFailure, NonExistentDomain };
class DNSServer {
 public:
  bool start(uint16_t, const String &, uint32_t) { running = true; return true; }
  void stop() { running = false; }
  void processNextRequest() {}
  void setErrorReplyCode(DNSReplyCode) {}
  bool running = false;
};
