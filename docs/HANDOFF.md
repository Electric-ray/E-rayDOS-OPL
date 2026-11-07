# E-ray DOS OPL Emulator - 인수인계 (HANDOFF)  2026-11-07 기준

이 문서는 긴 대화를 새 대화창으로 옮기기 위한 요약이다. 상세 근거/수치는 `docs/PROGRESS.md` (8~15장이 최신, 1~7장은 초기 탐색).

## 1. 프로젝트 / 환경
- 목표: 사운드카드 없는 DOS PC 에서 **AdLib(OPL2) 음악을 소프트웨어 합성 → PC 스피커(PWM)로 재생**하는 TSR. **OPL3 는 목표 아님(OPL2 까지).**
- 사용자 환경: Windows 98 순수 DOS 모드, 메모리 관리자 **QEMM (필수) + EMM386 (지원해야 함)**, Open Watcom 1.9. 목표 CPU Pentium III급(최소 Pentium 90).
  대상 게임: 페르시아왕자 1, 대항해시대 2(Uncharted Waters 2), 프린세스메이커 2 → **conventional 메모리 최소화가 최우선**.
- 시험은 **86Box**(사용자가 직접 실행, 스크린샷/텍스트로 결과 전달) + **DOSBox-X**(내가 자동 시험; QEMM/EMM386 없음, 사이클 정확도 없음).
  86Box VM: Pentium 90(i430NX, 32MB) / Pentium II OverDrive 266(Apollo Pro 133, 16MB). 이 구성은 **UMB 0K** (LH 불가).
- 사용자 언어: 한국어. 한국어로 답한다.

## 2. 로컬 도구 / 경로 (Windows PC, Claude Desktop MCP)
- 작업 폴더: `C:\source\E-rayDOS-OPL`  (git init 됨, `ref/`·`build*/` 는 .gitignore)
- 컴파일러: **`C:\WATCOM19` (순수 Open Watcom 1.9) 사용**. `C:\WATCOM` 은 혼합 설치라 헤더 오류 → 쓰지 말 것. MinGW g++ 6.3 (호스트 도구/참조 빌드), Python 3.14, Git, DOSBox-X(`C:\DOSBox-X`), 86Box(`D:\86Box`).
- 참조 소스(`ref/`, 수정 금지): ymfm, adlcom(GPLv3), E-RayDSB, hearo, opal, VSBHDA(DBOPL 사본), SBEMU.
- 도구 사용 요령 (겪은 함정):
  - 파일 작성/수정: `mcp__Desktop_Commander__write_file`(기존 파일은 `mode: rewrite` 명시), `edit_block`(old_string/new_string). 큰 PowerShell here-string 은 조용히 실패하기도 함.
  - 실행: `mcp__Windows-MCP__PowerShell`. **wcc/wpp/wlink/wasm 은 PowerShell 에서 인자가 깨지므로 `cmd /c "..."` 로 호출.** 콘솔 한글은 깨져 보임(무시).
  - DOS 실행 파일 이름은 **8.3 (8자 이하)**. DOSBox-X 시험은 `-conf` 파일의 `[autoexec]` 로 자동 실행, 결과는 `> 파일` 로 받아 읽기. 실행 전 `Stop-Process dosbox-x`.
  - 강제 종료된 DOSBox-X 는 열린 파일 크기가 0 으로 보임 → 로그는 매번 open/append/close.
  - `eopl.c` 등 16비트 코드: Watcom `_asm` 블록 안에서는 C 매크로 못 씀(리터럴 사용).

## 3. 아키텍처 (확정된 것)
- **실행 모델: 16비트 실모드/V86 TSR** (JLM/JEMM 은 QEMM 에서 불가 → 폐기). 엔진=**DBOPL(VSBHDA 사본)을 16비트(`wpp -mm -3`)로 포팅**, 업스트림 미수정.
  - `tools/gen_dbopl32.py`: int16 누적 오버플로(wrap) 수정 → int32 누적.
  - `tools/gen_dbopl16.py <src> <dst> [opl2,nofp]`: 16bit `int` 깨짐 4종 패치 + OPL2 전용(WaveTable 8KB→5KB) + **FP 제거(`nofp`, `tools/gen_tables.py` 가 호스트 FP 로 만든 상수표 사용; 레이트 8000/8192/11025/16000/22050)**.
  - 검증: 32bit/FP 빌드와 **PCM 비트 동일** (`tools/test_variants.ps1`, 레이트별 해시: 8000 A63FB02E, 11025 4A03FCF2, 16000 E21AC1AC, 22050 F1F339FB; OPL2 9ch 22050/10s = C9CE7162).
- **PC 스피커 출력**: PIT ch2 **mode 0 PWM**(mode 3 은 소리 안 남 — 시뮬+86Box 확인). count 는 0..T-2 로 스케일. hearo 의 mode 3 코드는 부적합.
  샘플 클록 두 가지: **PIT ch0 IRQ0**(기본, 16.16 누산기로 BIOS INT 8 을 18.2Hz 체인) / **RTC IRQ8 8192Hz (`/RTC`, PIT·INT 8 불간섭)**.
