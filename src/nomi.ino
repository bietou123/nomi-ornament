// ============================================================
//  NOMI 桌面表情摆件 —— 彩屏表情脸 (v7 空心线条眼 + 整画面旋转摇摆)
//  主控: ESP32-WROOM-32U   屏幕: 1.44" 128x128 ST7735 (SPI)
//  接线: GND-GND VCC-3V3 SCL-18 SDA-23 RST-4 DC-2 CS-5 BLK-3V3
//  风格: 纯黑底 + 暖白空心线条眼，整张脸绕屏幕中心缓慢旋转摇晃
// ============================================================
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>
#include <ESP32Servo.h>
// 品牌图片（128x128 RGB565）不在仓库内提供；自行放置 liqun_img.h 后才会编入 LIQUN 表情
#if __has_include("liqun_img.h")
  #include "liqun_img.h"
  #define NOMI_HAS_LIQUN_IMG 1
#else
  #define NOMI_HAS_LIQUN_IMG 0
#endif

#define TFT_CS 5
#define TFT_DC 2
#define TFT_RST 4
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

#define YAW_PIN   13     // 扭头
#define PITCH_PIN 25     // 摆头
Servo yawS, pitchS;
float yawCur = 90, pitchCur = 90;

GFXcanvas16 cv(128, 128);    // 场景画布（直立）
GFXcanvas16 ob(128, 128);    // 旋转输出画布

uint16_t BG, EYE, RED, BLU, GOLD, GRN, BRN, LQGRN, SMK;

const int ELX = 40;     // 左眼基准 x
const int ERX = 88;     // 右眼基准 x
const int CY  = 54;     // 眼基准 y（偏上，给下方道具留空间）
const int HW  = 10;     // 眼半宽
const int HH  = 18;     // 眼半高

enum Mood { IDLE, HAPPY, SAD, SURPRISE, WINK, LOVE, SLEEP,
            DEAD, SPIRAL, DERP, CRY, SMOKE, LIQUN,
            SLEEPY, BUTTERFLY, APPLE, SPINNER,
            RADIO, COFFEE, BUBBLE, MUSIC };
Mood mood = IDLE;

float lookX = 0, lookY = 0, tgtLX = 0, tgtLY = 0;
float anim = 0;                          // 道具动画相位
float swayPh = 0, rollAng = 0;           // 旋转摇摆
float zoom = 1.0;                        // 表情切换缩放过渡（<1 由小弹大）
unsigned long tLook, tMood, tBlink;
bool blinking = false; unsigned long blinkT = 0; int blinkN = 0; bool slowBlink = false;

// ---------- 本地时钟 + 信息显示状态（本地模式，无联网）----------
// 默认时间 = 编译时刻（__TIME__ = "HH:MM:SS"），之后靠 millis() 在单片机上计时
int baseH = (__TIME__[0]-'0')*10 + (__TIME__[1]-'0');
int baseM = (__TIME__[3]-'0')*10 + (__TIME__[4]-'0');
int baseS = (__TIME__[6]-'0')*10 + (__TIME__[7]-'0');
unsigned long baseMs = 0;                 // setup 里记开机 millis
bool infoShowing = false;                 // 当前是否在叠加显示时间
unsigned long infoStart = 0, infoUntil = 0, tInfo = 0;

void nowHMS(int &h, int &m, int &s) {
  unsigned long el = (millis() - baseMs) / 1000;
  unsigned long t  = ((unsigned long)baseH * 3600 + baseM * 60 + baseS + el) % 86400UL;
  h = t / 3600; m = (t % 3600) / 60; s = t % 60;
}

// ---------- 整画面旋转：场景绕中心(64,64)缓慢左右歪头 ----------
void updateRoll() {
  swayPh += 0.020;
  float amp = 0.22, w = 1.0;             // ~12.6°
  if (mood == MUSIC) { amp = 0.40; w = 2.4; }      // 音乐摇：更大更快
  if (mood == SLEEP || mood == DEAD) amp = 0.0;    // 静止场景不摇
  rollAng = amp * sin(swayPh * w);
}
void rotateBlit() {
  uint16_t *s = cv.getBuffer();
  uint16_t *d = ob.getBuffer();
  float inv = 1.0f / (zoom < 0.05f ? 0.05f : zoom);   // 逆缩放：zoom<1 时画面由小放大
  int32_t ci = (int32_t)(cos(rollAng) * 256 * inv);
  int32_t si = (int32_t)(sin(rollAng) * 256 * inv);
  int idx = 0;
  for (int y = 0; y < 128; y++) {
    int32_t dy = y - 64;
    for (int x = 0; x < 128; x++) {
      int32_t dx = x - 64;
      int sx = 64 + ((dx * ci + dy * si) >> 8);
      int sy = 64 + ((-dx * si + dy * ci) >> 8);
      d[idx++] = (sx >= 0 && sx < 128 && sy >= 0 && sy < 128) ? s[sy * 128 + sx] : BG;
    }
  }
  tft.drawRGBBitmap(0, 0, d, 128, 128);
}

