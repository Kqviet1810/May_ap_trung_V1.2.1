// Test-only: mo private de kiem chung logic bao ve. Khong sua code san pham.
#define private public
#define protected public
#include "config.h"
#include "kernel_shims.h"
#include "hmi.h"
#include "machine_control.h"
#undef private
#undef protected
#include <vector>
#include <string>

using namespace Mayap;
static int gFail = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { \
  ++gFail; printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)
static void section(const char *n) { printf("\n[%s]\n", n); }

// ---------------------------------------------------------------------------
static void testFaultTable() {
  section("1. BANG LOI DUNG CHUNG");
  const uint16_t codes[] = {101,102,103,104,110,111,112,120,201,202,203,204,
                            301,302,303,304,305,306,307,308,309,310,311,312,
                            401,402};
  for (uint16_t c : codes) {
    const FaultDescriptor &d = faultDescriptor(static_cast<FaultCode>(c));
    char m[80];
    snprintf(m, sizeof(m), "E%u co descriptor rieng", c);
    CHECK(d.code != FaultCode::None, m);
    snprintf(m, sizeof(m), "E%u alarmBit hop le", c);
    CHECK((d.alarmBit & ALARM_KNOWN_MASK) == d.alarmBit && d.alarmBit != 0, m);
    snprintf(m, sizeof(m), "E%u co title/cause/action", c);
    CHECK(d.title[0] && d.cause[0] && d.action[0], m);
    // Tieu de phai vua man hinh 128 px voi font 6x12 (21 ky tu).
    snprintf(m, sizeof(m), "E%u title <= 21 ky tu", c);
    CHECK(strlen(d.title) <= 21, m);
    // Hanh dong phai wrap gon trong 2 dong x 24 ky tu.
    snprintf(m, sizeof(m), "E%u action <= 48 ky tu", c);
    CHECK(strlen(d.action) <= 48, m);
    // Emergency/Stop bat buoc phai co hanh dong bao ve nao do.
    if (d.severity == FaultSeverity::Emergency) {
      snprintf(m, sizeof(m), "E%u Emergency phai cat nhiet", c);
      CHECK(d.inhibitsHeat, m);
    }
  }
  CHECK(faultDescriptor(static_cast<FaultCode>(999)).code == FaultCode::None,
        "Ma la tra ve descriptor unknown, khong crash");
  {
    uint16_t real = 0U;
    for (uint16_t c = 0U; c < 1000U; ++c)
      if (faultDescriptor(static_cast<FaultCode>(c)).code != FaultCode::None) ++real;
    CHECK(real == FAULT_CODE_COUNT,
          "FAULT_CODE_COUNT khop so dong that cua bang loi");
    CHECK(real == sizeof(codes)/sizeof(codes[0]),
          "Bo test phu het moi ma loi trong bang");
  }
  // FaultManager phai co du slot cho MOI ma loi cung luc, neu khong hai ma
  // cuoi bang se dung chung mot slot va che mat nhau.
  {
    FaultManager fm;
    uint32_t t = 1000;
    for (uint16_t c : codes) fm.set(static_cast<FaultCode>(c), true, t);
    uint8_t distinct = 0;
    for (uint16_t c : codes) if (fm.active(static_cast<FaultCode>(c))) ++distinct;
    char m[96];
    snprintf(m, sizeof(m), "Giu duoc dong thoi ca %zu ma loi (dem duoc %u)",
             sizeof(codes)/sizeof(codes[0]), distinct);
    CHECK(distinct == sizeof(codes)/sizeof(codes[0]), m);
    HmiFaultItem buf[HMI_FAULT_DISPLAY_CAPACITY];
    const uint8_t n = fm.copyActiveForHmi(buf, HMI_FAULT_DISPLAY_CAPACITY);
    CHECK(n == HMI_FAULT_DISPLAY_CAPACITY, "copyActiveForHmi lap day duoc bo dem HMI");
    // Thu tu phai giam dan theo displayPriority.
    bool ordered = true;
    for (uint8_t i = 1; i < n; ++i) {
      if (faultDescriptor(static_cast<FaultCode>(buf[i-1].code)).displayPriority <
          faultDescriptor(static_cast<FaultCode>(buf[i].code)).displayPriority)
        ordered = false;
    }
    CHECK(ordered, "Loi nghiem trong nhat luon dung dau danh sach HMI");
    CHECK(buf[0].code == 112 || buf[0].code == 401,
          "Loi uu tien cao nhat la qua nhiet khan cap hoac SSR dinh");
  }
  // HMI va firmware phai suy ra cung mot AlarmBit.
  for (uint16_t c : codes) {
    CHECK(alarmBitForFaultCode(c) ==
          faultDescriptor(static_cast<FaultCode>(c)).alarmBit,
          "HMI alarmBit == firmware alarmBit");
  }
  // Uu tien hien thi phai duy nhat de thu tu loi on dinh.
  for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); ++i)
    for (size_t j = i+1; j < sizeof(codes)/sizeof(codes[0]); ++j) {
      CHECK(faultDescriptor(static_cast<FaultCode>(codes[i])).displayPriority !=
            faultDescriptor(static_cast<FaultCode>(codes[j])).displayPriority,
            "displayPriority khong trung nhau");
    }
}

