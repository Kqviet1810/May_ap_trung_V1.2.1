#pragma once
// Stub PHAN CUNG: NVS flash. Giu trong RAM, du de thu vien luu/doc Wi-Fi va
// adapter luu cfgrev.
#include <Arduino.h>
#include <map>
#include <string>
class Preferences {
 public:
  bool begin(const char *ns, bool = false) { ns_ = ns ? ns : ""; return true; }
  void end() {}
  bool clear() { store().clear(); return true; }
  uint32_t getUInt(const char *k, uint32_t d = 0) {
    auto it = store().find(key(k)); return it == store().end() ? d : (uint32_t)atoll(it->second.c_str());
  }
  size_t putUInt(const char *k, uint32_t v) {
    char b[24]; snprintf(b, sizeof(b), "%u", v); store()[key(k)] = b; return 4;
  }
  bool getBool(const char *k, bool d = false) {
    auto it = store().find(key(k)); return it == store().end() ? d : it->second == "1";
  }
  size_t putBool(const char *k, bool v) { store()[key(k)] = v ? "1" : "0"; return 1; }
  String getString(const char *k, const char *d = "") {
    auto it = store().find(key(k)); return String(it == store().end() ? d : it->second.c_str());
  }
  size_t putString(const char *k, const char *v) {
    store()[key(k)] = v ? v : ""; return strlen(v ? v : "");
  }
  bool remove(const char *k) { return store().erase(key(k)) > 0; }
  bool isKey(const char *k) { return store().count(key(k)) != 0; }
  static std::map<std::string, std::string> &store() {
    static std::map<std::string, std::string> s; return s;
  }
 private:
  std::string key(const char *k) const { return ns_ + ":" + (k ? k : ""); }
  std::string ns_;
};
