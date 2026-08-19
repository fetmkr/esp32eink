// XIAO ESP32-S3 Plus + Seeed ePaper Display Board EE05
//   + Good Display GDEY0426T82 / GDEQ0426T82 (4.26", 800x480, B/W, SSD1677)
//
// 버튼
//   1 = FETM 아이덴티티 화면 (가운데 위에 시계가 돈다)
//   2 = 한글 시험지 화면
//   3 = 바탕 뒤집기 (흰 바탕 <-> 검은 바탕)
//
// 시리얼
//   0 1   화면 고르기          d   화면 버퍼 받아오기
//   f     지금 전체 갱신        m   갱신 방식 바꾸기
//   l     버튼 기록             t w 속도 측정
//   T<초> 시계 맞추기 (1970년부터의 초)
//
// 핀 근거: EE05 회로도, Seeed_Arduino_LCD 의 EE05 블록,
//          esp32 코어 variants/XIAO_ESP32S3_Plus/pins_arduino.h

#include <Arduino.h>
#include <SPI.h>
#include <time.h>
#include "soc/gpio_reg.h"
#include <GxEPD2_BW.h>
#include "epd_tune.h"

#include "kfont.h"
#include "font_pre_m14.h"
#include "font_pre_m18.h"
#include "font_pre_m24.h"
#include "font_pre_m32.h"
#include "font_pre_m48.h"
#include "font_pre_b46.h"
#include "fetm_logo.h"

#define EPD_SCK   D8   // GPIO7
#define EPD_MOSI  D10  // GPIO9
#define EPD_MISO  -1
#define EPD_CS    D7   // GPIO44
#define EPD_DC    D16  // GPIO10
#define EPD_RST   D11  // GPIO38
#define EPD_BUSY  D3   // GPIO4
#define EPD_PWR   D6   // GPIO43, 패널 전원 (HIGH = ON)

#define BTN1      D1   // GPIO2
#define BTN2      D2   // GPIO3
#define BTN3      D9   // GPIO8

static const int16_t SCR_W = 800;
static const int16_t SCR_H = 480;

