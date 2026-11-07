# E-ray DOS OPL Emulator - 개발 진행 기록

작업 폴더: `C:\source\E-rayDOS-OPL`
테스트 환경: 86Box (VM `adlcom테스트` = Pentium II 300MHz / p2bls / 8MB), 자동 스모크 테스트는 DOSBox-X 사용

## 1. 로컬 툴체인 조사 결과
| 용도 | 경로 | 비고 |
|---|---|---|
| Open Watcom 1.9 (순정) | `C:\WATCOM19` | **빌드에 사용** (ADLCOM 권장 버전, wcc386/wlink/dos4g 정상) |
| Open Watcom (혼합/2.0 beta) | `C:\WATCOM` | wpp386/wcc386 은 1.9 바이너리 + 2.0 헤더 → 헤더 오류. 사용 안 함 |
| MinGW g++ 6.3 | `C:\MinGW\bin` | **YMFM(C++14) 컴파일에 사용** (-m32 -march=i586 -ffreestanding) |
| Python 3.14 / Git | PATH | 생성 스크립트 / 소스 pin |
| JWasm | `C:\JWasm` | 필요 시 어셈블러 |
| DOSBox-X | `C:\DOSBox-X` | 자동 테스트 |
| 86Box | `D:\86Box` | 실사용 검증 (VM: `C:\Users\tiram\86Box VMs\adlcom테스트`) |

참조 소스: `ref/ymfm` (commit 81aec25), `ref/adlcom`, `ref/E-RayDSB` (git clone, 수정하지 않음)

## 2. Phase 1 결과 (YMFM DOS 독립 실행)

### 2.1 개발계획서 4.2 컴파일러 문제 - 결론
- **방안 A (Watcom으로 YMFM 직접 컴파일): 불가.** `wpp386`이 `constexpr`, `std::array/unique_ptr` 등을 처리 못함 (E336/E042 등).
- **채택: 방안 B 변형** — YMFM 을 MinGW g++(C++14)로 *프리스탠딩 COFF 오브젝트*로 컴파일하고,
  Watcom `wlink`가 그대로 링크 (`system dos4g`). libstdc++/libc 의존 없음.
  - 호출 규약: 경계는 C API(`opl_core.h`) + `__cdecl` (Watcom/GCC 공통, `_` 접두).
  - `new/delete` → 4KB 정적 bump arena, `memcpy/memmove/memset` → `rt_stubs.cpp`.
  - 전역 생성자(.ctors) 의존 제거 (placement new 수동 생성).
- `tools/gen_min.py`: 업스트림 `ymfm_opl.cpp`에서 YM3812/YMF262 필요분만 남긴 `ymfm_opl_min.cpp`를 **빌드 시 생성**
  (log_keyon(std::string), OPLL, Y8950, YMF278B 등 제거). 업스트림 파일은 수정하지 않음.

### 2.2 검증
| 항목 | OPL2 | OPL3 |
|---|---|---|
| 호스트(MinGW) 해시 | 0F69E317 | FFAFABA4 |
| DOS(Watcom+DOS4GW, DOSBox-X) 해시 | 0F69E317 | FFAFABA4 |
→ **호스트와 DOS 실행 결과 PCM 비트 단위 동일.** (22050Hz, 10초, 아르페지오 테스트, 중간 레지스터 write 포함)
YMFM 네이티브 레이트 49715Hz → 출력 레이트로 선형보간 리샘플.

### 2.3 메모리 실측 (개발계획서 5.1 추정치 대비)
| 구성 | 실측 |
|---|---|
| YMFM OPL2/OPL3 코드 | 18,072 B |
| 래퍼+스텁 코드 | 약 2.1 KB |
| 칩 상태 + arena | 약 21.3 KB (arena 4KB: OPL2 1.6KB / OPL3 3.3KB 사용) |
| **합계(PCM 버퍼 제외)** | **약 41 KB** (계획 30~80KB 범위 내) |

### 2.4 성능
DOSBox-X는 사이클 정확도가 없어 의미 있는 수치 아님(참고: normal core, cycles=max 에서 22050Hz OPL2 0.75x).
**실제 CPU 성능은 86Box(P2 300 및 느린 CPU 설정)·실기에서 측정 필요.**

## 3. 빌드 / 실행
```
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Target all   # host + dos
build\gen_test_host.exe opl2 22050 10 out.wav                           # 호스트 (WAV 청취 가능)
build\gen_test.exe opl3 22050 10 out.wav                                # DOS (DOS4GW.EXE 필요)
```
참고: PowerShell 에서 wcc386/wlink 인자가 깨지므로 `cmd /c` 로 호출 (build.ps1 반영).

## 4. 핵심 설계 이슈 (결정 필요)
ADLCOM은 두 형태: ① 16-bit 리얼모드 TSR(EMM386 AX=4A15 I/O 트랩) ② **JLM(JEMM 로더블 모듈, 32-bit flat PE)**.
YMFM 코어는 32-bit flat 코드이므로 **리얼모드 TSR에는 직접 넣을 수 없다.**
→ 현실적 경로: **JLM(JEMM) 기반**으로 YMFM 코어를 통합 (코드/상태는 확장 메모리에 상주 → conventional 점유 최소화에 유리).
EMM386(MS)·QEMM 사용자용 리얼모드 경로는 모드전환 구조 필요(후순위).

## 5. 다음 단계
1. Phase 2: PC Speaker PWM 출력 (PIT ch2 + 61h) — 실기/86Box 검증 필요 (86Box PC Speaker 에뮬레이션 사용)
2. 실행 구조: 인터럽트(PIT ch0)에서 PCM 소비 + YMFM 생성 위치(JLM ring0 에서 타이머 IRQ 내 생성 vs 버퍼 선생성) 결정
3. Phase 3: `jadlcom.c`/`res_opl2.c` 의 UART 전송부를 `oplc_write_port()` 로 교체


---

# 6. 외부 조사 문서(hearo / SBEMU / VSBHDA / Opal) 검토 및 반영

문서의 주장을 소스를 직접 받아(`ref/hearo`, `ref/opal`, `ref/VSBHDA`, `ref/SBEMU`) 검증했다.

## 6.1 검증 결과 요약
| 문서 주장 | 검증 | 비고 |
|---|---|---|
| hearo `pcspeaker.c`: 1024x2 더블버퍼, PIT ch0 샘플타이밍, ISR→ch2, `pc_pump()`가 채움, 18kHz 상한 | **사실** | MIT, Watcom 16-bit 리얼모드 |
| hearo가 TSR이 아님 / pump 문제 | **사실** | 주석에도 "pc_pump 안 부르면 무음" 명시 |
| **hearo = "PWM"** | **의심** | 헤더 주석은 *mode 0*, 코드는 `outp(0x43,0x96)` = **mode 3**(방형파, 주파수 변조 성격). 듀티 PWM은 mode 0(0x90)이 정석 → **Phase 2에서 mode 0/3 둘 다 구현해 86Box·실기로 비교** (그대로 재사용 금지) |
| Opal: C11, MIT, Nuked 동일, 소형 | **사실** | 단 **Watcom 1.9 컴파일 불가**(C99 for-선언), uint64 사용(`__divdi3`), 쓰기큐 64KB(축소 가능) |
| Opal을 YMFM 대안으로 | **성능 탈락** | 아래 벤치: YMFM보다도 4~5배 느림 |
| VSBHDA Pentium II 266 간단한 게임 / PIII 500 쾌적 | **사실** (`vsbhda.txt`) | 단 PCM/DMA/MIDI 포함 수치 |
| SBEMU는 DOSBox OPL3 사용 | 사실 (VSBHDA는 `DBOPL.CPP`) | |

## 6.2 문서가 놓친, 소스 검토에서 나온 핵심 발견
1. **DBOPL(DOSBox OPL)이 가장 유력한 엔진.** VSBHDA의 `DBOPL.CPP`는 **Watcom 1.9로 그대로 컴파일**되고 (MinGW 우회 불필요),
   MinGW 호스트 빌드와 **PCM 해시가 비트 단위로 동일**(OPL2 9ch C8B57F6B / OPL3 18ch 65937A25).
2. **VSBHDA는 OPL 샘플 생성을 사운드 IRQ ISR 안에서 수행**한다 (`SNDISR_Interrupt` → `VOPL3_GenSamples`, 카드 버퍼 여유분만큼 청크 생성).
   "무거운 합성을 ISR에서 하지 않는다"는 우리 원칙 대신, **작은 청크 + 버퍼 여유 기반 ISR 생성**이 실증된 대안이다 → Phase 2/3 기술검증 항목.
3. **OPL 타이머/상태 레지스터**: 게임은 타이머 레지스터(02/03/04)를 써서 OPL 존재를 감지한다. VSBHDA는 칩 밖(`VOPL3.CPP`)에서
   타이머를 가짜로 처리한다. **현 `opl_core`(YMFM, no-op 인터페이스)는 타이머가 동작하지 않아 감지에 실패한다** → Phase 3에서 VOPL3 방식 이식 필요.
