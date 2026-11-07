#!/usr/bin/env python3
"""VSBHDA DBOPL 사본 -> 16bit(Watcom wpp -mm -3) 리얼모드/V86 빌드용 패치본 생성. 업스트림은 수정하지 않는다.
DBOPL 은 'int 가 32bit' 라고 가정한 곳이 있다. 16bit int 에서 깨지는 곳을 차분 테스트(32bit 빌드와 연산자 상태/PCM 해시 비교)로 찾아 고쳤다:
  1) Bitu/Bits (uintptr_t) 가 16bit 가 됨                      -> unsigned long / long
  2) `1 << N`, `0xffu << N`, `256 << N` 등 int/unsigned 상수 시프트가 0 으로 -> L/UL 접미사
  3) uint8_t 테이블 원소 시프트 (EnvelopeIncreaseTable[i] << 21, AttackSamplesTable[i] << s) -> uint32_t 캐스트
  4) int16_t * uint16_t 곱 (파형 x 곱셈테이블)                 -> int32_t 곱
  + gen_dbopl32.py 의 int32 누적(오버플로 wrap 방지)
옵션 (3번째 인자, 쉼표 구분):
  opl2 : OPL2 전용 -> WaveTable 8KB -> 5KB (파형 0..3 만 사용: 0x000..0x9FF)
  nofp : 런타임 부동소수점/수학 라이브러리 제거. InitTables/Setup 의 FP 계산을 tools/gen_tables.py 가 만든 상수표로 대체
         (kSine512, kMul384, 지원 샘플레이트별 속도표). opl2 와 함께만 허용. FPU 없는 386/486SX 에서도 동작.
검증: 32bit 빌드와 PCM 해시 비트 동일 (OPL2/9ch C9CE7162, OPL3/18ch 21030ACB @22050Hz 10s)
usage: gen_dbopl16.py <VSBHDA/src dir> <out dir> [opl2,nofp]"""
import sys, re, os, subprocess
src, dst = sys.argv[1], sys.argv[2]
opts = set(sys.argv[3].split(',')) if len(sys.argv) > 3 else set()
OPL2_ONLY = 'opl2' in opts
NOFP = 'nofp' in opts
if NOFP and not OPL2_ONLY: raise SystemExit('nofp requires opl2 (the exponential wave 7 needs pow)')
here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call([sys.executable, os.path.join(here, 'gen_dbopl32.py'), src, dst])
if NOFP:
    subprocess.check_call([sys.executable, os.path.join(here, 'gen_tables.py'), os.path.join(dst, 'eopl_tables.inc')])

def edit(name, fn):
    p = os.path.join(dst, name)
    t = open(p, encoding='latin-1').read()
    t2 = fn(t)
    open(p, 'w', encoding='latin-1', newline='').write(t2)

def must(t, old, new):
    if old not in t: raise SystemExit('patch anchor not found: ' + old)
    return t.replace(old, new)

def h(t):
    t = must(t, 'typedef uintptr_t Bitu;', 'typedef unsigned long Bitu;')
    t = must(t, 'typedef intptr_t Bits;', 'typedef long Bits;')
    return t

SETUP_NEW = '''const RateSet *rs = FindRateSet( rate );
	noiseAdd = rs->noiseAdd;
	noiseCounter = 0;
	noiseValue = 1;	//Make sure it triggers the noise xor the first time
	lfoAdd = rs->lfoAdd;
	lfoCounter = 0;
	vibratoIndex = 0;
	tremoloIndex = 0;
	for ( int i = 0; i < 16; i++ ) freqMul[i] = rs->freqMul[i];
	for ( int i = 0; i < 76; i++ ) linearRates[i] = rs->linearRates[i];
	for ( int i = 0; i < 62; i++ ) attackRates[i] = rs->attackRates[i];
	for ( int i = 62; i < 76; i++ ) attackRates[i] = 8UL << RATE_SH;
	'''

FIND_RATE = '''#include "eopl_tables.inc"
static const RateSet *FindRateSet( uint32_t rate )
{
	const RateSet *best = &kRates[0];
	uint32_t bd = 0xFFFFFFFFUL;
	for ( int i = 0; i < EOPL_NRATES; i++ ) {
		uint32_t d = kRates[i].rate > rate ? kRates[i].rate - rate : rate - kRates[i].rate;
		if ( d < bd ) { bd = d; best = &kRates[i]; }
	}
	return best;
}

'''

