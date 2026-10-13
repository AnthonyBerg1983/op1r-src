#!/usr/bin/env python3
"""MIDI clock-out probe for the OP-1 (docs/11 §5): capture F8 / FA / FB / FC / F2 with timestamps and measure jitter and
drift.

  python3 tools/midi_clock_probe.py --list
  python3 tools/midi_clock_probe.py monitor [--port OP-1]               # live, one line per beat
  python3 tools/midi_clock_probe.py record -o run.csv [--seconds 60]    # capture to CSV (Ctrl-C stops)
  python3 tools/midi_clock_probe.py analyse run.csv [--bpm 120] [--plot run.png] [--clock alsa|host]
  python3 tools/midi_clock_probe.py --selftest

OP-1: COM -> tempo sync out = MIDI (mode 1) or MIDI + PO (mode 3), USB cable to this PC.
Timestamps (Linux): `alsa` = ALSA sequencer event times (python-rtmidi deltas, summed), `host` = perf_counter in the
receive callback (adds Python scheduling noise; the difference between both shows the host's own noise).

Report per clock run (a run ends at a gap > 4 clock periods or at FC):
  interval  mean / std / percentiles / min / max, histogram, most common values (steps of 2.902 ms = 128-sample
            audio blocks at 44.1 kHz would show the clock is quantised to audio blocks)
  tempo     BPM from a line fit over the whole run, error vs --bpm in ppm (drift)
  phase     residual of each clock to the fitted line: std / max (jitter that accumulates musically)
  dropouts  gaps > 1.5 periods, bursts < 0.5 period (USB batching)
  transport FA / FB -> first F8 delay, F2 song position values
"""
import argparse, csv, math, sys, time

BLOCK_MS = 128 / 44100 * 1000                     # one OP-1 audio block

# ---------------------------------------------------------------- capture
def open_port(name):
    import rtmidi
    mi = rtmidi.MidiIn(rtmidi.API_LINUX_ALSA)
    ports = mi.get_ports()
    idx = next((i for i, p in enumerate(ports) if name.lower() in p.lower()), None)
    if idx is None:
        sys.exit(f'no MIDI input matching "{name}"; ports: {ports}')
    mi.open_port(idx)
    mi.ignore_types(sysex=True, timing=False, active_sense=True)   # keep clock / start / stop
    return mi, ports[idx]

def capture(port, on_event, seconds=None):
    """on_event(t_alsa, t_host, bytes); runs until Ctrl-C or `seconds`"""
    mi, name = open_port(port)
    state = {'t': 0.0}
    def cb(ev, _):
        msg, delta = ev
        state['t'] += delta
        on_event(state['t'], time.perf_counter(), msg)
    mi.set_callback(cb)
    print(f'listening on {name}' + (f' for {seconds} s' if seconds else ' (Ctrl-C to stop)'), file=sys.stderr)
    t_end = time.time() + seconds if seconds else None
    try:
        while t_end is None or time.time() < t_end:
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    mi.close_port()

def cmd_record(a):
    rows = []
    capture(a.port, lambda ta, th, m: rows.append((ta, th, m)), a.seconds)
    if not rows:
        sys.exit('nothing received (is tempo sync out on in COM?)')
    th0 = rows[0][1]
    with open(a.output, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['t_alsa', 't_host', 'bytes'])
        for ta, th, m in rows:
            w.writerow([f'{ta:.6f}', f'{th - th0:.6f}', ' '.join(f'{b:02X}' for b in m)])
    print(f'{len(rows)} messages -> {a.output}', file=sys.stderr)

def cmd_monitor(a):
    st = {'last': None, 'iv': [], 'n': 0, 'beat': 0}
    def ev(ta, th, m):
        t = ta if a.clock == 'alsa' else th
        if m[0] in (0xFA, 0xFB, 0xFC):
            print({0xFA: 'START', 0xFB: 'CONTINUE', 0xFC: 'STOP'}[m[0]]); st['last'] = None; st['iv'] = []; return
        if m[0] == 0xF2:
            print(f'SPP {m[1] | m[2] << 7}'); return
        if m[0] != 0xF8: return
        if st['last'] is not None: st['iv'].append((t - st['last']) * 1000)
        st['last'] = t
        if len(st['iv']) >= 24:
            iv = st['iv']; mean = sum(iv) / len(iv)
            sd = math.sqrt(sum((x - mean) ** 2 for x in iv) / len(iv))
            st['beat'] += 1
            print(f'beat {st["beat"]:5d}  {60000 / (mean * 24):7.2f} bpm  interval {min(iv):6.2f} / {mean:6.2f} / '
                  f'{max(iv):6.2f} ms  std {sd:5.2f} ms  late(>1.5x) {sum(x > 1.5 * mean for x in iv)}')
            st['iv'] = []
    capture(a.port, ev, a.seconds)

