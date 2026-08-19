// 회색 4단계를 담는 그림판.
//
// Adafruit_GFX 에는 한 픽셀 2비트짜리 그림판이 없다. 그래서 만든다.
// 담는 형식은 GxEPD2_4G 의 drawImage_4G(bpp=2) 가 기대하는 것과 같게 맞췄다.
//   한 바이트에 픽셀 4개, 위 비트부터.
//   값 3 = 흰색, 2 = 연한 회색, 1 = 진한 회색, 0 = 검정.
//   한 줄은 ceil(w/4) 바이트.
// [확인: GxEPD2_4G 의 GxEPD2_426_GDEQ0426T82::writeImage_4G 본문]

#pragma once
#include <Adafruit_GFX.h>
#include <GxEPD2_4G_4G.h>

class Canvas2 : public Adafruit_GFX
{
  public:
    Canvas2(int16_t w, int16_t h) : Adafruit_GFX(w, h)
    {
      _stride = (w + 3) / 4;
      _size = (size_t)_stride * h;
      _buf = (uint8_t*)malloc(_size);
    }
    ~Canvas2() { free(_buf); }

    uint8_t* buffer() { return _buf; }
    size_t   size() const { return _size; }
    bool     ok() const { return _buf != nullptr; }

    static uint8_t levelOf(uint16_t color)
    {
      if (color == GxEPD_WHITE)     return 3;
      if (color == GxEPD_LIGHTGREY) return 2;
      if (color == GxEPD_DARKGREY)  return 1;
      return 0;                                 // 검정
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
      if (!_buf || x < 0 || y < 0 || x >= _width || y >= _height) return;
      uint8_t lv = levelOf(color);
      size_t i = (size_t)y * _stride + (x >> 2);
      uint8_t sh = 6 - 2 * (x & 3);
      _buf[i] = (uint8_t)((_buf[i] & ~(0x03 << sh)) | (lv << sh));
    }

    void fillScreen(uint16_t color) override
    {
      if (!_buf) return;
      uint8_t lv = levelOf(color);
      memset(_buf, lv * 0x55, _size);     // 한 바이트에 같은 값 네 번
    }

  private:
    uint8_t* _buf = nullptr;
    size_t   _size = 0;
    int16_t  _stride = 0;
};
