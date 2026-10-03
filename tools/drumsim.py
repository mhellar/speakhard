# Offline port of speakbeat_hard's Perc engine, to measure and balance the kit.
import math, random, sys, wave, struct
import numpy as np

FS = 22050
def dcoef(sec): return math.exp(-1.0 / (sec * FS))
def wrap1(p): return p - math.floor(p)
def fsin(p): return math.sin(2 * math.pi * p)
def softclip(x):
    if x > 3: return 1.0
    if x < -3: return -1.0
    return x * (27 + x * x) / (27 + 9 * x * x)
def nz(): return random.uniform(-1, 1)

def render(p, sec, chugHz=73.4):
    name, wave_, f0, f1, ptime, dec, gain, noise, drive = p
    env, d, penv, pd = 1.0, dcoef(dec), 1.0, dcoef(ptime)
    ph = ph2 = prev = lp = bp = 0.0; mph = [0.0] * 6; age = 0
    out = np.zeros(int(sec * FS))
    MF = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    CR = [1.0, 1.006, 1.4983, 1.5073, 2.0, 1.994]
    for n in range(len(out)):
        if env < 0.0001: break
        f = f1 + (f0 - f1) * penv; penv *= pd; o = 0.0
        if wave_ == 'SINE':
            ph = wrap1(ph + f / FS); o = fsin(ph) + nz() * noise * penv
        elif wave_ == 'KICK':
            ph = wrap1(ph + f / FS); o = fsin(ph) + (nz() * noise * (1 - age / 110) if age < 110 else 0)
        elif wave_ == 'SNR':
            ph = wrap1(ph + f / FS); nn = nz(); h = nn - prev; prev = nn
            o = fsin(ph) * (0.2 + 0.8 * penv) + h * noise
        elif wave_ == 'CLAP':
            nn = nz(); q = 0.42; lp += q * bp; bp += q * (nn - lp - 0.6 * bp)
            g = 1.0 - (age % 220) / 260.0 if age < 660 else 1.0
            o = bp * 1.6 * g
        elif wave_ == 'METAL':
            s = 0.0; sc = f0 / FS
            for i in range(6):
                mph[i] = wrap1(mph[i] + MF[i] * sc); s += 1 if mph[i] < 0.5 else -1
            lp += 0.55 * (s - lp); h = s - lp; bp += 0.55 * (h - bp); o = (h - bp)
        elif wave_ == 'FM':
            ph = wrap1(ph + f0 / FS); ph2 = wrap1(ph2 + f0 * f1 / FS)
            o = fsin(wrap1(ph + noise * (0.25 + penv) * fsin(ph2)))
        elif wave_ == 'SLAM':
            ph = wrap1(ph + f / FS); lp += 0.12 * (nz() - lp)
            o = lp * 2.6 * (0.35 + 0.65 * penv) + fsin(ph) * penv
        elif wave_ == 'CHUG':
            s = 0.0; r = chugHz / FS
            for i in range(6):
                mph[i] = wrap1(mph[i] + r * CR[i]); s += mph[i] * 2 - 1
            fz = softclip(s * 1.1); c = 0.05 + 0.32 * penv
            lp += c * (fz - lp); bp += c * (lp - bp); o = bp * 1.3
        age += 1
        o *= env; env *= d
        if drive > 0: o = softclip(o * drive) / 1.15
        out[n] = o * gain
    return out

def hp(x, fc):                       # one-pole highpass, roughly what a small speaker hears
    a = math.exp(-2 * math.pi * fc / FS); y = np.zeros_like(x); s = 0.0
    for i, v in enumerate(x): s = a * s + (1 - a) * v; y[i] = v - s
    return y

def stats(x):
    w = x[:int(0.1 * FS)]
    rms = math.sqrt(np.mean(w ** 2)) + 1e-9
    xs = hp(x, 150); ws = xs[:int(0.1 * FS)]
    rmsS = math.sqrt(np.mean(ws ** 2)) + 1e-9
    return np.max(np.abs(x)), 20 * math.log10(rms), 20 * math.log10(rmsS)

def load_kit(path):
    import re
    src = open(path, encoding='utf-8').read()
    body = src[src.index('const PercDef PERC[12]'):]
    body = body[:body.index('};')]
    kit = []
    for m in re.finditer(r'\{"([^"]+)",\s*W_(\w+),\s*([^}]*)\}', body):
        vals = [float(v.strip().rstrip('f')) for v in m.group(3).split(',')]
        kit.append((m.group(1), m.group(2), *vals))
    return kit

if __name__ == '__main__':
    random.seed(1)
    kit = load_kit(sys.argv[1])
    print(f"{'name':6} {'peak':>5} {'rms100ms':>9} {'>150Hz':>7}")
    for p in kit:
        x = render(p, 1.5)
        pk, r, rs = stats(x)
        print(f"{p[0]:6} {pk:5.2f} {r:8.1f}dB {rs:6.1f}dB")
