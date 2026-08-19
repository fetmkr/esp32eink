#!/usr/bin/env python3
"""헤더에 박힌 1비트 배열을 다시 PNG로 되살려 눈으로 확인한다."""
import re, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
w = int(re.search(r"_WIDTH\s+(\d+)", t).group(1))
h = int(re.search(r"_HEIGHT\s+(\d+)", t).group(1))
body = t[t.index("{"):t.rindex("}")]
data = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)]
stride = (w + 7) // 8
im = Image.new("1", (w, h), 0)
px = im.load()
for y in range(h):
    for x in range(w):
        if data[y * stride + x // 8] & (1 << (7 - x % 8)):
            px[x, y] = 1
# 비트1=잉크 이므로 반전해서 저장 (검정 잉크로 보이게)
Image.eval(im.convert("L"), lambda v: 255 - v).save(dst)
print("%s -> %s  %dx%d" % (src, dst, w, h))