// ---------- 绘图小工具 ----------
void thickLine(int x0, int y0, int x1, int y1, uint16_t c) {
  for (int o = -1; o <= 1; o++) { cv.drawLine(x0 + o, y0, x1 + o, y1, c); cv.drawLine(x0, y0 + o, x1, y1 + o, c); }
}
void ring(int x, int y, int r, uint16_t c) { cv.drawCircle(x, y, r, c); cv.drawCircle(x, y, r - 1, c); }
void fatLine(int x0, int y0, int x1, int y1, int w, uint16_t c) {
  for (int ox = -w; ox <= w; ox++)
    for (int oy = -w; oy <= w; oy++) cv.drawLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c);
}
void rrOut(int x, int y, int w, int h, int r, uint16_t c) {     // 粗描边圆角矩形
  cv.drawRoundRect(x, y, w, h, r, c);
  cv.drawRoundRect(x + 1, y + 1, w - 2, h - 2, (r - 1 > 0 ? r - 1 : 0), c);
}
void heart(int cx, int cy, int s, uint16_t c) {
  cv.fillCircle(cx - s / 2, cy - s / 3, s / 2 + 1, c);
  cv.fillCircle(cx + s / 2, cy - s / 3, s / 2 + 1, c);
  cv.fillTriangle(cx - s, cy - s / 4, cx + s, cy - s / 4, cx, cy + s, c);
}
void note(int x, int y, uint16_t c) {
  cv.fillCircle(x, y, 3, c);
  cv.fillRect(x + 2, y - 13, 2, 13, c);
  cv.fillRect(x + 2, y - 13, 6, 3, c);
}
void butterfly(int x, int y, float f, uint16_t c) {
  int w = 5 + (int)(2 * sin(f));
  ring(x - 4, y - 2, w, c); ring(x - 4, y + 4, w - 1, c);
  ring(x + 4, y - 2, w, c); ring(x + 4, y + 4, w - 1, c);
  thickLine(x, y - 6, x, y + 6, c);
}
void apple(int x, int y, uint16_t c, float a) {
  cv.fillCircle(x - 4, y, 8, c); cv.fillCircle(x + 4, y, 8, c);
  cv.fillRect(x - 11, y - 2, 22, 8, c);
  cv.drawFastVLine(x, y - 13, 6, BRN);
  cv.fillTriangle(x + 1, y - 11, x + 8, y - 13, x + 5, y - 7, GRN);
  int bite = 2 + (int)fmod(a * 0.5, 9);          // 咬痕渐大，循环（越啃越多）
  cv.fillCircle(x + 9, y - 3, bite, BG);         // 右上角被吃掉一口
  cv.drawCircle(x + 9, y - 3, bite + 1, c);      // 咬口边缘
}
void spinner(int x, int y, float a, uint16_t c) {
  for (int i = 0; i < 3; i++) {
    float ang = a + i * 2.094f;
    int bx = x + (int)(13 * cos(ang)), by = y + (int)(13 * sin(ang));
    ring(bx, by, 5, c);
  }
  cv.fillCircle(x, y, 3, c);
}
void radio(int x, int y, float a, uint16_t c) {
  cv.drawRoundRect(x, y, 30, 18, 3, c);
  cv.drawCircle(x + 22, y + 9, 4, c);
  cv.drawFastVLine(x + 6, y, 6, c);
  thickLine(x + 25, y - 1, x + 32, y - 12, c);
  if (((int)(a) % 2) == 0) { cv.drawCircle(x + 32, y - 12, 4, c); cv.drawCircle(x + 32, y - 12, 8, c); }
}
void coffee(int x, int y, float a, uint16_t c) {
  cv.drawRoundRect(x, y, 18, 14, 2, c);
  cv.drawRoundRect(x + 18, y + 2, 6, 7, 2, c);
  for (int i = 0; i < 2; i++) {
    int hx = x + 5 + i * 8; int dy = (int)(3 * sin(a * 2 + i));
    thickLine(hx, y - 3 + dy, hx + 2, y - 9 + dy, c);
    thickLine(hx + 2, y - 9 + dy, hx, y - 14 + dy, c);
  }
}
void bubbles(float a) {
  for (int i = 0; i < 4; i++) {
    float ph = a * 0.6f + i * 1.7f;
    int bx = 96 - i * 4 + (int)(6 * sin(ph));
    int by = 108 - (int)(fmod(ph * 9, 90));
    if (by > 8) cv.drawCircle(bx, by, 3 + i, BLU);
  }
}
void musicNotes(float a) {
  uint16_t cols[3] = { RED, GRN, BLU };
  for (int i = 0; i < 3; i++) {
    float ph = a * 0.7f + i * 2.0f;
    int nx = 24 + i * 36 + (int)(5 * sin(ph));
    int ny = 104 - (int)(fmod(ph * 7, 70));
    if (ny > 6) note(nx, ny, cols[i]);
  }
}
void zzz(int x, int y, float a) {
  cv.setTextColor(EYE);
  int n = ((int)(a * 0.5)) % 3;
  cv.setTextSize(1); cv.setCursor(x, y);            cv.print("z");
  if (n >= 1) { cv.setTextSize(2); cv.setCursor(x + 6, y - 8); cv.print("z"); }
  if (n >= 2) { cv.setTextSize(2); cv.setCursor(x + 16, y - 18); cv.print("Z"); }
}

