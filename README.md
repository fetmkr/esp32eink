# esp32eink

Seeed XIAO ESP32-S3 Plus + XIAO ePaper Display Board EE05
+ Good Display GDEY0426T82 (4.26인치, 800x480, 흑백, SSD1677)

FETM 아이덴티티 화면과 한글 렌더링 시험 화면. 초 단위 시계는 부분 갱신으로 돈다.

## 화면과 버튼

| 버튼 | 하는 일 |
|---|---|
| 1 | FETM 아이덴티티 (가운데 위에 시계) |
| 2 | 한글 렌더링 시험지 |
| 3 | 바탕 뒤집기 (흰 바탕 <-> 검은 바탕) |

시계가 한 칸 갈 때마다 보드 LED 가 짧게 반짝인다.

## 빌드와 올리기

    pio run
    pio run -t upload

## 시리얼 명령

| 글자 | 하는 일 |
|---|---|
| `0` `1` | 화면 고르기 |
| `d` | 화면 버퍼를 base64 로 뱉기 |
| `f` | 지금 즉시 전체 갱신 (잔상 지우기) |
| `m` | 갱신 방식 바꾸기 (전체 <-> 부분) |
| `i` | 바탕 뒤집기 |
| `p` | 시계 로그 켜고 끄기 |
| `l` | 버튼 눌린 기록 보기 |
| `G<숫자>` | 잔상 털기 간격 (부분 갱신 몇 번마다) |
| `B<16진수>` | 테두리 색 (`B0` 검정, `B1` 흰색, `B80` VCOM, `B` 자동) |
| `C<온도>` | 부분 갱신 파형을 가짜 온도로 고르기 |
| `N` `V` | 파형 되돌리기 / 직접 만든 파형 쓰기 |
| `T<초>` | 시계 맞추기 (1970년부터의 초) |

## 도구

| 파일 | 하는 일 |
|---|---|
| `tools/build_fonts.py` | Pretendard 에서 쓰는 글자만 뽑아 1비트 폰트 헤더 굽기 |
| `tools/png_to_header.py` | PNG 를 1비트 C 배열로 |
| `tools/header_to_png.py` | 그 배열을 다시 PNG 로 되살려 검산 |
| `tools/fb_capture.py` | 보드가 그린 화면을 받아 PNG 로 저장 |
| `tools/screen.py` | 화면 바꾸기 |
| `tools/sweep_temp.py` | 부분 갱신 파형을 온도로 바꿔가며 훑기 |

시계 맞추기:

    python3 -c "import serial,glob,time; s=serial.Serial(sorted(glob.glob('/dev/cu.usbmodem*'))[0],115200); time.sleep(4); s.write(('T%d\n'%int(time.time())).encode())"

## 문서

- `docs/NOTES.md` — 실측값과 하면 안 되는 것들. **먼저 읽을 것.**
- `docs/datasheet/` — SSD1677 컨트롤러 데이터시트
- `docs/gooddisplay_demo/` — 이 패널 제조사 공식 예제 코드
- `docs/schematic/` — EE05 보드 회로도

## 한글

Adafruit_GFX 의 폰트 구조체는 글자를 끊김 없는 한 구간으로만 담아서 한글을 못 넣는다.
그래서 쓰는 글자만 골라 담고 코드포인트로 이진 탐색하는 표를 따로 만들었다
(`src/kfont.h`, `tools/build_fonts.py`). 폰트는 Pretendard (SIL OFL).

문장을 새로 쓰려면 `tools/build_fonts.py` 의 `STRINGS` 에 넣고 다시 돌린다.
목록에 없는 글자를 쓰면 시리얼에 `[kfont] 빠진 글자 U+XXXX` 가 찍힌다.
