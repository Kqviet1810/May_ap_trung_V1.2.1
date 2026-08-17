#pragma once
// Stub PHAN CUNG: khong co TCP server tren host. Ghi lai route da dang ky de
// test co the goi handler truc tiep, mo phong nguoi dung mo trang captive.
#include <Arduino.h>
#include <map>
#include <functional>
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTP_ANY 255
typedef int HTTPMethod;
class WebServer {
 public:
  explicit WebServer(int = 80) {}
  void on(const char *uri, std::function<void()> fn) { routes_[uri] = fn; }
  void on(const char *uri, HTTPMethod, std::function<void()> fn) { routes_[uri] = fn; }
  void onNotFound(std::function<void()> fn) { notFound_ = fn; }
  void begin() { started = true; }
  void stop() { started = false; }
  void close() { started = false; }
  void handleClient() {}
  void send(int code, const char *type, const String &body) {
    lastCode = code; lastType = type ? type : ""; lastBody = body.v_;
  }
  void send(int code, const char *type, const char *body) {
    lastCode = code; lastType = type ? type : ""; lastBody = body ? body : "";
  }
  void sendHeader(const char *, const String &, bool = false) {}
  void send_P(int code, const char *type, const char *body) { send(code, type, body); }
  String arg(const char *name) const {
    auto it = args_.find(name);
    return it == args_.end() ? String("") : String(it->second);
  }
  bool hasArg(const char *name) const { return args_.count(name) != 0; }
  String uri() const { return String(uri_); }
  HTTPMethod method() const { return method_; }
  void setContentLength(size_t) {}
  // --- dieu khien tu test ---
  bool visit(const char *uri) {
    uri_ = uri;
    auto it = routes_.find(uri);
    if (it != routes_.end()) { it->second(); return true; }
    if (notFound_) { notFound_(); return true; }
    return false;
  }
  void setArg(const char *k, const char *v) { args_[k] = v; }
  void clearArgs() { args_.clear(); }
  bool hasRoute(const char *u) const { return routes_.count(u) != 0; }
  size_t routeCount() const { return routes_.size(); }
  bool started = false;
  int lastCode = 0;
  std::string lastType, lastBody, uri_;
  HTTPMethod method_ = HTTP_GET;
 private:
  std::map<std::string, std::function<void()>> routes_;
  std::function<void()> notFound_;
  std::map<std::string, std::string> args_;
};
