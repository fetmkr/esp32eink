#!/usr/bin/env python3
"""부분 갱신 파형을 온도로 바꿔가며 훑는다.

칩 안에는 제조사가 넣은 파형이 34벌 있고 온도로 고른다 (데이터시트 6.9절).
온도가 낮을수록 더 길고 센 파형이 배정된다.
화면 시계 오른쪽에 지금 쓰는 값이 같이 찍히니, 보이는 것과 값을 바로 짝지을 수 있다.
"""
import glob, sys, time, serial

HOLD = float(sys.argv[1]) if len(sys.argv) > 1 else 8.0
TEMPS = [40, 25, 10, 0, -10, -20]

s = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=1)
time.sleep(8)
s.reset_input_buffer()
s.write(("T%d\n" % int(time.time())).encode()); s.flush()
time.sleep(4); s.reset_input_buffer()

for t in TEMPS:
    print("\n===== 가짜온도 %d도 (%.0f초 동안) =====" % (t, HOLD))
    s.write(("C%d\n" % t).encode()); s.flush()
    t0 = time.time()
    while time.time() - t0 < HOLD:
        l = s.readline().decode("utf-8", "replace").rstrip()
        if "[시계" in l or "파형 ->" in l:
            print(l)
print("\n===== 내가 만든 파형 =====")
s.write(b"V"); s.flush()
t0 = time.time()
while time.time() - t0 < HOLD:
    l = s.readline().decode("utf-8", "replace").rstrip()
    if "[시계" in l or "파형 ->" in l:
        print(l)
print("\n===== 기본(실온) 으로 되돌림 =====")
s.write(b"N"); s.flush()
time.sleep(2)
s.close()
