#!/usr/bin/env python3
"""Host prototype of the "aether" tilt theremin (docs/36, step 2): sensor model -> tilt -> pitch / volume / vibrato
-> theremin tone. Block-based like the engine (128 samples, sensor sample-and-hold at ~69 Hz, 4 mg steps + noise,
Z offset / gain as measured). Writes WAVs + a plot per scenario.

  python3 harness/aether_proto.py [outdir]      (default /tmp/aether)"""
import os, sys, wave
import numpy as np

SR, BLOCK = 44100, 128
SENSOR_DT = 640 / SR                     # firmware reads every 10 x 64-frame blocks (~69 Hz)
G = 16384                                # raw units per g
LSB = 64                                 # 10-bit left-justified: 4 mg steps

# engine parameters (the 4 knobs + Shift functions, defaults)
RANGE = 12.0                             # semitones at FULL tilt (blue)
FULL = 40.0                              # degrees for the full range
DEAD = 3.0                               # dead zone around home, degrees (Shift blue)
TIMBRE = 0.35                            # 0 = pure sine .. 1 = reedy (green)
GLIDE = 0.06                             # seconds, key to key (Shift green)
VIB = 0.5                                # vibrato from motion, 0..1 (white)
VIB_RATE = 5.5                           # Hz (Shift white)
VOL_TILT = 1.0                           # how much side tilt changes the volume (red)
SMOOTH = 0.03                            # sensor smoothing time constant, seconds


def sensor(fwd_deg, side_deg, shake_g, rng):
    """true tilt (+ shake acceleration in g) -> raw X, Y, Z as the OP-1 reports them (docs/36 table)"""
    f, s = np.radians(fwd_deg), np.radians(side_deg)
    gx, gy, gz = np.sin(s), -np.sin(f) * np.cos(s), np.cos(f) * np.cos(s)
    x = gx + shake_g[0]; y = gy + shake_g[1]; z = (gz + shake_g[2]) * 0.95 + 0.094   # Z gain / offset (measured)
    raw = np.array([x, y, z]) * G + rng.normal(0, 1.2 * LSB, 3)
    return np.clip(np.round(raw / LSB) * LSB, -32768, 32767)


def render(path, keys, seconds, name, outdir, seed=1):
    """path(t) -> (fwd_deg, side_deg, shake xyz in g); keys(t) -> MIDI note or None"""
    rng = np.random.default_rng(seed)
    n_blocks = int(seconds * SR / BLOCK)
    out = np.zeros(n_blocks * BLOCK)
    log = []
    sm = None; slow = None; prev_sm = None; home = None; next_read = 0.0; raw = None
    note_hz = None; phase = 0.0; gain = 0.0; vib_env = 0.0; vib_ph = 0.0; 
    a_sm = 1 - np.exp(-BLOCK / SR / SMOOTH)
    a_gl = 1 - np.exp(-BLOCK / SR / GLIDE)
    for b in range(n_blocks):
        t = b * BLOCK / SR
        if t >= next_read:                                   # the firmware's ~69 Hz sample-and-hold
            f, s, sh = path(t)
            raw = sensor(f, s, np.array(sh), rng); next_read += SENSOR_DT
        v = raw / G
        sm = v if sm is None else sm + a_sm * (v - sm)       # one-pole per block
        fwd = np.degrees(np.arctan2(-sm[1], sm[2])); side = np.degrees(np.arctan2(sm[0], sm[2]))
        key = keys(t)
        if key is not None and home is None: home = (fwd, side)          # note-on: this angle is "home"
        if key is None: home = None
        # pitch: dead zone, then linear in degrees up to FULL
        d = 0.0
        if home is not None:
            d = fwd - home[0]; d = np.sign(d) * max(abs(d) - DEAD, 0.0)
        semis = RANGE * np.clip(d / (FULL - DEAD), -1, 1)
        # volume: raising the right side (side angle down) fades out; the other way stays full
        vol = 1.0
        if home is not None:
            vol = np.clip(1 + VOL_TILT * min(side - home[1], 0.0) / 30.0, 0, 1)
        # motion: how much the vector's velocity changes (velocity minus its 150 ms average, g/s) = shaking in any
        # direction; a tilt at steady speed gives ~0. Opens the vibrato depth (fast attack, ~1 s release)
        vel = (sm - prev_sm) * SR / BLOCK if prev_sm is not None else np.zeros(3); prev_sm = sm.copy()
        slow = vel.copy() if slow is None else slow + (1 - np.exp(-BLOCK / SR / 0.15)) * (vel - slow)
        hp = np.linalg.norm(vel - slow); m = np.clip((hp - 0.5) * 0.5, 0, 1)
        vib_env += (m - vib_env) * (0.3 if m > vib_env else 0.003)
        target_hz = 440 * 2 ** ((key - 69) / 12) if key is not None else note_hz
        if target_hz is not None:
            note_hz = target_hz if note_hz is None else note_hz * (target_hz / note_hz) ** a_gl   # log glide
        target_gain = vol if key is not None else 0.0
        g0, g1 = gain, gain + (target_gain - gain) * (0.15 if key is not None else 0.08)      # attack / release
        gain = g1
        if note_hz is None:
            continue
        i = np.arange(BLOCK)
        vib = VIB * vib_env * 0.4 * np.sin(vib_ph + 2 * np.pi * VIB_RATE * i / SR)             # up to +-40 cents
        vib_ph += 2 * np.pi * VIB_RATE * BLOCK / SR
        hz = note_hz * 2 ** ((semis + vib) / 12)
        ph = phase + np.cumsum(2 * np.pi * hz / SR); phase = ph[-1] % (2 * np.pi)
        sn = np.sin(ph)
        y = np.tanh((1 + 4 * TIMBRE) * (sn + 0.3 * TIMBRE * sn * sn)) / np.tanh(1 + 4 * TIMBRE)  # even harmonics
        out[b * BLOCK:(b + 1) * BLOCK] = y * np.linspace(g0, g1, BLOCK, endpoint=False) * 0.5
        log.append((t, fwd, side, semis + (key or 0) - 69 if key else np.nan, gain, vib_env))
    os.makedirs(outdir, exist_ok=True)
    wav = os.path.join(outdir, name + '.wav')
    with wave.open(wav, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(out, -1, 1) * 32767).astype('<i2').tobytes())
    return wav, np.array(log)