// ---------------------------------------------------------------------------
static void testSanitizeSingleSource() {
  section("2. SANITIZE - MOT NGUON DUY NHAT");
  srand(12345);
  for (int i = 0; i < 4000; ++i) {
    MachineConfig a{};
    uint8_t *raw = reinterpret_cast<uint8_t *>(&a);
    for (size_t k = 0; k < sizeof(a); ++k) raw[k] = rand() & 0xFF;
    MachineConfig b = a, c = a;
    mayapSanitizeConfig(a);
    sanitizeConfig(b);            // duong HMI
    sanitizeMachineConfig(c);     // duong firmware/Web/EEPROM
    if (memcmp(&a, &b, sizeof(a)) != 0 || memcmp(&a, &c, sizeof(a)) != 0) {
      CHECK(false, "HMI/firmware sanitize cho ket qua khac nhau");
      return;
    }
    // Bat bien an toan sau sanitize.
    CHECK(a.targetTemp >= TARGET_TEMP_MIN_C && a.targetTemp <= TARGET_TEMP_MAX_C, "SV trong dai");
    CHECK(a.lowTempAlarm <= a.targetTemp - LOW_ALARM_GAP_C, "Bao thap < SV");
    CHECK(a.highTempAlarm >= a.targetTemp + HIGH_ALARM_GAP_C, "Bao cao > SV");
    CHECK(a.emergencyTemp >= a.highTempAlarm + EMERGENCY_ABOVE_HIGH_C, "Khan cap > bao cao");
    CHECK(a.emergencyTemp <= EMERGENCY_MAX_C, "Khan cap <= tran cung");
    CHECK(a.ventOffTemp <= a.ventOnTemp - VENT_HYSTERESIS_C, "Vent co hysteresis");
    CHECK(a.alarmEnabled, "Coi bao dong khong the tat");
    CHECK(a.controlMode == ControlMode::Pid, "Luon o che do PID");
    CHECK(a.sensorTimeoutSec >= CFG_SENSOR_TIMEOUT_MIN_S &&
          a.sensorTimeoutSec <= CFG_SENSOR_TIMEOUT_MAX_S, "Timeout cam bien trong dai");
    // Idempotent: sanitize lan hai khong duoc doi gi.
    MachineConfig d = a; mayapSanitizeConfig(d);
    CHECK(memcmp(&a, &d, sizeof(a)) == 0, "sanitize idempotent");
    if (gFail) return;
  }
  printf("  4000 cau hinh ngau nhien: 3 duong sanitize cho ket qua giong het nhau\n");
}

