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

빌드 환경이 둘이다. 흑백용 라이브러리와 회색용 라이브러리가 클래스 이름이
겹쳐서 한 펌웨어에 같이 못 넣는다.

    pio run -e xiao_ee05 -t upload    # 흑백. 시계, 한글 시험지, 버튼
    pio run -e gray4 -t upload        # 회색 4단계 배경 + 흑백 시계와 항적

### gray4 환경

배경은 회색 4단계로 곱게 그리고, 시계와 항적처럼 움직이는 자리만 흑백
부분 갱신으로 민다.

| 버튼 | 하는 일 |
|---|---|
| 1 | FETM 화면 (시계와 항적이 돈다) |
| 2 | 한글 시험지 (회색) |
| 3 | 바탕 뒤집기 |

| 항목 | 실측 |
|---|---|
| 회색 배경 다시 그리기 | 4.4초 |
| 시계만 부분 갱신 | 0.41초 |
| 시계 + 항적 한 번에 | 0.42초 |

**어떻게 되는지는 `docs/NOTES.md` 를 볼 것.** 함정이 하나 있다.

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
