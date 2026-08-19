// 회색 4단계 화면 + 흑백 시계
//
// 바탕 그림(로고, 워드마크, 라벨)은 회색 4단계로 한 번 그린다. 글자 가장자리가
// 매끄럽다. 시계 칸만 순수한 흑백으로 그려두고, 그 칸만 흑백 부분 갱신으로
// 매초 민다. 전자잉크는 전원을 끊어도 화면을 유지하므로 나머지 회색은 그대로다.
//
// 왜 회색으로는 시계를 못 도나:
//   회색 4단계는 화면 메모리 두 장을 색을 나타내는 데 다 쓴다.
//     (0x26,0x24) = (1,1) 흰색  (1,0) 연한회색  (0,1) 진한회색  (0,0) 검정
//   흑백 부분 갱신은 그 두 장을 "이전 모습" 과 "지금 모습" 으로 쓴다.
//   즉 회색에서는 "전에 무슨 색이었나" 를 담을 자리가 없다.
//   그래서 차이만 밀어내는 부분 갱신이 원리적으로 안 된다.
//   [확인: SSD1677 데이터시트 Table 6-5, GxEPD2_4G 의 writeImage_4G 본문]
//
// 회색 전체 갱신 4.2초, 흑백 부분 갱신 0.42초. 둘 다 실측값이다.

#include <Arduino.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <GxEPD2_4G_4G.h>
#include "soc/gpio_reg.h"

#include "../kfont.h"
#include "../font_pre_m14.h"
#include "../font_pre_m18.h"
#include "../font_pre_m32.h"
#include "../font_preg_m18.h"
#include "../font_preg_m24.h"
#include "../font_preg_m32.h"
#include "../font_preg_m48.h"
#include "../font_preg_b46.h"
#include "../fetm_logo_gray.h"
#include "canvas2.h"
#include "epd_mixed.h"

#define EPD_SCK   D8
#define EPD_MOSI  D10
#define EPD_MISO  -1
#define EPD_CS    D7
#define EPD_DC    D16
#define EPD_RST   D11
#define EPD_BUSY  D3
#define EPD_PWR   D6
#define BTN1      D1
#define BTN2      D2
#define BTN3      D9

static const int16_t SCR_W = 800;
static const int16_t SCR_H = 480;