// ---------------------------------------------------------------------------
static void testSettingsTableMatchesSanitize() {
  section("3. BANG SETTINGS <-> SANITIZE");
  for (uint8_t i = 0; i < SETTING_COUNT; ++i) {
    const SettingItem &it = SETTINGS[i];
    char m[96];
    snprintf(m, sizeof(m), "'%s' co min < max", it.label);
    CHECK(it.minimum < it.maximum, m);
    snprintf(m, sizeof(m), "'%s' co buoc nhay > 0", it.label);
    CHECK(it.step > 0.0f, m);
    // Dat gia tri bien roi sanitize: gia tri phai TON TAI duoc (rieng cac o co
    // rang buoc dong thi settingLimits da thu hep san).
    for (int end = 0; end < 2; ++end) {
      MachineConfig cfg{};
      float lo, hi; settingLimits(cfg, it, lo, hi);
      const float v = end ? hi : lo;
      writeSetting(cfg, it, v);
      MachineConfig after = cfg;
      mayapSanitizeConfig(after);
      const float got = readSetting(after, it);
      snprintf(m, sizeof(m), "'%s' bien %.2f khong bi sanitize sua thanh %.2f",
               it.label, v, got);
      CHECK(fabsf(got - v) < 0.051f, m);
    }
  }
  // Moi nhom phai lien tuc va phu het bang.
  uint8_t total = 0;
  for (uint8_t g = 0; g < GROUP_COUNT; ++g) {
    CHECK(GROUPS[g].first == total, "Nhom lien tuc, khong ho/khong chong lan");
    total = static_cast<uint8_t>(total + GROUPS[g].count);
  }
  CHECK(total == SETTING_COUNT, "Tat ca setting deu thuoc mot nhom");
  // Moi thong so xuat hien dung mot lan.
  int seen[SETTING_COUNT] = {};
  for (uint8_t i = 0; i < SETTING_COUNT; ++i) ++seen[GROUP_SETTING_INDEXES[i]];
  for (uint8_t i = 0; i < SETTING_COUNT; ++i)
    CHECK(seen[i] == 1, "Moi setting duoc tham chieu dung 1 lan");
}

// ---------------------------------------------------------------------------
static void testEepromSchema() {
  section("4. EEPROM SCHEMA 3/4/5");
  CHECK(sizeof(ConfigRecordV3) != sizeof(ConfigRecordV4) &&
        sizeof(ConfigRecordV4) != sizeof(ConfigRecordV5) &&
        sizeof(ConfigRecordV3) != sizeof(ConfigRecordV5),
        "Ba schema co kich thuoc khac nhau (truong size phan biet duoc)");
  CHECK(sizeof(ConfigRecordV5) <= EEPROM_CONFIG_SLOT_BYTES, "V5 vua slot 256 B");

  srand(999);
  for (int i = 0; i < 500; ++i) {
    MachineConfig c{};
    uint8_t *raw = reinterpret_cast<uint8_t *>(&c);
    for (size_t k = 0; k < sizeof(c); ++k) raw[k] = rand() & 0xFF;
    mayapSanitizeConfig(c);
    const MachineConfig back = unpackConfigV5(packConfig(c));
    CHECK(machineConfigEqual(c, back), "pack/unpack V5 khong mat du lieu");
    CHECK(back.cloudEnabled == c.cloudEnabled, "cloudEnabled duoc luu");
    if (gFail) return;
  }
  // Ban ghi V4 cu doc len phai mac dinh ONLINE.
  PackedMachineConfigV4 old{};
  old.targetTemp = 37.5f; old.tempHysteresis = 0.2f;
  old.lowTempAlarm = 36.5f; old.highTempAlarm = 38.2f; old.emergencyTemp = 39.0f;
  old.kp = 18; old.ki = 0.8f; old.kd = 45; old.pidCycleSec = 10;
  old.maxHeaterPower = 100; old.lowHumidityAlarm = 45;
  old.humidityAlarmDelaySec = 60; old.circulationFanEnabled = 1;
  old.ventOnTemp = 38.0f; old.ventOffTemp = 37.6f; old.turningEnabled = 1;
  old.turnIntervalMin = 120; old.turnMaxRunSec = 300; old.totalIncubationDays = 21;
  old.powerRestoreDelaySec = 30; old.sensorTimeoutSec = 10; old.alarmEnabled = 1;
  const MachineConfig m4 = unpackConfigV4(old);
  CHECK(m4.cloudEnabled == true, "Ban ghi V4 cu -> ONLINE mac dinh");
  CHECK(fabsf(m4.targetTemp - 37.5f) < 0.001f, "V4 giu nguyen SV");
  CHECK(m4.sensorTimeoutSec == 10, "V4 giu nguyen timeout cam bien");
}

// ---------------------------------------------------------------------------
// Bo phat hien loi thanh nhiet chay tren MachineController that.
static void feed(MachineController &M, uint32_t &t, uint32_t stepMs,
                 uint32_t totalMs, float temp, float power,
                 bool ssrOn, bool masterOn) {
  for (uint32_t e = 0; e < totalMs; e += stepMs) {
    t += stepMs; g_fakeMillis = t;
    M.temperature_ = temp;
    M.updateHeaterIntegrity(t, power, ssrOn, masterOn);
  }
}

