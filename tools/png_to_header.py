#!/usr/bin/env python3
"""PNG -> 1비트 C 헤더. MSB first, 행마다 바이트 경계 정렬 (Adafruit_GFX drawBitmap 형식).
비트 1 = 잉크(검정)."""
import sys
from PIL import Image

src, dst, name, size = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
GRAY = len(sys.argv) > 5 and sys.argv[5] == "gray"

im = Image.open(src)
# 투명 배경은 흰색으로 깔아준다
if im.mode in ("RGBA", "LA", "P"):
    im = im.convert("RGBA")
    bg = Image.new("RGBA", im.size, (255, 255, 255, 255))
    im = Image.alpha_composite(bg, im)
im = im.convert("L").resize((size, size), Image.LANCZOS)

w, h = im.size
px_l = im.load()
ink = 0
if GRAY:
    # 회색 4단계. 한 픽셀 2비트, 한 바이트에 4개, 위 비트부터.
    # 값은 잉크 진하기다. 0 = 안 그림, 3 = 검정.
    stride = (w + 3) // 4
    data = bytearray()
    for y in range(h):
        row = bytearray(stride)
        for x in range(w):
            lv = ((255 - px_l[x, y]) * 3 + 127) // 255
            if lv:
                row[x >> 2] |= lv << (6 - 2 * (x & 3))
                ink += 1
        data += row
else:
    # 임계값 이분화. 128보다 어두우면 잉크
    bw = im.point(lambda p: 255 if p < 128 else 0, mode="1")
    stride = (w + 7) // 8
    px = bw.load()
    data = bytearray()
    for y in range(h):
        for bx in range(stride):
            b = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and px[x, y]:
                    b |= 1 << (7 - bit)
                    ink += 1
            data.append(b)

with open(dst, "w") as f:
    f.write("// %s 에서 자동 생성. 수정하지 말 것.\n" % src)
    f.write("// %dx%d, %s, 위 비트부터\n" % (w, h,
            "2bpp 회색 4단계 (0 = 안 그림, 3 = 검정)" if GRAY else "1bpp, 비트 1 = 검정"))
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    f.write("#define %s_WIDTH  %d\n" % (name.upper(), w))
    f.write("#define %s_HEIGHT %d\n\n" % (name.upper(), h))
    f.write("const uint8_t %s[] PROGMEM = {\n" % name)
    for i in range(0, len(data), 16):
        f.write("  " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",\n")
    f.write("};\n")

print("%s -> %s  %dx%d  %d bytes  잉크픽셀 %d/%d (%.1f%%)"
      % (src, dst, w, h, len(data), ink, w * h, 100.0 * ink / (w * h)))
