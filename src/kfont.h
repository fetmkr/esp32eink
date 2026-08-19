// 한글을 쓰기 위한 최소 비트맵 폰트.
//
// Adafruit_GFX 의 기본 폰트(GFXfont)는 코드포인트가 연속 구간 하나뿐이다.
// 한글 완성형은 U+AC00 부터 11172자라 그 구조에 담을 수 없다.
// 그래서 쓰는 글자만 골라 담고 코드포인트로 이진 탐색하는 표를 따로 만든다.
//
// 비트맵: 글자마다 위에서 아래로, 한 줄은 ceil(w/8) 바이트, MSB 먼저, 비트 1 = 잉크.

#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

struct KGlyph
{
  uint16_t cp;    // 유니코드 코드포인트 (BMP 만)
  uint32_t off;   // 비트맵 배열에서의 시작 위치
  uint8_t  w, h;  // 글자 상자 크기 (픽셀)
  int8_t   dx;    // 기준점에서 왼쪽으로 얼마나 (보통 0 근처)
  int8_t   dy;    // 밑선에서 위로 얼마나 (음수 = 밑선 위)
  uint8_t  adv;   // 다음 글자까지 이동량
};

struct KFont
{
  const uint8_t* bits;
  const KGlyph*  glyphs;
  uint16_t       count;
  uint8_t        ascent;      // 밑선 위 높이
  uint8_t        lineHeight;  // 줄 간격 기준값
};

// UTF-8 에서 코드포인트 하나를 꺼내고 다음 위치를 돌려준다.
const char* kfNext(const char* s, uint32_t* cp);

// 없으면 nullptr
const KGlyph* kfFind(const KFont& f, uint32_t cp);

int16_t kfWidth(const KFont& f, const char* s, int16_t track = 0);
void kfDraw(Adafruit_GFX& g, int16_t x, int16_t y, const char* s,
            const KFont& f, uint16_t color, int16_t track = 0);
void kfDrawRight(Adafruit_GFX& g, int16_t right, int16_t y, const char* s,
                 const KFont& f, uint16_t color, int16_t track = 0);
void kfDrawCenter(Adafruit_GFX& g, int16_t cx, int16_t y, const char* s,
                  const KFont& f, uint16_t color, int16_t track = 0);

// 시계처럼 숫자가 계속 바뀌는 곳에 쓴다.
// Pretendard 는 숫자 폭이 글자마다 달라서, 그냥 그리면 1 이 나올 때마다
// 뒤 글자가 앞뒤로 흔들린다. 숫자를 같은 폭의 칸 안에 가운데 놓아 고정한다.
int16_t kfDigitCell(const KFont& f);                       // 가장 넓은 숫자의 폭
int16_t kfWidthMono(const KFont& f, const char* s);
void kfDrawMono(Adafruit_GFX& g, int16_t x, int16_t y, const char* s,
                const KFont& f, uint16_t color);
void kfDrawMonoCenter(Adafruit_GFX& g, int16_t cx, int16_t y, const char* s,
                      const KFont& f, uint16_t color);