- **모든 합성은 ISR 안**(IF=0, 전용 스택 768B, 청크 32샘플, 링 2048B, 저수위 1024). foreground pump 없음. OPL 레지스터 쓰기/트랩은 `pcspk_call_private`(IF=0+전용 스택)로 ISR 과 상호 배제. **EOI+STI 중첩 방식은 금지**(포트 쓰기 경쟁 + DOS/4GW 충돌 확인).
- **포트 가로채기(QEMM)**: ADLCOM 의 QPI 방식 재사용. `src/trapglue.asm`(IISP 헤더 + V86 콜백 → DS=DGROUP 로 `eopl_trap` 호출), `src/eopl.c`(QPI 래퍼, AdLib 에뮬: 주소래치/데이터/타이머 상태 감지, **IN AL,DX 런타임 NOP 패치**, 안전한 제거 순서).
- 제어 API: INT 2Fh 멀티플렉스(ID C0h~FFh 스캔, 서명 'EOPL'; AL=0 확인,1 OPL쓰기,2 상태,3 제거,4 정보,5 핸들러주소).
- 설치 시 CPU 벤치로 샘플레이트 자동 선택(최악 9ch 합성이 CPU 47% 이하인 최고 레이트).

## 4. 실측으로 확정된 사실
- 용량(최악 9ch sustained, 합성+IRQ): P90 ≈ 11kHz 한계(89%), 16kHz 128%(끊김), PII-266: 11k 28% / 16k 40% / 22k 56%. 16bit 엔진은 32bit 의 약 2.1배 CPU. QEMM 은 IRQ 비용 약 2배(P90 3→6µs/IRQ).
- 합성 부하는 활성 채널 수에 비례(DBOPL 이 무음 채널 생략): 실제 음악은 최악보다 훨씬 가벼움. (구 bench 가 감쇠음 때문에 낙관적이었던 결함은 수정함.)
- 상주 크기: 55,488 → (OPL2전용) 49,424 → (FP 제거) 38,016 → (QEMM 트랩) 39,824 → (진단+RTC+8192표) **43,152 B**. 남은 감축안: 설치 전용 코드 이미지 끝 이동(링커 order), MulTable 이중 보유 제거, 링 축소, Chip OPL3 채널 제거(~2KB), 레이트표 압축, 진단/RTC 분리.
- 사용자 86Box(QEMM)에서 확인됨: 설치/제거, **ADLPLAY 가 AdLib 감지(상태 06/C6), 포트 쓰기·상태 읽기 카운트, IN 패치 동작**. DOS/4GW 없는 16bit ISR 은 QEMM 에서 다운 없음.
- 과거 결함(수정 완료): Ctrl-C 시 INT 8 후킹이 남아 다음 실행이 죽음 → 테스트 프로그램에 INT 23h/1Bh 가로채기+atexit 추가.

## 5. 현재 상태 / 미해결 (2026-11-07)
**해결됨 (86Box + QEMM, Pentium II 266, 사용자 청취/`EOPL /S` 확인)**
- 게임 무음의 원인 2가지를 찾아 수정: ①게임이 PIT ch0 을 18.2Hz 로 되돌림 -> **기본 클록을 RTC 8192Hz 로 변경**(`/PIT`, `/R:n` 은 PIT 클록). ②대항해시대2 가 포트 61h 게이트를 끄고 PIT ch2 를 mode 3 으로 바꿈 -> **스피커 가드**(ISR 이 16 IRQ 마다 61h bit0-1 / ch2 상태를 확인해 복구, `/S` 의 `port61h fixed`, `PIT ch2 fixed`).
- 페르시아왕자1, 대항해시대2, 그날이오면: AdLib 음악 재생됨.
- 합성 중 **인터럽트 창**(`/Y:n`, 기본 16): RTC ISR 이 EOI 를 먼저 보내고 합성을 서브블록으로 쪼개 블록 사이에 STI;NOP;CLI(프로그램 스택으로 복귀 후). 창 안에서 도착하는 OPL 쓰기는 두 번째 전용 스택(512B)에서 실행. 서브블록 4/8 은 16 보다 나빴음(청취). 상주 약 45.4KB(45,376B).
- `/S` 에 IRQ 속도 계측(`measured IRQ rate`, 8192 IRQ 마다 BIOS 틱으로 측정): P-II 266 에서 평균 ~96%(3~4% 유실), underrun 0.

**시도했다가 되돌린 것**: 자동 게인(AGC)/고정 게인 — 엔진 출력이 PWM 143단계 중 2~4단계만 쓰는 것은 사실이지만(`test/amptest.c`), AGC 를 켜면 찢어지는 소리와 속도 변화처럼 들려서(사용자 청취) 제거. `/G:n` 고정 게인만 남음(기본 ×1).