4. **라이선스**: ADLCOM `LICENSE`는 **GPLv3**, VSBHDA GPLv2, DBOPL(DOSBox) GPLv2+, YMFM BSD-3, Opal/hearo MIT.
   → 결과물을 **GPLv3**로 배포하면 모두 결합 가능 (개발계획서에 명시 필요).

## 6.3 엔진 비교 벤치 (22,050Hz 출력, 동일 소스/동일 `oplc_*` API)
호스트(MinGW -O2 -m32 i586), 실시간 대비 배수 (클수록 가볍다), 채널을 모두 울린 부하 기준:
| 엔진 | OPL2 9ch | OPL3 9ch | OPL3 18ch | 코드 | 상태/BSS |
|---|---:|---:|---:|---:|---:|
| **DBOPL** | **x467** | **x500** | **x254** | 15.5KB | ~14KB(테이블 9.4+칩 4.4) |
| YMFM | x121 | x84 | x59 | 18~33KB | ~21KB |
| Opal | x27 | x28 | x25 | 9.3KB | ~1.9KB |

DOS(DOSBox-X, 동일 조건, 사이클 정확도 아님 / 상대비교용) 10초 렌더: DBOPL OPL2 9ch 8.2s vs YMFM 25.7s (**3.1배**), OPL3 18ch 17.4s vs 63.5s (**3.6배**).
→ **DBOPL ≈ YMFM의 3~4배 빠름.** Opal은 Pentium 목표에 부적합(정확도 우선 설계, 64-bit 연산 다용).
주의: 정확도는 YMFM/Nuked > DBOPL 이나, 8bit PC Speaker 출력(≤18kHz)에서는 차이가 청취상 무의미할 가능성이 높다(청취 비교 필요).
주의: 호스트 수치는 현대 CPU 기준 상대치. **절대 CPU 부하는 86Box(P2-300 및 저속 설정)/실기에서 측정.**

## 6.4 개정된 결정사항
- **엔진 1순위: DBOPL (VSBHDA 사본, Watcom 1.9 네이티브 빌드)**, 2순위 YMFM(정확도 비교용, MinGW 우회 경로 유지), Opal은 보류.
  - 엔진 교체 가능하도록 `oplc_*` API 뒤에 격리 (`opl_core.cpp`=YMFM, `dbopl_core.cpp`=DBOPL, `opal_core.c`=Opal).
  - 계획서의 "YMFM 핵심" 표현은 "OPL 엔진(DBOPL 기본, YMFM 선택)"으로 개정.
- **PC Speaker 출력부**: hearo `pcspeaker.c`를 **설계 참고**(더블버퍼·BIOS 틱 보존 Bresenham·안전한 close 순서)로 활용하되,
  mode 0/3 PWM 검증 후 확정. 16-bit 리얼모드 코드이므로 JLM(32bit)용으로 로직을 재작성.
- **TSR/JLM 구조**: VSBHDA(JEMM V86 모니터 + ISR 생성) 구조가 ADLCOM-JLM 방향을 뒷받침. SBEMU/VSBHDA의 `PTRAP`(포트 트랩), `VOPL3`(포트·타이머 처리)를 참고.
- 우선순위표 갱신: hearo(PCM 출력 설계) 1, ADLCOM(트랩/TSR) 1, **VSBHDA(DBOPL+VOPL3+ISR 생성) 1**, SBEMU 2, YMFM 2, Opal 4.

## 6.5 빌드
```
powershell -ExecutionPolicy Bypass -File tools\build_dbopl.ps1   # Watcom 단독, build\gendbopl.exe (8.3 이름 주의)
```


---

# 7. Phase 2 - PC Speaker 출력 (프로토타입 결과)

## 7.1 PIT 모드 판정 (시뮬레이터 `tools/pit_sim.py`, 8254 모델)
스피커/귀를 샘플 주기 박스 평균(저역통과)으로 모델링하고 원신호와의 상관/SNR 계산 (11025/18000/22050Hz 공통 경향):
| 방식 | 상관 | SNR |
|---|---:|---:|
| **mode 0 (0x90) + count를 0..T-2로 스케일** (채택) | +1.000 | 36~42 dB (약 6bit) |
| mode 0 + raw U8 (count가 T 초과 → 포화) | 0.64~0.92 | 2~8 dB |
| mode 3 (0x96, hearo 실제 코드) + raw U8 | **0.02** | 0 dB |
| mode 3 + 스케일 | 0.15~0.22 | 0.2 dB |
→ hearo의 mode 3 코드는 *이 모델에서는* 가청 대역 신호가 거의 없다(주파수만 변함). 실제 칩/86Box 동작은 **청취로 최종 확인 필요**.
→ 해상도 한계: 샘플주기 T tick 이 PWM 레벨 수 = 22kHz: 53레벨(5.7bit), 18kHz: 65(6.0bit), 11kHz: 107(6.7bit). 음질↔샘플레이트 트레이드오프.

## 7.2 드라이버 `src/pcspk.c` (v2)
- PIT ch2 mode 0 PWM, ch0=샘플레이트 IRQ0. ISR은 O(1): 링버퍼에서 count 1개를 포트 42h로 출력.
- BIOS INT 8 은 16.16 누산기로 65536 PIT tick 마다 체인 → **18.2Hz 시계 보존 (오차 ≤ 1%, 55ms 틱 양자화 범위)**.
- 링버퍼 2048B, 전경 `pcspk_pump()`가 128샘플 청크로 보충.
- **ISR 보조 보충(assist)**: 전경이 pump 를 못 부르면(게임이 CPU 점유) 링 잔량 < 768 일 때 ISR 이 128샘플 청크 1개를 직접 생성 (VSBHDA 방식, 지연 상한 고정).
  `pcspk_lock()` 으로 OPL 상태 재진입 방지 (레지스터 쓰기 중 ISR 생성 금지).
- 종료 시 PIT/PIC/스피커 상태 복구 순서는 hearo의 안전 순서를 채용.

## 7.3 측정 (DOSBox-X, `cycles=fixed` — Pentium급 근사, **절대치 아님**)
OPL2 9채널 화음, DBOPL, assist 켬. 숫자 = underrun(샘플 단위):
| 샘플레이트 | 45,000 cycles (~P75-100급) | 100,000 | 200,000 |
|---|---:|---:|---:|
| 11,025Hz | 0 | 0 | 0 |
| 16,000Hz | 0 | 0 | 0 |
| 22,050Hz | **21,773 (~16%)** | 0 | 0 |
| 22,050Hz + 게임이 100ms 정지 | 25,848 | **0 (assist 361회)** | **0 (assist 391회)** |
- 150ms 정지(11kHz): assist 없으면 underrun 다수 / 있으면 0 (`dbx_spk2`).
- 결론(잠정): **Pentium 100급은 11~16kHz가 안전권, 22kHz는 더 빠른 CPU 필요.** assist 로 게임 점유 상황도 견딤.
- 한계: DOSBox cycles 는 실제 Pentium CPI 와 다르다. ISR 은 DOS4GW IRQ 반사 오버헤드 포함(JLM ring0 에서는 더 가벼울 것).
  **86Box(P2-300 외 저속 CPU 설정)와 실기에서 재측정 필요. 청취 평가(mode 0 음질, 6bit 노이즈) 필요.**

## 7.4 실행 / 빌드
```
powershell -ExecutionPolicy Bypass -File tools\build_spk.ps1     # build\spktest.exe (DBOPL + pcspk)
spktest <rate> <pitmode 0|3> <seconds> [busy_ms] [assist 0|1]    # 예: spktest 16000 0 20
```
86Box VM 에는 `spktest.exe` 와 `DOS4GW.EXE`(C:\WATCOM19\binw) 를 복사해 실행. 아무 키나 누르면 중단.

## 7.5 남은 과제 / 다음 단계
1. 86Box 에서 청취 및 mode 0 vs mode 3 비교, 저속 CPU 설정별 underrun 측정
2. 6bit 한계 개선: 노이즈 셰이핑/디더, 레벨 압축(리미터) — 음질 평가 후
3. Phase 3: ADLCOM 트랩 + `oplc_write_port`/타이머 상태(VOPL3 방식) 연결, JLM(32bit ring0) 환경으로 `pcspk` 이식
   (IRQ0 후킹을 JEMM 하에서 어떻게 할지(V86 인터럽트 vs ring0 IDT) 별도 기술검증 필요)


## 7.6 86Box/QEMM 에서 ISR 보조 보충(assist) 중 다운 (사용자 보고)
- 증상: `spktest 16000 0 20 100`(100ms 정지 + assist 켬)에서 assist 가 계속 올라가다 QEMM `Exception #6 at 042D:08DA`
  (Flags=7002: **NT 비트 켜짐**, AX=IP, 명령어 바이트가 쓰레기 → 깨진 인터럽트 프레임/잘못된 복귀 주소로 추정).
  같은 조건에서 assist 끔(`... 100 0`)은 UNDERRUN=0 으로 정상 (링 2048샘플=128ms > 정지 100ms).
