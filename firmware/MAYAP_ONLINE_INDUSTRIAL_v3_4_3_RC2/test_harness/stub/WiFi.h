#pragma once
// Stub PHAN CUNG: radio Wi-Fi khong ton tai tren host. Mo phong du de thu vien
// that chay duoc: ket noi, mat ket noi, quet mang, AP mode.
#include <Arduino.h>
#include <string>

#define WL_CONNECTED 3
#define WL_IDLE_STATUS 0
#define WL_DISCONNECTED 6
#define WL_NO_SSID_AVAIL 1
#define WL_CONNECT_FAILED 4
#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
#define WIFI_MODE_AP 2
#define WIFI_MODE_STA 1
#define WIFI_SCAN_RUNNING -1
#define WIFI_SCAN_FAILED -2
typedef int wifi_mode_t;
typedef int wl_status_t;

struct IPAddress {
  uint8_t o[4]{192, 168, 4, 1};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { o[0]=a;o[1]=b;o[2]=c;o[3]=d; }
  String toString() const {
    char b[20]; snprintf(b, sizeof(b), "%u.%u.%u.%u", o[0],o[1],o[2],o[3]);
    return String(b);
  }
  operator uint32_t() const { return (uint32_t)o[0]<<24 | (uint32_t)o[1]<<16 | (uint32_t)o[2]<<8 | o[3]; }
};

struct WiFiStub {
  // --- dieu khien tu test ---
  int forcedStatus = WL_CONNECTED;
  bool apActive = false;
  int scanCount = 3;

  int status() { return forcedStatus; }
  wifi_mode_t getMode() { return apActive ? WIFI_AP_STA : WIFI_STA; }
  bool mode(wifi_mode_t m) { apActive = (m == WIFI_AP || m == WIFI_AP_STA); return true; }
  void persistent(bool) {}
  void setAutoReconnect(bool) {}
  bool setSleep(bool) { return true; }
  void begin(const char *, const char *) { forcedStatus = WL_CONNECTED; }
  void begin() { forcedStatus = WL_CONNECTED; }
  bool disconnect(bool = false, bool = false) { forcedStatus = WL_DISCONNECTED; return true; }
  bool softAP(const char *, const char * = nullptr) { apActive = true; return true; }
  bool softAPdisconnect(bool = false) { apActive = false; return true; }
  IPAddress softAPIP() { return IPAddress(192,168,4,1); }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
  IPAddress localIP() { return IPAddress(192,168,1,50); }
  String macAddress() { return String("B8:5F:CC:2C:01:FC"); }
  String SSID() { return String("MAYAP-TEST-AP"); }
  String SSID(int i) { char b[24]; snprintf(b,sizeof(b),"NET%d",i); return String(b); }
  int32_t RSSI(int = 0) { return -55; }
  uint8_t encryptionType(int) { return 3; }
  int scanNetworks(bool = false, bool = false) { return scanCount; }
  int scanComplete() { return scanCount; }
  void scanDelete() {}
  int channel(int = 0) { return 6; }
  void macAddress(uint8_t *m) {
    static const uint8_t v[6] = {0xB8,0x5F,0xCC,0x2C,0x01,0xFC};
    memcpy(m, v, 6);
  }
};
extern WiFiStub WiFi;
