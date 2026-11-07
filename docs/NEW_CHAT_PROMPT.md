이 대화는 "E-ray DOS OPL Emulator" 개발을 이어서 진행하는 새 대화다. 한국어로 답한다.

## 프로젝트
사운드카드 없는 DOS PC 에서 AdLib(OPL2, OPL3 는 목표 아님) 음악을 소프트웨어로 합성해 PC 스피커(PIT mode 0 PWM)로 재생하는 TSR 을 개발 중이다.
환경: Windows 98 순수 DOS, 메모리 관리자 QEMM(필수) + EMM386(지원해야 함), Open Watcom 1.9(`C:\WATCOM19` 사용, `C:\WATCOM` 은 쓰지 말 것). 목표 CPU Pentium III급(최소 Pentium 90). conventional 메모리 최소화가 최우선.
작업 폴더: `C:\source\E-rayDOS-OPL` (Claude Desktop 의 MCP 도구로 로컬 PC 접근: Desktop Commander 파일 쓰기/편집, Windows-MCP PowerShell). 시험은 내가 DOSBox-X 로 자동화하고, 86Box 실기 시험은 사용자가 직접 해서 스크린샷/텍스트로 알려 준다.

## 먼저 할 일 (순서대로)
1. `C:\source\E-rayDOS-OPL\docs\HANDOFF.md` 를 읽어라 (전체 상태 요약, 환경, 아키텍처, 확정 사실, 미해결 과제, 빌드/시험 명령, 도구 사용 함정). 필요하면 `docs\PROGRESS.md` 의 8~15장(최신)을 참고한다. 1~7장은 초기 탐색 기록이다.
2. `tools\build_eopl.ps1` 로 현재 소스가 빌드되는지 확인하고, 필요하면 `tools\test_variants.ps1` 로 엔진 PCM 해시 동일성을 재확인한다.

## 현재 상태 한 줄 요약
16비트 실모드 TSR(`src/eopl.c`, `src/pcspk16.c`, `src/trapglue.asm`, DBOPL 16비트 포팅 엔진, FP 제거, OPL2 전용)이 QEMM QPI 로 AdLib 포트(388h/389h)를 가로채 ISR 안에서 합성한다. 상주 크기 43,152B.
86Box+QEMM 에서 설치/제거/AdLib 감지(ADLPLAY)/포트 트랩/IN 패치까지 확인됨. **그러나 실제 게임(페르시아왕자1, 대항해시대2, 프린세스메이커2)은 실행되지만 소리가 안 난다.**

## 지금 최우선 과제
게임 무음 원인 규명 및 해결. 가설: ①게임이 PIT ch0 을 재설정해 우리 샘플 클록이 깨짐(유력) ②Sound Blaster 선택 ③AdLib 쓰기 없음 ④IN 패치 부작용.
진단 도구는 이미 준비되어 있다(`dist\README_GAME_DIAG.TXT`, `EOPL /DIAG`, `/S`, `/D`, 우회 모드 `/RTC`=RTC 8192Hz 클록). 아래에 사용자가 보낸 시험 결과를 붙여 넣겠다 — 이 결과를 분석해 HANDOFF.md 5장의 "결과별 대응"에 따라 다음 단계를 진행하라.
(결과가 아직 없으면, 사용자에게 `dist\README_GAME_DIAG.TXT` 의 [A]/[B] 시험을 요청하라.)

## 그 다음
EMM386 지원(콜백이 EMM386 복사본으로 보호모드에서 실행될 가능성 → 86Box 탐사용 진단 핸들러부터), 상주 메모리 감축(목표 약 30KB), 레이트 자동선택 정교화, SB FM 포트 옵션, 실게임 안정성 검증.

## 지켜 달라는 원칙
- `ref/` 업스트림 소스는 수정하지 말고 빌드 스크립트로 패치한다.
- DOSBox-X 로 검증한 것과 86Box 실기로만 확인되는 것을 구분해서 정직하게 보고한다. 추정은 추정이라고 쓴다.
- 변경마다 자동 검증(해시 동일성, 설치/제거 수명주기, 자체시험)을 하고 `docs\PROGRESS.md` 에 새 절을 append 한다.
- 사용자에게 시험을 부탁할 때는 명령 순서와 알려 줄 항목을 구체적으로 적고, 재부팅이 필요 없게 안전한 제거 경로를 유지한다.
- 응답은 가능한 한 간결하게(긴 로그 대신 핵심 숫자만).

## 사용자가 보낸 최신 시험 결과 (여기에 붙여 넣기)
(게임별: 사운드카드 선택값 / [A] `EOPL /S`, `EOPL /D` 출력 / [B] `/RTC` 에서 소리 여부)