- 검증한 가설 (DOSBox-X 에서):
  - 스택검사(`__STK`) 호출: 맵/오브젝트에 없음 → **기각**.
  - assist 스택 소모: 실측 **312바이트** → DBOPL 생성이 DOS/4GW 인터럽트 스택을 넘칠 가능성은 낮음 (완전 기각은 아님).
  - **전용 스택 + STI(중첩 허용)는 DOS/4GW 를 죽인다**: `DOS/4GW error 2001, exception 0Dh at CS=80(USE16)` 재현.
    DOS/4GW 의 16비트 인터럽트 래퍼가 중첩 IRQ 를 자기 스택 위라고 가정하기 때문. (이 환경에서는 중첩 허용 시 전용 스택 금지)
- 아직 원인 미확정. 의심: DOS/4GW 의 실모드↔보호모드 인터럽트 반사(특히 콘솔 출력 중 DOS 호출과 겹침)가 긴 ISR 구간과 충돌, QEMM(VCPI) 환경에서 증폭.
- v3 에서 assist 방식을 선택 가능하게 함 (`amode 0/1/2/3`, `achunk`, `quiet`) → 86Box 에서 `dist\README.TXT` 의 6단계로 이분 탐색.
- 중요한 설계 결론: **DOS/4GW 는 시험 틀일 뿐, 최종 제품(JLM/V86 모니터)의 인터럽트 환경과 다르다.** DOS/4GW 특이 현상에 과도하게 맞추지 말고,
  최종 환경(Phase 3)에서 ISR 생성 방식을 다시 검증한다.
- 대안(ISR 생성 없이 정지 흡수): 링버퍼를 8192B(16kHz 기준 512ms)로 키우면 ISR 생성 없이도 긴 정지를 흡수. 비용 8KB.
  단, 게임이 pump 를 영영 부르지 않는 실제 TSR 상황에서는 결국 ISR(또는 다른 주기 후크) 생성이 필요.


---

# 8. ChatGPT 중간검토(2026-10-31) 반영 / 환경 재확정 / 아키텍처 결정

## 8.1 확정된 실제 환경 (이전에 내가 놓친 것)
- 대상: **Pentium III급, Windows 98 "Command Prompt Only" 실제 DOS 모드, 메모리 관리자 QEMM**, Open Watcom 1.9, PC Speaker 출력.
- 대상 게임: 프린세스메이커 2, 대항해시대 2 등 → **conventional memory(최대 실행 크기)가 최우선 지표.**
- **정정(내 오류)**: 6장에서 "JLM/JEMM 기반이 현실적"이라고 제안했으나 이는 사용자의 환경이 QEMM임을 모른 채 ADLCOM 의 JLM 빌드만 보고 한 판단.
  JEMM 전용 모듈은 QEMM 에서 쓸 수 없다. **JLM 방향은 폐기.** (7.6 절의 크래시도 QEMM+DOS/4GW 환경에서 발생 — 환경 정보가 있었다면 더 일찍 맞는 시험을 했을 것)

## 8.2 검토 항목별 판정
| 검토 지적 | 판정 | 조치 |
|---|---|---|
| 현재 spktest/gendbopl 은 DOS/4GW 32bit 테스트, QEMM TSR 아님 | **맞음** | 최종 모델과 분리해서 관리. 결과를 "TSR 동작"으로 해석하지 않음 |
| 32bit 코어 vs ADLCOM 16bit V86 QPI TSR 간극이 최대 위험 | **맞음 (P0)** | 8.4 에서 실험으로 결정 |
| `oplc_read_status()`=0, OPL 타이머 미구현 | **맞음** | P0 목록, VOPL3 방식 이식 예정 |
| ISTACK 16KB 과다 | **맞음** | 실측 312B → **2KB 로 축소 완료** (`pcspk.c`) |
| ISR assist 최악 시간/IRQ 지연 실측 필요 | 맞음 | P1 |
| `ref/` 라이선스 혼재 | 맞음 | 결과물 GPLv3 (ADLCOM GPLv3 기준) 명시 필요 |
| MAP 의 64KB STACK 등은 DOS/4GW 값이며 TSR 상주량 아님 | 맞음 | 최종 TSR 은 `MEM /C` 로 측정 |
| 8kHz 시험 로그 없음 | 맞음 | P1 에 8kHz 포함 |
| 서로 다른 빌드의 로그를 섞어 읽지 말 것 | 맞음 | 이후 로그에 빌드 식별 기록 |

## 8.3 검토 중 새로 발견·수정한 결함
1. **DBOPL(VSBHDA 사본) 16bit 누적 오버플로**: 출력 `int16_t` 에 `output[i] += sample` 누적, 클램프 없음 → 다채널/큰 소리에서 **부호 반전(wrap)**.
   측정: OPL2 9ch 에서 0.57%, OPL3 18ch 에서 3.5% 샘플이 최대 ±65531 만큼 틀어짐 (틱/팝 잡음 원인).
   **수정**: `tools/gen_dbopl32.py` 가 사본을 int32 누적으로 변환, 어댑터에서 int16 클램프. DOS/호스트 해시 일치 재확인
   (새 기준 해시: OPL2/9ch `C9CE7162`, OPL3/18ch `21030ACB`, 22050Hz 10초). 이전 해시(C8B57F6B/65937A25)는 폐기.
2. 어댑터 임시 버퍼 크기: 엔진이 `memset(output,0,4*samples)` 로 버퍼보다 더 지움(최대 수백 B 초과) → 정확한 int32 버퍼(256샘플×2)로 교체해 해소.
3. 7.6 의 assist 전용 스택 + STI(중첩) 조합은 DOS/4GW 를 죽임(재현). 중첩 허용 시 전용 스택 금지.

## 8.4 아키텍처 선택: QEMM 에서 DBOPL 을 어떻게 실행하나
ADLCOM 의 QEMM 경로는 **QPI I/O 콜백 = 16bit V86 모드 핸들러(`retf` 복귀)** (`res_glue.s`, IISP 체인, shadow register cache 512B). 32bit 코드를 직접 링크할 수 없다.

| 안 | 내용 | 상주 메모리(추정) | 위험 |
|---|---|---|---|
| **B. DBOPL 16bit 포팅 (채택·실증)** | 전부 16bit V86 TSR. 모드 전환 없음 | **~30KB, UMB 적재 가능** | 속도 ~2.1배 느림(DOSBox 상대) |
| A. 16bit 스텁 + VCPI 로 32bit 코어 호출 | 합성만 보호모드(확장 메모리) | ~6KB + 확장메모리 | VCPI 모드전환 직접 구현, QEMM 호환·IRQ 처리 복잡 |
| JLM(JEMM) | - | - | QEMM 불가 → **폐기** |

**실증 결과 (B)**: Watcom 1.9 `wpp -mm -3` 로 DBOPL 이 컴파일/링크되고, 패치 후 **32bit 빌드와 PCM 이 비트 단위로 동일**
(OPL2 9ch, OPL3 18ch, 22050Hz 및 16000Hz). 방법: 32bit 빌드를 정답 기준으로 삼아 테이블 체크섬 → 연산자 상태 → PCM 해시 순으로 **차분 디버깅**.
16bit `int` 로 깨진 곳 4종 (`tools/gen_dbopl16.py` 가 자동 패치, 업스트림 미수정):
`Bitu/Bits`(uintptr_t), `1<<N`·`0xffu<<N` 상수 시프트, `uint8_t` 테이블 원소 시프트(EnvelopeIncreaseTable/AttackSamplesTable), `int16*uint16` 곱.
빌드/검증: `tools\build16.ps1` → `build\gen16.exe`.

크기 (16bit, 실측 맵): DBOPL 코드 9,030B + 어댑터 534B. 정적 테이블 ~9.4KB(WaveTable 8KB = OPL3 8파형; **OPL2 전용이면 4KB 로 축소 가능**) + Chip 4.3KB.
→ 엔진 합계 약 23KB (+ 링 2KB + 스택 ~1KB + QPI 글루/shadow ~1KB + PC Speaker 코드) ≈ **28~30KB (추정, `MEM /C` 로 실측 필요)**.

속도 (DOSBox-X `cycles=fixed 100000` 상대치, 실기 아님): 출력 22050Hz 실시간 대비
| | 16bit | 32bit |
|---|---|---|
| OPL2 9ch | x1.03 (≈97 "cycles/ms") | x2.19 |
| OPL3 18ch | x0.51 (≈196) | x1.08 |
16kHz OPL2 9ch: 16bit x1.42 / 32bit x3.03. DBOPL 은 **출력 샘플레이트에 비례**해 비용이 든다(내부 리샘플).
→ Pentium III(수백 MIPS급) 에서는 22kHz OPL2 가 CPU 의 대략 1/4, OPL3 18ch 가 1/2 정도로 *추정*. 실측 필요. Pentium 100급은 OPL3 22kHz 불가.

