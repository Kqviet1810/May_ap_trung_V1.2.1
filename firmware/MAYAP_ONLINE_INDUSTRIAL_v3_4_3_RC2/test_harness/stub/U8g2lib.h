#pragma once
// Stub U8g2 with a real 128x64 framebuffer so HMI layouts can be rendered and
// checked for overflow/overlap on the host.
#include <Arduino.h>
#include <string>
#include <vector>

#define U8G2_R0 0
#define U8X8_PIN_NONE 255

enum FontId { FONT_5x8 = 0, FONT_6x12, FONT_HELVB10, FONT_HELVB12, FONT_HELVB14, FONT_LOGISOSO20 };
extern const int u8g2_font_5x8_tf;
extern const int u8g2_font_6x12_tf;
extern const int u8g2_font_helvB10_tf;
extern const int u8g2_font_helvB12_tf;
extern const int u8g2_font_helvB14_tf;
extern const int u8g2_font_logisoso20_tn;

struct DrawnItem { int x, y, w, h; std::string text; int font; bool box; };

class U8G2Stub {
 public:
  U8G2Stub(int, int) {}
  bool begin() { return true; }
  void setBusClock(uint32_t) {}
  void setI2CAddress(uint8_t) {}
  void setContrast(uint8_t) {}
  void setDrawColor(uint8_t c) { color_ = c; }
  void setFontMode(uint8_t) {}
  void setFont(int f) { font_ = f; }
  void clearBuffer() { memset(fb_, 0, sizeof(fb_)); items_.clear(); }
  void sendBuffer() { ++frames_; }

  int charWidth() const {
    switch (font_) {
      case FONT_5x8: return 5;
      case FONT_6x12: return 6;
      case FONT_HELVB10: return 7;
      case FONT_HELVB12: return 8;
      case FONT_HELVB14: return 10;
      case FONT_LOGISOSO20: return 15;
    }
    return 6;
  }
  int fontHeight() const {
    switch (font_) {
      case FONT_5x8: return 8;
      case FONT_6x12: return 10;
      case FONT_HELVB10: return 11;
      case FONT_HELVB12: return 13;
      case FONT_HELVB14: return 15;
      case FONT_LOGISOSO20: return 20;
    }
    return 10;
  }
  int getStrWidth(const char *s) const {
    if (!s) return 0;
    int w = 0;
    const int cw = charWidth();
    for (const char *p = s; *p; ++p) {
      if (font_ == FONT_LOGISOSO20 && (*p == '.' || *p == ':')) w += 8;
      else if (font_ >= FONT_HELVB10 && (*p == ' ' || *p == 'I' || *p == 'i' || *p == '.')) w += cw - 3;
      else w += cw;
    }
    return w;
  }
  void drawStr(int x, int y, const char *s) {
    if (!s) return;
    const int h = fontHeight();
    items_.push_back({x, y - h + 1, getStrWidth(s), h, s, font_, false});
    plotText(x, y - h + 1, s);
  }
  void drawBox(int x, int y, int w, int h) {
    items_.push_back({x, y, w, h, "", font_, true});
    for (int j = y; j < y + h; ++j)
      for (int i = x; i < x + w; ++i) setPx(i, j, color_ ? 1 : 0);
  }
  void drawFrame(int x, int y, int w, int h) {
    for (int i = x; i < x + w; ++i) { setPx(i, y, 1); setPx(i, y + h - 1, 1); }
    for (int j = y; j < y + h; ++j) { setPx(x, j, 1); setPx(x + w - 1, j, 1); }
  }
  void drawHLine(int x, int y, int w) { for (int i = x; i < x + w; ++i) setPx(i, y, 1); }
  void drawVLine(int x, int y, int h) { for (int j = y; j < y + h; ++j) setPx(x, j, 1); }

  const std::vector<DrawnItem> &items() const { return items_; }
  uint32_t frames() const { return frames_; }
  int color() const { return color_; }

  // Text preview: 128x64 -> character grid using the recorded draw items.
  std::string preview() const {
    char grid[64][129];
    for (int r = 0; r < 64; ++r) { memset(grid[r], ' ', 128); grid[r][128] = 0; }
    for (const auto &it : items_) {
      if (it.box) {
        for (int r = it.y; r < it.y + it.h && r < 64; ++r)
          for (int c = it.x; c < it.x + it.w && c < 128; ++c)
            if (r >= 0 && c >= 0) grid[r][c] = '#';
        continue;
      }
      const int cw = it.w > 0 && !it.text.empty()
                   ? (int)(it.w / it.text.size()) : 6;
      for (size_t k = 0; k < it.text.size(); ++k) {
        const int c = it.x + (int)k * (cw > 0 ? cw : 1);
        const int r = it.y + it.h / 2;
        if (r >= 0 && r < 64 && c >= 0 && c < 128) grid[r][c] = it.text[k];
      }
    }
    std::string out;
    for (int r = 0; r < 64; ++r) {
      bool any = false;
      for (int c = 0; c < 128; ++c) if (grid[r][c] != ' ') { any = true; break; }
      if (!any) continue;
      char head[8]; snprintf(head, sizeof(head), "%02d|", r);
      out += head;
      std::string line(grid[r], 128);
      while (!line.empty() && line.back() == ' ') line.pop_back();
      out += line; out += "\n";
    }
    return out;
  }

 private:
  void setPx(int x, int y, int v) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) { ++oob_; return; }
    fb_[y][x] = (uint8_t)v;
  }
  void plotText(int x, int y, const char *s) {
    const int w = getStrWidth(s), h = fontHeight();
    if (x < 0 || y < 0 || x + w > 128 || y + h > 64) ++oob_;
  }
  uint8_t fb_[64][128]{};
  std::vector<DrawnItem> items_;
  int font_ = FONT_6x12;
  int color_ = 1;
  uint32_t frames_ = 0;
 public:
  mutable int oob_ = 0;
};

using U8G2_ST7567_ENH_DG128064I_F_HW_I2C = U8G2Stub;
using U8G2_ST7567_JLX12864_F_HW_I2C = U8G2Stub;
