// 부분 갱신 파형을 바꿔보기 위한 껍데기.
//
// 문제: 검은 바탕에 가는 흰 획을 부분 갱신으로 그리면 안 보인다.
//       흰색 쪽으로 미는 힘이 모자란다.
//
// 데이터시트 근거 (docs/datasheet/SSD1677_Rev1.0_Nov2018.pdf)
//
//  6.9절: "읽은 온도와 표시 모드에 따라 OTP 안에서 TR0 부터 TR33 까지 순서대로
//          찾는다. 마지막으로 맞는 것이 선택되고, 그에 해당하는 파형이 LUT
//          레지스터에 실린다"
//        -> 칩 안에 제조사가 넣어둔 파형이 34벌 있고 온도로 고른다.
//           온도가 낮을수록 잉크가 굼떠서 더 길고 센 파형이 배정된다.
//
//  0x22 명령표: 0x20 = 온도값 불러오기, 0x10 = OTP 에서 파형 불러오기,
//               0x08 = 표시 모드 2(부분), 0x04 = 표시 시작
//        GxEPD2 의 부분 갱신은 0xFC 라서 0x20 이 켜져 있다. 즉 늘 실제 온도를
//        읽어 파형을 고른다. 그 비트만 끈 0xDC 를 쓰면 내가 0x1A 로 써넣은
//        온도가 그대로 쓰인다.
//
//  0x1A 명령표: 온도 레지스터에 쓰기. 12비트, 두 바이트로 나눠 보낸다.
//               값은 섭씨 x 16 이다. (Table 6-8 에서 5도 = 0x050 = 80 = 5x16)
//
//  주의 (Table 6-8 아래): "적용 온도 범위 전체를 덮도록 하라. 맞는 온도 범위가
//        없으면 화면이 갱신되지 않는다." 그러니 터무니없는 값을 넣지 말 것.
//
// 아래는 파형을 통째로 직접 써넣는 길도 같이 열어둔다 (0x32).
//  Figure 6-6: 105바이트 구조
//     0~49    VS[nX-LUTm]  표 5개 x 10바이트 (한 바이트에 구간 4개, 2비트씩)
//     50~99   TP[nA..nD], RP[n]  묶음 10개 x 5바이트
//     100~104 프레임 속도
//  Table 6-5: 이전 버퍼(0x26)와 현재 버퍼(0x24) 비트 조합이 표를 고른다
//     (0,0) 검정->검정 = LUT0     (0,1) 검정->흰색 = LUT1   <- 약한 자리
//     (1,0) 흰색->검정 = LUT2     (1,1) 흰색->흰색 = LUT3
//  Table 6-6: 00=VSS(안 밈) 01=VSH1(검정 쪽) 10=VSL(흰색 쪽) 11=VSH2

#pragma once
#include <GxEPD2_BW.h>

class EpdTune : public GxEPD2_426_GDEQ0426T82
{
  public:
    EpdTune(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_426_GDEQ0426T82(cs, dc, rst, busy) {}

    // 온도 레지스터에 값을 써넣는다. 섭씨.
    void writeTemp(int degC)
    {
      int16_t v = (int16_t)(degC * 16);      // 12비트, 섭씨 x 16
      _writeCommand(0x1A);
      _writeData((uint8_t)((v >> 4) & 0xFF));   // A[11:4]
      _writeData((uint8_t)((v & 0x0F) << 4));   // A[3:0] 을 위쪽 4비트에
    }

    // 화면 테두리(VBD)를 어느 색으로 몰지 정한다.
    // 데이터시트 0x3C 명령표:
    //   A[7:6] 00 = 파형으로 몬다 (A[1:0] 이 표 번호)
    //          01 = 고정 전압 (A[5:4])   10 = VCOM   11 = 띄움(기본값)
    //   A[1:0] 00 = LUT0, 01 = LUT1, 10 = LUT2, 11 = LUT3
    // Table 6-5: LUT0 은 검정으로, LUT1 은 흰색으로 미는 표다.
    //   0x00 = 검정 테두리   0x01 = 흰색 테두리 (GxEPD2 기본)
    //   0x80 = VCOM (제조사 예제가 쓰는 값)
    void setBorder(uint8_t v)
    {
      _writeCommand(0x3C);
      _writeData(v);
    }

    // 파형 표를 통째로 써넣는다 (105바이트)
    void writeLut(const uint8_t* lut, uint16_t n)
    {
      _writeCommand(0x32);
      _writeData(lut, n);
    }

    // 드라이버의 _setPartialRamArea 와 같은 일. 그쪽은 private 이라 못 부른다.
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

    // 부분 갱신. mode 에 따라 어느 파형을 쓸지 갈린다.
    //   0xFC : GxEPD2 기본. 실제 온도를 읽어 OTP 파형을 고른다
    //   0xDC : 내가 써넣은 온도로 OTP 파형을 고른다
    //   0xCC : OTP 를 안 불러온다. 0x32 로 써넣은 내 파형을 쓴다
    void updatePart(uint8_t mode)
    {
      _writeCommand(0x21);
      _writeData(0x00);
      _writeData(0x00);
      _writeCommand(0x22);
      _writeData(mode);
      _writeCommand(0x20);
      _waitWhileBusy("부분갱신", 4000);
    }

    // 온도를 읽어본다 (0x1B)
    int readTemp()
    {
      // 읽기는 GxEPD2 가 지원 안 한다. 써넣은 값 확인용은 아니고 자리만 둔다.
      return -999;
    }
};

// ------------------------------------------------ 직접 만든 파형 (필요할 때만)
#define VS4(a, b, c, d) (uint8_t)(((a) << 6) | ((b) << 4) | ((c) << 2) | (d))
#define V_OFF 0
#define V_BLK 1   // VSH1, 검정 쪽
#define V_WHT 2   // VSL,  흰색 쪽

// 흰색으로 미는 구간(LUT1)을 길게 잡은 것.
static const uint8_t LUT_WHITE_STRONG[105] = {
  // LUT0 검정->검정 : 안 민다
  0,0,0,0,0,0,0,0,0,0,
  // LUT1 검정->흰색 : 짧게 반대로 흔든 뒤 길게 흰색으로
  VS4(V_OFF,V_OFF,V_BLK,V_BLK), VS4(V_WHT,V_WHT,V_WHT,V_WHT), 0,0,0,0,0,0,0,0,
  // LUT2 흰색->검정
  VS4(V_OFF,V_OFF,V_WHT,V_WHT), VS4(V_BLK,V_BLK,V_BLK,V_BLK), 0,0,0,0,0,0,0,0,
  // LUT3 흰색->흰색 : 안 민다
  0,0,0,0,0,0,0,0,0,0,
  // LUT4 VCOM
  0,0,0,0,0,0,0,0,0,0,
  // TP[0A..0D], RP[0]
  0, 0, 6, 6, 0,
  // TP[1A..1D], RP[1]
  12, 12, 12, 12, 0,
  0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0,
  0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0,  0,0,0,0,0,
  0x22, 0x22, 0x22, 0x22, 0x22
};