**결정(잠정)**: **B(16bit V86 TSR, QPI 콜백 재사용)를 주 경로**로 한다. A(VCPI)는 메모리/속도가 부족할 때의 후속 최적화 후보로만 보류.

## 8.5 우선순위 (검토 문서 P0~P2 + 내 조정)
P0
1. ~~실행 모델 결정~~ → B 채택 (사용자 확인 필요)
2. **ADLCOM QEMM QPI 콜백 최소 TSR**: 388h/389h(+38Ah/38Bh) 쓰기를 가로채 링크 큐에 기록만 (DBOPL 없이 먼저 동작 확인, 86Box+QEMM)
3. OPL 타이머/status(02/03/04h, 상태 비트) — VOPL3 방식, 게임의 OPL 감지 통과
4. PC Speaker 드라이버를 16bit V86/실모드 ISR 로 재작성 (현 `pcspk.c` 는 DOS/4GW 용 기준선으로 보존). **IRQ0 를 QEMM V86 에서 후킹**하는 안전성 검증
P1
5. 16bit 엔진을 TSR 에 통합: 레지스터 큐 → 합성 청크(ISR assist 재설계: 7.6 의 DOS/4GW 실패는 환경 특이, QEMM/V86 에서 재검증)
6. 상주 메모리 측정(`MEM /C`, Largest executable program size), UMB 적재, 초기화 코드 비상주
7. 8/11/16/22kHz 비교, 링/청크 크기, assist 최악 시간, 8kHz 포함
P2
8. 게임 시험(프린세스메이커 2, 대항해시대 2): OPL2 → 음악 → 장시간 → 종료 복구 → OPL3
9. 음질 평가(6bit PWM 노이즈), 필요 시 디더/노이즈셰이핑

## 8.6 사용자 확인 필요
- B(16bit 포팅) 방향 동의 여부, QEMM 버전/설정(UMB 크기, `LOADHIGH` 사용), 목표 최대 상주 크기(예: 30KB 이하?)
- OPL2 전용(WaveTable 4KB 절감) 우선인지, OPL3 도 첫 릴리스에 포함할지


---

# 9. Phase 2b - 16bit 실모드 PC Speaker 드라이버 (QEMM/V86 대비)

## 9.1 구현
- `src/pcspk16.c`: DOS/4GW 의존 없는 **실모드 INT 8 후킹** 드라이버 (Watcom 16bit `-mm -3`). 설계는 `pcspk.c` 와 동일(mode 0 PWM, 16.16 BIOS 틱 체인, 링버퍼, ISR assist).
  - ISR 은 IF=0 으로 전체 실행(중첩 없음). **assist 는 전용 스택(1KB)에서 실행** — 인터럽트된 프로그램의 스택이 작을 수 있으므로 OPL 엔진을 그 위에서 돌리지 않는다.
  - 청크 64샘플(IF=0 시간 상한을 줄임). 링 2048B, assist 임계 768.
- `test/spk16.c`, `tools/build_spk16.ps1` → `build16\spk16.exe` (47KB, `dist\spk16.exe`).
- 16bit DBOPL(8장) + 16bit 드라이버 결합: DOS/4GW·32bit 코드 전혀 없음.

## 9.2 DOSBox-X 측정 (순수 DOS, 단일 빌드: spk16, 상대치)
| 조건 | 100,000 cycles | 200,000 cycles |
|---|---|---|
| 16kHz | UNDERRUN 0 | 0 |
| 16kHz + 100ms 정지 | 0 (assist 90) | 0 (assist 8) |
| 22.05kHz | 0 | 0 |
| 22.05kHz + 100ms 정지 | **545** (CPU 부족) | 0 (assist 682) |
- BIOS 시계 보존: ISR 시간 ≈ BIOS 시간 (오차 ≤ 1%).
- ISR 전용 스택 최대 사용: **340B / 1024B**.
- 16bit 엔진은 32bit 의 약 2.1배 CPU 를 쓰므로 22kHz 는 100,000 cycles(~P100~P200급) 에서 한계, **16kHz 가 안전권**. P-III 에서는 여유 *추정*.

## 9.3 이 시험의 목적 / 대기 중
86Box 에서 **QEMM 유무 × 6가지 조건**으로 실행 (`dist\README16.TXT`).
- DOS/4GW 없이 QEMM 위에서 다운이 없으면 → 7.6 의 크래시는 DOS/4GW(+QEMM) 특이 현상으로 확정, 실모드 ISR 방식으로 진행.
- 다운되면 → QEMM V86 에서 IRQ0 장시간 처리(IF=0) 문제이므로 청크/EOI+STI 방식 재설계.

## 9.4 다음 (사용자 결과 후)
- ADLCOM `res_opl2.c` 의 `WRITE_COM`/`WRITE_VAL_NUKE` 를 엔진 호출로 교체. **ADLCOM 은 이미 타이머 상태 읽기(`timer_reg`)를 구현** → VOPL3 이식 불필요, 그대로 재사용.
- 트랩 핸들러(V86 콜백)에서의 레지스터 쓰기는 `cli` 로 보호한 직접 `WriteReg`(IF=0 ISR 생성과 상호 배제) 또는 SPSC 큐 중 택일.
- QPI/EMM386 트랩은 DOSBox-X 로 시험 불가 → 86Box 필요. 설치부(`adlcom.c`)·QPI 글루(`res_glue.s`)는 재사용.


---

# 10. 86Box 실측 (2026-11-01): Pentium 90 vs Pentium II 266

## 10.1 시험 구성 (사용자 제공 사양)
| 구성 | CPU | 메모리 | 보드 |
|---|---|---|---|
| A | **Pentium (P54C) 90MHz** | 32MB | ASUS PCI/I-P54NP4 (i430NX) |
| B | **Pentium II OverDrive 266MHz** | 16MB | ASUS P3V133 (Apollo Pro 133) |
오디오 장치 없음(PC Speaker 만). 사용자 보고: 소리는 "픽, 픽" 하는 끊김 소리.

## 10.2 결과
| 구성 | 조건 | 결과 |
|---|---|---|
| A (P90) | `spk16 16000 0 20` | **UNDERRUN 57,756 / 14s (≈26%)**, assist 18, 다운 없음 |
| A (P90) | `spk16 16000 0 20 100` | UNDERRUN 50,218 / 12s, assist 20 |
| A (P90) | `spk16 22050 0 20 100` | UNDERRUN 182,194 / 17s (≈48%), assist 54 |
| B (PII 266) | 점유 100ms 포함 | **UNDERRUN ≈ 0, assist 200~300** |
- **다운(QEMM Exception) 재현 없음** (DOS/4GW 를 뺀 16bit 실모드 ISR 에서는 안정). 7.6 의 크래시는 DOS/4GW(+QEMM) 특이 현상으로 판단.
  ※ 아직 "QEMM 유무" 구분 보고는 받지 못함 → bench 로 재확인 요청.
- P90 의 대량 underrun = **합성 속도 부족**(CPU-bound). 해석: 합성이 재생을 못 따라가 foreground pump 가 계속 lock 을 쥐고 있어 ISR assist 가 거의 못 돈다(assist 18~20).
  assist 는 CPU 를 새로 만들어 주지 않으므로 CPU 부족 상황의 해결책이 아니다. (시험 프로그램은 pump 와 ISR 이 같은 CPU 를 나눠 씀)
- P2-266: 100ms 점유에도 assist 200~300 회로 보충, underrun 0 → **ISR assist 구조는 CPU 여유가 있으면 정상 동작**.

## 10.3 상대 성능 보정
DOSBox-X 상대치로 P90 ≈ `cycles=fixed ~45,000` 가정 시 16kHz OPL2 9ch 합성 80% (실측 P90 은 >100% → IRQ 오버헤드/86Box 효과 포함 추정).
`spk16 bench` 로 실제 합성%·IRQ%를 분리 측정 가능 (DOSBox-X 상대 참고치):
| cycles | 11kHz 합성 / IRQ | 16kHz 합성 / IRQ |
|---|---|---|
| 45,000 (≈P90) | 55% / 3% | 80% / 4% |
| 100,000 | 25% / 2% | 36% / 3% |
| 200,000 (≈PII 266+) | 12% / 2% | 18% / 3% |
※ DOSBox-X 는 IRQ 오버헤드를 약 2~3µs/IRQ 로 거의 0 에 가깝게 모델링 → **실제 86Box/QEMM 의 IRQ 비용은 bench 로 직접 측정 필요**.

