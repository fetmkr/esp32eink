// 회색 4단계 배경 + 흑백 부분 갱신
//
// 배경(로고, 워드마크, 라벨)은 회색 4단계로 한 번 그린다. 글자 가장자리가
// 매끄럽다. 시계와 배처럼 움직이는 자리만 흑백 부분 갱신으로 자주 민다.
//
// ============================================================================
//  이 화면을 만들면서 세 번 데었다. 다음에 비슷한 걸 만들 때 반드시 볼 것.
// ============================================================================
//
// [1] 부분 갱신은 창을 정해도 화면 전체를 훑는다
//
//     0x44, 0x45 로 정하는 창은 "데이터를 쓰는 자리" 일 뿐이다.
//     실제로 움직이는 픽셀은 두 화면 메모리(0x26, 0x24) 값이 다른 곳뿐이다.
//
//     근거(실측): 창을 480줄에서 60줄로 줄여도 갱신 시간이 407ms 로 같았다.
//                 창만 훑는다면 시간이 줄어야 한다.
//
//     좋은 점: 자리를 여러 개 써놓고 갱신을 한 번만 부르면 여러 곳이 같이
//              움직인다. 시계만 411ms, 시계 + 항적 442ms. 거의 안 는다.
//     조심할 점: 안 건드린 자리의 두 메모리가 서로 다르면 거기도 움직인다.
//
// [2] 회색과 흑백은 같은 메모리를 다르게 읽는다
//
//        (0x26, 0x24)   회색에서       흑백 부분 갱신에서
//          (1, 1)       흰색           안 움직임
//          (1, 0)       연한 회색      흰색 -> 검정 으로 움직임
//          (0, 1)       진한 회색      검정 -> 흰색 으로 움직임
//          (0, 0)       검정           안 움직임
//
//     회색 그림에서 중간 두 단계를 쓴 자리(글자 가장자리)가 흑백 기준으로는
//     전부 "바뀐 곳" 이다. 그래서 회색으로 그린 뒤 시계 칸만 밀려 했더니
//     화면 전체가 다시 칠해졌다.
//
//     그래서 흑백으로 넘어가기 전에 두 메모리를 같은 값으로 채운다.
//     (epd_mixed.h 의 fillBothPlanes) 눈에 보이는 회색 그림은 전자잉크라
//     그대로 남는다.
//
// [3] 갱신이 끝나면 두 메모리에 "지금 모습" 을 다시 쓴다. 반드시 두 곳 다.
//
//     갱신하는 동안 컨트롤러가 0x24 를 건드린다. 그래서 갱신이 끝나면 두
//     메모리가 서로 달라져 있다. 그대로 두면 다음에 다른 자리만 밀 때
//     화면 전체를 훑으면서 이 자리도 "아직 안 바뀐 곳" 으로 보고 또 민다.
//     이미 옮겨진 알갱이를 계속 문지르니 글자가 겹쳐 보이고 그 칸이 옅어진다.
//
//     GxEPD2 의 writeImageAgain 이 하는 일이 이것이다. 세 번 빠뜨렸다.
//     그래서 아래 stageRegion / commitRegion 으로 묶어 한쪽만 쓸 수 없게 했다.
//
// ============================================================================
//
// 순서
//   1. 배경을 회색으로 한 번 그린다 (4.4초). 움직일 자리는 순수 흑백으로.
//   2. hibernate() -> initBW(). 흑백 설정으로 되돌리고 두 메모리를 같게 채운다.
//   3. 움직일 때마다 stageRegion -> updatePartBW -> commitRegion (0.44초).
//   4. 잔상이 쌓이면 1번부터 다시.
//
// 값 (실측)
//   회색 전체 4.4초 / 흑백 부분 0.44초 / 회색 화면 한 장 96000바이트
//
#include <Arduino.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <GxEPD2_4G_4G.h>
#include "soc/gpio_reg.h"

#include "../kfont.h"
#include "../font_pre_m14.h"
#include "../font_pre_m18.h"
#include "../font_pre_m24.h"
#include "../font_pre_m32.h"
#include "../font_preg_m18.h"
#include "../font_preg_m24.h"
#include "../font_preg_m32.h"
#include "../font_preg_m48.h"
#include "../font_preg_b46.h"
#include "../fetm_logo_gray.h"
#include "../fetm_logo_gray160.h"
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
// 시계 칸은 맨 아래 가운데. x 와 폭은 8의 배수여야 한다.
static const int16_t CLOCK_X = 272;
static const int16_t CLOCK_W = 256;
static const int16_t CLOCK_Y = 372;
static const int16_t CLOCK_H = 44;

static const int16_t M = 48;

// 항적 띠. 지도(길)는 고정이고 배만 움직인다.
// x 와 폭은 8의 배수여야 한다.
static const int16_t TRK_X = 48;
static const int16_t TRK_W = 704;
static const int16_t TRK_Y = 36;
static const int16_t TRK_H = 96;
static const int16_t TRK_STEP = 8;          // 한 걸음에 8픽셀
static const uint32_t TRK_MS = 450;         // 한 걸음 간격. 갱신이 420ms 라 그보다 조금 길게