**미해결**
1. **음질**: 소리가 약간 뭉개져 "울먹이는" 느낌(빽빽한 곳/단음 구분 없음). 후보: 샘플 타이밍(합성 비용/IRQ 유실 3~4%), 8kHz 샘플링 에일리어싱, PWM 해상도, 86Box 스피커 에뮬레이션 특성. 비교 시험 후보: `/R:16000`(PIT, ch0 을 안 건드리는 게임), 컴파일러 `-5` 최적화, DBOPL 핫루프 최적화.
2. **프린세스메이커2** 는 상주 45KB 로 conventional 이 모자라 실행 불가(39.8KB 빌드는 실행됨 — 문턱은 남은 conv 약 559~562KB). **사용자 결정: 후순위**. UMB 적재(`LOADHI`)가 해법이나 86Box 구성이 UMB 0K 라 미검증.
3. EMM386 미지원, SB FM 포트(2x8h/2x9h) 미지원, PIT ch0 가상화 미구현, 실기 미검증.

## 6. 이후 할 일 (우선순위)
1. 게임 무음 해결 (위).
2. **EMM386 지원**: ADLCOM 분석상 EMM386 I/O 가상화(INT 2Fh AX=4A15h)는 콜백 주소를 **단일 세그먼트(CS=0) 기준**으로 계산하고 코드를 `size=resident_end` 만큼 EMM386 가 가져가 **보호모드에서 실행**(선택자 0D8h)할 가능성 → 상주 엔진과 데이터 공유 방법이 불확실. 86Box 에서 탐사용 진단 핸들러(선택자/한계/저주소 쓰기 가능 여부)가 필요. 후보: PM 핸들러→공유 메일박스→실모드 ISR 소비 / 엔진을 EMM386 복사본에 포함 / (비권장)CMOS 메일박스.
3. 상주 메모리 감축(목표 약 30KB 이하), 레이트 자동선택 정교화, SB FM 포트 옵션(BLASTER), 8kHz 포함 저속 CPU 검증, 장시간/실게임 안정성, 라이선스(결과물 GPLv3 명시), 배포 패키징.

## 7. 빌드 / 시험 명령
```
powershell -ExecutionPolicy Bypass -File tools\build_eopl.ps1     # 엔진 3변형 + eopl.exe(nf) + eopltest.exe + adlplay.exe  → build\ 로 복사
powershell -ExecutionPolicy Bypass -File tools\test_variants.ps1   # 변형 간 PCM 해시 동일성 (DOSBox-X)
(spk16: tools\build_spk16.ps1, bench: spk16 bench <rate>)           # PC 스피커 드라이버 단독 시험/CPU 벤치
```
- DOSBox-X 자체 시험: `eopl /NOTRAP` 로 설치 후 `eopltest trap`(QEMM 호출 규약 모사 자체시험), `eopltest <초>`(API 게임 흉내+CPU 몫), `eopltest pit`(PIT0 재설정 감지, /DIAG 필요), `adlplay <초>`(게임식 직접 포트 접근), `eopl /S` `/D` `/U`.
- 사용자 배포물: `dist\` (eopl.exe, eopltest.exe, adlplay.exe, spk16.exe, README*.TXT). `dist\새 텍스트 문서.txt` 는 사용자가 둔 파일이니 건드리지 말 것.

## 8. 주요 파일
- `src/eopl.c`(TSR 본체, 설치/상태/제거/트랩), `src/trapglue.asm`, `src/pcspk16.c`(16bit 드라이버: PIT/RTC 클록, ISR 합성, 전용 스택), `src/pcspk.h`, `src/opl_core.h`, `src/dbopl_core.cpp`(엔진 어댑터; `-dEOPL_OPL2_ONLY`)
- `src/pcspk.c`, `test/spktest.c`: DOS/4GW 32bit 프로토타입(기준선/참고용, 최종 제품 아님). `src/opl_core.cpp`(YMFM 래퍼, MinGW 우회), `src/opal_core.c`: 대체 엔진(비교용).
- `tools/`: gen_dbopl32.py, gen_dbopl16.py, gen_tables.py, gen_min.py(YMFM), build*.ps1, pit_sim.py(8254 PWM 시뮬), test_variants.ps1
- `test/`: eopltest.c, adlplay.c, spk16.c, gen_test.c(호스트/DOS 공용 해시·성능), dbg*.c

## 9. 이 프로젝트에서 지킨 원칙 (이어서 지킬 것)
- 업스트림 소스(`ref/`)는 수정하지 않고 빌드 시 스크립트로 패치.
- 결과는 **정직하게**: DOSBox-X 로 검증한 것과 86Box 실기로만 확인되는 것을 구분해서 보고. 추정치는 추정이라고 표기.
- 변경마다 자동 검증(해시 동일성 / 수명주기 / 자체시험) 후 `docs/PROGRESS.md` 에 기록(새 절 append).
- 사용자에게 시험을 부탁할 때는 **명령 순서와 알려 줄 항목을 구체적으로** 적고, 재부팅이 필요 없게 안전한 종료/제거 경로를 유지.
