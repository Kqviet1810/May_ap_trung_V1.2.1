#pragma once
// Wire stub co GIA LAP AT24C32 (4 KB) de duong luu cau hinh duoc chay that
// tu dau den cuoi tren may tinh: ghi hai khe A/B, CRC, doc nguoc schema.
#include <Arduino.h>

struct TwoWire {
  static const int EEPROM_ADDR = 0x57;
  uint8_t mem[4096]{};
  bool eepromEnabled = true;      // dat false de mo phong EEPROM hong

  bool begin(int, int, uint32_t) { return true; }
  void end() {}
  void setTimeOut(uint16_t) {}

  void beginTransmission(uint8_t a) { addr_ = a; txLen_ = 0; }
  size_t write(uint8_t b) { if (txLen_ < sizeof(tx_)) tx_[txLen_++] = b; return 1; }
  size_t write(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; ++i) write(p[i]);
    return n;
  }
  uint8_t endTransmission(bool = true) {
    if (addr_ != EEPROM_ADDR) return 0;
    if (!eepromEnabled) return 2;
    if (txLen_ >= 2) {
      uint16_t a = (uint16_t)((tx_[0] << 8) | tx_[1]);
      for (uint8_t i = 2; i < txLen_; ++i)
        if ((size_t)(a + i - 2) < sizeof(mem)) mem[a + i - 2] = tx_[i];
      pending_ = a;
    } else if (txLen_ == 2 || txLen_ == 0) {
      // dia chi cho lenh doc, hoac ACK polling
    }
    if (txLen_ >= 2) readPtr_ = (uint16_t)((tx_[0] << 8) | tx_[1]);
    return 0;
  }
  uint8_t requestFrom(uint8_t a, uint8_t n, bool = true) {
    rxLen_ = 0; rxPos_ = 0;
    if (a != EEPROM_ADDR || !eepromEnabled) return 0;
    for (uint8_t i = 0; i < n && i < sizeof(rx_); ++i) {
      const size_t idx = (size_t)readPtr_ + i;
      rx_[rxLen_++] = idx < sizeof(mem) ? mem[idx] : 0xFF;
    }
    readPtr_ = (uint16_t)(readPtr_ + rxLen_);
    return rxLen_;
  }
  int available() { return rxLen_ - rxPos_; }
  int read() { return rxPos_ < rxLen_ ? rx_[rxPos_++] : -1; }

 private:
  uint8_t addr_ = 0, tx_[64]{}, rx_[64]{};
  uint8_t txLen_ = 0, rxLen_ = 0, rxPos_ = 0;
  uint16_t readPtr_ = 0, pending_ = 0;
};
extern TwoWire Wire;