// ---------- 抽利群烟：嘴叼香烟 + 红明灭烟头 + 上升烟雾 ----------
void cigarette(int mx, int my, float a) {
  int ex = mx + 30, ey = my + 7;                 // 烟头端（向右下伸出）
  fatLine(mx, my, ex, ey, 1, EYE);               // 白色烟杆
  fatLine(mx, my, mx + 8, my + 2, 1, GOLD);      // 靠嘴端滤嘴（橙黄）
  float glow = 0.5f + 0.5f * sin(a * 4.0f);      // 烟头明灭
  uint16_t tip = tft.color565(255, (int)(50 + 90 * glow), 30);
  cv.fillCircle(ex, ey, 3, tip);
  cv.fillCircle(ex, ey, 1, tft.color565(255, 220, 120));
}
void smoke(int x0, int y0, float a) {
  for (int i = 0; i < 3; i++) {
    float life = fmod(a * 0.5f + i * 2.0f, 6.0f);   // 0..6 上升生命周期
    int sy = y0 - (int)(life * 14);
    if (sy < 4) continue;
    int sx = x0 + (int)(8 * sin(life * 1.3f + i));
    int r = 2 + (int)(life / 2);
    int g = 175 - (int)(life * 23); if (g < 45) g = 45;   // 越高越淡
    cv.drawCircle(sx, sy, r, tft.color565(g, g, g + 8));
  }
  // 偶尔吐一大团烟
  float puff = fmod(a, 13.0f);
  if (puff < 2.0f) {
    int py = y0 - 18 - (int)(puff * 10);
    if (py > 4) cv.drawCircle(x0 + 4, py, 5 + (int)(puff * 2), SMK);
  }
}

