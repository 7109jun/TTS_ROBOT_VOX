# ROBOT VOX 사용 가이드

C17로 처음부터 구현한 포먼트 음성 합성(TTS) + 로봇 보컬 변환기입니다. 외부 TTS, 사전학습 모델, 녹음된 음소 데이터, 외부 API를 쓰지 않으며 libc/libm만 사용합니다. 영어와 한국어를 지원합니다.

## 실행 파일
`bin/` 폴더에 빌드된 실행 파일이 있습니다. 설치 없이 바로 실행할 수 있습니다.
- Windows x64: `bin/robotvox.exe`
- Linux x64: `bin/robotvox-linux-x64` (`chmod +x` 후 실행)

## 빌드
```
make                              # Linux / MinGW
cmake -B build && cmake --build build
make test                         # DSP 자체 테스트
```
Windows 실행 파일은 Zig(`zig cc -target x86_64-windows-gnu ... -lshell32 -lwinmm`)로도 빌드할 수 있습니다.

## 명령
```
robotvox speak   "텍스트"  -o out.wav        # 텍스트 -> 로봇 음성
robotvox sing    "가사" --melody "C3:1 E3:1 G3:1 C4:2" --bpm 120 -o out.wav
robotvox convert in.wav -o out.wav           # WAV -> 로봇 음성
robotvox live --carrier follow               # 마이크 -> 로봇 음성 (Ctrl+C 종료, 헤드폰 권장)
robotvox phonemes "텍스트"                   # 규칙 기반 발음 확인
```
한국어 입력은 `--lang ko`를 붙이면 숫자도 한국어로 읽습니다. 모든 옵션은 `robotvox --help`로 확인합니다.

## 프리셋 (`--preset`)
`harder`(= `daft`, 기본값), `classic`, `choir`, `clean`

## 주요 옵션
| 옵션 | 설명 |
|---|---|
| `--consonant X` | 자음 선명도 (1 = 기본, 3 = 3배) |
| `--power X` | 목소리 힘 0~2 (1 = 기본, 작으면 부드럽고 크면 팽팽함) |
| `--key A --scale pent` | 음계 고정 (major, minor, pent, majpent, chromatic) |
| `--grid BPM` | 음절 길이를 16분음표 격자에 맞춤 |
| `--chord "7,12"` | 보코더 반송파에 얹을 화음 (반음 간격) |
| `--harmony "7,12"` | 피치 시프트한 추가 목소리 |
| `--pitch-shift ST` | 전체 음높이 이동 (반음) |
| `--ringmod HZ` `--bitcrush BITS` `--chorus MIX` `--delay MS` | 효과 |

## 구조
`text.c`(UTF-8, 숫자) → `g2p_en.c`/`g2p_ko.c`(규칙 기반 발음) → `prosody.c`(길이, 억양, 음계, 노래) → `synth.c`(성문 펄스 + 포먼트 공명기 + 노이즈) → `dsp_*.c`(보코더, 위상 보코더 피치 시프터, 링모드, 비트크러셔, 코러스, 딜레이) → `audio.c`/`live.c`(실시간)

## 알려진 한계
- 영어 발음은 규칙 기반에 짧은 불규칙 단어 표를 더한 방식이라 특이한 단어는 틀릴 수 있습니다.
- 한국어 음운 규칙은 단어 안에서만 적용되고 띄어쓰기를 넘지 않습니다.
- `live`는 보코더, 링모드, 비트크러셔, 드라이브만 지원합니다. ALSA(Linux)와 winmm(Windows) 장치 입출력은 실제 기기에서 충분히 검증되지 않았습니다.
