// 회색 화면 위에 흑백 부분 갱신을 얹기 위한 껍데기.
//
// 왜 필요한가:
//   GxEPD2_4G 는 회색 모드에서 흑백으로 넘어갈 때 화면 메모리를 통째로 밀고
//   전체 갱신을 강제한다. 안전하게 만든 것이지만, 우리는 시계 칸만 흑백으로
//   따로 밀고 싶다.
//
//     void _InitDisplay() {
//       ...
//       if (_refresh_mode == grey_refresh) {
//         _writeScreenBuffer(0x24, 0xff);      // 화면 메모리를 흰색으로
//         _writeScreenBuffer(0x26, 0xff);
//         _refresh_mode = forced_full_refresh; // 전체 갱신 강제
//       }
//     }
//
//   전자잉크는 전원을 끊어도 화면을 유지한다. 그러니 메모리를 밀어도 눈에
//   보이는 그림은 그대로다. 시계 칸의 "이전 모습" 을 내가 직접 넣어주면
//   그 칸만 흑백 부분 갱신으로 밀 수 있다.
//
// 아래 순서는 GxEPD2_4G 의 _InitDisplay, _setPartialRamArea, _Update_Part 를
// 그대로 옮긴 것이다. 그 함수들이 private 이라 부를 수 없어서 다시 적었다.

#pragma once
#include <GxEPD2_4G_4G.h>

class EpdMixed : public GxEPD2_426_GDEQ0426T82
{
  public:
    EpdMixed(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_426_GDEQ0426T82(cs, dc, rst, busy) {}

    // 흑백 부분 갱신을 쓸 수 있는 상태로 만든다.
    // SWRESET 이 들어가서 회색용으로 바꿔 뒀던 전압과 파형이 기본값으로 돌아온다.
    void initBW(uint8_t border = 0x01)
    {
      delay(10);
      _writeCommand(0x12);           // SWRESET
      delay(10);
      _writeCommand(0x0C);           // soft start
      _writeData(0xAE); _writeData(0xC7); _writeData(0xC3);
      _writeData(0xC0); _writeData(0x80);
      _writeCommand(0x01);           // Driver output control
      _writeData((HEIGHT - 1) % 256);
      _writeData((HEIGHT - 1) / 256);
      _writeData(0x02);
      _writeCommand(0x3C);           // 테두리
      _writeData(border);
      _writeCommand(0x18);           // 내장 온도계 사용
      _writeData(0x80);
      // 두 장을 같은 값으로 맞춰둔다. 이걸 안 하면 시계 칸만 밀어도
      // 화면 전체가 다시 칠해진다.
      fillBothPlanes(0xFF);
    }

    // 이 패널은 게이트 방향이 뒤집혀 있어 y 를 뒤집어 넣는다.
    void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
    {
      y = HEIGHT - y - h;
      _writeCommand(0x11);
      _writeData(0x01);
      _writeCommand(0x44);
      _writeData(x % 256); _writeData(x / 256);
      _writeData((x + w - 1) % 256); _writeData((x + w - 1) / 256);
      _writeCommand(0x45);
      _writeData((y + h - 1) % 256); _writeData((y + h - 1) / 256);
      _writeData(y % 256); _writeData(y / 256);
      _writeCommand(0x4e);
      _writeData(x % 256); _writeData(x / 256);
      _writeCommand(0x4f);
      _writeData((y + h - 1) % 256); _writeData((y + h - 1) / 256);
    }

    // 1비트 그림 한 조각을 화면 메모리에 쓴다.
    //   cmd 0x24 = 지금 모습, 0x26 = 이전 모습
    //   bmp 는 비트 1 이 글자. 패널은 비트 1 이 흰색이다.
    //   invert = true  : 글자를 검정으로 (흰 바탕용)
    //   invert = false : 글자를 흰색으로 (검은 바탕용)
    void writeWindow(uint8_t cmd, const uint8_t* bmp, int16_t w, int16_t h,
                     bool invert)
    {
      int16_t wb = (w + 7) / 8;
      _writeCommand(cmd);
      _startTransfer();
      for (int16_t row = 0; row < h; row++)
        for (int16_t b = 0; b < wb; b++)
        {
          uint8_t v = bmp[(int32_t)row * wb + b];
          _transfer(invert ? (uint8_t)~v : v);
        }
      _endTransfer();
    }

    // 화면 메모리 두 장을 같은 값으로 채운다. "바뀐 곳 없음" 상태로 만든다.
    //
    // 왜 필요한가 (실측으로 확인):
    //   부분 갱신은 창을 정해도 화면 전체를 훑는다. 창은 데이터를 쓰는 자리일
    //   뿐이고, 실제로 움직이는 픽셀은 두 메모리 값이 다른 곳뿐이다.
    //   (창을 480줄에서 60줄로 줄여도 갱신 시간이 407ms 로 같았다)
    //
    //   회색으로 그린 뒤에는 메모리에 회색용 값이 들어 있다. 회색의 중간 두
    //   단계(연한 회색, 진한 회색)는 흑백 기준으로 읽으면 "바뀐 곳" 이다.
    //   그래서 시계 칸만 밀려고 해도 화면 전체가 다시 칠해진다.
    //
    //   두 장을 같게 채우면 아무 데도 안 움직인다. 눈에 보이는 회색 그림은
    //   전자잉크라 전원과 상관없이 그대로 남는다.
    void fillBothPlanes(uint8_t value)
    {
      setRamArea(0, 0, WIDTH, HEIGHT);
      for (uint8_t cmd : { (uint8_t)0x24, (uint8_t)0x26 })
      {
        _writeCommand(cmd);
        _startTransfer();
        for (uint32_t i = 0; i < (uint32_t)WIDTH / 8 * HEIGHT; i++) _transfer(value);
        _endTransfer();
      }
    }

    // 흑백 부분 갱신
    void updatePartBW()
    {
      _writeCommand(0x21);
      _writeData(0x00); _writeData(0x00);
      _writeCommand(0x22);
      _writeData(0xfc);
      _writeCommand(0x20);
      _waitWhileBusy("흑백 부분갱신", 2000);
    }

    void powerOffPub() { powerOff(); }
};