## 10.4 결정 / 설계 반영
- **첫 릴리스는 OPL2 전용 (사용자 결정).** OPL3(18ch, 스테레오, 2배 CPU)는 후속. 후속 최적화: OPL2 에서는 파형 4종뿐 → WaveTable 8KB→4KB(약 4KB 절감 가능).
- **샘플레이트는 CPU 에 맞춰 선택해야 한다**: P90급은 8~11kHz, P-II 266+ 는 16~22kHz. → 설치 시 **자동 벤치로 레이트 선택**(+수동 /R 옵션) 기능을 설계에 포함.
- 최소 사양 목표: Pentium 90 에서 **11kHz** 안정(9ch 풀로드 합성 ≈55%+IRQ) / 권장 Pentium II 233+ 에서 16~22kHz.
- 합성 부하는 활성 채널 수에 비례(DBOPL 은 무음 채널 생략) → 실제 게임 음악은 9ch 풀로드 시험보다 가벼울 수 있음 (실게임 측정 필요).
- 시험 프로그램의 pump/lock 구조는 실제 TSR 과 다름: **TSR 에는 foreground pump 가 없고 모든 합성이 ISR(assist) 경로**. 따라서 TSR 에서는 ISR 합성의 청크 크기/IF 상태/EOI 타이밍을 별도로 설계·측정해야 한다(P1).


---

# 11. 86Box 실측 2차 (QEMM 유무 × P90 / PII-266) 분석 + 결함 2건 수정

## 11.1 사용자 실측 (원본 데이터 요약)
IRQ 오버헤드 (IRQ 1회당, `bench` (3)):
| CPU | QEMM 없음 | QEMM 있음 | 비율 |
|---|---|---|---|
| Pentium 90 | 3.0 µs | **6.0 µs** | 2.0배 |
| Pentium II 266 | 1.7 µs | 2.7 µs (22kHz 는 6.0) | 1.6배 |
→ **QEMM(V86) 은 IRQ 비용을 약 2배**로 만들지만 절대값은 작다 (P90 16kHz: 5% → 10%, 22kHz: 7% → 14%).
다운(QEMM Exception)은 QEMM 유무와 무관하게 재현되지 않음 → 16bit 실모드 ISR 는 QEMM(V86) 에서도 안정.
P90 실행: 11kHz 는 underrun 0 (QEMM 유무 모두), 16kHz 는 24~26% underrun, 22kHz 는 50% 이상. P2-266: 전 조건 underrun 0.

## 11.2 결함 1 - "한 번 실행하면 다음 실행에서 조용히 다운" (원인 확정)
스크린샷의 `^C`: 사용자가 Ctrl-C 로 중단 → DOS 가 프로그램을 즉시 종료하며 `pcspk_close()` 가 실행되지 않음
→ INT 8 후킹 + PIT 16kHz + PWM 이 해제된 메모리를 가리킨 채 남음 → 다음 프로그램이 그 자리에 로드되면 다음 IRQ0 에서 쓰레기 실행.
**수정**: INT 23h(Ctrl-C), INT 1Bh(Ctrl-Break) 를 가로채 플래그만 세우고 반환 → 메인 루프가 정상 종료, `atexit` 로도 정리.
**교훈(TSR 에도 적용)**: 후킹한 인터럽트는 어떤 종료 경로에서도 해제되어야 한다. 상주 TSR 은 해제 대신 "남아도 안전"해야 하므로 코드가 메모리에 영구 상주해야 한다.

## 11.3 결함 2 - bench 가 낙관적이었음 (원인 확정)
시험 음색 `0x20=0x01`(EGT 꺼짐)은 건반을 누르고 있어도 엔벨로프가 계속 감쇠 → 수 초 뒤 **무음**.
DBOPL 은 무음 채널을 건너뛰므로 합성 비용이 0 에 가까워짐. 구 bench(4초간 감쇠)는 실제보다 가볍게 측정,
반면 실제 시험은 0.27초마다 재키온해 항상 풀로드. 관측: 파이프라인 생성 10.8k 샘플/s vs bench 능력 20k/s (격차 46%) → 원인 확인.
**수정**: bench 를 EGT=1(지속) 로 바꿔 9채널이 항상 울리는 최악 조건으로 측정.

## 11.4 보정된 용량 (최악 조건: OPL2 9채널 풀, 16bit DBOPL, 합성+IRQ(QEMM 없음) 부하)
DOSBox-X `cycles=fixed` 상대치 (86Box P90 실측과 대조해 **P90 ≈ 약 49,000 cycles** 로 보정):
| 샘플레이트 | 45,000 | 100,000 | 200,000 |
|---|---|---|---|
| 11,025 Hz | 104% | 47% | 24% |
| 16,000 Hz | 151% | 69% | 36% |
| 22,050 Hz | 208% | 95% | 49% |
- 근거: 86Box P90 은 16kHz 에서 필요량의 약 74% 만 생성(=필요 ≈135%), 11kHz 는 underrun 0 → 보정치와 일치.
- 결론: **P90 = 11kHz 가 한계(여유 거의 없음), P-II 266 = 22kHz 가 여유, P-III 급은 22kHz 충분.** 활성 채널이 적은 실제 음악은 더 가볍다(DBOPL 이 무음 채널 생략).
- **16bit DBOPL 의 CPU 비용이 바인딩 제약**임이 명확해짐 → (a) 설치 시 자동 벤치로 레이트 선택, (b) 필요 시 DBOPL 핫루프 최적화(어셈블리/알고리즘)를 후속 과제로.

## 11.5 이번에 한 최적화
- PCM→PWM 변환을 16bit 연산으로 (32bit 곱셈 헬퍼 호출 제거), 게인 1 은 빠른 경로. 청크 128 (assist 는 64).
- DOSBox-X 45k cycles 11kHz: underrun 1594 → 44 (경계 조건에서 개선). 16kHz 는 CPU 한계라 변화 없음.
- ISR 전용 스택 1536B (사용 456B).

## 11.6 다음
- 사용자 재측정: 새 bench (worst-case) + Ctrl-C 연속 실행 확인
- ADLCOM 트랩 → 엔진 통합 착수. 레이트 자동 선택(설치 시 2초 벤치, 부하 70% 이하 최고 레이트) 설계에 포함.


---

# 12. Phase 3a - 상주 TSR (ISR 전용 합성) 구현 결과

## 12.1 구현 (`src/eopl.c`, `src/pcspk16.c` v4, `test/eopltest.c`)
- **EOPL.EXE**: 16bit 실모드 TSR. OPL2(DBOPL16) → PC 스피커 PWM. **모든 합성이 IRQ0 ISR 안에서** 일어남(foreground pump 없음).
  - ISR: IF=0 로 전체 실행, 링 잔량 < 저수위(1024/2048) 이면 32샘플 청크를 **전용 스택**에서 합성. 18.2Hz BIOS 틱은 16.16 누산기로 정확히 체인.
  - OPL 레지스터 쓰기(`pcspk_oplwrite`): `cli` + 전용 스택 → ISR 과 상호 배제. (EOI+STI 중첩 방식은 **채택하지 않음**: 게임의 포트 쓰기가 반쯤 적용된 상태에서 ISR 이 합성하는 경쟁이 생김 — 설계 규칙으로 문서화)
  - INT 2Fh 멀티플렉스(ID 자동 스캔 C0h~FFh, 서명 'EOPL'): 00 확인 / 01 OPL 쓰기 / 02 상태 / 03 제거. 포트 트랩 전단(Phase 3b)이 같은 쓰기 경로를 호출할 예정.
  - 제거(`/U`): INT 8/INT 2Fh 가 다른 프로그램에 의해 재후킹되었으면 거절, 아니면 벡터·PIT·스피커 복구 후 메모리 반환.
  - 설치 시 환경 블록 반환, `printf` 미사용(자체 출력), `new` 대신 정적 버퍼(힙이 상주 영역 밖으로 자라는 문제 회피).
  - **샘플레이트 자동 선택**: 설치 시 1초간 최악 조건(9채널 sustained) 합성 속도를 측정해 CPU 의 약 47% 이하로 합성 가능한 최고 레이트 선택.
- **EOPLTEST.EXE**: INT 2Fh 로 레지스터만 쓰는 클라이언트 + 루프 카운터(게임에 남는 CPU 측정).

## 12.2 DOSBox-X 검증 (`cycles=fixed 100000`, 단일 빌드)
| 항목 | 결과 |
|---|---|
| 설치 → 상태 → 제거 → 상태 → 재설치 → 제거 | 모두 정상, **제거 후 conventional 608K 전부 복구** |
| 16kHz, pump 없는 클라이언트 8초 | **UNDERRUN 0**, ISR 합성 청크 ~3,980회(32샘플), ISR 스택 사용 460B |
| 게임 쪽 CPU | 기준 240,255 loops/s → 설치 후 214,516 (**89% 남음**, 이 시험 음악 기준) |
| 자동 선택 | 느린 모사 조건에서 11025Hz 선택 |
- 이 시험 음악은 음이 감쇠해 무음이 되는 구간이 있어 합성 부하가 최악 조건(36%)보다 훨씬 낮게(약 10%) 나옴 → **실제 음악 부하는 최악 대비 상당히 작을 것으로 기대**(채널 수/엔벨로프에 의존, 실게임 측정 필요).

