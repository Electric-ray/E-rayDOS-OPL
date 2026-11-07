#!/usr/bin/env python3
"""VSBHDA DBOPL 사본 -> 32bit 누적 버전 생성 (원본은 수정하지 않는다).
문제: VSBHDA 의 DBOPL 은 출력을 int16_t 로 받아 `output[i] += sample` 로 누적하고 클램프가 없다.
      채널이 많이/크게 울리면 16bit 합산이 wrap(부호 반전)되어 지직거린다. (원본 DOSBox 는 int32 누적 후 클램프)
해결: 블록 생성 계층의 출력 버퍼를 int32_t 로 바꾸고, 어댑터(dbopl_core.cpp)가 int16 으로 클램프한다.
usage: gen_dbopl32.py <VSBHDA/src dir> <out dir>"""
import sys, re, os, shutil
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for f in ('DBOPL.CPP', 'DBOPL.H', 'CONFIG.HPP'):
    shutil.copy(os.path.join(src, f), os.path.join(dst, f))

pats = [
    (r'int16_t\s*\*\s*output', 'int32_t* output'),      # SynthHandler / BlockTemplate / GenerateBlock2,3 / GeneratePercussion
    (r'int16_t\s*\*\s*base',   'int32_t* base'),
    (r'int16_t\s*\*\s*buffer', 'int32_t* buffer'),      # Chip::Generate
]
total = 0
for f in ('DBOPL.CPP', 'DBOPL.H'):
    p = os.path.join(dst, f)
    t = open(p, encoding='latin-1').read()
    for a, b in pats:
        t, n = re.subn(a, b, t); total += n
    open(p, 'w', encoding='latin-1', newline='').write(t)
print('patched %d declarations -> int32 accumulation' % total)
