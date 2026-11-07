#!/usr/bin/env python3
"""PIT ch2 (8254) -> PC speaker 출력 파형 시뮬레이터.
mode 0 (LSB-only, 0x90): count write -> OUT low immediately, high at terminal count (= PWM duty)
mode 3 (LSB-only, 0x96): square wave, new count takes effect at the end of the current half period (= frequency, ~50% duty)
스피커/귀는 저역통과이므로, 샘플 주기(T tick) 박스 평균을 '들리는 신호'로 보고 원신호와의 상관/SNR을 계산한다."""
import numpy as np, sys

PIT = 1193182.0

def render(counts, T, mode):
    """counts: 샘플별 PIT count(0..255). T: 샘플 주기(PIT tick). 반환: tick 단위 0/1 OUT 배열"""
    n = len(counts)
    out = np.zeros(n * T, dtype=np.uint8)
    if mode == 0:
        for k, c in enumerate(counts):
            low = min(int(c) + 1, T)          # N+1 tick 동안 low, 이후 high (count>=T 이면 주기 내내 low)
            out[k*T + low:(k+1)*T] = 1
        return out
    # mode 3
    pos = 0; level = 1; remaining = 0; cur = 2
    for k, c in enumerate(counts):
        cnt = int(c) if c >= 2 else 2          # count 0/1 은 비정상 -> 2 로 제한
        pend = cnt
        end = (k + 1) * T
        while pos < end:
            if remaining == 0:                  # 반주기 종료 -> 대기 중 count 로 재장전
                cur = pend
                remaining = (cur + 1) // 2 if level else cur // 2   # high 반주기는 올림
                remaining = max(1, remaining)
            step = min(remaining, end - pos)
            out[pos:pos+step] = level
            pos += step; remaining -= step
            if remaining == 0: level ^= 1
    return out

def heard(out, T):
    n = len(out) // T
    return out[:n*T].reshape(n, T).mean(axis=1)    # 샘플 주기 박스필터

def metrics(x, y):
    x = x - x.mean(); y = y - y.mean()
    if y.std() < 1e-9: return 0.0, -99.0
    r = float(np.dot(x, y) / (np.linalg.norm(x) * np.linalg.norm(y)))
    g = np.dot(x, y) / np.dot(y, y)
    err = x - g * y
    snr = 10 * np.log10(np.dot(x, x) / max(np.dot(err, err), 1e-12))
    return r, float(snr)

def run(rate):
    T = int(round(PIT / rate))
    n = int(rate * 0.25)
    t = np.arange(n) / rate
    sig = 0.5 * np.sin(2*np.pi*440*t) + 0.3 * np.sin(2*np.pi*1320*t) + 0.15 * np.sin(2*np.pi*2200*t)
    sig /= np.abs(sig).max()
    u8 = np.clip(((sig * 0.5 + 0.5) * 255).round(), 0, 255).astype(int)         # hearo 식: U8 샘플 그대로
    scaled = np.clip(((1 - (sig * 0.5 + 0.5)) * (T - 2)).round() + 0, 0, T - 2).astype(int)  # T에 맞게 스케일(밝을수록 low 짧게)
    print(f"--- {rate} Hz  (T={T} PIT tick/sample, mode0 유효 진폭단계={T-1}레벨 ≈ {np.log2(T-1):.1f}bit) ---")
    for name, cnt, mode in [("mode0 + 스케일 count (제안)", scaled, 0),
                            ("mode0 + raw U8 (hearo 샘플 그대로)", u8, 0),
                            ("mode3 + raw U8 (hearo 실제 코드)", u8, 3),
                            ("mode3 + 스케일 count", scaled + 2, 3)]:
        out = render(cnt, T, mode)
        h = heard(out, T)
        r, snr = metrics(sig, h)
        r2, snr2 = metrics(sig, -h)
        r, snr = (r, snr) if abs(r) >= abs(r2) or True else (r2, snr2)
        best = max([(r, snr), (r2, snr2)], key=lambda p: p[0])
        print(f"{name:40s} 상관={best[0]:+.3f}  SNR={best[1]:6.1f} dB   duty평균={out.mean():.2f}")

if __name__ == '__main__':
    for r in (11025, 18000, 22050):
        run(r)
