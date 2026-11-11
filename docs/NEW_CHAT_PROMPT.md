이 대화는 "E-ray DOS OPL Emulator (EOPL)" 개발을 이어서 진행하는 새 대화다. 한국어로 답한다.

## 프로젝트
사운드카드 없는 DOS PC 에서 AdLib(OPL2 전용) 음악을 소프트웨어로 합성해 PC 스피커(PIT ch2 mode 0 PWM)로 재생하는 16비트 실모드 TSR. QEMM QPI 로 AdLib 포트(388h/389h)를 가로채 ISR 안에서 DBOPL(16비트 정수 포팅)로 합성한다. 상세 설계/근거: `docs/PROGRESS.md`(17장이 최신), 인수인계 요약: `docs/HANDOFF.md`(5장=현재 상태, 지식), 사용법/옵션: `README.md`.
환경: Windows 98 순수 DOS, QEMM(필수)+EMM386(지원해야 함), Open Watcom 1.9(`C:\WATCOM19`, `C:\WATCOM` 은 쓰지 말 것). 작업 폴더 `C:\source\E-rayDOS-OPL`(Claude Desktop MCP: Desktop Commander 파일 읽기/쓰기/edit_block, Windows-MCP PowerShell; 먼저 `tool_search` 로 도구를 로드한다). 시험: 나는 DOSBox-X 로 자동 시험, **86Box 와 실기(삼성 Sens 센트리노 노트북, QEMM 9.0)는 사용자가 직접 시험해 청취 결과/`EOPL /S` 사진을 알려 준다.**

## 먼저 할 일
1. `docs\HANDOFF.md` 를 읽어라(특히 5장: 실기 소음 원인 분석 결과와 대기 중인 시험, 6장 이후 계획, 7장 빌드/시험 명령, 2장 도구 함정).
2. `tools\build_eopl.ps1` 로 현재 소스가 빌드되는지 확인(`build16nf\eopl.exe`), 필요하면 `tools\test_variants.ps1` 로 엔진 PCM 해시 동일성 확인(7개 레이트 전부 IDENTICAL 이어야 함).

## 현재 상태 한 줄 요약
1차 마무리(2026-11-11). 실기(센트리노 노트북) 소음 원인을 NLAB 으로 분리: 고주파 = RTC 8192Hz 반송파, 틱 = USB Legacy SMI(0.26초마다 0.4ms 정지, BIOS 에서 끄면 사라짐), 거친 소리 = 합성 지터(`/NEST`). `/PDM`(PIT ch2 모드 2 펄스 밀도)이 틱·고주파를 크게 줄였고 음질은 만족, **기본 프리셋은 음량이 작아 `/PDM:4~6`+`/G` 를 추가**(실제 게임 청취는 아직). `/RTC16`,`/RTC32`,`/RTC2X` 는 불가/폐기로 제거. 상주 48,544B.

## 지금 최우선 과제
사용자가 실기 게임에서 `EOPL /NEST /PDM:4 /G:6`, `/PDM:5 /G:4`, `/PDM:6 /G:3` 등의 음량·클리핑(`/S` 의 clipped)·안정성을 알려 줄 것이다(결과가 없으면 시험을 요청). 결과로 프리셋/게인 기본값을 정하고, 필요하면 노이즈 셰이핑(샘플 간 양자화 오차 전달)·적응형 바이어스를 설계한다.

## 그 다음
`/NEST` 게임 안정성(QEMM 트랩 포함), EMM386 지원(HANDOFF 6장), 상주 메모리 감축(목표 ~30KB), UMB 적재(`LOADHI`), SB FM 포트, 게임 호환성 목록. 커밋은 사용자가 한다. 시험 도구 `NLAB`(`dist\NLAB.TXT`)로 다른 기기의 설정을 찾을 수 있다.

## 지켜 달라는 원칙
- `ref/` 업스트림 소스는 수정하지 말고 빌드 스크립트로 패치한다.
- DOSBox-X 로 검증한 것 / 86Box 로만 확인되는 것 / 실기에서만 확인되는 것을 구분해 정직하게 보고한다. 추정은 추정이라고 쓴다. 실기 소음은 86Box 와 다르게 들리므로 86Box 결과를 실기에 일반화하지 않는다.
- 변경마다 자동 검증(해시 동일성, 설치/제거 수명주기+`mem /c` 608K 복구, 자체시험)을 하고 `docs\PROGRESS.md` 에 새 절을 append, 사용자 배포용 바이너리는 `dist\eopl.exe` 로 복사한다(`dist\새 텍스트 문서.txt` 는 사용자 파일이니 건드리지 말 것).
- 사용자에게 시험을 부탁할 때는 명령 순서와 알려 줄 항목을 구체적으로 적고, 항목마다 `EOPL /U` 로 제거해 재부팅이 필요 없게 한다. 실험 기능은 기본 꺼짐(옵션)으로 두고 위험을 알린다.
- 응답은 간결하게(긴 로그 대신 핵심 숫자만). 코드 수정은 Python 스크립트로 일괄 치환할 때 파일을 `encoding='utf-8'`, 줄바꿈(CRLF)을 보존해 읽고 쓴다(`gen_tables.py` 등 한글 포함 파일은 인코딩 지정 필수). Watcom 도구는 `cmd /c` 로 호출, DOS 파일명은 8.3.