// ---------- 开机动画：利群烟盒升起 + 抽出一根烟 ----------
void packBody(int px, int py, int pw, int ph) {
  cv.fillRoundRect(px, py, pw, ph, 4, LQGRN);
  cv.drawRoundRect(px, py, pw, ph, 4, EYE);
  cv.drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 3, EYE);
  cv.fillRect(px + 7, py + 22, 11, 4, EYE);        // 简化"利群"字色块
  cv.fillRect(px + 24, py + 22, 11, 4, EYE);
  cv.fillRect(px + 7, py + 32, 28, 2, GOLD);       // 金线条
}
void bootBlit() { tft.drawRGBBitmap(0, 0, cv.getBuffer(), 128, 128); }
void bootAnim() {
  int px = 42, py = 50, pw = 44, ph = 60;
  // 1) 烟盒从下方升入
  for (int f = 0; f <= 16; f++) {
    cv.fillScreen(BG);
    packBody(px, py + (16 - f) * 3, pw, ph);
    bootBlit(); delay(35);
  }
  // 2) 顶盖掀开
  for (int f = 0; f <= 12; f++) {
    cv.fillScreen(BG);
    packBody(px, py, pw, ph);
    int lift = f * 2;
    thickLine(px, py, px + f, py - lift, EYE);
    thickLine(px + pw, py, px + pw - f, py - lift, EYE);
    thickLine(px + f, py - lift, px + pw - f, py - lift, EYE);
    bootBlit(); delay(35);
  }
  // 3) 香烟缓缓升出
  int cigX = px + pw / 2;
  for (int f = 0; f <= 26; f++) {
    cv.fillScreen(BG);
    packBody(px, py, pw, ph);
    int len = 4 + f * 2;
    int top = py - len;
    cv.fillRect(cigX - 3, top, 6, len + 6, EYE);     // 白杆
    cv.fillRect(cigX - 3, py - 4, 6, 9, GOLD);       // 滤嘴留盒口
    if (f > 18) {                                    // 升到位后点亮烟头
      float glow = 0.5f + 0.5f * sin(f * 0.7f);
      cv.fillCircle(cigX, top, 3, tft.color565(255, (int)(50 + 90 * glow), 30));
      smoke(cigX, top - 4, f * 0.5f);
    }
    bootBlit(); delay(45);
  }
  // 4) 简单退场
  for (int f = 0; f <= 8; f++) {
    cv.fillScreen(BG);
    packBody(px, py + f * 8, pw, ph);
    bootBlit(); delay(25);
  }
  cv.fillScreen(BG); bootBlit();
}

// 竖胶囊眼（空心线条），hh 为当前半高(眨眼用)
void capsule(int cx, int cy, int hh) {
  if (hh < 3) hh = 3;
  rrOut(cx - HW, cy - hh, HW * 2, hh * 2, HW, EYE);
}

// side: 0=左眼 1=右眼
void eye(int side, float lid) {
  int bx = side ? ERX : ELX;
  int cx = bx, cy = CY;
  bool movable = (mood == IDLE || mood == SURPRISE || mood == CRY ||
                  mood == BUTTERFLY || mood == APPLE ||
                  mood == SPINNER || mood == RADIO || mood == COFFEE ||
                  mood == BUBBLE);
  if (movable) {
    // 微动漂移：两组不可公约正弦叠加，眼神更自然
    float dx = 0.18f * sin(anim * 0.13f) + 0.10f * sin(anim * 0.37f + 1.3f);
    float dy = 0.12f * sin(anim * 0.17f + 0.5f);
    cx += (int)((lookX + dx) * 8); cy += (int)((lookY + dy) * 6);
  }
  if (mood == DERP) cx += side ? -9 : 9;

  int hh = (int)(HH * (1.0 - lid));

  switch (mood) {
    case HAPPY: case MUSIC: {        // 笑眼 ⌣（浅弧，不是半圆）
      ring(cx, cy + 11, 17, EYE);
      cv.fillRect(cx - 19, cy + 2, 38, 26, BG);
      return;
    }
    case SAD: {                                   // 难过 ⌒（浅弧向下）
      ring(cx, cy - 9, 16, EYE);
      cv.fillRect(cx - 18, cy - 26, 36, 24, BG);
      return;
    }
    case SURPRISE:                                // 瞪圆（空心圆环）
      ring(cx, cy, 14, EYE);
      return;
    case WINK: {                                  // 俏皮挤眼：左眼常睁，右眼周期一挤
      if (side == 0) { capsule(cx, cy, hh); return; }
      float ph = fmod(anim, 14.0f);               // 约 1.9s 一个挤眼周期
      if (ph < 3.0f) { ring(cx, cy + 11, 16, EYE); cv.fillRect(cx - 18, cy + 2, 36, 24, BG); } // 挤成笑弧
      else capsule(cx, cy, hh);                   // 其余时间睁着
      return;
    }
    case LOVE:                                    // 爱心眼（实心点缀）
      heart(cx, cy, 12, RED);
      return;
    case SLEEP:                                   // 睡觉 —  + z z
      cv.drawFastHLine(cx - HW, cy, HW * 2, EYE);
      cv.drawFastHLine(cx - HW, cy + 1, HW * 2, EYE);
      if (side == 1) zzz(cx + 12, cy - 4, anim);
      return;
    case SLEEPY: {                                // 没睡醒：半睁空心矮胶囊
      int h = 7 + (int)(2 * sin(anim * 0.6));
      rrOut(cx - HW, cy - h, HW * 2, h + HH - 2, HW, EYE);
      cv.fillRect(cx - HW - 1, cy - HH - 1, HW * 2 + 2, HH - h, BG);
      return;
    }
    case DEAD: {                                  // 叉叉死鱼眼 ✕（加粗+脉动抖动）
      int s = 12 + (int)(2 * sin(anim * 3));
      int j = (int)(2 * sin(anim * 7 + side));
      fatLine(cx - s, cy - s + j, cx + s, cy + s + j, 2, EYE);
      fatLine(cx - s, cy + s + j, cx + s, cy - s + j, 2, EYE);
      return;
    }
    case SPIRAL:                                  // 蚊香晕眼 @
      cv.drawCircle(cx, cy, 13, EYE); cv.drawCircle(cx, cy, 9, EYE);
      cv.drawCircle(cx, cy, 5, EYE); cv.fillCircle(cx, cy, 2, EYE);
      return;
    case CRY: {                                   // 流泪：泪珠持续往下淌（两眼错相）
      capsule(cx, cy, hh);
      float t = fmod(anim * 0.5f + side * 1.3f, 4.0f);    // 0..4 一滴的生命
      int ty = cy + HH + 2 + (int)(t * 13);
      int tr = 3 - (int)t; if (tr < 1) tr = 1;
      cv.fillCircle(cx, ty, tr, BLU);
      cv.fillTriangle(cx - tr, ty - 1, cx + tr, ty - 1, cx, ty - tr - 4, BLU);
      return;
    }
    case SMOKE:                                   // 抽烟：眯眼享受
      rrOut(cx - HW, cy - 5, HW * 2, 10, 4, EYE);
      cv.fillRect(cx - HW - 1, cy - HH - 1, HW * 2 + 2, HH - 4, BG);
      return;
    default:                                      // IDLE 及各活动场景默认眼
      capsule(cx, cy, hh);
      return;
  }
}

