#!/usr/bin/env python3
"""보드에 'd' 를 보내 화면 버퍼를 받아 PNG 로 복원한다.

포트를 여는 순간 보드가 리셋될 수 있어서, 'd' 를 주기적으로 계속 보내며
---FB-BEGIN--- 이 나올 때까지 기다린다.
"""
import base64, glob, sys, time
import serial
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else "frame.png"
pre = sys.argv[2] if len(sys.argv) > 2 else ""   # 덤프 전에 보낼 명령 (예: "1")
timeout_s = float(sys.argv[3]) if len(sys.argv) > 3 else 40.0

port = sorted(glob.glob("/dev/cu.usbmodem*"))[0]
ser = serial.Serial(port, 115200, timeout=0.5)
# 포트를 여는 순간 보드가 리셋된다. 부팅이 끝날 때까지 기다린 뒤 명령을 보낸다.
time.sleep(3.0)
ser.reset_input_buffer()
if pre:
    ser.write(pre.encode()); ser.flush()
    time.sleep(4.0)          # 화면 새로 그리는 시간
    ser.reset_input_buffer()

w = h = n = None
bpp = 1
b64 = []
done = False
t0 = time.time()
last_poke = 0.0
while time.time() - t0 < timeout_s and not done:
    if w is None and time.time() - last_poke > 1.5:
        ser.write(b"d"); ser.flush()
        last_poke = time.time()
    line = ser.readline().decode("utf-8", "replace").strip()
    if not line:
        continue
    if line.startswith("---FB-BEGIN") or line.startswith("---FB2-BEGIN"):
        bpp = 2 if line.startswith("---FB2") else 1
        p = line.replace("-", " ").split()
        w, h, n = int(p[2]), int(p[3]), int(p[4])
        b64 = []
    elif line.startswith("---FB-END") or line.startswith("---FB2-END"):
        if w is not None:
            done = True
    elif w is not None:
        b64.append(line)
ser.close()

if not done:
    print("덤프 못 받음 (w=%s, 받은 줄 %d)" % (w, len(b64)))
    sys.exit(1)

data = base64.b64decode("".join(b64))
print("받음: %dx%d, %d bytes (기대 %d)" % (w, h, len(data), n))
if len(data) != n:
    print("!! 크기 불일치")
    sys.exit(1)

if bpp == 1:
    stride = (w + 7) // 8
    im = Image.new("L", (w, h), 255)
    px = im.load()
    ink = 0
    for y in range(h):
        row = data[y * stride:(y + 1) * stride]
        for x in range(w):
            if row[x // 8] & (1 << (7 - x % 8)):
                px[x, y] = 0            # 비트 1 = 검정 잉크
                ink += 1
    im.save(out)
    print("저장: %s   잉크 %.1f%%" % (out, 100.0 * ink / (w * h)))
else:
    # 회색 4단계. 한 바이트에 픽셀 4개, 위 비트부터. 3 = 흰색, 0 = 검정.
    LEVEL = [0, 96, 176, 255]
    stride = (w + 3) // 4
    im = Image.new("L", (w, h), 255)
    px = im.load()
    hist = [0, 0, 0, 0]
    for y in range(h):
        row = data[y * stride:(y + 1) * stride]
        for x in range(w):
            lv = (row[x >> 2] >> (6 - 2 * (x & 3))) & 0x03
            px[x, y] = LEVEL[lv]
            hist[lv] += 1
    im.save(out)
    tot = float(w * h)
    print("저장: %s   검정 %.1f%% 진회 %.1f%% 연회 %.1f%% 흰색 %.1f%%"
          % (out, 100 * hist[0] / tot, 100 * hist[1] / tot,
             100 * hist[2] / tot, 100 * hist[3] / tot))