## 12.3 상주 크기 (실측 55,488 B = 54K) 와 감축 계획
링크 맵 분석 (바이트):
| 구성 | 크기 | 비고 |
|---|---|---|
| DBOPL 코드 | 8,763 | 필수 |
| eopl + pcspk16 + core 코드 | 4,122 | 설치부 ~1.3KB 포함 |
| **`_TEXT` (C 런타임 + 수학)** | **17,507** | pow/exp/trig(FP 테이블 초기화) + startup/argv. 설치 때만 필요한 부분이 대부분 |
| DGROUP 상수/데이터 | ~2,900 | |
| **BSS** | **19,968** | DBOPL 테이블 ~9.4K(WaveTable 8K=OPL3 파형 8종) + Chip 4.3K + 링 2K + ISR 스택 1.5K + int32 버퍼 2K |
| 스택 | 2,048 | 설치 시 |
감축 항목(우선순위, 총 약 -20KB 예상 → 목표 약 34KB):
1. **DBOPL 테이블/속도표를 빌드 시 사전 계산**(지원 레이트 8000/11025/16000/22050 별 attack/linear rate 포함) → FP·수학 라이브러리 제거(-약 8~10KB), **FPU 없는 386/486SX 도 동작**.
2. **OPL2 전용**: WaveTable 8KB→4KB, int32 출력 버퍼 2KB→0.5KB(BLK 128 mono).
3. ISR 스택 1.5KB→0.8KB(청크 임시버퍼 128→64), 설치 스택 2KB→1KB, 링 2KB 유지 또는 1KB.
4. 설치 전용 코드/자료를 이미지 끝(DGROUP 뒤)으로 보내 반환 (링커 `order`), 필요 시 LOADHIGH(UMB) 병용.
※ QEMM 에서 `LH`(UMB 적재)로 실행하면 conventional 점유를 ~0 으로 만들 수 있음 → 시험 항목에 포함.

## 12.4 남은 일 (Phase 3b)
- **AdLib 포트(388h/389h, SB 228h/229h 등) 가로채기**: ADLCOM 의 QPI/EMM386 글루(`res_glue.s`)를 재사용하고, 핸들러의 `WRITE_COM` 를 `pcspk_oplwrite` 로 교체.
  글루가 DS 를 모르는 V86 콜백에서 호출되므로 `DS=DGROUP` 설정 래퍼 필요. DOSBox-X 로는 시험 불가 → 86Box(QEMM) 에서 AdLib 테스트 프로그램/게임으로 검증.
- ADLCOM `emulate_opl2_read()` 의 타이머 상태 에뮬레이션 재사용(게임의 OPL 감지 통과).


---

# 13. 상주 크기 감축 (55,488 -> 38,016 B) 과 검증

## 13.1 배경
86Box 실측(사용자): EOPL 상주 55,504B(MEM). 이 구성은 **UMB 0K**(`Upper 0`, `Largest free upper memory block 0K`)라 `LH` 로 올릴 수 없고 전량 conventional 소모(약 602K 중 9%).
프린세스메이커 2 등 conventional 민감 게임을 위해 3b(포트 트랩)보다 **크기 감축을 먼저** 수행 (DOSBox-X 로 자동 검증 가능, 최종 메모리 배치가 정해져야 3b 글루도 한 번에 맞출 수 있음).

## 13.2 적용한 감축과 효과
| 단계 | 내용 | 상주 크기 |
|---|---|---:|
| 0 | 시작 (FP 초기화, OPL2+OPL3 엔진, 2KB 설치 스택) | 55,488 |
| 1 | **OPL2 전용 엔진**: WaveTable 8KB→5KB(파형 0..3 = 0x000..0x9FF 만 사용), 출력 버퍼 int32[256×2]→[128] mono, ISR 스택 1.5KB→768B, 청크 버퍼 128→64샘플, 설치 스택 2KB→1.5KB | 49,424 (-6,064) |
| 2 | **FP/수학 라이브러리 제거**: `InitTables/Setup` 의 FP 계산을 빌드 시 생성한 상수표로 대체 (`kSine512`, `kMul384`, 4개 레이트 세트 8000/11025/16000/22050) | **38,016 (-11,408)** |
- `_TEXT`(C 런타임+수학): 17,507 → **4,696 B**. 수학(약 9.3KB) + FP 부수 코드(Pentium FDIV 감지 1.5KB, FP 예외 0.8KB, 실수 출력 변환 4.5KB) 가 전부 사라짐.
- **FPU 없는 386/486SX 에서도 동작** (런타임에 x87 명령 없음).
- 현재 구성: 코드 16.7KB (DBOPL 9.0 + eopl 2.1 + pcspk 1.5 + core 0.4 + 런타임 4.7) + 데이터 ≈ 21KB (BSS ≈ 13KB: WaveTable 5 + Chip 4.3 + 링 2 + ISR스택 0.8 + 버퍼 0.5 ..., 상수 ≈ 5KB: 레이트표 2.5 + 사인 1 + 곱셈 0.8 + 기타).

## 13.3 정확성 검증 (자동, `tools\test_variants.ps1`)
세 엔진 변형이 **모든 지원 레이트에서 OPL2 9채널 PCM 비트 동일**:
| 레이트 | 해시 (full = opl2 = opl2+nofp) |
|---|---|
| 8000 Hz | A63FB02E |
| 11025 Hz | 4A03FCF2 |
| 16000 Hz | E21AC1AC |
| 22050 Hz | F1F339FB |
- 방법: 호스트(MinGW) 에서 원본 FP DBOPL 로 테이블을 덤프(`tools/gen_tables.py`)하고, 16bit 패치(`tools/gen_dbopl16.py opl2,nofp`)가 FP 코드를 상수 사용으로 교체. 업스트림 미수정.
- 기능 회귀 없음: DOSBox-X 에서 설치/상태/제거/재설치, pump 없는 클라이언트 UNDERRUN 0, 게임 쪽 loops/s 213,931 (이전과 동일), ISR 스택 최대 328B/768B, 제거 후 608K 완전 복구.

## 13.4 빌드/검증 명령
```
powershell -ExecutionPolicy Bypass -File tools\build_eopl.ps1      # 3개 엔진 변형 + eopl.exe(nf) + eopltest.exe
powershell -ExecutionPolicy Bypass -File tools\test_variants.ps1   # 변형 간 PCM 해시 동일성 (DOSBox-X)
```

## 13.5 남은 감축 여지 (현재 38KB → 약 30KB 목표)
- 설치 전용 코드(`pick_rate`, 인자 파싱, 멀티플렉스 스캔 ≈ 1.5KB) + 런타임의 startup/argv/int86(≈ 2.5KB) 를 이미지 끝으로 보내 반환 (링커 `order`) 또는 최소 startup 사용.
- `MulTable` 상수(0.77KB)가 RAM 복사와 이중 보유 → const 직접 사용. 링 2KB→1KB 검토. Chip 의 OPL3 채널(9개) 미사용 분(≈2KB) 제거 검토.
- 레이트표 4세트(2.5KB): 지원 레이트를 줄이거나 압축.
- UMB 가 있는 구성에서는 `LH EOPL` 로 conventional 점유를 ~0 으로.


---

# 14. Phase 3b - QEMM 포트 가로채기 (AdLib OPL2) 구현

## 14.1 범위 결정 (사용자)
- **목표는 OPL2 재현까지** (OPL3 제외). **QEMM 과 EMM386 모두 지원 필요.** → QEMM 먼저 완성, EMM386 은 탐사 후.

## 14.2 구현
- `src/trapglue.asm`: ADLCOM `res_glue.s` 에서 파생(GPLv3)한 **QEMM QPI I/O 콜백**(V86). IISP 헤더(`EB 10`, 체인 dword, 시그니처 `424B`, 리셋 점프)
  + 핸들러. 388h/389h 가 아니면 체인으로 넘김. 우리 포트면 `pushad`, **DS=DGROUP** 로 전환, `eopl_trap(port, flags, ax, ip_off, ip_seg)`(cdecl) 호출,
  반환 AX 를 IN 결과로 돌려줌, 나머지 레지스터 보존. 어셈블 결과를 `wdis` 로 확인(헤더 바이트·오프셋 정확).