GxEPD2_BW<EpdTune, 8> display(EpdTune(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

GFXcanvas1* cv = nullptr;
static int  screen = 0;        // 0 = FETM, 1 = 한글 시험지
static bool darkMode = false;  // true = 검은 바탕에 흰 글씨

// 캔버스는 비트 1 이 잉크다. 흰 바탕이면 바탕 0 / 글씨 1, 검은 바탕이면 그 반대.
static inline uint16_t BG() { return darkMode ? 1 : 0; }
static inline uint16_t FG() { return darkMode ? 0 : 1; }

// ------------------------------------------------------------------ 버튼

static const uint8_t BTN_PIN[3]  = { BTN1, BTN2, BTN3 };
static const char*   BTN_NAME[3] = { "버튼1", "버튼2", "버튼3" };

struct BtnEvent { uint32_t ms; uint8_t idx; uint8_t level; };
static const int BTN_LOG_N = 32;
volatile BtnEvent btnLog[BTN_LOG_N];
volatile uint16_t btnLogHead = 0;
volatile uint32_t btnEdges[3] = { 0, 0, 0 };
volatile uint32_t btnLastLow[3] = { 0, 0, 0 };
volatile bool btnPending[3] = { false, false, false };

// 실측: 뗄 때 변화가 1ms 안에 다섯 개씩 몰려 들어온다. 회로도의 10K 풀업과
// 100nF 때문에 신호가 천천히 올라가고, 칩이 그 비탈을 여러 번 넘나드는 것으로
// 읽는다. 그래서 시간으로만 거르지 않고, 떼진 것을 확인해야 다시 받는다.
volatile bool btnArmed[3] = { true, true, true };

// LED 는 인터럽트 안에서 바로 켜야 한다. digitalWrite 는 IRAM 에 없어서
// 인터럽트 안에서 부르면 위험하다. 레지스터에 직접 쓴다. 활성 LOW.
#define LED_ON()   REG_WRITE(GPIO_OUT_W1TC_REG, 1UL << LED_BUILTIN)
#define LED_OFF()  REG_WRITE(GPIO_OUT_W1TS_REG, 1UL << LED_BUILTIN)
volatile uint32_t ledHoldUntil = 0;

static void IRAM_ATTR btnEvent(uint8_t i)
{
  uint32_t now = millis();
  btnEdges[i]++;
  uint16_t h = btnLogHead++;
  btnLog[h % BTN_LOG_N].ms = now;
  btnLog[h % BTN_LOG_N].idx = i;
  btnLog[h % BTN_LOG_N].level = 2;
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
  {
    if (btnArmed[i]) continue;
    if (digitalRead(BTN_PIN[i]) == HIGH && millis() - btnLastLow[i] > 120)
      btnArmed[i] = true;
  }
}

static void dumpBtnLog()
{
  Serial.printf("지금 %lu ms.  상태 %d %d %d.  변화 횟수 %lu %lu %lu\n",
                (unsigned long)millis(),
                digitalRead(BTN_PIN[0]), digitalRead(BTN_PIN[1]), digitalRead(BTN_PIN[2]),
                (unsigned long)btnEdges[0], (unsigned long)btnEdges[1],
                (unsigned long)btnEdges[2]);
  uint16_t h = btnLogHead, n = h < BTN_LOG_N ? h : BTN_LOG_N;
  for (uint16_t k = 0; k < n; k++)
  {
    uint16_t idx = (h - n + k) % BTN_LOG_N;
    Serial.printf("  [%8lu ms] %s\n",
                  (unsigned long)btnLog[idx].ms, BTN_NAME[btnLog[idx].idx]);
  }
}

// ------------------------------------------------------------------ 시계

// 시계가 놓이는 자리. 부분 갱신을 하려면 x 와 폭이 8의 배수여야 한다.
static const int16_t CLOCK_X = 272;
static const int16_t CLOCK_W = 256;
static const int16_t CLOCK_Y = 48;
static const int16_t CLOCK_H = 44;

// 부분 갱신에 어느 파형을 쓸지
//   0xFC = GxEPD2 기본. 실제 온도를 읽어 OTP 파형을 고른다
//   0xDC = 내가 써넣은 온도로 OTP 파형을 고른다
//   0xCC = 내가 0x32 로 써넣은 파형을 쓴다
static uint8_t partMode = 0xFC;
static int  fakeTemp = 25;

static const char* partModeName()
{
  return partMode == 0xFC ? "실온" : partMode == 0xDC ? "가짜온도" : "내파형";
}

static void refreshWindowTuned(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (partMode == 0xFC) { display.epd2.refresh(x, y, w, h); return; }
  if (partMode == 0xDC) display.epd2.writeTemp(fakeTemp);
  if (partMode == 0xCC) display.epd2.writeLut(LUT_WHITE_STRONG, sizeof(LUT_WHITE_STRONG));
  display.epd2.setRamArea(x, y, w, h);
  display.epd2.updatePart(partMode);
}

static void nowText(char* out, size_t n)
{
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  snprintf(out, n, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

// 시계 칸을 바탕색으로 지운다
static void clearClockBox(GFXcanvas1& c)
{
  c.fillRect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, BG());
}

// 시계 칸에 시각을 쓴다
static void drawClock(GFXcanvas1& c)
{
  char buf[16];
  nowText(buf, sizeof(buf));
  clearClockBox(c);
  kfDrawMonoCenter(c, CLOCK_X + CLOCK_W / 2, CLOCK_Y + pre_m32.ascent,
                   buf, pre_m32, FG());
}

// ------------------------------------------------------------------ 화면

static const int16_t M        = 48;
static const int16_t RULE_TOP = 92;
static const int16_t RULE_BOT = 392;
static const int16_t LOGO_X   = 48;
static const int16_t LOGO_Y   = 148;
static const int16_t DIV_X    = 288;
static const int16_t TEXT_X   = 328;
static const int16_t LEAD     = 56;
static const int16_t BASE1    = 181;

static void screenIdentity(GFXcanvas1& c)
{
  c.fillScreen(BG());
  uint16_t fg = FG();

  kfDraw(c, M, 76, "전자 기술을 섞다", pre_m18, fg, 1);
  kfDrawRight(c, SCR_W - M, 76, "서울 · 대한민국", pre_m18, fg, 1);
  drawClock(c);
  c.drawFastHLine(M, RULE_TOP, SCR_W - 2 * M, fg);

  c.drawBitmap(LOGO_X, LOGO_Y, fetm_logo, FETM_LOGO_WIDTH, FETM_LOGO_HEIGHT, fg);
  c.drawFastVLine(DIV_X, LOGO_Y, FETM_LOGO_HEIGHT, fg);

  static const char* words[4] = { "FUTURE", "ELECTRONICS", "TECHNOLOGY", "MIXER" };
  for (int i = 0; i < 4; i++)
    kfDraw(c, TEXT_X, BASE1 + i * LEAD, words[i], pre_b46, fg, 0);

  c.drawFastHLine(M, RULE_BOT, SCR_W - 2 * M, fg);
  kfDraw(c, M, 424, "디자인 · 하드웨어 · 시스템", pre_m18, fg, 1);
  kfDrawRight(c, SCR_W - M, 424, "FETM.KR", pre_m18, fg, 1);
}

struct SizeRow { const char* label; const KFont* font; };

static void screenHangul(GFXcanvas1& c)
{
  c.fillScreen(BG());
  uint16_t fg = FG();

  kfDraw(c, M, 62, "한글 렌더링 시험 / Pretendard", pre_m24, fg, 0);
  c.drawFastHLine(M, 82, SCR_W - 2 * M, fg);

  static const SizeRow rows[] = {
    { "14px", &pre_m14 }, { "18px", &pre_m18 }, { "24px", &pre_m24 },
    { "32px", &pre_m32 }, { "48px", &pre_m48 },
  };
  const char* sample = "다람쥐 헌 쳇바퀴에 타고파";
  int16_t y = 82;
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
  {
    const KFont& f = *rows[i].font;
    y += f.ascent + 18;
    kfDraw(c, M, y, rows[i].label, pre_m14, fg, 0);
    kfDraw(c, 120, y, sample, f, fg, 0);
    y += f.lineHeight - f.ascent;
  }

  c.drawFastHLine(M, RULE_BOT, SCR_W - 2 * M, fg);
  kfDraw(c, M, 424, "전자 기술을 섞다 · 0123456789 · ABCabc", pre_m18, fg, 1);
  kfDrawRight(c, SCR_W - M, 424, "FETM.KR", pre_m18, fg, 1);
}

// ------------------------------------------------------------------ 패널로 보내기

static bool powerPending = false;
static uint32_t lastActivity = 0;

// 갱신 방식
//   0 = 항상 전체 갱신. 깨끗하다. 느리다.
//   1 = 부분 갱신 위주. 빠르다. 잔상이 남는다.
// 제조사 안내: 부분 갱신 다섯에서 여섯 번마다 전체 갱신을 한 번 해야 잔상이
// 안 쌓인다. [확인: good-display.com/news/134.html]
static int refreshMode = 0;
static int partialCount = 0;
static const int PARTIAL_LIMIT = 5;

// 잔상 털기 간격 (부분 갱신 몇 번마다 전체 갱신 한 번인가).
//
// 제조사 권고는 5~6번마다다.
//   [확인: docs/gooddisplay_demo/.../GDEQ0426T82_Arduino.ino 주석]
//   "After 5 partial refreshes, implement a full screen refresh to clear
//    the ghosting caused by partial refreshes."
//   [확인: Waveshare FAQ] 부분 갱신만 계속 쓰면 화면이 비정상이 되고 되돌릴 수 없다
//
// 그런데 시계는 초마다 도니까 그대로 쓰면 5초마다 2초씩 멈춘다. 못 쓴다.
// 300번(5분)으로 잡았다. 권고보다 50배 넉넉하다. 잔상이 눈에 거슬리면 줄인다.
// 시리얼로 G<숫자> 를 보내면 다시 올리지 않고 바꿀 수 있다.
static int clockFullEvery = 300;

// 시계 로그는 기본으로 끈다. 시리얼 출력이 시계를 붙잡는 일을 막는다.
// 시리얼로 p 를 보내면 켜진다.
bool clockLog = false;
static int clockTicks = 0;

static const char* fullReason = "?";

// 테두리 색. 0x00 검정, 0x01 흰색, 0x80 VCOM.
// 기본은 바탕색을 따라간다. 시리얼로 B0 B1 B8 을 보내면 바꿔볼 수 있다.
static int borderOverride = -1;
static uint8_t borderValue() {
  if (borderOverride >= 0) return (uint8_t)borderOverride;
  return darkMode ? 0x00 : 0x01;
}

static void pushToPanel(bool forceFull)
{
  const uint8_t* buf = cv->getBuffer();
  display.epd2.setBorder(borderValue());
  bool full = forceFull || (refreshMode == 0) || (partialCount >= PARTIAL_LIMIT);

  uint32_t t0 = millis(), t1, t2;
  if (full)
  {
    // GxEPD2 가 자기 안에서 쓰는 순서 그대로 따른다. 줄이면 화면이 겹쳐 보인다.
    display.epd2.writeImageForFullRefresh(buf, 0, 0, SCR_W, SCR_H, true, false, false);
    t1 = millis();
    display.epd2.refresh(false);
    t2 = millis();
    display.epd2.writeImageAgain(buf, 0, 0, SCR_W, SCR_H, true, false, false);
    partialCount = 0;
  }
  else
  {
    display.epd2.writeImage(buf, 0, 0, SCR_W, SCR_H, true, false, false);
    t1 = millis();
    display.epd2.refresh(0, 0, SCR_W, SCR_H);
    t2 = millis();
    display.epd2.writeImageAgain(buf, 0, 0, SCR_W, SCR_H, true, false, false);
    partialCount++;
  }
  powerPending = true;
  lastActivity = millis();
  Serial.printf("  [%s%s%s] 보내기 %3lu / 갱신 %4lu / 뒷정리 %3lu"
                "  => 눈에 보이기까지 %lu ms, 다음 동작까지 %lu ms\n",
                full ? "전체" : "부분", full ? " 이유:" : "", full ? fullReason : "",
                (unsigned long)(t1 - t0),
                (unsigned long)(t2 - t1), (unsigned long)(millis() - t2),
                (unsigned long)(t2 - t0), (unsigned long)(millis() - t0));
}

// 시계 칸만 보내고 그 칸만 갱신한다
static void pushClockWindow()
{
  const uint8_t* buf = cv->getBuffer();
  display.epd2.writeImagePart(buf, CLOCK_X, CLOCK_Y, SCR_W, SCR_H,
                              CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, true, false, false);
  refreshWindowTuned(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
  display.epd2.writeImagePartAgain(buf, CLOCK_X, CLOCK_Y, SCR_W, SCR_H,
                                   CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, true, false, false);
}

// 실측으로 확인한 것 (2026-08-19):
//   부분 갱신을 두 걸음으로 나눠 (창을 바탕색으로 한 번 민 뒤 내용을 그리기)
//   보려 했으나, 미는 동작이 눈에 그대로 보여서 숫자가 깜박였다. 더 나빴다.
//   기본 한 걸음이 맞다. 검은 바탕에서도 잘 넘어간다.
//
//   온도를 속여 다른 파형을 고르는 것도 해봤다 (epd_tune.h 참고).
//   -20도부터 40도까지 바꿔도 갱신 시간이 418ms 로 똑같았고 화면도 같았다.
//   이 패널 칩에는 부분 갱신 파형이 한 벌만 들어 있다는 뜻이다.

static void pushClock()
{
  uint32_t t0 = millis();
  drawClock(*cv);
  pushClockWindow();
  uint32_t t2 = millis();
  powerPending = true;
  lastActivity = millis();
  extern bool clockLog;
  if (clockLog)
  {
    char nowbuf[16];
    nowText(nowbuf, sizeof(nowbuf));
    Serial.printf("  [시계 %s] 파형 %s => %lu ms  (%d/%d, 바탕 %s)\n",
                  nowbuf, partModeName(), (unsigned long)(t2 - t0),
                  clockTicks % clockFullEvery, clockFullEvery,
                  darkMode ? "검정" : "흰색");
  }
}

static void idlePowerOff()
{
  if (!powerPending || millis() - lastActivity < 3000) return;
  display.epd2.powerOff();
  powerPending = false;
}

static void render(int which, bool push)
{
  if (which == 0) screenIdentity(*cv);
  else            screenHangul(*cv);
  if (push) { fullReason = "화면 바꿈"; pushToPanel(false); }
}

// ------------------------------------------------------------------ 덤프

static void dumpFrame(GFXcanvas1& c)
{
  static const char T[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  const uint8_t* buf = c.getBuffer();
  size_t n = (size_t)((c.width() + 7) / 8) * c.height();
  Serial.printf("---FB-BEGIN %d %d %u---\n", c.width(), c.height(), (unsigned)n);
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
  Serial.println("---FB-END---");
}

// ------------------------------------------------------------------ setup / loop

void setup()
{
  Serial.begin(115200);
  // USB 가 꽂혀 있는데 아무도 안 읽어가면 출력 버퍼가 차고, printf 한 번이
  // 최대 2초까지 멈춰 선다 (HWCDC.cpp: tx_timeout_ms 100ms x 20회).
  // 시계가 1초마다 찍으니 그대로 두면 시계가 멈춘다. 기다리지 말고 버리게 한다.
  Serial.setTxTimeoutMs(0);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }
  delay(300);
  Serial.println("\n=== FETM / EE05 + GDEY0426T82 ===");

  setenv("TZ", "KST-9", 1);   // 한국 시간
  tzset();

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(20);
  pinMode(EPD_BUSY, INPUT);

  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < 3; i++)
  { digitalWrite(LED_BUILTIN, LOW); delay(120); digitalWrite(LED_BUILTIN, HIGH); delay(120); }

  for (int i = 0; i < 3; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[0]), isr0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[1]), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN[2]), isr2, FALLING);

  cv = new GFXcanvas1(SCR_W, SCR_H);
  if (!cv || !cv->getBuffer()) { Serial.println("!! canvas alloc 실패"); return; }

  SPI.end();
  SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS);
  // 첫 인자를 0 으로 두면 GxEPD2 가 매 갱신마다 찍는 진단 줄이 없어진다.
  // 그 줄도 시리얼을 막는 원인이었다.
  display.init(0, true, 2, false);
  // 기본값 4MHz 를 20MHz 로. 더 올려도 안 빨라진다 (바이트마다 드는 호출 비용이 벽)
  display.epd2.selectSPI(SPI, SPISettings(20000000, MSBFIRST, SPI_MODE0));

  render(screen, true);
  Serial.println("준비됨. 버튼 1=FETM 2=한글 3=바탕뒤집기");
}