# ---------------------------------------------------------------- analysis
def pct(xs, p):
    s = sorted(xs); k = (len(s) - 1) * p / 100
    lo = int(k); hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)

def linfit(xs, ys):
    n = len(xs); mx = sum(xs) / n; my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return b, my - b * mx

def runs_of(events):
    """split F8 times into runs: at FC / FA, and at gaps > 4 periods"""
    runs, cur = [], []
    for t, m in events:
        if m[0] == 0xF8:
            if len(cur) > 2:
                med = sorted(cur[i + 1] - cur[i] for i in range(len(cur) - 1))[(len(cur) - 1) // 2]
                if t - cur[-1] > 4 * med: runs.append(cur); cur = []
            cur.append(t)
        elif m[0] in (0xFC, 0xFA) and cur:
            runs.append(cur); cur = []
    if cur: runs.append(cur)
    return [r for r in runs if len(r) >= 48]       # at least 2 beats

def analyse_run(ts, bpm=None):
    iv = [(b - a) * 1000 for a, b in zip(ts, ts[1:])]
    n = len(iv); mean = sum(iv) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in iv) / n)
    period, t0 = linfit(list(range(len(ts))), ts)                   # seconds per clock
    res = [(t - (t0 + i * period)) * 1000 for i, t in enumerate(ts)]
    r = {'clocks': len(ts), 'seconds': ts[-1] - ts[0], 'mean': mean, 'std': sd, 'min': min(iv), 'max': max(iv),
         'p1': pct(iv, 1), 'p50': pct(iv, 50), 'p99': pct(iv, 99), 'bpm': 60 / (period * 24),
         'phase_std': math.sqrt(sum(x * x for x in res) / len(res)), 'phase_max': max(abs(x) for x in res),
         'gaps': sum(x > 1.5 * mean for x in iv), 'bursts': sum(x < 0.5 * mean for x in iv), 'iv': iv, 'res': res}
    if bpm: r['ppm'] = (r['bpm'] / bpm - 1) * 1e6
    # interval values in 0.1 ms bins: the most common ones (block quantisation shows as steps of BLOCK_MS)
    bins = {}
    for x in iv: bins[round(x, 1)] = bins.get(round(x, 1), 0) + 1
    r['common'] = sorted(bins.items(), key=lambda kv: -kv[1])[:6]
    r['block_steps'] = sorted({round((x - mean) / BLOCK_MS) for x in iv if abs(x - mean) > BLOCK_MS / 2})
    return r

def report(events, bpm=None, label=''):
    out = []
    runs = runs_of(events)
    if not runs:
        return ['no clock run of >= 2 beats found']
    for k, ts in enumerate(runs):
        r = analyse_run(ts, bpm)
        out.append(f'run {k + 1}{label}: {r["clocks"]} clocks, {r["seconds"]:.1f} s, {r["bpm"]:.3f} bpm'
                   + (f' ({r["ppm"]:+.0f} ppm vs {bpm})' if bpm else ''))
        out.append(f'  interval ms  mean {r["mean"]:.3f}  std {r["std"]:.3f}  min {r["min"]:.3f}  P1 {r["p1"]:.3f}'
                   f'  P50 {r["p50"]:.3f}  P99 {r["p99"]:.3f}  max {r["max"]:.3f}')
        out.append(f'  phase ms     std {r["phase_std"]:.3f}  max {r["phase_max"]:.3f}  (vs a perfect clock at the fitted tempo)')
        out.append(f'  gaps (>1.5x) {r["gaps"]}  bursts (<0.5x) {r["bursts"]}')
        out.append('  most common intervals: ' + ', '.join(f'{v:.1f} ms x{c}' for v, c in r['common']))
        if r['block_steps']:
            out.append(f'  deviations in audio-block steps ({BLOCK_MS:.3f} ms): {r["block_steps"]}')
    # transport
    f8 = [t for t, m in events if m[0] == 0xF8]
    for t, m in events:
        if m[0] in (0xFA, 0xFB):
            nxt = next((x for x in f8 if x > t), None)
            out.append(f'{"START" if m[0] == 0xFA else "CONTINUE"} at {t:.3f} s -> first F8 '
                       + (f'{(nxt - t) * 1000:.2f} ms later' if nxt else 'never'))
        elif m[0] == 0xFC:
            out.append(f'STOP at {t:.3f} s; F8 after it: {sum(1 for x in f8 if x > t)}')
        elif m[0] == 0xF2:
            out.append(f'SPP {m[1] | m[2] << 7} at {t:.3f} s')
    return out