GxEPD2_4G_4G<EpdMixed, 8> display(EpdMixed(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Canvas2* cv = nullptr;                 // 회색 바탕 그림 (2비트)
GFXcanvas1* clk = nullptr;             // 시계 칸 지금 모습 (1비트)
GFXcanvas1* clkPrev = nullptr;         // 시계 칸 이전 모습 (1비트)

// 글자 단계 1,2,3 에 쓸 색. 3 이 가장 진한 쪽이다.
static const uint16_t GL_LIGHT[4] =
{ GxEPD_WHITE, GxEPD_LIGHTGREY, GxEPD_DARKGREY, GxEPD_BLACK };
static const uint16_t GL_DARK[4] =
{ GxEPD_BLACK, GxEPD_DARKGREY, GxEPD_LIGHTGREY, GxEPD_WHITE };

static bool darkMode = false;
static int  screen = 0;              // 0 = FETM, 1 = 한글 시험지
static const uint16_t* GLp() { return darkMode ? GL_DARK : GL_LIGHT; }
static uint16_t BG() { return darkMode ? GxEPD_BLACK : GxEPD_WHITE; }
static uint16_t FG() { return darkMode ? GxEPD_WHITE : GxEPD_BLACK; }

// 시계 칸. x 와 폭은 8의 배수여야 한다.
static const int16_t CLOCK_X = 272;
static const int16_t CLOCK_W = 256;
static const int16_t CLOCK_Y = 48;
static const int16_t CLOCK_H = 48;

static const int16_t M = 48;

// 항적 띠. 지도(길)는 고정이고 배만 움직인다.
// x 와 폭은 8의 배수여야 한다.
static const int16_t TRK_X = 48;
static const int16_t TRK_W = 704;
static const int16_t TRK_Y = 404;
static const int16_t TRK_H = 40;
static const int16_t TRK_STEP = 8;          // 한 걸음에 8픽셀

GFXcanvas1* trk = nullptr;
GFXcanvas1* trkPrev = nullptr;
static int16_t boatX = 0;                   // 띠 안에서의 위치

// 길은 완만한 물결 모양이다. 어느 x 에서 y 가 얼마인지.
static int16_t routeY(int16_t x)
{
  float t = (float)x / (float)TRK_W * 6.2831853f * 2.0f;
  return (int16_t)(TRK_H / 2 + sinf(t) * (TRK_H / 2 - 8));
}

// 항적 띠를 1비트로 그린다. 비트 1 = 글자/선.
static void drawTrackCanvas()
{
  trk->fillScreen(0);
  // 고정된 길. 점선으로.
  for (int16_t x = 0; x < TRK_W; x += 4)
    trk->drawPixel(x, routeY(x), 1);
  // 지나온 자취. 실선으로.
  for (int16_t x = 0; x < boatX; x++)
  {
    int16_t y = routeY(x);
    trk->drawPixel(x, y, 1);
    trk->drawPixel(x, y + 1, 1);
  }
  // 배. 작은 삼각형.
  int16_t bx = boatX, by = routeY(boatX);
  for (int16_t d = 0; d < 7; d++)
    trk->drawFastVLine(bx - 6 + d, by - d / 2 - 1, d + 2, 1);
  trk->drawFastHLine(bx - 7, by + 3, 9, 1);
}

static void nowText(char* out, size_t n)
{
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  snprintf(out, n, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

// 회색 그림판에 2비트 그림을 얹는다 (로고)
static void drawGrayBitmap(Canvas2& c, int16_t x, int16_t y,
                           const uint8_t* bmp, int16_t w, int16_t h)
{
  int16_t stride = (w + 3) / 4;
  for (int16_t row = 0; row < h; row++)
    for (int16_t col = 0; col < w; col++)
    {
      uint8_t lv = (bmp[(int32_t)row * stride + (col >> 2)] >> (6 - 2 * (col & 3))) & 3;
      if (lv) c.drawPixel(x + col, y + row, GLp()[lv]);
    }
}

// 시계 칸만 1비트로 그린다 (검정 글씨, 흰 바탕)
static void drawClockCanvas()
{
  char buf[16];
  nowText(buf, sizeof(buf));
  clk->fillScreen(0);
  int16_t cell = kfDigitCell(pre_m32);
  int16_t w = kfWidthMono(pre_m32, buf);
  kfDrawMono(*clk, (CLOCK_W - w) / 2, pre_m32.ascent + 4, buf, pre_m32, 1);
  (void)cell;
}

// 시계 칸을 그림판에 흑백으로 얹는다. 바탕색을 따라간다.
// 검은 바탕이면 검은 칸에 흰 숫자다.
static void blitClockBox(Canvas2& c)
{
  drawClockCanvas();
  const uint8_t* b = clk->getBuffer();
  int16_t wb = (CLOCK_W + 7) / 8;
  for (int16_t row = 0; row < CLOCK_H; row++)
    for (int16_t col = 0; col < CLOCK_W; col++)
    {
      bool ink = b[(int32_t)row * wb + (col >> 3)] & (0x80 >> (col & 7));
      // 시계 칸은 순수한 검정과 흰색만 쓴다. 회색이 섞이면 흑백 부분 갱신이
      // 그 자리를 "바뀐 곳" 으로 읽어서 어긋난다.
      c.drawPixel(CLOCK_X + col, CLOCK_Y + row,
                  ink ? (darkMode ? GxEPD_WHITE : GxEPD_BLACK)
                      : (darkMode ? GxEPD_BLACK : GxEPD_WHITE));
    }
  memcpy(clkPrev->getBuffer(), b, (size_t)wb * CLOCK_H);
}

// 1비트 조각을 회색 그림판에 순수 흑백으로 얹는다.
// 회색이 섞이면 흑백 부분 갱신이 그 자리를 "바뀐 곳" 으로 읽어 어긋난다.
static void blit1bit(Canvas2& c, GFXcanvas1& g, int16_t x0, int16_t y0,
                     int16_t w, int16_t h)
{
  const uint8_t* b = g.getBuffer();
  int16_t wb = (w + 7) / 8;
  for (int16_t row = 0; row < h; row++)
    for (int16_t col = 0; col < w; col++)
    {
      bool ink = b[(int32_t)row * wb + (col >> 3)] & (0x80 >> (col & 7));
      c.drawPixel(x0 + col, y0 + row,
                  ink ? (darkMode ? GxEPD_WHITE : GxEPD_BLACK)
                      : (darkMode ? GxEPD_BLACK : GxEPD_WHITE));
    }
}

static void composeIdentity(Canvas2& c)
{
  c.fillScreen(BG());
  kfDrawGray(c, M, 76, "전자 기술을 섞다", preg_m18, GLp(), 1);
  kfDrawGrayRight(c, SCR_W - M, 76, "서울 · 대한민국", preg_m18, GLp(), 1);
  c.drawFastHLine(M, 108, SCR_W - 2 * M, FG());

  drawGrayBitmap(c, 48, 158, fetm_logo_gray, FETM_LOGO_GRAY_WIDTH, FETM_LOGO_GRAY_HEIGHT);
  c.drawFastVLine(288, 158, FETM_LOGO_GRAY_HEIGHT, FG());

  static const char* words[4] = { "FUTURE", "ELECTRONICS", "TECHNOLOGY", "MIXER" };
  for (int i = 0; i < 4; i++)
    kfDrawGray(c, 328, 191 + i * 56, words[i], preg_b46, GLp(), 0);

  c.drawFastHLine(M, 392, SCR_W - 2 * M, FG());

  blitClockBox(c);

  // 항적 띠. 여기도 순수 흑백이어야 한다.
  drawTrackCanvas();
  blit1bit(c, *trk, TRK_X, TRK_Y, TRK_W, TRK_H);
  memcpy(trkPrev->getBuffer(), trk->getBuffer(),
         (size_t)((TRK_W + 7) / 8) * TRK_H);
}

static void composeHangul(Canvas2& c)
{
  c.fillScreen(BG());
  kfDrawGray(c, M, 62, "한글 렌더링 시험 / Pretendard", preg_m24, GLp(), 0);
  c.drawFastHLine(M, 82, SCR_W - 2 * M, FG());

  struct Row { const char* label; const KFontG* f; };
  static const Row rows[] = {
    { "18px", &preg_m18 }, { "24px", &preg_m24 },
    { "32px", &preg_m32 }, { "48px", &preg_m48 },
  };
  const char* sample = "다람쥐 헌 쳇바퀴에 타고파";
  int16_t y = 92;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
  {
    const KFontG& f = *rows[i].f;
    y += f.ascent + 22;
    kfDrawGray(c, M, y, rows[i].label, preg_m18, GLp(), 0);
    kfDrawGray(c, 130, y, sample, f, GLp(), 0);
    y += f.lineHeight - f.ascent;
  }

  c.drawFastHLine(M, 392, SCR_W - 2 * M, FG());
  kfDrawGray(c, M, 424, "회색 4단계 안티알리아싱", preg_m18, GLp(), 1);
  kfDrawGrayRight(c, SCR_W - M, 424, "FETM.KR", preg_m18, GLp(), 1);
}

// 화면 한 장을 처음부터 다시 그린다.
//
// hibernate() 를 부르면 라이브러리 안의 _init_4G_done 이 꺼져서, 다음
// drawImage_4G 가 회색 파형과 전압을 다시 실어준다. 이걸 안 하면 두 번째
// 그림부터 흑백 파형으로 그려져 화면이 망가진다.
static void showScreen();

// 그림판을 시리얼로 뱉는다. 맥에서 PNG 로 되살려 눈으로 확인한다.
static void dumpFrame(Canvas2& c)
{
  static const char T[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t* buf = c.buffer();
  size_t n = c.size();
  Serial.setTxTimeoutMs(100);      // 덤프는 통째로 다 나가야 한다
  Serial.printf("---FB2-BEGIN %d %d %u---\n", (int)c.width(), (int)c.height(),
                (unsigned)n);
  char line[100];
  int li = 0;
  for (size_t i = 0; i < n; i += 3)
  {
    uint32_t v = (uint32_t)buf[i] << 16;
    if (i + 1 < n) v |= (uint32_t)buf[i + 1] << 8;
    if (i + 2 < n) v |= (uint32_t)buf[i + 2];
    line[li++] = T[(v >> 18) & 63];
    line[li++] = T[(v >> 12) & 63];
    line[li++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    line[li++] = (i + 2 < n) ? T[v & 63] : '=';
    if (li >= 96) { line[li] = 0; Serial.println(line); li = 0; }
  }
  if (li) { line[li] = 0; Serial.println(line); }
  Serial.println("---FB2-END---");
  Serial.flush();
  Serial.setTxTimeoutMs(0);
}

static void pushGray()
{
  uint32_t t0 = millis();
  display.epd2.drawImage_4G(cv->buffer(), 2, 0, 0, SCR_W, SCR_H, false, false, false);
  Serial.printf("회색 전체 갱신 %lu ms\n", (unsigned long)(millis() - t0));
}

// 시계 칸과 항적 띠를 흑백으로 민다.
//
// 부분 갱신은 창을 정해도 화면 전체를 훑는다. 그러니 창을 두 번 잡아 각각
// 값을 써넣고, 갱신은 한 번만 부르면 두 자리가 같이 움직인다.
// 두 번 부르면 0.41초가 두 번 든다. 한 번이면 0.41초로 끝난다.
static void pushRegionsBW(bool moveBoat)
{
  drawClockCanvas();
  if (moveBoat)
  {
    boatX += TRK_STEP;
    if (boatX >= TRK_W) boatX = 0;
    drawTrackCanvas();
  }

  bool inv = !darkMode;
  uint32_t t0 = millis();

  display.epd2.setRamArea(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  display.epd2.writeWindow(0x26, clkPrev->getBuffer(), CLOCK_W, CLOCK_H, inv);
  display.epd2.setRamArea(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  display.epd2.writeWindow(0x24, clk->getBuffer(), CLOCK_W, CLOCK_H, inv);

  if (moveBoat)
  {
    display.epd2.setRamArea(TRK_X, TRK_Y, TRK_W, TRK_H);
    display.epd2.writeWindow(0x26, trkPrev->getBuffer(), TRK_W, TRK_H, inv);
    display.epd2.setRamArea(TRK_X, TRK_Y, TRK_W, TRK_H);
    display.epd2.writeWindow(0x24, trk->getBuffer(), TRK_W, TRK_H, inv);
  }

  display.epd2.updatePartBW();          // 한 번만. 두 자리가 같이 움직인다.
  uint32_t t1 = millis();

  memcpy(clkPrev->getBuffer(), clk->getBuffer(),
         (size_t)((CLOCK_W + 7) / 8) * CLOCK_H);
  if (moveBoat)
    memcpy(trkPrev->getBuffer(), trk->getBuffer(),
           (size_t)((TRK_W + 7) / 8) * TRK_H);

  static int n = 0;
  if (++n % 10 == 1)
    Serial.printf("부분 갱신 %lu ms (시계%s)\n", (unsigned long)(t1 - t0),
                  moveBoat ? " + 항적" : "");
}

static void showScreen()
{
  uint32_t t0 = millis();
  display.epd2.hibernate();               // 회색 초기화 상태로 되돌림
  if (screen == 0) composeIdentity(*cv);
  else             composeHangul(*cv);
  pushGray();
  display.epd2.powerOffPub();
  display.epd2.initBW(0x01);              // 부분 갱신용 준비. 두 메모리를 같게.
  Serial.printf("화면 %d (%s) 그리기 %lu ms\n", screen,
                darkMode ? "검은 바탕" : "흰 바탕",
                (unsigned long)(millis() - t0));
}

// ------------------------------------------------------------------ 버튼
//
// 회로도에서 확인한 자리: 버튼1 = D1(GPIO2), 버튼2 = D2(GPIO3), 버튼3 = D9(GPIO8).
// 뗄 때 신호가 여러 개로 쪼개져 들어오므로, 떼진 것을 확인해야 다음 누름을 받는다.
static const uint8_t BTN_PIN[3] = { BTN1, BTN2, BTN3 };
volatile bool btnArmed[3] = { true, true, true };
volatile bool btnPending[3] = { false, false, false };
volatile uint32_t btnLastLow[3] = { 0, 0, 0 };
volatile uint32_t ledHoldUntil = 0;

#define LED_ON()   REG_WRITE(GPIO_OUT_W1TC_REG, 1UL << LED_BUILTIN)
#define LED_OFF()  REG_WRITE(GPIO_OUT_W1TS_REG, 1UL << LED_BUILTIN)

static void IRAM_ATTR btnEvent(uint8_t i)
{
  uint32_t now = millis();
  if (!btnArmed[i]) return;
  btnArmed[i] = false;
  btnLastLow[i] = now;
  btnPending[i] = true;
  LED_ON();
  ledHoldUntil = now + 400;
}
static void IRAM_ATTR isr0() { btnEvent(0); }
static void IRAM_ATTR isr1() { btnEvent(1); }
static void IRAM_ATTR isr2() { btnEvent(2); }

static void rearmButtons()
{
  for (int i = 0; i < 3; i++)
    if (!btnArmed[i] && digitalRead(BTN_PIN[i]) == HIGH &&
        millis() - btnLastLow[i] > 120)
      btnArmed[i] = true;
}

void setup()
{
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }
  delay(300);
  Serial.println("\n=== 회색 바탕 + 흑백 시계 ===");
  setenv("TZ", "KST-9", 1);
  tzset();

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(20);
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 3; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[0]), isr0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[1]), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[2]), isr2, FALLING);

  cv = new Canvas2(SCR_W, SCR_H);
  clk = new GFXcanvas1(CLOCK_W, CLOCK_H);
  clkPrev = new GFXcanvas1(CLOCK_W, CLOCK_H);
  trk = new GFXcanvas1(TRK_W, TRK_H);
  trkPrev = new GFXcanvas1(TRK_W, TRK_H);
  if (!cv || !cv->ok() || !clk->getBuffer() || !clkPrev->getBuffer())
  { Serial.println("!! 자리 못 잡음"); return; }
  Serial.printf("남은 heap %u\n", (unsigned)ESP.getFreeHeap());

  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init(0, true, 2, false);
  display.epd2.selectSPI(SPI, SPISettings(20000000, MSBFIRST, SPI_MODE0));

  showScreen();
  Serial.println("준비됨. 버튼 1=FETM 2=한글 3=바탕뒤집기");
}

void loop()
{
  // 눌린 직후 400ms 는 켜둔다. 시계가 한 칸 갈 때도 짧게 반짝인다.
  if (ledHoldUntil && (int32_t)(millis() - ledHoldUntil) < 0) LED_ON();
  else { ledHoldUntil = 0; LED_OFF(); }

  for (int i = 0; i < 3; i++)
  {
    if (!btnPending[i]) continue;
    btnPending[i] = false;
    if (i == 0)      { if (screen == 0) continue; screen = 0; }
    else if (i == 1) { if (screen == 1) continue; screen = 1; }
    else             { darkMode = !darkMode; }
    showScreen();
  }

  // FETM 화면일 때만 시계가 돈다
  static int lastSec = -1;
  if (screen == 0)
  {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    if (tmv.tm_sec != lastSec)
    {
      lastSec = tmv.tm_sec;
      LED_ON(); delay(60); LED_OFF();
      pushRegionsBW(true);
    }
  }

  if (Serial.available())
  {
    int ch = Serial.read();
    if (ch == 'd' && cv) dumpFrame(*cv);
    else if (ch == 'T')
    {
      char b[24];
      size_t n = Serial.readBytesUntil('\n', b, sizeof(b) - 1);
      b[n] = 0;
      struct timeval tv = { (time_t)atol(b), 0 };
      settimeofday(&tv, nullptr);
      Serial.println("시계 맞춤");
    }
    else if (ch == 'r') showScreen();
    else if (ch == '0') { screen = 0; showScreen(); }
    else if (ch == '1') { screen = 1; showScreen(); }
    else if (ch == 'i') { darkMode = !darkMode; showScreen(); }
  }

  rearmButtons();
  delay(2);
}