- `src/eopl.c`:
  - QPI 래퍼(ADLCOM): `QEMM386$` IOCTL 로 진입점, 버전 ≥ 7.03, 포트 트랩 설정/해제, I/O 콜백 체인 get/set.
  - **AdLib 에뮬레이션**(ADLCOM `res_opl2.c` 로직): 388h 쓰기=주소 래치, 389h 쓰기=레지스터 쓰기(주소 04h 는 타이머 제어 기억),
    388h/389h 읽기=상태(타이머1/2 즉시 만료 비트 → 게임의 AdLib 감지 통과, 하위 비트 06h).
  - **IN AL,DX 런타임 패치**: 주소 ≥ 0x20 이후 게임의 상태포트 읽기(지연 루프) 명령(EC)을 NOP(90)으로 교체 → 트랩 비용 제거. `/NOPATCH` 로 끔.
  - 핸들러의 실제 작업은 **드라이버 전용 스택 + IF=0** (`pcspk_call_private`)에서 수행: QEMM 콜백 스택이 작아도 안전, ISR 과 상호 배제.
  - 안전한 제거: ①INT 2Fh/INT 8 이 우리 것인지 사전 점검 ②QEMM 콜백 체인에서 분리(맨 위가 아니어도 IISP 체인 순회) ③포트 트랩 해제 ④INT 8 복구 ⑤INT 2Fh 복구. 하나라도 불가하면 아무것도 바꾸지 않고 거절.
  - 설치 시 이미 다른 프로그램이 388h/389h 를 가로채고 있으면 거절. 지원 메모리 관리자가 없으면 안내 후 종료(`/NOTRAP` 로 API 전용 설치 가능).
- 시험 도구: `eopltest trap`(QEMM 호출 규약을 흉내 내 핸들러를 직접 호출), `adlplay`(게임처럼 포트 직접 접근 + 표준 지연 루프).

## 14.3 DOSBox-X 검증 (QEMM 없이, 시뮬레이션 호출)
| 항목 | 결과 |
|---|---|
| 감지 절차 | reset 후 상태 06h (E0 마스크 00), 타이머1 시작 후 C6h (E0 마스크 C0) → **OPL2 감지 OK** |
| IN 패치 | 주소 01h: 패치 안 함(EC 유지), 주소 A0h: NOP(90) → OK. `/NOPATCH`: 패치 안 함 → OK |
| 포트 경유 연주 | 8초간 포트 쓰기 1,800회, **UNDERRUN 0**, 게임 쪽 214k loops/s(변화 없음), ISR 스택 328/768B |
| 수명주기 | 설치/상태/제거/재설치, 제거 후 608K 복구 |
| 상주 크기 | **39,824 B** (+1.8KB: QPI·트랩 코드) |

## 14.4 한계 / 미검증
- **실제 QEMM 에서는 미검증** (DOSBox-X 에 QEMM 없음). 호출 규약(DX=포트, CX 비트2=OUT, 사용자 CS:IP 가 진입 시 [sp+8])은 ADLCOM 의 검증된 코드를 따름.
- **EMM386 경로 미구현.** ADLCOM README/소스 분석: EMM386 I/O 가상화(INT 2Fh AX=4A15h) 콜백은 **단일 세그먼트(CS=0) 기준 주소**, 코드를 `size=resident_end` 만큼 EMM386 가 가져가
  (보호모드 선택자 0D8h 로 사용자 코드 접근) → 핸들러가 상주 엔진(실모드 메모리)과 **직접 데이터를 공유할 수 없을 가능성**. 86Box 에서 환경을 탐사해야 설계 가능.
  후보: ①PM 핸들러가 공유 메일박스(조건에 맞는 저주소/UMB 영역)에 쓰고 실모드 ISR 이 소비 ②엔진 전체를 EMM386 복사본에 넣기(메모리·동기화 문제) ③CMOS 등 임시 포트 메일박스(비권장).
- Sound Blaster FM 포트(2x8h/2x9h, 388h 별칭)는 아직 가로채지 않음(AdLib 선택 게임만 대상).


---

# 15. 86Box QEMM 시험 결과 (Phase 3b) 와 게임 무음 진단 도구

## 15.1 사용자 시험 (86Box, QEMM, P-II 계열)
| 항목 | 결과 |
|---|---|
| 설치 | `AdLib trapped via QEMM QPI`, resident 39,824 B, 16000Hz 자동 선택 |
| `ADLPLAY 10` | **AdLib 감지 성공** (상태 06 / C6), 화음 연주, 쓰기 1123회 |
| `EOPL /S` | port writes=2246, status reads=291, **patched IN=2**, underruns=0, ISR stack 328B |
| 패치 on/off | 동일 (ADLPLAY 는 약 112 writes/s 로 쓰기 속도 제한이 없음) |
| 게임 3종 | 페르시아왕자 1, 대항해시대 2, 프린세스메이커 2: **게임은 정상 실행, 소리 없음** |
→ **QEMM QPI 트랩 경로(체인, 감지, 타이머 상태, 엔진 호출, 패치)는 실제 QEMM 에서 동작이 검증됨.** 게임 무음은 별도 원인.

## 15.2 무음 원인 후보 (가설, 확정 아님)
1. **PIT 채널0 충돌 (유력)**: 샘플 클록이 PIT ch0 이다. 게임은 음악 박자용으로 PIT ch0 을 재설정/INT 8 후킹하는 경우가 많고, 그러면 EOPL 의 샘플 클록·BIOS 틱 체인 가정이 깨진다. 3종 모두 무음인 것과 부합.
2. 게임에서 Sound Blaster 선택(AdLib 아님) → DSP 감지(22xh) 실패. 3. 게임이 AdLib 에 음을 쓰지 않음. 4. IN 패치가 게임 로직을 깨는 경우(타이머 상태 확인용 IN).

## 15.3 구현한 진단/우회 (v5)
- **진단 (항상 켜짐)**: AdLib 388h/389h 쓰기 카운트, **키온(B0-B8 bit5) 횟수**, 레지스터 사용 비트맵(그룹별 개수), **마지막 32개 쓰기 로그**.
- **`/DIAG`**: ①**SB 포트 220h-22Fh 트랩**(읽기=FF 개방 버스, 접근 횟수/포트 마스크 기록) ②**PIT ch0 감시**: ISR 이 256 IRQ 마다 ch0 카운터와 read-back 상태를 읽어
  `카운터 > 우리 분주비` 또는 `모드 ≠ 3` 이면 "재설정됨"으로 집계 (`max counter seen`, `REPROGRAMMED samples`).
- **`/RTC` 모드**: 샘플 클록을 PIT ch0 대신 **RTC 주기 인터럽트(IRQ8/INT 70h, 8192Hz 고정)** 로. PIT ch0/INT 8 을 전혀 건드리지 않아 게임의 타이머 재설정과 충돌하지 않음.
  PWM 레벨 144(약 7.2bit, 16kHz 의 73 보다 해상도↑), 합성 부하는 16kHz 의 절반. 단점: 반송파 8192Hz 가 가청 대역("삐-" 잡음 가능), 대역폭 4kHz, INT 15h/83h·86h BIOS 대기 함수와 충돌 가능.
- 명령: `EOPL /S`(요약), `EOPL /D`(레지스터 분포 + 최근 쓰기 로그). 상주 크기 43,152 B (진단/RTC/8192Hz 레이트표 포함 +3.3KB; 안정화 후 정리 예정).

## 15.4 DOSBox-X 검증
- `/DIAG`: SB 프로빙 3회 기록·IN=FF OK, PIT ch0 재설정 시 `max counter 8184 > 74`, `REPROGRAMMED samples=2` → **감지 OK**. 정상 상태에서는 0.
- `/RTC`: IRQ8 8192Hz 동작(31,411 IRQ/3.8초), UNDERRUN 0, 설치/제거 OK. (DOSBox-X 는 포트 I/O 비용을 크게 잡아 loops/s 가 더 떨어져 보임; 실기 ISA 포트 접근은 약 1µs 대로 영향 작을 것으로 추정)
- 자체 시험: 감지 절차, 패치, SB 프로빙, `eopltest trap`/`pit` 모두 통과.

## 15.5 사용자 시험 요청
`dist\README_GAME_DIAG.TXT`: 게임별로 [A] `EOPL /DIAG` → 게임 → `EOPL /S` `EOPL /D` 출력, [B] `EOPL /RTC /DIAG` → 같은 게임 소리 여부. 게임의 사운드카드 선택값도 기록.

## 15.6 결과별 다음 단계
- PIT0 재설정 확인 + RTC 에서 소리 남 → RTC 를 자동 선택하거나, **PIT ch0 가상화**(포트 40h/43h 트랩 + 게임 INT 8 을 가상 주기로 호출) 로 16kHz 유지.
- SB 접근 → SB FM 포트(2x8h/2x9h) 및 DSP 감지 거부/에뮬레이션 추가.
- note-on=0 → 감지 절차/타이머 에뮬 보강.


---

# 16. 86Box 결과 분석(2026-11-07)과 스피커 가드 (우선순위: 게임 소리 > 크기)

