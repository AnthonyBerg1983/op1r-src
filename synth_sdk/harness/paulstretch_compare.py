#!/usr/bin/env python3
"""Host checks of the fixed-point paulstretch core (shelved/paulstretch/paulstretch_dsp.c) against float references.

  python3 harness/paulstretch_compare.py [--wav in.wav]   (pyenv python: numpy, matplotlib)

Builds the core as a shared library (ctypes) and checks:
  1. real FFT / inverse FFT vs numpy (SNR)
  2. the whole stretch vs a float paulstretch with the SAME random phases, window and source positions
     (isolates the fixed-point error; the magnitude uses the same alpha-max-beta-min approximation)
  3. the approximation itself: float with exact |X| vs float with the approximation (long-term spectrum, LSD)
  4. work per frame / spread per 128-sample block
Writes harness/out/paulstretch_compare.png and WAVs (input, port, float reference) for listening.
"""
import argparse, ctypes, os, subprocess, sys, wave
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(HERE)
OUT = os.path.join(HERE, 'out')
SR, NMAX = 44100, 16384
ALPHA, BETA = 31470 / 32768, 13036 / 32768


def build():
    subprocess.run([sys.executable, os.path.join(SDK, 'shelved', 'paulstretch', 'gen_paulstretch_tables.py')], check=True,
                   capture_output=True)
    so = os.path.join(HERE, 'build', 'libpaulstretch.so')
    os.makedirs(os.path.dirname(so), exist_ok=True)
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-shared', '-fPIC', '-fsanitize=undefined', '-I', SDK,
                    os.path.join(SDK, 'shelved', 'paulstretch', 'paulstretch_dsp.c'), '-o', so], check=True)
    lib = ctypes.CDLL(so)
    i32p = np.ctypeslib.ndpointer(np.int32, flags='C'); i16p = np.ctypeslib.ndpointer(np.int16, flags='C')
    lib.ps_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]
    lib.ps_set_source.argtypes = [ctypes.c_void_p, i16p, ctypes.c_int32, ctypes.c_int32]
    lib.ps_set_stretch.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.ps_render.argtypes = [ctypes.c_void_p, i16p, ctypes.c_int]
    lib.ps_units_per_frame.argtypes = [ctypes.c_void_p]
    lib.ps_set_pitch_inv.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    lib.ps_test_rfft.argtypes = [ctypes.c_void_p, i16p, i32p, i32p]
    lib.ps_test_irfft.argtypes = [ctypes.c_void_p, i32p, i32p, i32p]
    return lib


class Port:
    def __init__(self, lib, log2n, seed=1):
        self.lib, self.n = lib, 1 << log2n
        self.st = ctypes.create_string_buffer(512)
        self.mem = ctypes.create_string_buffer((8 << log2n) + 16)   # PS_MEM_BYTES
        lib.ps_init(self.st, self.mem, log2n, seed)


def window(n):                                   # as win() in the C code (table at sample centres, stride)
    s = NMAX // n
    m = np.arange(n) * s + s // 2
    x = -1 + (2 * m + 1) / NMAX
    return np.round((1 - x * x) ** 1.25 * 32767) / 32768


def snr(ref, x):
    return 10 * np.log10(np.sum(ref ** 2) / max(np.sum((ref - x) ** 2), 1e-30))