// 道具/场景层（在眼睛之后绘制）
void drawProps() {
  switch (mood) {
    case HAPPY: {                                  // 腮红点
      cv.fillCircle(ELX - 4, CY + 24, 3, RED);
      cv.fillCircle(ERX + 4, CY + 24, 3, RED);
      return;
    }
    case LOVE: {                                   // 头顶飘心
      int hx = 64 + (int)(22 * sin(anim * 0.6));
      int hy = 26 + (int)(6 * sin(anim * 1.3));
      heart(hx, hy, 6, RED);
      return;
    }
    case SURPRISE:                                 // 张嘴 o
      ring(64, 92, 5, EYE);
      return;
    case SMOKE: {                                  // 嘴叼香烟 + 上升烟雾
      int mx = 54, my = 96;
      cigarette(mx, my, anim);
      smoke(mx + 30, my + 3, anim);
      return;
    }
    case BUTTERFLY: {
      int bx = 64 + (int)(30 * sin(anim * 0.7)), by = 26 + (int)(8 * sin(anim * 1.5));
      butterfly(bx, by, anim * 3, EYE); return;
    }
    case APPLE:   apple(64, 100, RED, anim); return;
    case SPINNER: spinner(64, 98, anim * 4, EYE); return;
    case RADIO:   radio(50, 96, anim, EYE); return;
    case COFFEE:  coffee(54, 96, anim, EYE); return;
    case BUBBLE:  bubbles(anim); return;
    case MUSIC:   musicNotes(anim); return;
    case SLEEPY:  zzz(88, 36, anim); return;
    default: return;
  }
}

void render() {
  updateRoll();
  anim += 0.12;
  zoom += (1.0f - zoom) * 0.16f;            // 切换缩放回弹到 1.0
  float lid = 0;
  if (blinking) {
    unsigned long e = millis() - blinkT;
    int dur = slowBlink ? 130 : 70;          // 慢眨更悠
    if (e < dur)         lid = (float)e / dur;
    else if (e < dur * 2) lid = 1.0 - (float)(e - dur) / dur;
    else { lid = 0; if (--blinkN > 0) blinkT = millis(); else blinking = false; }
  }
  cv.fillScreen(BG);
  if (mood == LIQUN) {                     // 品牌图整屏显示（随脸一起旋转）
#if NOMI_HAS_LIQUN_IMG
    cv.drawRGBBitmap(0, 0, (uint16_t *)LIQUN_IMG, 128, 128);
#else
    cv.fillScreen(0x0420);                 // 未提供图片时的占位画面
    cv.setTextColor(0xFFFF);
    cv.setTextSize(2);
    cv.setCursor(20, 48);
    cv.print("SMOKE");
    cv.setTextSize(1);
    cv.setCursor(34, 76);
    cv.print("no image");
#endif
    if (infoShowing) drawClock();
    rotateBlit();
    return;
  }
  eye(0, lid);
  eye(1, lid);
  drawProps();
  if (infoShowing) drawClock();          // 时间叠在表情之上，不遮挡眼睛
  rotateBlit();
}