static void testHeaterProtection() {
  section("5. BAO VE THANH NHIET (401/402)");
  static MachineController M;
  uint32_t t = 1000; g_fakeMillis = t;
  M.sensorUsable_ = true;
  M.sensorStartupGraceUntil_ = 0;
  M.config_ = MachineConfig{}; mayapSanitizeConfig(M.config_);

  // --- 401: da tat het ma nhiet tang 2 C trong 4 phut ---
  M.temperature_ = 37.0f;
  feed(M, t, 1000, 30000, 37.0f, 0.0f, false, false);
  CHECK(!M.heaterRunawayLatched_, "Chua bao runaway trong cua so quan sat");
  for (int s = 0; s < 240; ++s) {
    t += 1000; g_fakeMillis = t;
    M.temperature_ = 37.0f + s * (2.0f / 240.0f);
    M.updateHeaterIntegrity(t, 0.0f, false, false);
  }
  CHECK(M.heaterRunawayLatched_, "401 bao khi nhiet tang du da cat het");
  CHECK(M.faults_.active(FaultCode::HeaterRunaway), "401 vao FaultManager");
  CHECK(M.faults_.heatInhibited(), "401 cat quyen cap nhiet");
  CHECK(faultDescriptor(FaultCode::HeaterRunaway).severity == FaultSeverity::Emergency,
        "401 muc KHAN CAP");

  // --- 401 khong duoc bao nham khi nhiet chi dao dong nho / tut xuong ---
  static MachineController M2;
  uint32_t t2 = 1000; g_fakeMillis = t2;
  M2.sensorUsable_ = true; M2.sensorStartupGraceUntil_ = 0;
  M2.config_ = MachineConfig{}; mayapSanitizeConfig(M2.config_);
  for (int s = 0; s < 3600; ++s) {           // 1 gio nguoi dan
    t2 += 1000; g_fakeMillis = t2;
    M2.temperature_ = 37.0f - s * 0.002f + ((s % 7) * 0.02f);  // tut dan + nhieu
    M2.updateHeaterIntegrity(t2, 0.0f, false, false);
  }
  CHECK(!M2.heaterRunawayLatched_, "401 KHONG bao nham khi may nguoi dan");

  // --- 402: day 100 % suot 20 phut ma nhiet dung yen ---
  static MachineController M3;
  uint32_t t3 = 1000; g_fakeMillis = t3;
  M3.sensorUsable_ = true; M3.sensorStartupGraceUntil_ = 0;
  M3.config_ = MachineConfig{}; mayapSanitizeConfig(M3.config_);
  feed(M3, t3, 1000, 600000, 30.0f, 100.0f, true, true);   // 10 phut
  CHECK(!M3.heaterStallLatched_, "Chua bao 402 truoc 15 phut");
  feed(M3, t3, 1000, 400000, 30.0f, 100.0f, true, true);   // them ~6.7 phut
  CHECK(M3.heaterStallLatched_, "402 bao sau 15 phut khong len nhiet");
  CHECK(!M3.faults_.heatInhibited(),
        "402 KHONG cat nhiet (cat cung vo ich, can nguoi can thiep)");

  // --- 402 khong bao khi thanh nhiet van lam viec binh thuong ---
  static MachineController M4;
  uint32_t t4 = 1000; g_fakeMillis = t4;
  M4.sensorUsable_ = true; M4.sensorStartupGraceUntil_ = 0;
  M4.config_ = MachineConfig{}; mayapSanitizeConfig(M4.config_);
  for (int s = 0; s < 2400; ++s) {           // 40 phut gia nhiet nguoi
    t4 += 1000; g_fakeMillis = t4;
    M4.temperature_ = 20.0f + s * 0.006f;    // ~0.36 C/phut
    M4.updateHeaterIntegrity(t4, 100.0f, true, true);
  }
  CHECK(!M4.heaterStallLatched_, "402 KHONG bao khi dang gia nhiet nguoi that");

  // --- Auto Tune duoc mien tru ---
  static MachineController M5;
  uint32_t t5 = 1000; g_fakeMillis = t5;
  M5.sensorUsable_ = true; M5.sensorStartupGraceUntil_ = 0;
  M5.config_ = MachineConfig{}; mayapSanitizeConfig(M5.config_);
  M5.autotune_.configure(37.5f); M5.autotune_.start(t5, 37.0f);
  for (int s = 0; s < 400; ++s) {
    t5 += 1000; g_fakeMillis = t5;
    M5.temperature_ = 37.0f + s * 0.01f;
    M5.updateHeaterIntegrity(t5, 0.0f, false, false);
  }
  CHECK(!M5.heaterRunawayLatched_, "Auto Tune duoc mien tru khoi 401");
}

