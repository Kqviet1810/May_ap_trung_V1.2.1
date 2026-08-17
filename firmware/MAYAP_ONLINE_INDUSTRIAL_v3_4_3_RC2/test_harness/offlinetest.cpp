// Xac minh cong tac ONLINE/OFFLINE that su cat radio, dung thu vien that.
#define main arduino_main_unused
#include "MAYAP_ONLINE_INDUSTRIAL_v3_4_3.ino"
#undef main
#include <mqtt_client.h>
using namespace Mayap;
static int gFail = 0, gPass = 0;
#define CHECK(c, m) do { if (c) ++gPass; else { ++gFail; printf("  FAIL: %s\n", m); } } while (0)

int main() {
  printf("===== ONLINE/OFFLINE VOI THU VIEN THAT =====\n");
  // Ghi truoc mot cau hinh OFFLINE vao EEPROM gia lap, roi boot.
  {
    MachineConfig c{}; c.cloudEnabled = false; mayapSanitizeConfig(c);
    PersistentStore store; store.begin();
    MachineConfig readback{};
    const bool saved = store.saveConfig(c, readback);
    CHECK(saved, "Luu duoc cau hinh OFFLINE vao EEPROM");
    CHECK(!readback.cloudEnabled, "EEPROM giu dung cloudEnabled=false");
  }
  setup();
  CHECK(!Machine.config().cloudEnabled, "Boot len doc dung che do OFFLINE");
  CHECK(!webBridgeStarted, "OFFLINE: KHONG khoi dong WebBridge");
  CHECK(!g_mqtt.started, "OFFLINE: KHONG khoi dong client MQTT");
  CHECK(g_mqtt.subscribed.empty(), "OFFLINE: khong subscribe topic nao");
  CHECK(g_mqtt.published.empty(), "OFFLINE: khong phat goi nao");

  NetState n; PortalState p;
  mayapReadNetStatus(n, p);
  CHECK(n == NetState::Disabled, "HMI bao mang DA TAT");
  CHECK(!mayapRequestWifiPortal(), "OFFLINE: khong mo duoc portal (dung y do)");

  for (int i = 0; i < 200; ++i) {
    g_fakeMillis += 250; Machine.update(g_fakeMillis);
    Machine.serviceI2c(g_fakeMillis); loop();
  }
  CHECK(g_mqtt.published.empty(), "OFFLINE: van im lang sau 50 s chay");
  CHECK(Machine.config().totalIncubationDays >= CFG_DAYS_MIN,
        "OFFLINE: may van chay dieu khien binh thuong");

  printf("\nPASS = %d   FAIL = %d   ->  %s\n", gPass, gFail,
         gFail ? "CO LOI" : "TAT CA DAT");
  return gFail ? 1 : 0;
}