void startBlink(int n) { blinking = true; blinkT = millis(); blinkN = n; slowBlink = (random(0, 6) == 0); }

// 云台跟随表情自然动（头跟眼睛看的方向转 + 轻微摇摆）
void updateServos() {
  float t = swayPh;
  float yawTgt = 90, pitchTgt = 90;
  switch (mood) {
    case SLEEP: yawTgt = 90;            pitchTgt = 68;                       break; // 低头睡
    case DEAD:  yawTgt = 76;            pitchTgt = 76;                       break; // 歪头瘫
    case MUSIC: yawTgt = 90 + 20 * sin(t * 2.4); pitchTgt = 90 - 10 * fabs(sin(t * 3)); break; // 大幅摇+点头
    default:
      yawTgt   = 90 + lookX * 22 + 5 * sin(t);
      pitchTgt = 90 - lookY * 14 + 3 * sin(t * 0.7 + 1);
      if (mood == HAPPY || mood == LOVE || mood == SURPRISE || mood == WINK) pitchTgt += 4 * sin(t * 2);
      break;
  }
  if (yawTgt < 0) yawTgt = 0; if (yawTgt > 180) yawTgt = 180;
  if (pitchTgt < 0) pitchTgt = 0; if (pitchTgt > 180) pitchTgt = 180;
  yawCur   += (yawTgt - yawCur) * 0.12;
  pitchCur += (pitchTgt - pitchCur) * 0.12;
  yawS.write((int)yawCur);
  pitchS.write((int)pitchCur);
}

void pickMood(unsigned long now) {
  int a = random(0, 100);
  if      (a < 20) mood = IDLE;
  else if (a < 25) mood = HAPPY;
  else if (a < 30) mood = SAD;
  else if (a < 34) mood = SURPRISE;
  else if (a < 38) mood = WINK;
  else if (a < 43) mood = LOVE;
  else if (a < 47) mood = CRY;
  else if (a < 51) mood = DEAD;
  else if (a < 55) mood = SPIRAL;
  else if (a < 59) mood = DERP;
  else if (a < 64) mood = SMOKE;
  else if (a < 69) mood = LIQUN;
  else if (a < 73) mood = SLEEP;
  else {
    Mood acts[8] = { SLEEPY, BUTTERFLY, APPLE, SPINNER,
                     RADIO, COFFEE, BUBBLE, MUSIC };
    mood = acts[random(0, 8)];
  }
  zoom = 0.7f;                                   // 触发由小弹大的切换过渡
  bool isAct = (mood >= SLEEPY);
  tMood = now + (mood == IDLE  ? random(2000, 3500)
                : mood == SMOKE ? random(3000, 4500)
                : mood == LIQUN ? random(2500, 4000)
                : isAct        ? random(2800, 4500)
                               : random(1100, 2000));
}

// ============================================================
//  本地时钟叠加显示（7 段数码管风格，叠在表情之上，随脸一起旋转）
// ============================================================
const uint8_t SEG7[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};

void seg7(int x, int y, int n, uint16_t col) {
  uint8_t mk = SEG7[n]; int w = 14, h = 26, t = 3;
  if (mk & 1)  cv.fillRect(x + t,     y,            w - 2 * t, t,         col); // a 上
  if (mk & 2)  cv.fillRect(x + w - t, y + t,        t,         h / 2 - t, col); // b 右上
  if (mk & 4)  cv.fillRect(x + w - t, y + h / 2,    t,         h / 2 - t, col); // c 右下
  if (mk & 8)  cv.fillRect(x + t,     y + h - t,    w - 2 * t, t,         col); // d 下
  if (mk & 16) cv.fillRect(x,         y + h / 2,    t,         h / 2 - t, col); // e 左下
  if (mk & 32) cv.fillRect(x,         y + t,        t,         h / 2 - t, col); // f 左上
  if (mk & 64) cv.fillRect(x + t,     y + h / 2 - t / 2, w - 2 * t, t,    col); // g 中
}