// ---------------------------------------------------------------------------
static void testHmiNavigation() {
  section("6. DIEU HUONG HMI");
  CHECK(MAIN_COUNT == 4, "MENU CHINH dung 4 muc");
  CHECK(!strcmp(mainItemLabel(0), "CAI DAT ME"), "Muc 1 = CAI DAT ME");
  CHECK(!strcmp(mainItemLabel(1), "CAI DAT CHUNG"), "Muc 2 = CAI DAT CHUNG");
  CHECK(!strcmp(mainItemLabel(2), "NHAT KY"), "Muc 3 = NHAT KY");
  CHECK(!strcmp(mainItemLabel(3), "TU CHINH PID"), "Muc 4 = TU CHINH PID");
  CHECK(GROUPS[GROUP_BATCH].count == 3, "CAI DAT ME chi 3 thong so");

  currentConfig = MachineConfig{}; mayapSanitizeConfig(currentConfig);
  currentRuntime = MachineRuntime{};

  // Moi nhom: duyet het moi dong, chon roi thoat, khong duoc ket o dau.
  for (uint8_t g = 0; g < GROUP_COUNT; ++g) {
    const uint8_t n = settingListItemCount(g);
    CHECK(n >= GROUPS[g].count + 1U, "Nhom co it nhat cac thong so + Thoat");
    CHECK(settingListExitIndex(g) == n - 1U, "Thoat luon o dong cuoi");
    for (uint8_t i = 0; i < n; ++i) {
      selectedGroup = g; listIndex = i; listTop = 0; view = View::SettingList;
      goBack();
      CHECK(view == View::MainMenu || view == View::GeneralMenu,
            "Thoat nhom luon ve dung cap tren");
    }
  }
  // Nhom 0 ve MENU CHINH, nhom 1..6 ve CAI DAT CHUNG.
  selectedGroup = GROUP_BATCH; view = View::SettingList; goBack();
  CHECK(view == View::MainMenu && mainIndex == MAIN_BATCH, "Nhom me -> MENU CHINH");
  for (uint8_t g = GENERAL_GROUP_FIRST; g < GROUP_COUNT; ++g) {
    selectedGroup = g; view = View::SettingList; goBack();
    CHECK(view == View::GeneralMenu, "Nhom chung -> CAI DAT CHUNG");
    CHECK(listIndex == g - GENERAL_GROUP_FIRST, "Con tro dung ve dung nhom vua ra");
  }
  // Moi View deu co duong ve, khong View nao tu khoa.
  const View all[] = {View::Home, View::MainMenu, View::GeneralMenu,
                      View::SettingList, View::EditSetting, View::TurnStats,
                      View::AutoTune, View::EventLog, View::Confirm,
                      View::Alarm, View::WifiPortal};
  for (View v : all) {
    view = v; selectedGroup = 2; confirmReturnView = View::Home;
    alarmReturnView = View::Home;
    for (int hop = 0; hop < 8 && view != View::Home; ++hop) goBack();
    CHECK(view == View::Home, "Nhan giu nhieu lan luon ve duoc man hinh chinh");
  }
  // Dong "Cau hinh Wi-Fi" chi hien khi dang ONLINE.
  currentConfig.cloudEnabled = true;
  CHECK(groupExtra(GROUP_NETWORK) == GroupExtra::WifiPortal, "ONLINE: co dong Wi-Fi");
  CHECK(settingListItemCount(GROUP_NETWORK) == 3, "ONLINE: 1 setting + Wi-Fi + Thoat");
  currentConfig.cloudEnabled = false;
  CHECK(groupExtra(GROUP_NETWORK) == GroupExtra::None, "OFFLINE: an dong Wi-Fi");
  CHECK(settingListItemCount(GROUP_NETWORK) == 2, "OFFLINE: 1 setting + Thoat");
  currentConfig.cloudEnabled = true;
}