def load_csv(path, clock):
    ev, ta_th = [], []
    with open(path) as f:
        for row in csv.DictReader(f):
            t = float(row['t_alsa'] if clock == 'alsa' else row['t_host'])
            ev.append((t, [int(b, 16) for b in row['bytes'].split()]))
            ta_th.append((float(row['t_alsa']), float(row['t_host'])))
    return ev, ta_th

def cmd_analyse(a):
    ev, ta_th = load_csv(a.file, a.clock)
    print('\n'.join(report(ev, a.bpm, f' ({a.clock} timestamps)')))
    d = [(th - ta) * 1000 for ta, th in ta_th]
    m = sum(d) / len(d)
    print(f'host noise: callback time - ALSA time  std {math.sqrt(sum((x - m) ** 2 for x in d) / len(d)):.3f} ms'
          f'  max {max(d) - m:.3f} ms (above the mean)')
    if a.plot:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        runs = runs_of(ev)
        fig, ax = plt.subplots(3, 1, figsize=(11, 9))
        for k, ts in enumerate(runs):
            r = analyse_run(ts, a.bpm)
            ax[0].plot([t - ts[0] for t in ts[1:]], r['iv'], '.', ms=2, label=f'run {k + 1}')
            ax[1].hist(r['iv'], bins=200, alpha=0.6)
            ax[2].plot([t - ts[0] for t in ts], r['res'], lw=0.7)
        ax[0].set_ylabel('interval ms'); ax[0].set_xlabel('s'); ax[0].legend()
        ax[1].set_xlabel('interval ms'); ax[1].set_ylabel('count')
        ax[2].set_ylabel('phase error ms'); ax[2].set_xlabel('s')
        fig.tight_layout(); fig.savefig(a.plot); print('->', a.plot)

# ---------------------------------------------------------------- self-test
def selftest():
    import random
    random.seed(1)
    bpm, ppm = 120.0, 150.0
    period = 60 / (bpm * 24) * (1 - ppm / 1e6)       # +150 ppm fast
    ev = [(0.0, [0xFA])]
    t = 0.01
    blk = BLOCK_MS / 1000
    for i in range(24 * 120):                         # 1 minute, jitter: block quantisation + 0.2 ms noise
        ideal = 0.01 + i * period
        ev.append((math.ceil(ideal / blk) * blk + random.gauss(0, 0.0002), [0xF8]))
    ev.append((ev[-1][0] + 0.005, [0xFC]))
    ev.sort(key=lambda e: e[0])
    r = analyse_run([t for t, m in ev if m[0] == 0xF8], bpm)
    ok = abs(r['ppm'] - ppm) < 20 and 0.5 < r['phase_std'] < 1.6 and r['gaps'] == 0 and r['block_steps']
    print('\n'.join(report(ev, bpm, ' (synthetic)')))
    print('selftest', 'ok' if ok else 'FAILED')
    return 0 if ok else 1

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--list', action='store_true'); ap.add_argument('--selftest', action='store_true')
    sub = ap.add_subparsers(dest='cmd')
    for name in ('monitor', 'record'):
        p = sub.add_parser(name)
        p.add_argument('--port', default='OP-1'); p.add_argument('--seconds', type=float)
        p.add_argument('--clock', choices=('alsa', 'host'), default='alsa')
        if name == 'record': p.add_argument('-o', '--output', required=True)
    p = sub.add_parser('analyse')
    p.add_argument('file'); p.add_argument('--bpm', type=float); p.add_argument('--plot')
    p.add_argument('--clock', choices=('alsa', 'host'), default='alsa')
    a = ap.parse_args()
    if a.selftest: return selftest()
    if a.list:
        import rtmidi
        print('\n'.join(rtmidi.MidiIn(rtmidi.API_LINUX_ALSA).get_ports())); return 0
    {'monitor': cmd_monitor, 'record': cmd_record, 'analyse': cmd_analyse}.get(a.cmd, lambda _: ap.print_help())(a)
    return 0

if __name__ == '__main__':
    sys.exit(main())