void loop()
{
  // 눌린 직후 400ms 는 계속 켠다. 그 밖에는 2초마다 아주 짧게만 깜빡.
  if (ledHoldUntil && (int32_t)(millis() - ledHoldUntil) < 0) LED_ON();
  else
  {
    ledHoldUntil = 0;
    bool held = (digitalRead(BTN_PIN[0]) == LOW) || (digitalRead(BTN_PIN[1]) == LOW) ||
                (digitalRead(BTN_PIN[2]) == LOW);
    // 시계가 도는 화면에서는 초마다 반짝이는 것이 살아있음 표시를 대신한다.
    // 그래서 여기서는 안 깜빡인다.
    if (held || (screen != 0 && millis() % 2000 < 40)) LED_ON(); else LED_OFF();
  }

  if (Serial.available())
  {
    int ch = Serial.read();
    if (ch == 'd' && cv) dumpFrame(*cv);
    else if (ch == '0' || ch == '1') { screen = ch - '0'; render(screen, true); }
    else if (ch == 'f') { fullReason = "손으로 지우기"; render(screen, false); pushToPanel(true); }
    else if (ch == 'm')
    {
      refreshMode = 1 - refreshMode; partialCount = 0;
      Serial.printf("갱신 방식 -> %s\n", refreshMode == 0 ? "항상 전체" : "부분 위주");
    }
    else if (ch == 'l') dumpBtnLog();
    else if (ch == 'B')
    {
      char b[8];
      size_t n = Serial.readBytesUntil('\n', b, sizeof(b) - 1);
      b[n] = 0;
      if (b[0] == 0) borderOverride = -1;
      else borderOverride = (int)strtol(b, nullptr, 16);
      Serial.printf("테두리 -> 0x%02X (%s)\n", borderValue(),
                    borderOverride < 0 ? "바탕색 따라감" : "직접 지정");
      fullReason = "테두리 바꿈";
      render(screen, false);
      pushToPanel(true);
    }
    else if (ch == 'p') { clockLog = !clockLog;
      Serial.printf("시계 로그 -> %s\n", clockLog ? "켬" : "끔"); }
    else if (ch == 'G')
    {
      char b[12];
      size_t n = Serial.readBytesUntil('\n', b, sizeof(b) - 1);
      b[n] = 0;
      int v = atoi(b);
      if (v >= 1) { clockFullEvery = v; clockTicks = 0; }
      Serial.printf("잔상 털기 -> 부분 갱신 %d번마다 (%d초마다)\n",
                    clockFullEvery, clockFullEvery);
    }
    else if (ch == 'N') { partMode = 0xFC; Serial.println("파형 -> 실온(기본)"); }
    else if (ch == 'V') { partMode = 0xCC; Serial.println("파형 -> 내가 만든 표"); }
    else if (ch == 'C')
    {
      char b[12];
      size_t n = Serial.readBytesUntil('\n', b, sizeof(b) - 1);
      b[n] = 0;
      fakeTemp = atoi(b);
      partMode = 0xDC;
      Serial.printf("파형 -> 가짜온도 %d도\n", fakeTemp);
    }
    else if (ch == 'i')      // 바탕 뒤집기 (버튼3 과 같은 일)
    {
      darkMode = !darkMode; clockTicks = 0; fullReason = "바탕 뒤집기";
      render(screen, false); pushToPanel(true);
    }
    else if (ch == 'T')
    {
      char buf[24];
      size_t n = Serial.readBytesUntil('\n', buf, sizeof(buf) - 1);
      buf[n] = 0;
      struct timeval tv = { (time_t)atol(buf), 0 };
      settimeofday(&tv, nullptr);
      char now[16]; nowText(now, sizeof(now));
      Serial.printf("시계 맞춤 -> %s\n", now);
      fullReason = "시계 맞춤";
      render(screen, false);
      pushToPanel(true);
    }
  }

  for (int i = 0; i < 3; i++)
  {
    if (!btnPending[i]) continue;
    btnPending[i] = false;
    uint32_t pressedAt = btnLastLow[i];
    // 이미 그 화면이면 아무것도 안 한다. 다시 그리면 2초 동안 시계가 멈춘다.
    if (i == 0 || i == 1)
    {
      int want = (i == 0) ? 0 : 1;
      if (screen == want)
      {
        Serial.printf("%s: 이미 그 화면이라 그냥 둠\n", BTN_NAME[i]);
        continue;
      }
      screen = want;
      fullReason = "화면 바꿈";
      render(screen, false);
      pushToPanel(true);
    }
    else
    {
      darkMode = !darkMode;
      Serial.printf("바탕 -> %s\n", darkMode ? "검정" : "흰색");
      fullReason = "바탕 뒤집기";
      render(screen, false);
      pushToPanel(true);     // 화면 전체가 뒤집히니 전체 갱신이 맞다
    }
    clockTicks = 0;
    Serial.printf("%s: 누른 뒤 %lu ms 만에 화면 바뀜\n",
                  BTN_NAME[i], (unsigned long)(millis() - pressedAt));
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
      // 초가 바뀌는 순간 LED 를 짧게 반짝인다. 화면 갱신은 421ms 동안
      // loop 를 붙잡으므로, 붙잡히기 전에 켜고 끈다.
      LED_ON();
      delay(60);
      LED_OFF();
      clockTicks++;
      if (clockTicks % clockFullEvery == 0)
      { drawClock(*cv); fullReason = "1분마다 잔상 털기"; pushToPanel(true); }
      else pushClock();
    }
  }

  rearmButtons();
  idlePowerOff();
  delay(2);
}