// ---------------------------------------------------------------------------
static void testTargetShiftParity() {
  section("7. DOI SV: HMI VA WEB CHO KET QUA GIONG NHAU");
  MachineConfig base{}; mayapSanitizeConfig(base);
  for (float sv = TARGET_TEMP_MIN_C; sv <= TARGET_TEMP_MAX_C; sv += 0.1f) {
    MachineConfig hmi = base;
    const float oldTarget = hmi.targetTemp;
    hmi.targetTemp = sv;
    mayapShiftThresholdsWithTarget(hmi, oldTarget);
    mayapSanitizeConfig(hmi);

    MachineConfig web = base;
    const float oldTargetW = web.targetTemp;
    web.targetTemp = sv;
    mayapShiftThresholdsWithTarget(web, oldTargetW);
    sanitizeMachineConfig(web);

    CHECK(machineConfigEqual(hmi, web), "Doi SV tu HMI == doi SV tu Web");
    CHECK(hmi.lowTempAlarm < hmi.targetTemp, "Bao thap luon duoi SV sau khi dich");
    CHECK(hmi.highTempAlarm > hmi.targetTemp, "Bao cao luon tren SV sau khi dich");
    if (gFail) return;
  }
  printf("  101 muc SV: hai duong cho ket qua giong het nhau\n");
}

// ---------------------------------------------------------------------------
static void testAlarmTextFits() {
  section("8. VAN BAN CANH BAO VUA MAN HINH");
  const uint16_t codes[] = {101,102,103,104,110,111,112,120,201,202,203,204,
                            301,302,303,304,305,306,307,308,309,310,311,312,401,402};
  for (uint16_t c : codes) {
    char action[2][26];
    const uint8_t lines = wrapText(faultActionText(c), action, 24U);
    char m[96];
    snprintf(m, sizeof(m), "E%u: hanh dong wrap vua 2 dong", c);
    CHECK(lines >= 1 && lines <= 2, m);
    // Khong duoc cut mat chu: tong do dai wrap >= 90% chuoi goc.
    size_t got = strlen(action[0]) + (lines > 1 ? strlen(action[1]) : 0);
    snprintf(m, sizeof(m), "E%u: khong mat chu khi wrap (%zu/%zu)", c, got,
             strlen(faultActionText(c)));
    CHECK(got + 2 >= strlen(faultActionText(c)), m);
    for (uint8_t i = 0; i < lines; ++i) {
      snprintf(m, sizeof(m), "E%u dong %u <= 24 ky tu", c, i);
      CHECK(strlen(action[i]) <= 24, m);
    }
  }
  const uint8_t sev[] = {0,1,2,3};
  for (uint8_t v : sev) CHECK(severityText(v)[0], "Moi muc nghiem trong co ten");
}

// ---------------------------------------------------------------------------
static void testEventLogText() {
  section("9. NHAT KY HIEN DUNG TEN SU KIEN");
  const uint16_t codes[] = {101,102,103,104,110,111,112,120,201,202,203,204,
                            301,302,303,304,305,306,307,308,309,310,311,312,401,402};
  for (uint16_t c : codes) {
    HmiEventItem e{};
    e.code = static_cast<uint16_t>(1000U + c);
    e.type = 4;
    char title[40], detail[40];
    eventText(e, title, sizeof(title), detail, sizeof(detail));
    char m[96];
    snprintf(m, sizeof(m), "Nhat ky E%u hien ten loi, khong phai 'SU KIEN'", c);
    CHECK(strstr(title, "SU KIEN") == nullptr, m);
    snprintf(m, sizeof(m), "Nhat ky E%u co ma loi trong tieu de", c);
    char expect[10]; snprintf(expect, sizeof(expect), "E%03u", c);
    CHECK(strstr(title, expect) != nullptr, m);
  }
  // Su kien mang.
  const uint16_t netCodes[] = {90, 91, 92};
  for (uint16_t c : netCodes) {
    HmiEventItem e{}; e.code = c; e.type = 18;
    char title[40], detail[40];
    eventText(e, title, sizeof(title), detail, sizeof(detail));
    char m[80];
    snprintf(m, sizeof(m), "Su kien mang %u co ten rieng", c);
    CHECK(strstr(title, "SU KIEN") == nullptr, m);
  }
}

int main() {
  printf("================ MAYAP v3.4.3 TEST SUITE ================\n");
  testFaultTable();
  testSanitizeSingleSource();
  testSettingsTableMatchesSanitize();
  testEepromSchema();
  testHeaterProtection();
  testHmiNavigation();
  testTargetShiftParity();
  testAlarmTextFits();
  testEventLogText();
  printf("\n========================================================\n");
  printf("PASS = %d   FAIL = %d   ->  %s\n", gPass, gFail,
         gFail ? "CO LOI" : "TAT CA DAT");
  return gFail ? 1 : 0;
}
