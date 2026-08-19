#!/usr/bin/env python3
"""화면 바꾸기.  사용법:  python3 tools/screen.py 0   또는   1"""
import glob, sys, time, serial

which = sys.argv[1] if len(sys.argv) > 1 else "0"
port = sorted(glob.glob("/dev/cu.usbmodem*"))[0]
ser = serial.Serial(port, 115200, timeout=1)
time.sleep(3.0)                      # 포트를 열면 보드가 리셋된다. 부팅 기다림
ser.reset_input_buffer()
ser.write(which.encode()); ser.flush()
t0 = time.time()
while time.time() - t0 < 12:
    l = ser.readline().decode("utf-8", "replace").rstrip()
    if l and not l.startswith("---") and len(l) < 90:
        print(l)
    if "panel push" in l:
        break
ser.close()
