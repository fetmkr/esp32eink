#!/usr/bin/env python3
"""Pretendard 에서 필요한 글자만 뽑아 1비트 비트맵 폰트 헤더를 만든다.

- STRINGS 에 적힌 문장에 쓰인 글자만 담는다. 문장을 늘리면 다시 돌리면 된다.
- Pretendard 는 SIL Open Font License 라 펌웨어에 넣어도 된다.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

FONT_DIR = os.path.expanduser("~/Library/Fonts")
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "src")

# 펌웨어가 화면에 찍는 모든 문자열. 여기 없는 글자는 폰트에 안 들어간다.
STRINGS = [
    "EST. 2026",
    "서울 · 대한민국",
    "FUTURE", "ELECTRONICS", "TECHNOLOGY", "MIXER",
    "디자인 · 하드웨어 · 시스템",
    "FETM.KR",
    "한글 렌더링 시험 / Pretendard",
    "다람쥐 헌 쳇바퀴에 타고파",
    "전자 기술을 섞다",
    "0123456789",
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    "px 크기",
    "회색 4단계 / 4 Gray Levels",
    "흑백 두 값",
    "안티알리아싱",
    "흰색", "연한 회색", "진한 회색", "검정",
    "갱신 4초",
]

# (헤더이름, 폰트파일, 픽셀크기)
FONTS = [
    ("pre_m14", "Pretendard-Medium.otf", 14),
    ("pre_m18", "Pretendard-Medium.otf", 18),
    ("pre_m24", "Pretendard-Medium.otf", 24),
    ("pre_m32", "Pretendard-Medium.otf", 32),
    ("pre_m48", "Pretendard-Medium.otf", 48),
    ("pre_b46", "Pretendard-Bold.otf", 46),
]

THRESHOLD = 128   # 흑백으로 자를 때 이보다 진하면 잉크로 친다

# 회색 4단계로 구울 크기들. 안티알리아싱된 글자를 4단계로 눌러 담는다.
# 한 픽셀에 2비트. 0 = 안 그림, 1 = 연한 회색, 2 = 진한 회색, 3 = 검정.
GRAY_FONTS = [
    ("preg_m18", "Pretendard-Medium.otf", 18),
    ("preg_m24", "Pretendard-Medium.otf", 24),
    ("preg_m32", "Pretendard-Medium.otf", 32),
    ("preg_m48", "Pretendard-Medium.otf", 48),
    ("preg_b46", "Pretendard-Bold.otf", 46),
]


def build(name, ttf_name, size, chars):
    path = os.path.join(FONT_DIR, ttf_name)
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()

    bits = bytearray()
    glyphs = []
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        bbox = font.getbbox(ch, anchor="ls")          # 밑선 기준 상자
        x0, y0, x1, y1 = bbox
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:                           # 공백 같은 것
            glyphs.append((ord(ch), len(bits), 0, 0, 0, 0, adv))
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        px = img.load()
        off = len(bits)
        stride = (w + 7) // 8
        for yy in range(h):
            row = bytearray(stride)
            for xx in range(w):
                if px[xx, yy] >= THRESHOLD:
                    row[xx >> 3] |= 0x80 >> (xx & 7)
            bits += row
        assert -128 <= x0 <= 127 and -128 <= y0 <= 127, (ch, x0, y0)
        assert w < 256 and h < 256 and adv < 256, (ch, w, h, adv)
        glyphs.append((ord(ch), off, w, h, x0, y0, adv))

    glyphs.sort(key=lambda g: g[0])

    out = os.path.join(OUT_DIR, "font_%s.h" % name)
    with open(out, "w", encoding="utf-8") as f:
        f.write("// %s %dpx 에서 자동 생성. tools/build_fonts.py 를 다시 돌려서 고칠 것.\n"
                % (ttf_name, size))
        f.write("// 글자 %d개, 비트맵 %d바이트\n" % (len(glyphs), len(bits)))
        f.write("#pragma once\n#include \"kfont.h\"\n\n")
        f.write("static const uint8_t %s_bits[] = {\n" % name)
        for i in range(0, len(bits), 16):
            f.write("  " + "".join("0x%02X," % b for b in bits[i:i + 16]) + "\n")
        f.write("};\n\n")
        f.write("static const KGlyph %s_glyphs[] = {\n" % name)
        for cp, off, w, h, dx, dy, adv in glyphs:
            f.write("  {0x%04X,%6d,%3d,%3d,%4d,%4d,%3d},  // %s\n"
                    % (cp, off, w, h, dx, dy, adv,
                       chr(cp) if cp > 32 else "sp"))
        f.write("};\n\n")
        f.write("static const KFont %s = { %s_bits, %s_glyphs, %d, %d, %d };\n"
                % (name, name, name, len(glyphs), ascent, ascent + descent))
    print("%-9s %-26s %2dpx  글자 %3d  비트맵 %6d바이트  ascent %d  줄높이 %d  -> %s"
          % (name, ttf_name, size, len(glyphs), len(bits), ascent, ascent + descent,
             os.path.basename(out)))
    return len(bits)


def build_gray(name, ttf_name, size, chars):
    """안티알리아싱 그대로 살려 4단계로 담는다. 한 픽셀 2비트."""
    path = os.path.join(FONT_DIR, ttf_name)
    font = ImageFont.truetype(path, size)
    ascent, descent = font.getmetrics()

    bits = bytearray()
    glyphs = []
    for ch in chars:
        adv = int(round(font.getlength(ch)))
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0, y1 - y0
        if w <= 0 or h <= 0:
            glyphs.append((ord(ch), len(bits), 0, 0, 0, 0, adv))
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        px = img.load()
        off = len(bits)
        stride = (w + 3) // 4            # 한 바이트에 픽셀 4개
        for yy in range(h):
            row = bytearray(stride)
            for xx in range(w):
                lv = (px[xx, yy] * 3 + 127) // 255      # 0..3
                if lv:
                    row[xx >> 2] |= lv << (6 - 2 * (xx & 3))
            bits += row
        glyphs.append((ord(ch), off, w, h, x0, y0, adv))

    glyphs.sort(key=lambda g: g[0])
    out = os.path.join(OUT_DIR, "font_%s.h" % name)
    with open(out, "w", encoding="utf-8") as f:
        f.write("// %s %dpx 회색 4단계. tools/build_fonts.py 로 자동 생성.\n"
                % (ttf_name, size))
        f.write("// 한 픽셀 2비트. 0 = 안 그림, 1 = 연한 회색, 2 = 진한 회색, 3 = 검정\n")
        f.write("// 한 줄은 ceil(w/4) 바이트, 위 비트부터.\n")
        f.write("#pragma once\n#include \"kfont.h\"\n\n")
        f.write("static const uint8_t %s_bits[] = {\n" % name)
        for i in range(0, len(bits), 16):
            f.write("  " + "".join("0x%02X," % b for b in bits[i:i + 16]) + "\n")
        f.write("};\n\n")
        f.write("static const KGlyph %s_glyphs[] = {\n" % name)
        for cp, off, w, h, dx, dy, adv in glyphs:
            f.write("  {0x%04X,%6d,%3d,%3d,%4d,%4d,%3d},\n"
                    % (cp, off, w, h, dx, dy, adv))
        f.write("};\n\n")
        f.write("static const KFontG %s = { %s_bits, %s_glyphs, %d, %d, %d };\n"
                % (name, name, name, len(glyphs), ascent, ascent + descent))
    print("%-10s %-26s %2dpx  글자 %3d  비트맵 %6d바이트  -> %s"
          % (name, ttf_name, size, len(glyphs), len(bits), os.path.basename(out)))
    return len(bits)


def main():
    chars = sorted(set("".join(STRINGS)))
    print("담을 글자 %d개: %s\n" % (len(chars), "".join(chars)))
    total = 0
    for name, ttf, size in FONTS:
        total += build(name, ttf, size, chars)
    print("\n흑백 비트맵 합계 %.1f KB" % (total / 1024.0))

    print("\n회색 4단계:")
    gtotal = 0
    for name, ttf, size in GRAY_FONTS:
        gtotal += build_gray(name, ttf, size, chars)
    print("\n회색 비트맵 합계 %.1f KB" % (gtotal / 1024.0))


if __name__ == "__main__":
    main()