def nofp_patch(t):
    # --- Chip::Setup: everything between 'double original' and the four-op channel setup comes from the generated rate sets
    a = t.index('double original = OPLRATE;')
    b = t.index('//Setup the channels with the correct four op flags')
    t = t[:a] + SETUP_NEW + t[b:]
    t = must(t, 'void Chip::Setup( uint32_t rate )', FIND_RATE + 'void Chip::Setup( uint32_t rate )')
    # --- InitTables: MulTable / sine base / exponential wave
    t, n1 = re.subn(r'for \( int i = 0; i < 384; i\+\+ \) \{.*?MulTable\[i\] = \(uint16_t\)\(val\);\s*\}',
                    'for ( int i = 0; i < 384; i++ ) {\n\t\tMulTable[i] = kMul384[i];\n\t}', t, flags=re.S)
    t, n2 = re.subn(r'WaveTable\[ 0x0200 \+ i \] = \(int16_t\)\(sin\([^;]*;', 'WaveTable[ 0x0200 + i ] = kSine512[i];', t)
    t, n3 = re.subn(r'WaveTable\[ 0x700 \+ i \] = [^;]*pow[^;]*;', 'WaveTable[ 0x700 + i ] = 0;   /* exponential wave = waveform 7 (OPL3 only) */', t)
    if (n1, n2, n3) != (1, 1, 1): raise SystemExit('nofp InitTables anchors not found: %s' % ((n1, n2, n3),))
    return t

def cpp(t):
    if NOFP: t = nofp_patch(t)
    t = must(t, '( ( 1 << WAVE_SH ) - 1 )', '( ( 1UL << WAVE_SH ) - 1 )')
    t = must(t, '( 256 << ( LFO_SH ) )', '( 256UL << ( LFO_SH ) )')
    t = must(t, '( 511 << ENV_EXTRA )', '( 511L << ENV_EXTRA )')
    t = must(t, '( ( 1 << RATE_SH ) - 1 )', '( ( 1UL << RATE_SH ) - 1 )')
    t = re.sub(r'(?<![\w.])1\s*<<', '1L <<', t)                              # 2) 1 << N
    t = re.sub(r'(\b0x[0-9a-fA-F]+|\b\d+)u\s*<<', r'\1UL <<', t)           # 2) 0xffu << N
    t = re.sub(r'EnvelopeIncreaseTable\[\s*index\s*\]\s*<<', '((uint32_t)EnvelopeIncreaseTable[ index ]) <<', t)   # 3)
    t = re.sub(r'AttackSamplesTable\[\s*index\s*\]\s*<<', '((uint32_t)AttackSamplesTable[ index ]) <<', t)         # 3)
    t = must(t, '(waveBase[ index & waveMask ] * MulTable[ vol >> ENV_EXTRA ]) >> MUL_SH',
                '(((int32_t)waveBase[ index & waveMask ] * (int32_t)MulTable[ vol >> ENV_EXTRA ]) >> MUL_SH)')      # 4)
    if OPL2_ONLY:
        # 5) OPL2 uses waveforms 0..3 only (WaveBaseTable 0x000/0x200/0x200/0x800, masks 1023/1023/511/511) => entries 0x000..0x9FF.
        #    Waves 4..7 (OPL3 only) live at 0xA00..0xFFF: drop that part of the table (saves 3 KB of resident data).
        t = must(t, 'static int16_t WaveTable[ 8 * 512 ];', 'static int16_t WaveTable[ 0xA00 ];   /* OPL2-only build */')
        t, n = re.subn(r'^[ \t]*WaveTable\[\s*0x(?:a|b|c|d|e|f)00\s*\+\s*i\s*\]\s*=.*$', '', t, flags=re.M)
        if n != 6: raise SystemExit('expected 6 OPL3-only WaveTable fills, found %d' % n)
    return t

edit('DBOPL.H', h)
edit('DBOPL.CPP', cpp)
print('16-bit DBOPL patch set applied (%s) -> %s' % (','.join(sorted(opts)) or 'full', dst))