def phases(r, m):                                 # the C LCG (state r), in the order ph_pairs consumes it
    ph = np.zeros(m + 1)
    for k in range(1, m // 2 + 1):
        for idx in ((k, m - k) if k != m - k else (k,)):
            r = (r * 1664525 + 1013904223) & 0xFFFFFFFF
            ph[idx] = (r >> 18) / 16384 * 2 * np.pi
    return ph, r


def adv_q16(h, st_q8):
    num = h << 16
    q, r = divmod(num, st_q8)
    return (q << 8) + (r << 8) // st_q8


def float_stretch(x, log2n, st_q8, frames, seed=1, approx=True):
    """float paulstretch with the port's frame positions and phase sequence; output delayed by one hop like the
    port (ps_render plays a silent frame first)"""
    n = 1 << log2n; h = n // 2; w = window(n)
    out = np.zeros((frames + 1) * h); tail = np.zeros(h)
    pos, frac, adv, r = 0, 0, adv_q16(h, st_q8), seed | 1
    for f in range(frames):
        idx = (pos + np.arange(n)) % len(x)
        X = np.fft.rfft(x[idx] * w) * 2 / n
        if approx:
            a, b = np.abs(X.real), np.abs(X.imag)
            mag = np.maximum(a, b) * ALPHA + np.minimum(a, b) * BETA
        else:
            mag = np.abs(X)
        ph, r = phases(r, h)
        Y = mag * np.exp(1j * ph); Y[0] = Y[h] = 0
        y = np.fft.irfft(Y) * n / 2 * w
        out[(f + 1) * h:(f + 2) * h] = tail + y[:h]
        tail = y[h:]
        frac += adv; pos = (pos + (frac >> 16)) % len(x); frac &= 0xFFFF
    return out


def to16(x): return np.clip(np.round(x * 32768), -32768, 32767).astype(np.int16)


def wav_write(path, x):
    with wave.open(path, 'wb') as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(SR); f.writeframes(to16(x).tobytes())


def test_signal():
    t = np.arange(int(SR * 2.0)) / SR
    tone = sum(0.12 / (k + 1) * np.sin(2 * np.pi * 220 * (k + 1) * t) for k in range(6))   # saw-ish chord tone
    chirp = 0.15 * np.sin(2 * np.pi * (300 + 900 * t) * t)
    env = np.minimum(1, t * 8) * np.exp(-t * 0.8)
    noise = np.random.default_rng(3).normal(0, 0.03, len(t))
    return (tone + chirp) * env + noise


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--wav'); a = ap.parse_args()
    lib = build(); os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(1)

    # 1. FFT accuracy
    for log2n in (10, 13, 14):
        p = Port(lib, log2n); n = p.n; h = n // 2
        x = to16(rng.normal(0, 0.2, n))
        re = np.zeros(h + 1, np.int32); im = np.zeros(h + 1, np.int32)
        lib.ps_test_rfft(p.st, x, re, im)
        X = np.fft.rfft(np.floor(x.astype(float) * np.round(window(n) * 32768) / 32768) * 256) * 2 / n
        Xp = re + 1j * im
        s_f = snr(np.concatenate([X.real, X.imag]), np.concatenate([Xp.real, Xp.imag]))
        Y = (rng.normal(0, 1, h + 1) + 1j * rng.normal(0, 1, h + 1)) * 4000; Y[0] = Y[h] = 0
        y = np.zeros(n, np.int32)
        lib.ps_test_irfft(p.st, np.round(Y.real).astype(np.int32), np.round(Y.imag).astype(np.int32), y)
        yr = np.fft.irfft(np.round(Y.real) + 1j * np.round(Y.imag)) * n / 2
        s_i = snr(yr, y.astype(float))
        print(f'N={n:5d}: forward rFFT SNR {s_f:5.1f} dB, inverse SNR {s_i:5.1f} dB')
        assert s_f > 60 and s_i > 60, (s_f, s_i)

    # 2. + 3. whole stretch
    if a.wav:
        with wave.open(a.wav) as f:
            raw = np.frombuffer(f.readframes(f.getnframes()), np.int16).reshape(-1, f.getnchannels())[:, 0]
        src = raw.astype(float) / 32768
    else:
        src = test_signal()
    src16 = to16(src); srcf = src16 / 32768
    log2n, st_q8, secs = 13, 256 * 8, 8.0
    p = Port(lib, log2n); h = p.n // 2
    lib.ps_set_source(p.st, src16, len(src16), 0); lib.ps_set_stretch(p.st, st_q8)
    nout = int(SR * secs) // 128 * 128
    out = np.zeros(nout, np.int16)
    for i in range(0, nout, 128):                    # like the engine: 128-sample blocks
        blk = np.zeros(128, np.int16); lib.ps_render(p.st, blk, 128); out[i:i + 128] = blk
    frames = nout // h
    ref = float_stretch(srcf, log2n, st_q8, frames)[:nout]
    exact = float_stretch(srcf, log2n, st_q8, frames, approx=False)[:nout]
    port = out / 32768
    s = snr(ref[h:], port[h:])
    print(f'stretch 8x, N=8192: port vs float (same phases) SNR {s:.1f} dB, rms port {np.std(port):.4f} '
          f'float {np.std(ref):.4f} src {np.std(srcf):.4f}')
    assert s > 40, s

    def ltas(x, nf=2048):                            # long-term average spectrum, dB
        fr = np.lib.stride_tricks.sliding_window_view(x, nf)[::nf // 2] * np.hanning(nf)
        return 10 * np.log10(np.mean(np.abs(np.fft.rfft(fr, axis=1)) ** 2, axis=0) + 1e-12)
    la, le = ltas(ref), ltas(exact)
    band = slice(int(50 / SR * 2048), int(12000 / SR * 2048))
    lsd = np.sqrt(np.mean((la[band] - le[band]) ** 2))
    print(f'magnitude approximation (alpha-max-beta-min) vs exact |X|: LTAS LSD {lsd:.2f} dB, level '
          f'{20 * np.log10(np.std(ref) / np.std(exact)):+.2f} dB')

    # 3b. spectral pitch: an octave up / a fifth down moves the 220 Hz partial, no aliasing
    tone = to16(0.4 * np.sin(2 * np.pi * 220 * np.arange(SR * 2) / SR))
    for pinv, want in ((32768, 440.0), (98304, 146.7)):
        q = Port(lib, 13); lib.ps_set_source(q.st, tone, len(tone), 0); lib.ps_set_pitch_inv(q.st, pinv)
        o = np.zeros(SR * 2, np.int16)
        for i in range(0, len(o) - 128, 128):
            b = np.zeros(128, np.int16); lib.ps_render(q.st, b, 128); o[i:i + 128] = b
        sp = np.abs(np.fft.rfft(o[8192:].astype(float) * np.hanning(len(o) - 8192)))
        f = np.fft.rfftfreq(len(o) - 8192, 1 / SR)[np.argmax(sp)]
        print(f'pitch 1/{pinv / 65536:.2f}: peak {f:.1f} Hz (want {want})')
        assert abs(f - want) < want * 0.01, f

    # 4. work
    upf = lib.ps_units_per_frame(p.st)
    for l2 in (12, 13, 14):
        q = Port(lib, l2); u = lib.ps_units_per_frame(q.st)
        print(f'N={1 << l2:5d}: {u} units/frame, {u * 128 * 4 // 3 // ((1 << l2) // 2) + 1} units per 128-sample block')
    del upf

    wav_write(os.path.join(OUT, 'paulstretch_input.wav'), srcf)
    wav_write(os.path.join(OUT, 'paulstretch_port.wav'), port)
    wav_write(os.path.join(OUT, 'paulstretch_float.wav'), exact / max(1e-9, np.max(np.abs(exact))) * 0.8)

    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(4, 1, figsize=(11, 12))
    for axi, (x, t) in zip(ax[:3], ((srcf, 'input'), (port, 'port (fixed point), stretch 8x, N=8192'),
                                    (exact, 'float reference, exact |X|'))):
        axi.specgram(x + 1e-9, NFFT=1024, Fs=SR, noverlap=512, cmap='magma', vmin=-120); axi.set_title(t)
        axi.set_ylim(0, 8000)
    fq = np.fft.rfftfreq(2048, 1 / SR)
    ax[3].plot(fq, la, label='float, approx |X|'); ax[3].plot(fq, le, label='float, exact |X|')
    ax[3].plot(fq, ltas(port), label='port', alpha=0.7); ax[3].set_xscale('log'); ax[3].set_xlim(50, 16000)
    ax[3].legend(); ax[3].set_title('long-term average spectrum (dB)')
    fig.tight_layout(); fig.savefig(os.path.join(OUT, 'paulstretch_compare.png'), dpi=80)
    print('->', os.path.join(OUT, 'paulstretch_compare.png'))


if __name__ == '__main__':
    main()