## 16.1 사용자 시험 결과 (86Box + QEMM)
- 페르시아왕자1: PIT 클록 무음, /RTC 소리 남(ISR 합성 정상, 쓰기 1518쌍, note-on 103). PIT ch0 max counter 65528 = 게임이 ch0 을 18.2Hz 로 되돌림.
- 대항해시대2(사운드 설정 메뉴 없음, 자동 감지): PIT/RTC 둘 다 무음. /RTC: 쓰기 5586쌍, note-on 1862, 감지 OK, IRQ 1,041,158, underrun 0 -> 합성/전달 정상, 출력 단계 문제 추정. 리듬 모드(BD:20) 사용.
- 프린세스메이커2(Adlib/SB 선택): /NOTRAP 로도 실행 불가, /U 후 실행 -> 상주 크기 문제. 이전 빌드 39,824B 에서는 실행됨(사용자 확인). 문턱은 남은 conv 559,072B 초과~약 562,400B 이하. **사용자 결정: PM2 는 후순위(포기 가능), 다른 게임 소리가 우선.**
## 16.2 구현: 스피커 가드 (항상 켜짐)
- ISR 이 16 IRQ 마다 포트 61h bit0-1 과 PIT ch2 상태(read-back 0xE8, mode 0 / LSB only 기대)를 확인, 바뀌었으면 복구 후 카운트(ix61, ixpit2, 마지막 값 last61/lastch2). RTC 모드는 RTC 레이트 nibble 도 확인. 가설: 대항해시대2 가 자체 PC 스피커 루틴으로 게이트/ch2 를 건드림(미확인 추정).
- /S 에 guard 줄과 sample-clock vector still ours 표시(게임이 INT 8/70h 를 가로챘는지).
- DOSBox-X 검증(test/spkbreak.c: 게이트 해제+ch2 mode3 3회): PIT/RTC 양쪽 fix61=3, fixpit2=3 -> 복구 로직 동작. **실제 게임 효과는 86Box 확인 필요.** 상주 43,744B(+576B).

## 16.3 86Box 결과 (가드 빌드, /RTC): 대항해시대2, 페르시아왕자1, 그날이오면 모두 음악 재생 확인 (사용자 보고)
- 대항해시대2: port61h fixed=2, PIT ch2 fixed=2, last61=0E, lastch2=36 -> 게임이 스피커 게이트 해제 + ch2 mode 3 로 변경, 가드가 복구. 가설 확정.
- 페르시아왕자1 / 그날이오면: 가드 수정 0, 소리 정상. underrun 0, 벡터 유지.
- 변경: **기본 클록을 RTC(8192Hz)로 변경.** PIT 클록은 /PIT 또는 /R:n(/R:AUTO 포함). 상주 43,792B. DOSBox-X: 기본/PIT/R:16000 설치·제거 정상, 608K 복구.
- 남은 과제: 음질(8kHz 대역/반송파 잡음), PIT ch0 가상화(PIT 16kHz 용), SB FM 포트, EMM386, 크기 감축(PM2).

## 16.4 음질 '울먹임' 대응: 합성 중 인터럽트 창 (2026-11-07)
- 가설(추정, 86Box 청취로 확인 필요): ISR 이 IF=0 으로 32샘플 청크를 합성하는 동안(P90 최악 ~80us/샘플, PII-266 ~25us/샘플) 다음 RTC IRQ 가 수~수십 샘플 주기 지연/병합 -> PWM 샘플 타이밍 지터(청크 주기 ~256Hz 변조).
- 구현(RTC 클록 전용): ① RTC ISR 이 샘플 출력 직후 **EOI 를 먼저** 보냄 ② 합성을 **8샘플 서브블록**으로 나누고 블록 사이에 인터럽트 창(프로그램 스택으로 복귀 -> STI;NOP;CLI -> 전용 스택 복귀) ③ 창 안에서 도착하는 OPL 쓰기(게임 타이머 ISR 등)는 **두 번째 전용 스택(512B)** 에서 실행(P.lock 으로 선택) ④ 엔진 상태는 서브블록 경계에서 일관: test/blktest.c 로 블록 4/8/32/전체 PCM 해시 동일 확인(DOSBox-X).
- 옵션 /Y:n: 서브블록 크기(0=창 없음=이전 동작). PIT 클록은 이전 동작 유지.
- DOSBox-X 검증(test/winwr.c: INT 1Ch 에서 OPL 쓰기 91회): underrun 0, 인터럽트 창 7104회, write-stack 104B, 충돌 없음. spkbreak 가드 복구 정상. 설치/제거/608K 복구 정상. 상주 44,800B(+1,008B).
- **86Box 청취 비교 필요**: /Y:0(이전) vs 기본(8) vs /Y:4 vs /Y:16.

## 16.5 사용자 청취: /Y:16 이 4/8 보다 나음, 울먹임 잔존 -> 샘플 클록 계측 추가
- 서브블록이 작을수록 블록당 오버헤드(채널 9회 호출 등)가 커서 총 CPU 증가 -> 4/8 이 16 보다 나빴을 가능성. 기본값을 16 으로 변경(SUB_DEF=16).
- **계측**: RTC ISR 이 8192 IRQ 마다 BIOS 틱(18.2065Hz)으로 경과 시간을 재서 실제 IRQ 속도 평균/최저/최고를 /S 에 표시(measured IRQ rate). IRQ 가 합성 중 병합/유실되면 명목 8192 보다 낮게 나옴. 해상도 ~5%.
- DOSBox-X(cycles=fixed 100000, 9채널 시험음) 상대 비교(/S avg): Y:0 6243, Y:4 7291, Y:8 6541, Y:16 6422, Y:32/64 6243 Hz. -> 합성 중 IRQ 유실이 에뮬레이션에서도 재현되고 작은 서브블록이 일부 회복. (DOSBox-X 의 RTC/타이밍 모델은 실기와 다름: 절대치로 해석 금지.) 이 시험 음은 9채널 지속음(최악 부하).
- 해석(추정): 합성 블록 시간 > 샘플 주기(122us) 이면 샘플 출력이 늦거나 유실 -> 샘플 간격 불균일(지터) + 실질 샘플레이트 저하. 음악이 빽빽한 구간에서만 나타나 '중간중간' 울먹임과 부합. 86Box 에서 실측 필요.
- 상주 45,312B.

## 16.6 86Box(P-II 266) 실측 + 원인 재판정: 출력 해상도(진폭) 문제
- /S (기본, /Y:16): 평균 IRQ 7925 / 7833 Hz (명목의 ~96%, 손실 3~4%), underrun 0. 최저/최고(7457/8286)는 BIOS 틱 양자화(18/19/20틱). **타이밍 유실은 주원인 아님.** 울먹임은 빽빽한 곳/단음 가리지 않음(사용자).
- 측정(test/amptest.c, 8192Hz): 엔진 스케일은 '9보이스 최대 = 만점'. 1보이스 최대음 peak=4072(12% FS)=PWM 약 17단계, 보통 멜로디(TL=10h) peak=1019(3%)=**4단계**, 약한 음 1단계, 9보이스 TL=10h 27%. => 실효 해상도 2~4bit, 143단계를 못 씀 = 양자화 왜곡(뭉개짐/울먹임) 원인 유력(추정, 86Box 청취로 확인 필요).
- 시뮬(build/qsim.py, 대역 150~3000Hz SNR): 현행 16.3 / 27.8 / 33.4 dB (3%/12%/27% FS) -> 고정 x4: 29.2 / 40.5 / 25.3 -> **AGC(피크 정규화): 39.5 dB 전 구간**. 디더/노이즈셰이핑은 이 지표에서 이득 없음(-1~2dB)이라 제외.
- 구현: **자동 게인(AGC) 기본 ON** (피크를 ~85% FS 로: 28000/env, 즉시 감쇠/약 0.5초 복귀, 최대 x32), 반올림 양자화. /G:n=고정 게인 x n(1~16), /G:0=AGC. /S 에 gain, clipped 표시. 엔진 PCM 변형 해시는 그대로 동일(드라이버 변경). 상주 45,888B.
- DOSBox-X: 설치/제거/608K 복구 정상, underrun 0, 9ch 시험음 AGC x7.2, /G:4 고정 시 clipped 120.

## 16.7 AGC 되돌림 (사용자 청취: 게인 조절 시 찢어지는 소리 증가, 재생 속도가 늘어졌다 빨라졌다 변화, 이전 버전이 더 나음)
- AGC/게인 기본 동작 제거, 양자화 원복(truncation, x1). 유지: 스피커 가드, RTC 기본 클록, 인터럽트 창(/Y:16 기본), IRQ 속도 계측. /G:n 고정 게인 옵션만 남김(기본 x1).
- 교훈: 재생 속도 변동('늘어졌다 빨라짐')은 AGC 게인 변화보다 샘플 타이밍(IRQ 유실/지터) 쪽 현상일 수 있음 -> 타이밍 개선이 다음 후보.
