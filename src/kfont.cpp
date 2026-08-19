#include "kfont.h"

const char* kfNext(const char* s, uint32_t* cp)
{
  const uint8_t* p = (const uint8_t*)s;
  uint8_t c = *p;
  if (c < 0x80)                { *cp = c;                       return (const char*)p + 1; }
  if ((c & 0xE0) == 0xC0)      { *cp = ((uint32_t)(c & 0x1F) << 6)
                                      | (p[1] & 0x3F);          return (const char*)p + 2; }
  if ((c & 0xF0) == 0xE0)      { *cp = ((uint32_t)(c & 0x0F) << 12)
                                      | ((uint32_t)(p[1] & 0x3F) << 6)
                                      | (p[2] & 0x3F);          return (const char*)p + 3; }
  if ((c & 0xF8) == 0xF0)      { *cp = ((uint32_t)(c & 0x07) << 18)
                                      | ((uint32_t)(p[1] & 0x3F) << 12)
                                      | ((uint32_t)(p[2] & 0x3F) << 6)
                                      | (p[3] & 0x3F);          return (const char*)p + 4; }
  *cp = 0xFFFD;
  return (const char*)p + 1;
}

const KGlyph* kfFind(const KFont& f, uint32_t cp)
{
  if (cp > 0xFFFF) return nullptr;
  int lo = 0, hi = (int)f.count - 1;
  while (lo <= hi)
  {
    int mid = (lo + hi) / 2;
    uint16_t v = f.glyphs[mid].cp;
    if (v == cp) return &f.glyphs[mid];
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

int16_t kfWidth(const KFont& f, const char* s, int16_t track)
{
  int16_t w = 0;
  uint32_t cp;
  bool first = true;
  while (*s)
  {
    s = kfNext(s, &cp);
    const KGlyph* g = kfFind(f, cp);
    if (!first) w += track;
    first = false;
    w += g ? g->adv : (f.ascent / 2);
  }
  return w;
}

void kfDraw(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s,
            const KFont& f, uint16_t color, int16_t track)
{
  uint32_t cp;
  while (*s)
  {
    s = kfNext(s, &cp);
    const KGlyph* g = kfFind(f, cp);
    if (!g)
    {
      // 폰트에 없는 글자. 조용히 넘기면 나중에 원인을 못 찾는다.
      Serial.printf("[kfont] 빠진 글자 U+%04X (폰트 %u자)\n",
                    (unsigned)cp, (unsigned)f.count);
      x += f.ascent / 2 + track;
      continue;
    }
    const uint8_t* bm = f.bits + g->off;
    uint8_t stride = (g->w + 7) / 8;
    for (uint8_t row = 0; row < g->h; row++)
    {
      const uint8_t* line = bm + (uint32_t)row * stride;
      for (uint8_t col = 0; col < g->w; col++)
      {
        if (line[col >> 3] & (0x80 >> (col & 7)))
          gfx.drawPixel(x + g->dx + col, y + g->dy + row, color);
      }
    }
    x += g->adv + track;
  }
}

void kfDrawRight(Adafruit_GFX& gfx, int16_t right, int16_t y, const char* s,
                 const KFont& f, uint16_t color, int16_t track)
{
  kfDraw(gfx, right - kfWidth(f, s, track), y, s, f, color, track);
}

void kfDrawCenter(Adafruit_GFX& gfx, int16_t cx, int16_t y, const char* s,
                  const KFont& f, uint16_t color, int16_t track)
{
  kfDraw(gfx, cx - kfWidth(f, s, track) / 2, y, s, f, color, track);
}

int16_t kfDigitCell(const KFont& f)
{
  int16_t w = 0;
  for (uint32_t c = '0'; c <= '9'; c++)
  {
    const KGlyph* g = kfFind(f, c);
    if (g && g->adv > w) w = g->adv;
  }
  return w;
}

int16_t kfWidthMono(const KFont& f, const char* s)
{
  int16_t cell = kfDigitCell(f);
  int16_t w = 0;
  uint32_t cp;
  while (*s)
  {
    s = kfNext(s, &cp);
    if (cp >= '0' && cp <= '9') { w += cell; continue; }
    const KGlyph* g = kfFind(f, cp);
    w += g ? g->adv : cell;
  }
  return w;
}

void kfDrawMono(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s,
                const KFont& f, uint16_t color)
{
  int16_t cell = kfDigitCell(f);
  uint32_t cp;
  while (*s)
  {
    s = kfNext(s, &cp);
    const KGlyph* g = kfFind(f, cp);
    if (!g) { x += cell; continue; }
    bool digit = (cp >= '0' && cp <= '9');
    int16_t step = digit ? cell : g->adv;
    int16_t ox = digit ? (cell - g->adv) / 2 : 0;   // 칸 가운데로
    char one[5];
    int n = 0;
    if (cp < 0x80) one[n++] = (char)cp;
    else if (cp < 0x800) { one[n++] = 0xC0 | (cp >> 6); one[n++] = 0x80 | (cp & 0x3F); }
    else { one[n++] = 0xE0 | (cp >> 12); one[n++] = 0x80 | ((cp >> 6) & 0x3F);
           one[n++] = 0x80 | (cp & 0x3F); }
    one[n] = 0;
    kfDraw(gfx, x + ox, y, one, f, color, 0);
    x += step;
  }
}

void kfDrawMonoCenter(Adafruit_GFX& gfx, int16_t cx, int16_t y, const char* s,
                      const KFont& f, uint16_t color)
{
  kfDrawMono(gfx, cx - kfWidthMono(f, s) / 2, y, s, f, color);
}

// ---------------------------------------------------------------- 회색 4단계

static const KGlyph* kfFindG(const KFontG& f, uint32_t cp)
{
  if (cp > 0xFFFF) return nullptr;
  int lo = 0, hi = (int)f.count - 1;
  while (lo <= hi)
  {
    int mid = (lo + hi) / 2;
    uint16_t v = f.glyphs[mid].cp;
    if (v == cp) return &f.glyphs[mid];
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

int16_t kfWidthG(const KFontG& f, const char* s, int16_t track)
{
  int16_t w = 0;
  uint32_t cp;
  bool first = true;
  while (*s)
  {
    s = kfNext(s, &cp);
    const KGlyph* g = kfFindG(f, cp);
    if (!first) w += track;
    first = false;
    w += g ? g->adv : (f.ascent / 2);
  }
  return w;
}

void kfDrawGray(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* s,
                const KFontG& f, const uint16_t levelColor[4], int16_t track)
{
  uint32_t cp;
  while (*s)
  {
    s = kfNext(s, &cp);
    const KGlyph* g = kfFindG(f, cp);
    if (!g) { x += f.ascent / 2 + track; continue; }
    const uint8_t* bm = f.bits + g->off;
    uint8_t stride = (g->w + 3) / 4;         // 한 바이트에 픽셀 4개
    for (uint8_t row = 0; row < g->h; row++)
    {
      const uint8_t* line = bm + (uint32_t)row * stride;
      for (uint8_t col = 0; col < g->w; col++)
      {
        uint8_t lv = (line[col >> 2] >> (6 - 2 * (col & 3))) & 0x03;
        if (lv) gfx.drawPixel(x + g->dx + col, y + g->dy + row, levelColor[lv]);
      }
    }
    x += g->adv + track;
  }
}

void kfDrawGrayRight(Adafruit_GFX& gfx, int16_t right, int16_t y, const char* s,
                     const KFontG& f, const uint16_t levelColor[4], int16_t track)
{
  kfDrawGray(gfx, right - kfWidthG(f, s, track), y, s, f, levelColor, track);
}