GFXcanvas1* trk = nullptr;
GFXcanvas1* trkPrev = nullptr;
static int16_t boatX = 0;                   // 띠 안에서의 위치

// 잔상 털기. 부분 갱신을 계속하면 옛 그림이 겹쳐 보인다.
// 이만큼 밀었으면 배경부터 통째로 다시 그린다 (4.7초).
// 시리얼로 G<숫자> 를 보내면 바꿀 수 있다.
static int  ghostEvery = 300;
static int  partCount = 0;
static bool boatOn = true;      // 시리얼 v 로 멈췄다 켰다

// 배가 가는 길. 완만한 물결 모양이다. 미리 보여주지 않고 지나간 자리만 남긴다.
static int16_t routeY(int16_t x)
{
  float t = (float)x / (float)TRK_W * 6.2831853f * 1.5f;
  return (int16_t)(60 + sinf(t) * 24);
}

// 돛단배. 수면 위치 y 에 놓는다. 대략 가로 42, 세로 42.
static void drawBoat(GFXcanvas1& g, int16_t x, int16_t y)
{
  for (int16_t d = 0; d < 9; d++)                 // 선체
    g.drawFastHLine(x - 20 + d * 2, y + d, 40 - d * 4, 1);

  const int16_t mx = x + 11;                      // 돛대는 앞쪽(가는 방향)으로
  g.fillRect(mx - 1, y - 34, 3, 34, 1);

  for (int16_t d = 0; d < 30; d++)                // 돛은 뒤로 퍼진다
  {
    int16_t wdt = 3 + d * 25 / 30;
    g.drawFastHLine(mx - 1 - wdt, y - 32 + d, wdt, 1);
  }
}