void drawClock() {
  int h, m, s; nowHMS(h, m, s);
  unsigned long el = millis() - infoStart, dur = 5000;
  float yoff;                                       // 滑入/滑出动画
  if      (el < 420)        { float p = el / 420.0f;              yoff = 36 * (1 - p) * (1 - p); }
  else if (el > dur - 420)  { float p = (el - (dur - 420)) / 420.0f; yoff = 36 * p * p; }
  else                      { yoff = 0; }
  int by = 92 + (int)yoff;
  int x  = 26;                                      // 四位 + 冒号整体居中(中心64)
  uint16_t col = EYE;
  seg7(x,      by, h / 10, col);
  seg7(x + 18, by, h % 10, col);
  if (s & 1) { cv.fillCircle(x + 38, by + 8, 2, col); cv.fillCircle(x + 38, by + 18, 2, col); } // 冒号随秒闪
  seg7(x + 44, by, m / 10, col);
  seg7(x + 62, by, m % 10, col);
}

void setup() {
  tft.setSPISpeed(40000000);
  // 先用无偏移的 BLACKTAB(128x160) 把整块物理 GRAM(含 144 偏移之外的底部行)刷黑，
  // 再切回正确的 144GREENTAB —— 否则底部偏移区残留开机噪声(彩条)永远清不掉。
  tft.initR(INITR_BLACKTAB);
  for (int r = 0; r < 4; r++) { tft.setRotation(r); tft.fillScreen(ST77XX_BLACK); }
  tft.initR(INITR_144GREENTAB);
  tft.setRotation(2);
  BG   = ST77XX_BLACK;
  EYE  = tft.color565(255, 244, 224);
  RED  = tft.color565(255, 80, 110);
  BLU  = tft.color565(120, 200, 255);
  GOLD = tft.color565(255, 200, 40);
  GRN  = tft.color565(120, 230, 140);
  BRN  = tft.color565(200, 140, 90);
  LQGRN = tft.color565(0, 140, 100);     // 利群烟盒墨绿
  SMK   = tft.color565(170, 170, 178);   // 烟雾灰
  tft.fillScreen(BG);
  randomSeed(esp_random());
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  yawS.setPeriodHertz(50);   yawS.attach(YAW_PIN, 500, 2400);
  pitchS.setPeriodHertz(50); pitchS.attach(PITCH_PIN, 500, 2400);
  yawS.write(90); pitchS.write(90);
  bootAnim();                                    // 开机动画：利群烟盒抽出一根烟
  Serial.begin(115200);
  baseMs = millis();                             // 本地计时起点
  unsigned long m = millis();
  tLook = m + 1000; tMood = m + 2500; tBlink = m + 1500;
  tInfo = m + 30000;                             // 首次时间显示 30s 后
  render();
}

void loop() {
  unsigned long now = millis();

  if (now > tLook) {
    int a = random(0, 100);
    if      (a < 38) { tgtLX = 0;  tgtLY = 0;  }
    else if (a < 54) { tgtLX = -1; tgtLY = 0;  }
    else if (a < 70) { tgtLX = 1;  tgtLY = 0;  }
    else if (a < 82) { tgtLX = 0;  tgtLY = -1; }
    else if (a < 90) { tgtLX = 0;  tgtLY = 1;  }
    else if (a < 95) { tgtLX = -1; tgtLY = 1;  }
    else             { tgtLX = 1;  tgtLY = -1; }
    tLook = now + random(700, 1700);
  }

  if (now > tMood) pickMood(now);

  // 时间显示调度：每 30s 弹一次，每次叠加显示 5s
  if (!infoShowing && now > tInfo) {
    infoShowing = true; infoStart = now; infoUntil = now + 5000;
    tInfo = now + 30000;
  }
  if (infoShowing && now > infoUntil) { infoShowing = false; tMood = now; }

  if (!blinking && now > tBlink) { startBlink(random(0, 5) == 0 ? 2 : 1); tBlink = now + random(2200, 5000); }

  lookX += (tgtLX - lookX) * 0.35;
  lookY += (tgtLY - lookY) * 0.35;

  render();
  updateServos();
  delay(16);
}
