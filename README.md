# E-ray DOS OPL Emulator (EOPL)

**사운드카드 없는 DOS PC에서 AdLib(OPL2) 음악을 PC 스피커로 재생하는 소프트웨어 사운드카드 (TSR)**

> *English summary:* EOPL is a 16-bit real-mode DOS TSR that traps the AdLib ports (388h/389h) through QEMM's I/O
> virtualization (QPI), synthesizes OPL2 music in software (DOSBox's DBOPL engine, ported to 16-bit, integer-only) and plays it
> on the PC speaker using PIT channel 2 PWM. No sound card required. **Status: experimental (alpha).**

게임이 AdLib 칩(YM3812/OPL2)에 쓰는 레지스터 접근을 가로채 소프트웨어로 FM 합성을 하고, 결과를 PC 내장 스피커(PIT 채널 2, mode 0 PWM)로 출력합니다.
상주 메모리를 줄이는 것을 최우선 목표로 한 16비트 실모드 TSR입니다.

```
 DOS 게임 ──(AdLib 포트 388h/389h)──▶ QEMM QPI 트랩 ──▶ OPL2 에뮬레이션(DBOPL, 16bit 정수)
                                                         │ PCM
                                                         ▼
                              샘플 클록 ISR (RTC 8192Hz 또는 PIT ch0)  ──▶ PIT ch2 PWM ──▶ PC 스피커
```

## 현재 상태 (Status)

| 항목 | 상태 |
|---|---|
| 동작 확인 환경 | **86Box** (Pentium II OverDrive 266) + 순수 DOS + **QEMM** |
| 확인된 게임 | 페르시아의 왕자 1, 대항해시대 2, 그날이 오면 — AdLib 음악 재생 (음질은 아래 참고) |
| 메모리 관리자 | QEMM 지원. **EMM386 은 아직 미지원** (계획 중) |
| 칩 | **OPL2 전용** (OPL3 는 목표 아님) |
| 상주 크기 | 약 45KB (conventional). 감축 작업 예정 |
| 실기(진짜 하드웨어) | **미검증** — 86Box 에서만 시험했습니다 |
| Pentium 90 | 합성이 느려 실용 어려움 (Pentium II 233 이상 권장, 아직 실기/VM 측정 중) |

### 알려진 한계

- **음질**: 8kHz급 샘플링과 PWM 해상도(약 7비트) 때문에 소리가 약간 뭉개지거나 "울먹이는" 느낌이 남을 수 있습니다. 개선 작업 중입니다.
- **프린세스메이커 2**: 상주 크기(약 45KB) 때문에 conventional 메모리가 모자라 실행되지 않습니다. UMB 로 올리면(`LOADHI`) 해결될 가능성이 있으나 미검증입니다.
- **PIT 클록 모드(`/PIT`, `/R:n`)**: 게임이 PIT 채널 0 을 재설정하면 소리가 나지 않습니다. 그래서 기본값은 RTC 클록입니다(대역폭 약 4kHz).
- **Sound Blaster FM 포트(2x8h/2x9h)** 는 아직 가로채지 않습니다. 게임에서 **AdLib** 을 선택하세요.
- 게임이 PC 스피커를 직접 쓰는 경우, 스피커 게이트/PIT ch2 를 되돌리는 "가드"가 동작합니다 (효과음과 동시 사용은 불가).

## 요구 사항

- MS-DOS / Windows 98 DOS 모드 (순수 DOS)
- **QEMM386 7.03 이상** (QPI 를 지원하는 버전). EMM386 은 아직 지원하지 않습니다
- 386 이상 (FPU 불필요 — 엔진에서 부동소수점을 제거했습니다). 권장: Pentium II 233MHz 이상
- PC 스피커

## 사용법

```
EOPL                설치 (기본: RTC 8192Hz 클록, QEMM 으로 AdLib 포트 가로채기)
EOPL /S             상태 보기 (쓰기 횟수, 언더런, 가드, IRQ 속도 등)
EOPL /D             레지스터 사용 분포 + 최근 쓰기 로그
EOPL /U             제거 (메모리 반환, 재부팅 불필요)
GAME.EXE            게임에서 사운드카드를 AdLib 으로 선택
```

상위 메모리(UMB)에 올리려면 (QEMM 이 UMB 를 확보한 경우):

```
LOADHI EOPL
```

### 옵션

| 옵션 | 설명 |
|---|---|
| `/RTC` | RTC 주기 인터럽트(IRQ8, 8192Hz)를 샘플 클록으로 사용. **기본값** — PIT 채널 0 / INT 8 을 건드리지 않음 |
| `/PIT` | PIT 채널 0(IRQ0)을 샘플 클록으로 사용, 샘플레이트 자동 선택(설치 시 CPU 벤치) |
| `/R:rate` `/R:AUTO` | PIT 클록의 샘플레이트 지정(8000 / 11025 / 16000 / 22050) 또는 자동 |
| `/Y:n` | 합성 서브블록 크기(샘플). 블록 사이에 인터럽트 창을 열어 샘플 타이밍 지터를 줄임. 기본 16, `0` = 끔 (RTC 전용) |
| `/C:n` `/W:n` | ISR 합성 청크 크기 / 링버퍼 저수위 (고급) |
| `/G:n` | 고정 소프트웨어 게인 ×n (1~16, 기본 ×1) |
| `/NOPATCH` | 상태 포트 `IN AL,DX` 런타임 NOP 패치 끔 |
| `/NOTRAP` | 포트 가로채기 없이 상주 (API/시험 전용) |
| `/DIAG` | 진단: Sound Blaster 포트(220h-22Fh) 접근 기록, PIT 채널 0 재설정 감시 |

제거(`/U`)는 인터럽트 벡터를 다른 프로그램이 다시 후킹했다면 안전을 위해 거절합니다.

## 동작 원리 (요약)

- **포트 가로채기**: QEMM 의 QPI(I/O 포트 트랩, V86 콜백)로 388h/389h 를 가로챕니다. 주소 래치, 레지스터 쓰기, 타이머 상태 읽기(게임의
  AdLib 감지 통과)를 에뮬레이션하고, 상태 포트를 읽는 `IN AL,DX` 지연 루프를 `NOP` 으로 런타임 패치해 트랩 비용을 줄입니다.
- **합성**: DOSBox 의 DBOPL 을 16비트(Open Watcom `-mm -3`)로 포팅했습니다. 빌드 시 업스트림 소스를 패치하며
  (`tools/gen_dbopl16.py`), 부동소수점은 빌드 시 생성한 상수표로 대체했습니다. 32비트/FP 빌드와 **PCM 이 비트 단위로 동일**함을 검증합니다.
- **출력**: PIT 채널 2 를 mode 0 으로 쓰는 PWM. 모든 합성은 샘플 클록 ISR 안에서 일어나며(전용 스택, 작은 서브블록 사이에 인터럽트 창),
  BIOS 18.2Hz 시계는 보존됩니다. 게임이 스피커 게이트(포트 61h)나 PIT ch2 를 바꾸면 ISR 이 되돌립니다.
- **제어 API**: INT 2Fh 멀티플렉스(서명 `EOPL`)로 상태 조회, 제거, 시험용 레지스터 쓰기를 제공합니다.

자세한 설계 근거, 실측 수치, 시행착오는 [`docs/PROGRESS.md`](docs/PROGRESS.md) 에 있습니다.

## 빌드

Windows 에서 PowerShell 로 빌드하는 DOS 크로스 개발 환경입니다.

필요 도구:

- **Open Watcom 1.9** (순정, 기본 경로 `C:\WATCOM19`; `-Watcom` 인자로 변경) — 2.0 베타 혼합 설치는 헤더 오류가 납니다
- **Python 3**
- **MinGW g++ (32bit 가능)** — 엔진 상수표 생성(`tools/gen_tables.py`)과 호스트 검증용
- **DOSBox-X** (자동 시험용, `C:\DOSBox-X`)
- **Git**

```powershell
# 1. 업스트림 참조 소스 받기 (ref/, 수정하지 않고 빌드 때 패치)
powershell -ExecutionPolicy Bypass -File tools\fetch_refs.ps1

# 2. 빌드: 엔진 3변형 + eopl.exe + 시험 프로그램
powershell -ExecutionPolicy Bypass -File tools\build_eopl.ps1      # 결과: build16nf\eopl.exe

# 3. 엔진 변형 간 PCM 해시 동일성 검증 (DOSBox-X)
powershell -ExecutionPolicy Bypass -File tools\test_variants.ps1
```

DOS 파일 이름은 8.3 형식이어야 합니다. Open Watcom 도구(`wcc`, `wpp`, `wlink`, `wasm`)는 PowerShell 에서 인자가 깨지므로 스크립트가 `cmd /c` 로 호출합니다.

### 시험

| 시험 | 설명 |
|---|---|
| `tools\test_variants.ps1` | 엔진 3변형(FP / OPL2 / OPL2+FP제거)의 PCM 해시 동일성, 지원 샘플레이트 전체 |
| `test/blktest.c` | 합성 블록 크기(4/8/32/전체)와 무관하게 PCM 동일한지 |
| `test/eopltest.c` | API 클라이언트(`eopltest <초>`), 트랩 자체시험(`eopltest trap`), PIT0 재설정 감지(`eopltest pit`) |
| `test/adlplay.c` | 게임처럼 AdLib 포트를 직접 접근하는 연주 프로그램 |
| `test/winwr.c`, `test/spkbreak.c` | 타이머 인터럽트에서의 OPL 쓰기, 게임이 스피커/PIT ch2 를 건드리는 상황 모사 |
| `test/amptest.c` | 엔진 출력 진폭 측정 (PWM 단계 활용도) |

DOSBox-X 에는 QEMM 이 없으므로 **QEMM 포트 가로채기와 실제 음질은 86Box(또는 실기)에서만** 확인할 수 있습니다.
시험 시 `/NOTRAP` 으로 설치하고 `eopltest`/`adlplay` 로 API 경로를 시험합니다.

## 저장소 구성

```
src/          TSR 본체 (eopl.c, pcspk16.c, trapglue.asm, pcspk.h, opl_core.h, dbopl_core.cpp)
              ※ pcspk.c, opl_core.cpp, opal_core.c, rt_stubs.cpp 는 초기 프로토타입/비교용(32bit DOS/4GW, YMFM, Opal)
tools/        빌드·생성 스크립트 (엔진 패치, 상수표 생성, 시뮬레이터)
test/         시험 프로그램
docs/         PROGRESS.md (개발 기록), HANDOFF.md (인수인계 요약), NEW_CHAT_PROMPT.md (AI 어시스턴트와 작업을 이어갈 때의 프롬프트)
ref/          업스트림 참조 소스 (git 에 포함하지 않음, tools\fetch_refs.ps1 로 받음)
```

## 로드맵

- [ ] 음질 개선 (샘플 타이밍 지터, 에일리어싱, PWM 해상도)
- [ ] EMM386 지원 (I/O 가상화 콜백이 보호모드에서 실행될 가능성 — 탐사 필요)
- [ ] 상주 메모리 감축 (목표 약 30KB), UMB 적재 시험
- [ ] PIT 채널 0 가상화로 16kHz 이상 샘플레이트 사용 (게임이 PIT 를 재설정해도 동작)
- [ ] Sound Blaster FM 포트(2x8h/2x9h) 지원
- [ ] 실기 시험, 게임 호환성 목록

## 라이선스

이 프로젝트는 **GNU GPL v3** 입니다 ([`LICENSE`](LICENSE)). 사용한 외부 코드와 출처는 [`THIRD_PARTY.md`](THIRD_PARTY.md) 를 참고하세요.

## 감사

- **ADLCOM** (Peter De Wachter, Jose Phillips, Electric-ray) — QEMM/EMM386 I/O 가로채기와 AdLib 에뮬레이션 설계의 기반
- **DOSBox Team** — DBOPL OPL2/OPL3 에뮬레이터
- **VSBHDA** (Baron-von-Riedesel) — DBOPL 사본, ISR 안에서 청크 합성하는 구조의 참고
- **hearo**, **SBEMU**, **YMFM**, **Opal** — PC 스피커 출력 설계 및 엔진 비교 참고

*Electric-ray — Bringing OPL Sound Back to DOS PCs.*