def plot(logs, outdir):
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(len(logs), 1, figsize=(10, 3 * len(logs)), squeeze=False)
    for a, (name, L) in zip(ax[:, 0], logs):
        a.set_title(name); a.plot(L[:, 0], L[:, 1], label='forward tilt (deg)'); a.plot(L[:, 0], L[:, 2], label='side tilt (deg)')
        b = a.twinx(); b.plot(L[:, 0], L[:, 3], 'k', label='pitch (semitones from A4)'); b.plot(L[:, 0], L[:, 4] * 10, 'r', label='gain x10')
        b.plot(L[:, 0], L[:, 5] * 10, 'g', label='vibrato env x10')
        a.legend(loc='upper left', fontsize=7); b.legend(loc='upper right', fontsize=7)
    fig.tight_layout(); p = os.path.join(outdir, 'aether.png'); fig.savefig(p, dpi=80); return p


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/aether'
    hold = lambda k, a, b: (lambda t: k if a <= t < b else None)
    still = [0, 0, 0]
    scen = {
        # A3 held, the OP-1 tilted forward slowly 0 -> 40 deg and back: an octave swoop (dead zone at the start)
        'swoop': (lambda t: (40 * np.sin(np.pi * min((t - 0.5) / 3.5, 1)) ** 2 if t > 0.5 else 0, 0, still), hold(57, 0.2, 4.6), 5),
        # held note, right side raised and lowered twice: fade out / in like the volume loop
        'fade': (lambda t: (0, -30 * np.sin(np.pi * (t - 0.5) / 2) ** 2 if t > 0.5 else 0, still), hold(64, 0.2, 4.6), 5),
        # held note, small 6 Hz hand shake from t = 1.5 s: motion vibrato
        'vibrato': (lambda t: (2 * np.sin(2 * np.pi * 6 * t) if t > 1.5 else 0, 0,
                               [0.15 * np.sin(2 * np.pi * 6 * t) if t > 1.5 else 0, 0, 0]), hold(69, 0.2, 4.6), 5),
        # legato melody while held 10 deg forward (home taken at the first note-on: no pitch offset), glides between keys
        'melody': (lambda t: (10, 0, still), lambda t: [None, 60, 62, 64, 67, 64, 62, 60, None][min(int(t / 0.5), 8)], 4.5),
    }
    logs = []
    for name, (path, keys, sec) in scen.items():
        wav, L = render(path, keys, sec, name, out)
        logs.append((name, L)); print(wav)
    print(plot(logs, out))
    sw = dict(logs)['swoop']
    assert np.nanmax(sw[:, 3]) - np.nanmin(sw[:, 3]) > 11      # an octave from 40 deg (range 12)
    fd = dict(logs)['fade']; held = fd[(fd[:, 0] > 0.6) & (fd[:, 0] < 4.5)]
    assert held[:, 4].min() < 0.1 and held[:, 4].max() > 0.9     # silence and full volume
    vb = dict(logs)['vibrato']
    assert vb[(vb[:, 0] > 0.5) & (vb[:, 0] < 1.4), 5].max() < 0.05 and vb[vb[:, 0] > 2, 5].mean() > 0.5   # noise: off; shake: on
    for nm in ('swoop', 'fade'):                                # slow tilting is not shaking
        assert dict(logs)[nm][:, 5].max() < 0.2, (nm, dict(logs)[nm][:, 5].max())
    print('ok')


if __name__ == '__main__':
    main()
