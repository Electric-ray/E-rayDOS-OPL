# E-ray DOS OPL Emulator - 인수인계 (HANDOFF)  2026-11-11 기준

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

## 5. 현재 상태 (2026-11-11, 1차 마무리)
**환경(시험)**: ①86Box(P-II 266, QEMM) ②**실기: 삼성 Sens P30(센트리노, 순수 DOS, QEMM 9.0)** — 사용자가 직접 청취/`EOPL /S` 사진을 알려 줌.

**실기 소음 원인 분리 결과(NLAB, 상세는 PROGRESS 18장)**
1. 고주파 = PWM 반송파(RTC 8192Hz)가 가청. 16kHz 이상이면 안 들림, 펄스가 좁을수록 감소.
2. 틱틱 = 약 0.26초마다 CPU 가 0.4~0.5ms 멈추고(**노트북 USB Legacy SMI**, CMOS 에서 끄면 사라짐) 그동안 PWM 이 HIGH 레일에 굳는 클릭.
3. 거친 소리 = 합성 중 샘플 IRQ 지연 → `/NEST` 로 개선.
4. 배제: BIOS 틱, PIC 우선순위, 드라이버 주기 점검, 제어 워드 매 샘플 전송, 양자화(게인).
5. RTC 분주 RS=2/1 은 128/256Hz(8192Hz 가 상한) → `/RTC16`,`/RTC32`,`/RTC2X` 제거됨.

**해결책**: `/NEST`(지터), `/PDM`(펄스 밀도: PIT ch2 모드 2, 반송파 ≥15kHz, CPU 정지 중에도 출력 안 굳음 → 실기에서 틱·고주파 모두 거의 사라짐), `/B:n`(PWM 레일 바이어스: 틱 감소).
**미해결**: `/PDM` 기본 프리셋은 음량이 작음 → `/PDM:4~6`+`/G` 추가(실제 게임 청취 미확인). 사용자가 다음에 알려 줄 것: 게임에서 `EOPL /NEST /PDM:4 /G:6` 등의 음량/클리핑/`/S`.
**옵션 현황**: README 옵션표 참고(`/RTC` 기본, `/PIT`, `/R:n`, `/Y:n`, `/NEST`, `/NOFIX`, `/G:n`, `/B:n`, `/PDM[:1..6]`, `/C`, `/W`, `/NOPATCH`, `/NOTRAP`, `/DIAG`). 상주 48,544B.
**시험 도구**: `dist\nlab.exe`(46개 청취 시험, `dist\NLAB.TXT`), 빌드 `tools\build_nlab.ps1`(`build_eopl.ps1` 뒤에 실행; `-dNLAB_DEBUG -dEOPL_PDM`). NLAB 디버그 스위치는 TSR 빌드에 없음.

## 6. 이후 할 일 (우선순위)
1. 실제 게임에서 `/NEST /PDM:n /G:m` 음량·클리핑·안정성 확인, 프리셋/게인 기본값 결정 → 노이즈 셰이핑(샘플 간 양자화 오차 전달)·적응형 바이어스(DC 오프셋만 천천히 추종) 검토.
2. **EMM386 지원**(ADLCOM 은 INT 2Fh AX=4A15h 로 포트 범위+핸들러 테이블 등록, DS:0..resident_end 코드 이미지를 EMM386 이 복사해 보호모드에서 실행 → 상주 엔진과 데이터 공유 방법 미해결; 진단 핸들러로 탐사, 후보: 공유 메일박스).
3. 상주 메모리 감축(48.5KB → 목표 ~30KB: 설치 전용 코드 이미지 끝으로, MulTable 이중 보유 제거, 링 축소, OPL3 채널 제거, 레이트표 축소), `LOADHI`/UMB(86Box 구성은 UMB 0K), 프린세스메이커 2.
4. SB FM 포트(2x8h/2x9h), 게임 호환성 목록, 다른 기기 실기 시험, IRQ 지연 측정기를 `/S` 에 통합해 USB Legacy SMI 안내.
5. (장기) PIT ch0 가상화 — `/PDM` 으로 대체 가능성이 높음.

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
