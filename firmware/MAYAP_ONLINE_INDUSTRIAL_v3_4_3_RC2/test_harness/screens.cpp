#include "config.h"
#include "kernel_shims.h"
#include "hmi.h"
#include "machine_control.h"

static void show(const char *name) {
  lcd.oob_ = 0;
  lcd.setDrawColor(1); lcd.setFontMode(0); lcd.clearBuffer();
  switch (view) {
    case View::Home: drawHome(); break;
    case View::MainMenu: drawMainMenu(); break;
    case View::GeneralMenu: drawGeneralMenu(); break;
    case View::SettingList: drawSettingList(); break;
    case View::EditSetting: drawEditSetting(); break;
    case View::TurnStats: drawTurnStats(); break;
    case View::AutoTune: drawAutoTune(); break;
    case View::EventLog: drawEventLog(); break;
    case View::Confirm: drawConfirm(); break;
    case View::Alarm: drawAlarm(); break;
    case View::WifiPortal: drawWifiPortal(); break;
  }
  printf("\n=== %s ===%s\n", name, lcd.oob_ ? "  *** OUT OF BOUNDS ***" : "");
  printf("%s", lcd.preview().c_str());
}

int main() {
  currentConfig = MachineConfig{};
  mayapSanitizeConfig(currentConfig);
  currentRuntime.temperature = 37.5f; currentRuntime.humidity = 58.0f;
  currentRuntime.sensorOnline = true; currentRuntime.batchRunning = true;
  currentRuntime.currentDay = 12; currentRuntime.heaterPower = 45.0f;
  currentRuntime.heaterOn = true; currentRuntime.circulationFanOn = true;
  currentRuntime.nextTurnMinutes = 95; currentRuntime.turnState = TurnState::Waiting;
  currentRuntime.turnCountToday = 3; currentRuntime.turnCountBatch = 47;
  currentRuntime.netState = NetState::Online;
  snprintf(currentRuntime.machineState, sizeof(currentRuntime.machineState), "DANG AP AUTO");
  snprintf(currentRuntime.dateText, sizeof(currentRuntime.dateText), "16/08/2026");

  view = View::Home; homePage = 0; show("HOME p1 (dang ap)");
  currentRuntime.batchRunning = false; currentRuntime.currentDay = 0;
  currentRuntime.heaterPower = 0; currentRuntime.nextTurnMinutes = 0;
  currentRuntime.turnState = TurnState::Stopped;
  snprintf(currentRuntime.machineState, sizeof(currentRuntime.machineState), "SAN SANG AUTO");
  show("HOME p1 (san sang)");

  currentRuntime.batchRunning = true; currentRuntime.currentDay = 12;
  currentRuntime.heaterPower = 45; currentRuntime.nextTurnMinutes = 95;
  currentRuntime.turnState = TurnState::Waiting;
  homePage = 1; show("HOME p2 TRANG THAI");

  homePage = 0;
  view = View::MainMenu; mainIndex = 1; listTop = 0; show("MENU CHINH");
  view = View::GeneralMenu; listIndex = 0; listTop = 0; show("CAI DAT CHUNG");
  listIndex = 5; listTop = 2; show("CAI DAT CHUNG (cuon)");

  view = View::SettingList; selectedGroup = 0; listIndex = 0; listTop = 0;
  show("CAI DAT ME");
  selectedGroup = 1; listIndex = 0; listTop = 0; show("NHIET & BAO VE");
  selectedGroup = 2; listIndex = 4; listTop = 1; show("DAO TRUNG");
  selectedGroup = 6; listIndex = 1; listTop = 0; show("KET NOI");

  view = View::WifiPortal; show("WIFI PORTAL (idle)");
  currentRuntime.portalState = PortalState::Active; show("WIFI PORTAL (active)");
  currentRuntime.portalState = PortalState::Idle;

  // Alarms
  view = View::Alarm; alarmIndex = 0;
  currentRuntime.activeFaultDisplayCount = 3;
  currentRuntime.activeFaultCount = 3;
  currentRuntime.activeFaults[0] = {112, 394, (uint8_t)FaultSeverity::Emergency, 0x01};
  currentRuntime.activeFaults[1] = {401,  18, (uint8_t)FaultSeverity::Emergency, 0x03};
  currentRuntime.activeFaults[2] = {120, 420, (uint8_t)FaultSeverity::Warning, 0x01};
  currentRuntime.temperature = 39.4f;
  show("ALARM E112");
  alarmIndex = 1; show("ALARM E401 (da ACK)");
  alarmIndex = 2; currentRuntime.humidity = 42.0f; show("ALARM E120");

  currentRuntime.primaryFaultCode = 112;
  view = View::Home; homePage = 0; show("HOME co loi");
  homePage = 1; show("HOME p2 co loi");
  return 0;
}
