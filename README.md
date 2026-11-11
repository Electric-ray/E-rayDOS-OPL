# E-ray DOS OPL Emulator (EOPL)

**사운드카드 없는 DOS PC에서 AdLib(OPL2) 음악을 PC 스피커로 재생하는 소프트웨어 사운드카드 (TSR)**

> *English summary:* EOPL is a 16-bit real-mode DOS TSR that traps the AdLib ports (388h/389h) through QEMM's I/O
> virtualization (QPI), synthesizes OPL2 music in software (DOSBox's DBOPL engine, ported to 16-bit, integer-only) and plays it
> on the PC speaker through PIT channel 2 (PWM, or an experimental pulse-density mode that is much quieter in noise on real laptops).
> No sound card required. **Status: experimental (alpha).** Tested on 86Box and one real laptop (Samsung Sens P30, Pentium M, QEMM 9.0).

게임이 AdLib 칩(YM3812/OPL2)에 쓰는 레지스터 접근을 가로채 소프트웨어로 FM 합성을 하고, 결과를 PC 내장 스피커(PIT 채널 2)로 출력합니다.
상주 메모리를 줄이는 것을 최우선 목표로 한 16비트 실모드 TSR입니다.

```
 DOS 게임 ──(AdLib 포트 388h/389h)──▶ QEMM QPI 트랩 ──▶ OPL2 에뮬레이션(DBOPL, 16bit 정수)
                                                         │ PCM
                                                         ▼
                  샘플 클록 ISR (RTC 8192Hz 또는 PIT ch0) ──▶ PIT ch2 (PWM 또는 펄스 밀도) ──▶ PC 스피커
```

## 현재 상태 (Status)

| 항목 | 상태 |
|---|---|
| 동작 확인 환경 | **86Box**(Pentium II OverDrive 266, QEMM) 와 **실기**(삼성 Sens P30, 센트리노 노트북, 순수 DOS, QEMM 9.0) |
| 확인된 게임 | 페르시아의 왕자 1, 대항해시대 2, 그날이 오면 — AdLib 음악 재생 (86Box) |
| 메모리 관리자 | QEMM 지원. **EMM386 은 아직 미지원** (계획 중) |
| 칩 | **OPL2 전용** (OPL3 는 목표 아님) |
| 상주 크기 | 약 48.5KB (conventional). 감축 작업 예정 |
| 음질 | 실기에서 소음 원인을 분리해 **`/PDM` 모드로 틱·고주파를 크게 줄임**(아래 "소리 품질 가이드"). 음량은 프리셋/게인으로 조정 필요 |
| Pentium 90 | 합성이 느려 실용 어려움 (Pentium II 233 이상 권장) |

### 알려진 한계

- **음량과 노이즈의 균형**: `/PDM`(펄스 밀도) 모드는 노이즈가 가장 적지만 기본 프리셋은 음량이 작습니다. 프리셋(`/PDM:4`~`6`)과 `/G` 로 올리면 음량은 커지지만 큰 소리에서 단계가 거칠어집니다. 기기마다 최적값이 다를 수 있습니다.
- **실기 검증은 한 대뿐**: 다른 노트북/데스크톱, 스피커 직결 PC 에서는 아직 시험하지 못했습니다. 특히 `/PDM` 은 0.84µs 의 매우 좁은 펄스를 쓰므로 PC 스피커 경로(코덱 경유 여부 등)에 따라 음량이 달라질 수 있습니다.
- **프린세스메이커 2**: 상주 크기(약 48.5KB) 때문에 conventional 메모리가 모자라 실행되지 않습니다. UMB 로 올리면(`LOADHI`) 해결될 가능성이 있으나 미검증입니다.
- **PIT 클록 모드(`/PIT`, `/R:n`)**: 소리는 좋지만 게임이 PIT 채널 0/INT 8 을 자기 타이밍으로 쓰면 **게임 속도가 극단적으로 느려지거나 빨라집니다**. 일반 게임에는 기본값(RTC)을 쓰세요.
- **Sound Blaster FM 포트(2x8h/2x9h)** 는 아직 가로채지 않습니다. 게임에서 **AdLib** 을 선택하세요.
- 게임이 PC 스피커를 직접 쓰는 경우, 스피커 게이트/PIT ch2 를 되돌리는 "가드"가 동작합니다 (효과음과 동시 사용은 불가).
- 노트북의 **USB Legacy(SMI)** 가 주기적으로 CPU 를 0.4ms 정도 멈춰 PWM 모드에서 "틱" 소리를 냅니다 (아래 참고).

## 요구 사항

- MS-DOS / Windows 98 DOS 모드 (순수 DOS)
- **QEMM386 7.03 이상** (QPI 를 지원하는 버전). EMM386 은 아직 지원하지 않습니다
- 386 이상 (FPU 불필요 — 엔진에서 부동소수점을 제거했습니다). 권장: Pentium II 233MHz 이상 (실기 센트리노 1GHz 급에서는 CPU 의 약 10% 사용)
- PC 스피커 (노트북은 내부 스피커/PC beep 경로)

## 사용법

```
EOPL [옵션]         설치 (기본: RTC 8192Hz 클록, QEMM 으로 AdLib 포트 가로채기)
EOPL /S             상태 보기 (쓰기 횟수, 언더런, 클리핑, 가드, IRQ 속도 등)
EOPL /D             레지스터 사용 분포 + 최근 쓰기 로그
EOPL /U             제거 (메모리 반환, 재부팅 불필요)
GAME.EXE            게임에서 사운드카드를 AdLib 으로 선택
```

상위 메모리(UMB)에 올리려면 (QEMM 이 UMB 를 확보한 경우):

```
LOADHI EOPL [옵션]
```

### 권장 시작 설정

처음에는 아래 순서로 시도해 보고, 소리를 듣고 조정하세요 (설정마다 `EOPL /U` 로 제거하면 재부팅이 필요 없습니다).

| 상황 | 명령 | 설명 |
|---|---|---|
| 노이즈 최소(권장 시작점) | `EOPL /NEST /PDM:4 /G:6` | 펄스 밀도 출력 + 중첩 합성. 음량이 작으면 `/PDM:5 /G:4`, `/PDM:6 /G:3` 순으로 |
| 호환성 우선 / 기본 | `EOPL` | RTC 8192Hz PWM. 86Box 에서는 깨끗하지만 실기에서는 고주파가 들림 |
| PWM 으로 틱만 줄이기 | `EOPL /NEST /B:18 /G:4` | 레일 바이어스. `/B` 가 작을수록 틱이 줄지만 헤드룸도 줄어듦 |
| 게임 속도가 정상인 게임 + 최고 음질 | `EOPL /PIT /NEST` | PIT 클록 16~22kHz. 게임 속도 문제가 있으면 사용하지 마세요 |

> 위 `/PDM` 설정 값은 시험용 톤과 한 대의 실기에서 들어본 결과를 바탕으로 한 **출발점**입니다. 게임 음악의 음량/클리핑은 `EOPL /S` 의 `clipped` 와 직접 청취로 확인하세요.

### 옵션 (자세히)

#### 샘플 클록 선택

| 옵션 | 설명 |
|---|---|
| `/RTC` | RTC 주기 인터럽트(IRQ8, **8192Hz**)를 샘플 클록으로 사용. **기본값**. PIT 채널 0 / INT 8 을 건드리지 않으므로 게임 속도에 영향이 없습니다. 8192Hz 는 RTC 의 최대 속도이고 대역폭은 약 4kHz 입니다. PWM 에서는 8kHz 반송파가 들릴 수 있습니다(→ `/PDM`) |
| `/PIT` | PIT 채널 0(IRQ0)을 샘플 클록으로 사용, 샘플레이트 자동 선택(설치 시 CPU 벤치로 최악 9채널 합성이 CPU 약 47% 이하인 최고 레이트). 소리는 가장 좋지만 **게임이 PIT ch0 을 쓰면 게임 속도가 망가집니다** |
| `/R:rate` `/R:AUTO` | PIT 클록의 샘플레이트 지정(8000 / 11025 / 16000 / 22050) 또는 자동 |
| `/NOFIX` | PIT 클록에서 게임이 PIT ch0 을 재설정하면 자동으로 되돌리는 기능(기본 켜짐)을 끔 |

#### 출력 방식 (노이즈/음량)

| 옵션 | 설명 |
|---|---|
| (기본) | **PWM**: PIT ch2 모드 0, 샘플마다 LOW 시간(count)을 써서 듀티로 진폭을 표현. 반송파 = 샘플 클록(RTC 에서는 8192Hz) |
| `/B:n` | **PWM 레일 바이어스**: 무음을 듀티 50% 가 아니라 HIGH 레일 근처(LOW n틱)에 둡니다. 풀스케일 ±n틱. CPU 가 멈출 때(SMI 등) 출력이 HIGH 에 굳으면서 나는 "틱" 클릭과 반송파가 줄어듭니다. 대신 헤드룸이 줄어 `/G` 와 함께 써야 합니다(예: `/B:18 /G:4`, `/B:10 /G:7`). n 은 1~120. `/PDM` 과 함께 쓰면 무시됩니다 |
| `/PDM[:1..6]` | **펄스 밀도 변조(실험)**: PIT ch2 를 모드 2(rate generator)로 돌려 하드웨어가 N 틱마다 1틱 폭 LOW 펄스를 스스로 냅니다. 샘플 IRQ 는 N 만 바꿔 씁니다. 평균 레벨 ≈ 1/N, 반송파 = 1193182/N Hz (≥15kHz → 가청 밖). CPU 가 멈춰도 마지막 N 으로 펄스가 계속 나가므로 틱이 생기지 않습니다. 샘플당 CPU 비용은 PWM 과 같습니다. `/G` 미지정 시 ×4 |
| `/G:n` | 고정 소프트웨어 게인 ×n (1~16, 기본 ×1). 50% 풀스케일 위는 기울기 1/2 인 소프트 니. `EOPL /S` 의 `clipped` 가 크면 낮추세요 |

`/PDM` 프리셋 (N 범위가 좁을수록 조용하고 해상도 좋음, 넓을수록(작은 N 포함) 음량 큼):

| 프리셋 | N 범위 | 스윙(대략) | 비고 |
|---|---|---|---|
| `/PDM:1` (기본) | 20..75 | ±1.9% | 가장 깨끗하지만 음량 작음 |
| `/PDM:2` | 16..48 | 더 큼, 반송파 25~75kHz | |
| `/PDM:3` | 28..75 | 작음 | 고주파가 가장 적음 |
| `/PDM:4` | 8..75 | 약 3배 (+9dB) | 음량 부족 시 첫 선택 |
| `/PDM:5` | 5..75 | 약 5배 (+14dB) | |
| `/PDM:6` | 3..75 | 약 8배 (+18dB) | 큰 소리에서 단계가 거칠어짐 |

#### 타이밍 / 합성 구조

| 옵션 | 설명 |
|---|---|
| `/Y:n` | 합성 서브블록 크기(샘플). 블록 사이에 인터럽트 창을 열어 샘플 타이밍 지터를 줄임. 기본 16, `0` = 끔 (RTC 전용) |
| `/NEST` | **실험적, 권장**: 합성을 인터럽트 허용 상태(IF=1)로 수행해 샘플 IRQ 가 수백 ns 안에 끼어들게 함(지터 최소화). 중첩 핸들러의 OPL 쓰기는 큐(128개)에 모았다가 블록 경계에서 적용. 전용 스택(1280B) 사용량이 커지며(`/S` 의 `ISR stack used`), DOS/4GW 등 확장기 게임에서의 안정성은 충분히 확인되지 않았습니다 |
| `/C:n` `/W:n` | ISR 합성 청크 크기 / 링버퍼 저수위 (고급) |

#### 포트 가로채기 / 진단

| 옵션 | 설명 |
|---|---|
| `/NOPATCH` | 상태 포트 `IN AL,DX` 런타임 NOP 패치 끔 |
| `/NOTRAP` | 포트 가로채기 없이 상주 (API/시험 전용, QEMM 없이도 설치 가능) |
| `/DIAG` | 진단: Sound Blaster 포트(220h-22Fh) 접근 기록, PIT 채널 0 재설정 감시 |
| `/S` | 상태 출력. 주요 항목: `underruns`(0 이어야 함), `measured IRQ rate`(명목 8192 의 약 99%), `clipped`, `ISR stack used`, `NEST ... DROPPED`(0 이어야 함), `speaker guard` |

제거(`/U`)는 인터럽트 벡터를 다른 프로그램이 다시 후킹했다면 안전을 위해 거절합니다.

## 소리 품질 가이드 (실기 노이즈 원인)

86Box 는 스피커 에뮬레이션이 가청 반송파/지터를 걸러 주기 때문에 **실기에서는 훨씬 시끄럽게 들립니다.** 실기(삼성 Sens P30)에서 `test/nlab.c`(소음 분리 시험)로 원인을 하나씩 가려낸 결과입니다 (자세한 표는 [`docs/PROGRESS.md`](docs/PROGRESS.md) 18장).

| 소리 | 원인 | 대응 |
|---|---|---|
| 8kHz 쯤의 고주파 "삐~" | PWM 반송파 = 샘플 클록(RTC 8192Hz)이 가청 대역. 16kHz 이상이면 거의 안 들림. 펄스가 좁을수록 줄어듦 | **`/PDM`** (반송파 ≥15kHz). 또는 `/PIT`(게임 속도 문제) |
| 주기적인 "틱, 틱, 틱" | CPU 가 약 0.26초마다 0.4~0.5ms 멈춤(노트북 **USB Legacy 에뮬레이션 SMI**)과 그 동안 PWM 이 HIGH 에 굳는 현상 | **CMOS/BIOS 에서 USB Legacy Support 를 끄기**(실기에서 틱이 사라짐, PS/2 키보드가 있는 경우) → 못 끄면 **`/PDM`** 또는 `/B:n` |
| 거친 "지지직"/휘파람 | 합성 시간 동안 샘플 IRQ 가 지연되는 타이밍 지터 | **`/NEST`** (RTC·PIT 모두 개선) |
| 음량이 작음 | `/PDM` 의 좁은 스윙 | `/PDM:4`~`6`, `/G`, 노트북 하드웨어 볼륨(Fn 키/BIOS PC beep 볼륨) |

시험으로 **배제된** 원인: 18.2Hz BIOS 타이머 틱, PIC 우선순위, 드라이버의 주기 점검, PIT 제어 워드 매번 전송, 게인에 따른 양자화.

### 소음 시험 프로그램 NLAB

`NLAB.EXE`(`test/nlab.c`, 설명 `dist/NLAB.TXT`)는 OPL 엔진 없이 같은 드라이버로 합성 신호(일정 레벨, 1kHz 음, 합성 부하 흉내)를 재생하며 클록·출력 방식·바이어스·PDM 프리셋을 번갈아 들어 볼 수 있는 시험 도구입니다. 다른 기기에서 EOPL 의 최적 설정을 찾거나 SMI 지연(`NLAB 23`: IRQ 지연 측정기)을 확인할 때 쓰세요. EOPL 을 제거한 상태에서 실행합니다.

## 동작 원리 (요약)

- **포트 가로채기**: QEMM 의 QPI(I/O 포트 트랩, V86 콜백)로 388h/389h 를 가로챕니다. 주소 래치, 레지스터 쓰기, 타이머 상태 읽기(게임의
  AdLib 감지 통과)를 에뮬레이션하고, 상태 포트를 읽는 `IN AL,DX` 지연 루프를 `NOP` 으로 런타임 패치해 트랩 비용을 줄입니다.
- **합성**: DOSBox 의 DBOPL 을 16비트(Open Watcom `-mm -3`)로 포팅했습니다. 빌드 시 업스트림 소스를 패치하며
  (`tools/gen_dbopl16.py`), 부동소수점은 빌드 시 생성한 상수표로 대체했습니다. 32비트/FP 빌드와 **PCM 이 비트 단위로 동일**함을 검증합니다(지원 레이트 8000/8192/11025/16000/22050).
- **출력**: PIT 채널 2 를 mode 0 PWM 또는 mode 2 펄스 밀도로 구동. 모든 합성은 샘플 클록 ISR 안에서 일어나며(전용 스택, 작은 서브블록 사이에 인터럽트 창 / `/NEST`),
  BIOS 18.2Hz 시계는 보존됩니다. 게임이 스피커 게이트(포트 61h)나 PIT ch2 를 바꾸면 ISR 이 되돌립니다(스피커 가드).
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

# 4. (선택) 소음 시험 프로그램
powershell -ExecutionPolicy Bypass -File tools\build_nlab.ps1      # 결과: build16\nlab.exe
```

DOS 파일 이름은 8.3 형식이어야 합니다. Open Watcom 도구(`wcc`, `wpp`, `wlink`, `wasm`)는 PowerShell 에서 인자가 깨지므로 스크립트가 `cmd /c` 로 호출합니다.
`build_eopl.ps1` 은 `build16\` 을 다시 만들므로 NLAB 은 그 뒤에 빌드하세요.

### 시험

| 시험 | 설명 |
|---|---|
| `tools\test_variants.ps1` | 엔진 3변형(FP / OPL2 / OPL2+FP제거)의 PCM 해시 동일성, 지원 샘플레이트 전체 |
| `test/blktest.c` | 합성 블록 크기(4/8/32/전체)와 무관하게 PCM 동일한지 |
| `test/eopltest.c` | API 클라이언트(`eopltest <초>`), 트랩 자체시험(`eopltest trap`), PIT0 재설정 감지(`eopltest pit`) |
| `test/adlplay.c` | 게임처럼 AdLib 포트를 직접 접근하는 연주 프로그램 |
| `test/winwr.c`, `test/spkbreak.c` | 타이머 인터럽트에서의 OPL 쓰기, 게임이 스피커/PIT ch2 를 건드리는 상황 모사 |
| `test/amptest.c` | 엔진 출력 진폭 측정 (PWM 단계 활용도) |
| `test/nlab.c` | **실기 청취용 소음 시험 프로그램**(46개 시험: 반송파, 음량, 합성 부하, 틱 원인, IRQ 지연 측정, 바이어스, 펄스 밀도) |

DOSBox-X 에는 QEMM 이 없으므로 **QEMM 포트 가로채기와 실제 음질은 86Box(또는 실기)에서만** 확인할 수 있습니다.
시험 시 `/NOTRAP` 으로 설치하고 `eopltest`/`adlplay` 로 API 경로를 시험합니다.
`NLAB` 의 디버그 스위치는 `-dNLAB_DEBUG` 로 빌드한 NLAB 에만 들어 있고 TSR 빌드에는 포함되지 않습니다.

## 저장소 구성

```
src/          TSR 본체 (eopl.c, pcspk16.c, trapglue.asm, pcspk.h, opl_core.h, dbopl_core.cpp)
              ※ pcspk.c, opl_core.cpp, opal_core.c, rt_stubs.cpp 는 초기 프로토타입/비교용(32bit DOS/4GW, YMFM, Opal)
tools/        빌드·생성 스크립트 (엔진 패치, 상수표 생성, 시뮬레이터)
test/         시험 프로그램 (nlab.c = 소음 시험)
docs/         PROGRESS.md (개발 기록), HANDOFF.md (인수인계 요약), NEW_CHAT_PROMPT.md (AI 어시스턴트와 작업을 이어갈 때의 프롬프트)
ref/          업스트림 참조 소스 (git 에 포함하지 않음, tools\fetch_refs.ps1 로 받음)
```

## 향후 개선점 (로드맵)

**우선순위 높음**
- [ ] **`/PDM` 음량 문제 해결**: 큰 소리에서 단계가 거칠어지는 문제를 노이즈 셰이핑(샘플 간 양자화 오차 전달)으로 보강, 게인 자동 보정, 프리셋 기본값 결정(실제 게임 청취 필요)
- [ ] **실제 게임 시험 확대**: `/NEST`·`/PDM` 조합의 게임 안정성(큐 `DROPPED`, 스택), 음량, 클리핑. 게임별 호환성 목록 작성
- [ ] **EMM386 지원**: ADLCOM 의 INT 2Fh AX=4A15h 방식(I/O 콜백이 보호모드 복사본에서 실행) — 상주 엔진과 데이터를 공유할 방법(공유 메일박스 등) 탐사 필요. 86Box 에서 진단 핸들러부터
- [ ] **상주 메모리 감축** (현재 약 48.5KB → 목표 약 30KB): 설치 전용 코드 이미지 뒤로 이동, `MulTable` 이중 보유 제거, 링 축소, OPL3 채널 상태 제거, 레이트표 축소, 진단 코드 분리. `LOADHI`(UMB) 적재 시험. 프린세스메이커 2 지원

**음질**
- [ ] 적응형 바이어스: 신호 크기는 그대로 두고 DC 오프셋만 천천히 따라가게 하여 헤드룸을 확보(AGC 에서 겪은 "속도가 변하는 것처럼 들림" 회피)
- [ ] 8192Hz 샘플링의 이미지 성분(음 재생 시 남는 고주파) 감소: 보간/필터
- [ ] PDM 의 N 갱신 지연(최대 반송파 1주기)에 따른 지터와 N 정수 양자화 평가·개선
- [ ] 설치 시 자동 설정(CPU/PC beep 경로 시험음으로 프리셋·게인 추천), USB Legacy SMI 감지(IRQ 지연 측정기를 `/S` 에 통합)해 사용자에게 BIOS 설정 안내
- [ ] PIT 채널 0 가상화(QPI 로 포트 40h/43h 트랩 + 게임 INT 8 을 가상 주기로 호출)로 16kHz 이상 샘플 클록 사용 — 매우 어려움, `/PDM` 으로 대체 가능성이 높음

**기능**
- [ ] Sound Blaster FM 포트(2x8h/2x9h) 지원
- [ ] OPL3(스테레오 불가, 4OP 일부)는 목표 아님 — 필요 시 별도 검토
- [ ] 실기 시험: Pentium(MMX)·PII·데스크톱 스피커 직결 PC·다른 노트북, 장시간 안정성
- [ ] 영어 README, 릴리스 패키징(dist)

## 라이선스

이 프로젝트는 **GNU GPL v3** 입니다 ([`LICENSE`](LICENSE)). 사용한 외부 코드와 출처는 [`THIRD_PARTY.md`](THIRD_PARTY.md) 를 참고하세요.

## 감사

- **ADLCOM** (Peter De Wachter, Jose Phillips, Electric-ray) — QEMM/EMM386 I/O 가로채기와 AdLib 에뮬레이션 설계의 기반
- **DOSBox Team** — DBOPL OPL2/OPL3 에뮬레이터
- **VSBHDA** (Baron-von-Riedesel) — DBOPL 사본, ISR 안에서 청크 합성하는 구조의 참고
- **hearo**, **SBEMU**, **YMFM**, **Opal** — PC 스피커 출력 설계 및 엔진 비교 참고
- 공개된 DOS PC 스피커 PWM 예제(`leonardo-ono/Assembly8086PlayPcmDigitizedSoundOnPCSpeakerTest` 의 `pwm2.asm`) — PIT ch2 mode 0 PWM 구동(제어 워드 매 샘플 전송 등) 비교 참고

*Electric-ray — Bringing OPL Sound Back to DOS PCs.*