// 항적 띠를 1비트로 그린다. 비트 1 = 선.
static void drawTrackCanvas()
{
  trk->fillScreen(0);
  // 지나온 자취만 그린다. 앞길은 안 보여준다.
  for (int16_t x = 0; x < boatX; x++)
  {
    int16_t y = routeY(x);
    trk->drawFastVLine(x, y, 3, 1);          // 자취는 3픽셀 굵기
  }
  drawBoat(*trk, boatX, routeY(boatX));
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
  kfDrawMono(*clk, (CLOCK_W - w) / 2, pre_m32.ascent + 5, buf, pre_m32, 1);  // 밑선 = 372+31+5 = 408
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

// 자리 잡기
//   테두리   24..776 x 24..456  (752 x 432)
//   로고 칸  160..320  세로 가운데. 위 136, 아래 136 으로 같다.
//   가로줄   146 과 334. 로고 칸에서 위아래로 똑같이 14 떨어져 있다.
//   항적 띠  36..132   위 칸 안에서 가운데
//   시계 칸  372..416  아래 칸 안에서 가운데
static void composeIdentity(Canvas2& c)
{
  c.fillScreen(BG());

  // 테두리
  c.drawRect(24, 24, 752, 432, FG());

  // 위 칸: 항적 띠. 순수 흑백이어야 한다.
  drawTrackCanvas();
  blit1bit(c, *trk, TRK_X, TRK_Y, TRK_W, TRK_H);
  memcpy(trkPrev->getBuffer(), trk->getBuffer(),
         (size_t)((TRK_W + 7) / 8) * TRK_H);

  c.drawFastHLine(M, 146, SCR_W - 2 * M, FG());

  // 가운데 칸: 마크와 워드마크. 둘 다 160 에서 320 사이에 든다.
  drawGrayBitmap(c, 48, 160, fetm_logo_gray160,
                 FETM_LOGO_GRAY160_WIDTH, FETM_LOGO_GRAY160_HEIGHT);
  c.drawFastVLine(248, 160, FETM_LOGO_GRAY160_HEIGHT, FG());

  static const char* words[4] = { "FUTURE", "ELECTRONICS", "TECHNOLOGY", "MIXER" };
  for (int i = 0; i < 4; i++)
    kfDrawGray(c, 288, 193 + i * 42, words[i], preg_b46, GLp(), 0);

  c.drawFastHLine(M, 334, SCR_W - 2 * M, FG());

  // 아래 칸: 글씨와 시계. 시계 글자 밑선과 양옆 글씨 밑선을 맞춘다.
  kfDrawGray(c, M, 408, "전자 기술을 섞다", preg_m18, GLp(), 1);
  kfDrawGrayRight(c, SCR_W - M, 408, "FETM.KR", preg_m18, GLp(), 1);
  blitClockBox(c);
}

static void composeHangul(Canvas2& c)
{
  c.fillScreen(BG());
  c.drawRect(24, 24, 752, 432, FG());
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
static void blit1bit(Canvas2& c, GFXcanvas1& g, int16_t x0, int16_t y0,
                     int16_t w, int16_t h);

// 한 자리를 밀 준비. 이전 모습과 지금 모습을 각각 넣는다.
// 두 곳을 늘 짝으로 다루려고 함수로 묶었다. 따로 쓰면 한쪽을 빠뜨리게 된다.
static void stageRegion(int16_t x, int16_t y, int16_t w, int16_t h,
                        const uint8_t* prev, const uint8_t* cur, bool inv)
{
  display.epd2.setRamArea(x, y, w, h);
  display.epd2.writeWindow(0x26, prev, w, h, inv);   // 이전 모습
  display.epd2.setRamArea(x, y, w, h);
  display.epd2.writeWindow(0x24, cur,  w, h, inv);   // 지금 모습
}

// 밀고 난 뒤 정리. 두 곳 다 지금 모습으로 맞춘다.
//
// 반드시 두 곳 다여야 한다. 갱신하는 동안 컨트롤러가 버퍼를 건드리기 때문이다.
// GxEPD2 의 writeImageAgain 도 두 곳 다 쓴다. 한쪽만 쓰면 다음 갱신에서 같은
// 변화가 또 걸려 글자가 겹쳐 보인다. 이걸 두 번 틀렸다.
static void commitRegion(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint8_t* cur, bool inv)
{
  display.epd2.setRamArea(x, y, w, h);
  display.epd2.writeWindow(0x24, cur, w, h, inv);
  display.epd2.setRamArea(x, y, w, h);
  display.epd2.writeWindow(0x26, cur, w, h, inv);
}

static void pushRegionsBW(bool moveClock, bool moveBoat)
{
  if (moveClock) drawClockCanvas();
  if (moveBoat)
  {
    boatX += TRK_STEP;
    if (boatX >= TRK_W) boatX = 0;
    drawTrackCanvas();
  }

  const bool inv = !darkMode;
  const size_t clkBytes = (size_t)((CLOCK_W + 7) / 8) * CLOCK_H;
  const size_t trkBytes = (size_t)((TRK_W + 7) / 8) * TRK_H;

  uint32_t t0 = millis();
  if (moveClock)
    stageRegion(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H,
                clkPrev->getBuffer(), clk->getBuffer(), inv);
  if (moveBoat)
    stageRegion(TRK_X, TRK_Y, TRK_W, TRK_H,
                trkPrev->getBuffer(), trk->getBuffer(), inv);

  display.epd2.updatePartBW();          // 자리가 몇 개든 갱신은 한 번만
  uint32_t t1 = millis();

  if (moveClock)
  {
    commitRegion(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, clk->getBuffer(), inv);
    memcpy(clkPrev->getBuffer(), clk->getBuffer(), clkBytes);
  }
  if (moveBoat)
  {
    commitRegion(TRK_X, TRK_Y, TRK_W, TRK_H, trk->getBuffer(), inv);
    memcpy(trkPrev->getBuffer(), trk->getBuffer(), trkBytes);
    // 회색 그림판에도 반영. 화면을 다시 그리거나 시리얼로 받아볼 때 맞게 나온다.
    blit1bit(*cv, *trk, TRK_X, TRK_Y, TRK_W, TRK_H);
  }

  partCount++;
  static int n = 0;
  if (++n % 10 == 1)
    Serial.printf("부분 갱신 %lu ms (%s%s) 배x=%d\n", (unsigned long)(t1 - t0),
                  moveClock ? "시계" : "", moveBoat ? " 항적" : "", boatX);
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
  partCount = 0;
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

  // FETM 화면일 때만 시계와 배가 움직인다.
  // 배는 시계와 따로 걸음을 옮긴다. 갱신이 0.42초라 0.45초에 한 걸음까지 된다.
  // 둘이 겹치는 순간에는 한 번만 갱신해서 0.42초로 끝낸다.
  static int lastSec = -1;
  static uint32_t lastBoat = 0;
  if (screen == 0)
  {
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    bool clockDue = (tmv.tm_sec != lastSec);
    bool boatDue  = boatOn && (millis() - lastBoat >= TRK_MS);
    if (clockDue || boatDue)
    {
      if (clockDue) lastSec = tmv.tm_sec;
      if (boatDue)  lastBoat = millis();
      LED_ON(); delay(40); LED_OFF();
      pushRegionsBW(clockDue, boatDue);
      if (partCount >= ghostEvery)
      {
        partCount = 0;
        Serial.println("잔상 털기: 배경부터 다시 그린다");
        showScreen();
      }
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
    else if (ch == 'v')
    {
      boatOn = !boatOn;
      Serial.printf("배 -> %s\n", boatOn ? "감" : "멈춤");
    }
    else if (ch == 'G')
    {
      char b[12];
      size_t n = Serial.readBytesUntil('\n', b, sizeof(b) - 1);
      b[n] = 0;
      int v = atoi(b);
      if (v >= 10) ghostEvery = v;
      partCount = 0;
      Serial.printf("잔상 털기 -> 부분 갱신 %d번마다\n", ghostEvery);
    }
    else if (ch == '0') { screen = 0; showScreen(); }
    else if (ch == '1') { screen = 1; showScreen(); }
    else if (ch == 'i') { darkMode = !darkMode; showScreen(); }
  }

  rearmButtons();
  delay(2);
}
