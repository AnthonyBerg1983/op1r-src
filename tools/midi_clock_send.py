#!/usr/bin/env python3
"""Drive the OP-1 as a MIDI clock slave and measure how it follows (docs/11 §3, §5).

The PC is the external sequencer: it sends F8 (24 per beat) plus FA / FB / FC / F2 with busy-wait timing, and records
what the OP-1 sends back on the same USB cable: sequencer notes (the probe) and, in sync mode 4, forwarded clocks.
Both use the same host clock (perf_counter), so delays are directly comparable.

OP-1 setup: COM -> tempo sync = MIDI in (mode 2) or MIDI in + PO (mode 4); a sequencer with notes on 1/16 steps
(dots or euclid are easiest, each trigger goes out as MIDI); OP-1 MIDI channel any.

  python3 tools/midi_clock_send.py run --bpm 120 --seconds 30 -o run.csv            # FA, clock, FC
  python3 tools/midi_clock_send.py run --bpm 100 --ramp 140 --seconds 40 -o ramp.csv # tempo ramp
  python3 tools/midi_clock_send.py run --bpm 120 --jitter 1.0 -o jit.csv             # +-1 ms gaussian jitter
  python3 tools/midi_clock_send.py run --bpm 120 --scenario stopcont -o sc.csv       # start, stop, continue
  python3 tools/midi_clock_send.py run --bpm 120 --scenario spp --spp 32 -o spp.csv  # stop, SPP 32, continue
  python3 tools/midi_clock_send.py analyse run.csv [--grid 6] [--plot run.png]
  python3 tools/midi_clock_send.py --selftest

Report:
  notes     each OP-1 note-on is placed on the clock timeline (fractional clock index from the sent clocks); its
            distance to the nearest grid step (default 6 clocks = 1/16) is the timing error: mean = latency, std =
            jitter, trend over time = drift (ms per minute); worst early / late; steps missed (gaps in the grid)
  start     FA / FB -> first note, and which step (clock index) it landed on
  forward   (mode 4) OP-1 F8 out vs the sent F8: delay mean / std / max, missing or extra clocks
"""
import argparse, csv, math, random, sys, threading, time

# ---------------------------------------------------------------- MIDI ports
def ports(name):
    import rtmidi
    mi, mo = rtmidi.MidiIn(rtmidi.API_LINUX_ALSA), rtmidi.MidiOut(rtmidi.API_LINUX_ALSA)
    pick = lambda pl: next((i for i, p in enumerate(pl) if name.lower() in p.lower()), None)
    i, o = pick(mi.get_ports()), pick(mo.get_ports())
    if i is None or o is None:
        sys.exit(f'no MIDI port matching "{name}": in {mi.get_ports()} out {mo.get_ports()}')
    mi.open_port(i); mo.open_port(o)
    mi.ignore_types(sysex=True, timing=False, active_sense=True)
    return mi, mo

def wait_until(t):
    """sleep until 2 ms before t, then spin (sub-0.1 ms send timing on an idle Linux host)"""
    while True:
        d = t - time.perf_counter()
        if d <= 0: return
        if d > 0.002: time.sleep(d - 0.002)

# ---------------------------------------------------------------- schedule
def schedule(a):
    """list of (time offset s, bytes, tag) to send"""
    ev, t = [], 0.5                                    # lead-in: half a second of silence
    def clocks(n, bpm0, bpm1=None):
        nonlocal t
        for k in range(n):
            bpm = bpm0 if bpm1 is None else bpm0 + (bpm1 - bpm0) * k / max(1, n - 1)
            j = random.gauss(0, a.jitter / 1000) if a.jitter else 0.0
            ev.append((t + j, [0xF8], 'clk'))
            t += 60 / (bpm * 24)
    n = int(a.seconds * a.bpm * 24 / 60)
    if a.scenario == 'basic':
        ev.append((t, [0xFA], 'start')); clocks(n, a.bpm, a.ramp)
        ev.append((t, [0xFC], 'stop'))
    elif a.scenario == 'stopcont':                     # 8 bars, stop 2 s (clock keeps running), continue 8 bars
        bar = 96
        ev.append((t, [0xFA], 'start')); clocks(8 * bar, a.bpm)
        ev.append((t, [0xFC], 'stop')); clocks(int(2 * a.bpm * 24 / 60), a.bpm)
        ev.append((t, [0xFB], 'continue')); clocks(8 * bar, a.bpm)
        ev.append((t, [0xFC], 'stop'))
    elif a.scenario == 'restarts':                     # 6 x (start, 4 bars, stop, 1 s clock only): offset per start
        bar = 96
        for _ in range(6):
            ev.append((t, [0xFA], 'start')); clocks(4 * bar, a.bpm)
            ev.append((t, [0xFC], 'stop')); clocks(int(a.bpm * 24 / 60), a.bpm)
    elif a.scenario == 'spp':                          # stopped: song position, then continue from there
        ev.append((t, [0xF2, a.spp & 0x7F, a.spp >> 7], f'spp {a.spp}')); clocks(24, a.bpm)
        ev.append((t, [0xFB], 'continue')); clocks(n, a.bpm)
        ev.append((t, [0xFC], 'stop'))
    return sorted(ev, key=lambda e: e[0])

def cmd_run(a):
    if a.seed is not None: random.seed(a.seed)
    sched = schedule(a)
    mi, mo = ports(a.port)
    rx, lock = [], threading.Lock()
    def cb(e, _):
        t = time.perf_counter()
        with lock: rx.append((t, e[0]))
    mi.set_callback(cb)
    print(f'sending {sum(1 for e in sched if e[2] == "clk")} clocks at {a.bpm} bpm'
          + (f' -> {a.ramp}' if a.ramp else '') + f', scenario {a.scenario}', file=sys.stderr)
    t0 = time.perf_counter()
    tx = []
    try:
        for off, msg, tag in sched:
            wait_until(t0 + off)
            mo.send_message(msg)
            tx.append((time.perf_counter(), msg, tag))
        time.sleep(0.3)
    except KeyboardInterrupt:
        mo.send_message([0xFC])
    mi.close_port(); mo.close_port()
    with open(a.output, 'w', newline='') as f:
        w = csv.writer(f); w.writerow(['dir', 't', 'bytes', 'tag'])
        rows = [('tx', t, m, tag) for t, m, tag in tx] + [('rx', t, m, '') for t, m in rx]
        for d, t, m, tag in sorted(rows, key=lambda r: r[1]):
            w.writerow([d, f'{t - t0:.6f}', ' '.join(f'{b:02X}' for b in m), tag])
    late = [(t - t0) - off for (t, _, _), (off, _, _) in zip(tx, sched)]
    print(f'{len(tx)} sent (send timing error max {max(late) * 1000:.3f} ms), {len(rx)} received -> {a.output}',
          file=sys.stderr)
    print('\n'.join(report(*load(a.output), a.grid)))

# ---------------------------------------------------------------- analysis
def load(path):
    tx, rx = [], []
    with open(path) as f:
        for r in csv.DictReader(f):
            m = [int(b, 16) for b in r['bytes'].split()]
            (tx if r['dir'] == 'tx' else rx).append((float(r['t']), m))
    return tx, rx

def stats(xs):
    if not xs: return 'n/a'
    m = sum(xs) / len(xs); sd = math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))
    return f'mean {m:+.3f}  std {sd:.3f}  min {min(xs):+.3f}  max {max(xs):+.3f} ms  (n {len(xs)})'

def clock_timeline(tx):
    """sent clocks counted from the last FA / F2 (song position: 1 unit = 6 clocks); continue keeps counting"""
    clk, k = [], 0
    for t, m in tx:
        if m[0] == 0xFA: k = 0
        elif m[0] == 0xF2: k = (m[1] | m[2] << 7) * 6
        elif m[0] == 0xF8: clk.append((t, k)); k += 1
    return clk

def place(t, clk):
    """fractional clock index of time t (between the surrounding sent clocks), and the local period"""
    lo, hi = 0, len(clk) - 1
    if t <= clk[0][0] or t >= clk[-1][0]: return None, None
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if clk[mid][0] <= t: lo = mid
        else: hi = mid
    (ta, ka), (tb, kb) = clk[lo], clk[hi]
    if kb != ka + 1: return None, None                # a song-position jump between them
    return ka + (t - ta) / (tb - ta), tb - ta

def report(tx, rx, grid=6):
    out = []
    t_first = min(t for t, m in tx)
    stale = sum(1 for t, m in rx if t < t_first)
    rx = [(t, m) for t, m in rx if t >= t_first]       # ignore what the OP-1 had queued before the run (USB tx ring)
    if stale: out.append(f'ignored {stale} messages received before the first sent one (queued in the OP-1 before the run)')
    clk = clock_timeline(tx)
    notes = [(t, m) for t, m in rx if m[0] & 0xF0 == 0x90 and len(m) > 2 and m[2] > 0]
    err, when, steps = [], [], []
    for t, m in notes:
        pos, per = place(t, clk)
        if pos is None: continue
        step = round(pos / grid)
        err.append((pos - step * grid) * per * 1000); when.append(t); steps.append(step)
    out.append(f'OP-1 notes: {len(notes)} received, {len(err)} on the clock timeline (grid {grid} clocks)')
    if err:
        out.append(f'  timing vs grid   {stats(err)}')
        if len(err) > 10:
            n = len(err); mx = sum(when) / n; my = sum(err) / n
            sxx = sum((x - mx) ** 2 for x in when)
            if sxx > 0:
                slope = sum((x - mx) * (y - my) for x, y in zip(when, err)) / sxx
                out.append(f'  drift            {slope * 60:+.3f} ms per minute (trend of the timing error)')
        used = sorted(set(steps))
        missing = [s for s in range(used[0], used[-1] + 1) if s not in used]
        out.append(f'  steps {used[0]}..{used[-1]}: {len(used)} with notes, {len(missing)} empty'
                   + (f' (first empty: {missing[:12]})' if missing else '') + '  <- compare with the pattern')
    starts = [t for t, m in tx if m[0] in (0xFA, 0xFB)] + [float('inf')]
    for a_, b_ in zip(starts, starts[1:]):            # per start: notes after the first, offset to the step grid in clocks
        seg = [place(t, clk)[0] for t, m in notes if a_ < t < b_]
        seg = [p for p in seg if p is not None][1:]
        if seg:
            offs = [p - round(p / grid) * grid for p in seg]
            out.append(f'  after start at {a_:.2f} s: {len(seg)} notes, offset to grid {sum(offs) / len(offs):+.2f} clocks'
                       f' (min {min(offs):+.2f}, max {max(offs):+.2f})')
    for t, m in tx:
        if m[0] in (0xFA, 0xFB):
            nx = next(((tn, mn) for tn, mn in notes if tn > t), None)
            if nx:
                pos, _ = place(nx[0], clk)
                out.append(f'{"START" if m[0] == 0xFA else "CONTINUE"} at {t:.3f} s -> first note {(nx[0] - t) * 1000:.2f} ms later'
                           + (f', clock {pos:.2f} (step {pos / grid:.2f})' if pos is not None else ''))
        elif m[0] == 0xF2:
            out.append(f'SPP {m[1] | m[2] << 7} sent at {t:.3f} s')
    # mode 4: forwarded clocks
    ftx = [t for t, m in tx if m[0] == 0xF8]
    frx = [t for t, m in rx if m[0] == 0xF8]
    if frx:
        d, j = [], 0
        for t in frx:
            while j + 1 < len(ftx) and ftx[j + 1] <= t: j += 1
            d.append((t - ftx[j]) * 1000)
        out.append(f'forwarded F8: {len(frx)} received for {len(ftx)} sent; delay after the last sent F8: {stats(d)}')
    other = sorted({f'{m[0]:02X}' for t, m in rx if m[0] in (0xFA, 0xFB, 0xFC, 0xF2)})
    if other: out.append(f'OP-1 also sent: {" ".join(other)}')
    return out

def cmd_analyse(a):
    tx, rx = load(a.file)
    print('\n'.join(report(tx, rx, a.grid)))
    if a.plot:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        clk = clock_timeline(tx)
        pts = [(t, (p - round(p / a.grid) * a.grid) * per * 1000) for t, m in rx if m[0] & 0xF0 == 0x90 and len(m) > 2
               and m[2] > 0 for p, per in [place(t, clk)] if p is not None]
        fig, ax = plt.subplots(2, 1, figsize=(11, 7))
        ax[0].plot([p[0] for p in pts], [p[1] for p in pts], '.', ms=3); ax[0].set_ylabel('note timing error ms'); ax[0].set_xlabel('s')
        ax[1].hist([p[1] for p in pts], bins=100); ax[1].set_xlabel('ms'); ax[1].set_ylabel('notes')
        fig.tight_layout(); fig.savefig(a.plot); print('->', a.plot)

# ---------------------------------------------------------------- self-test
def selftest():
    """synthetic OP-1: notes on every 2nd 1/16, 3 ms late, 64-sample quantised, drifting +2 ms/min; forwarded clocks"""
    random.seed(3)
    class A: bpm, ramp, seconds, jitter, scenario, spp = 120.0, None, 30, 0.0, 'basic', 0
    tx = [(off, m) for off, m, tag in schedule(A())]
    clk = clock_timeline(tx)
    q = 64 / 44100
    rx = []
    for t, k in clk:
        rx.append((t + 0.0007, [0xF8]))
        if k % 12 == 0:
            late = 0.003 + 0.002 * t / 60
            rx.append((math.ceil((t + late) / q) * q, [0x90, 60, 100]))
    rx.sort(key=lambda e: e[0])
    out = report(tx, rx, 6)
    print('\n'.join(out))
    txt = '\n'.join(out)
    ok = ('timing vs grid   mean +3.' in txt or 'timing vs grid   mean +4.' in txt) and 'drift            +2.' in txt \
        and 'forwarded F8: 1440 received for 1440 sent' in txt
    print('selftest', 'ok' if ok else 'FAILED')
    return 0 if ok else 1

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--selftest', action='store_true')
    sub = ap.add_subparsers(dest='cmd')
    p = sub.add_parser('run')
    p.add_argument('--port', default='OP-1'); p.add_argument('--bpm', type=float, default=120)
    p.add_argument('--ramp', type=float, help='end tempo (linear ramp over the run)')
    p.add_argument('--seconds', type=float, default=30); p.add_argument('--jitter', type=float, default=0, help='ms (gaussian)')
    p.add_argument('--scenario', choices=('basic', 'stopcont', 'spp', 'restarts'), default='basic')
    p.add_argument('--spp', type=int, default=32, help='song position (1/16 steps)')
    p.add_argument('--grid', type=int, default=6, help='clocks per sequencer step (6 = 1/16)')
    p.add_argument('--seed', type=int); p.add_argument('-o', '--output', required=True)
    p = sub.add_parser('analyse')
    p.add_argument('file'); p.add_argument('--grid', type=int, default=6); p.add_argument('--plot')
    a = ap.parse_args()
    if a.selftest: return selftest()
    {'run': cmd_run, 'analyse': cmd_analyse}.get(a.cmd, lambda _: ap.print_help())(a)
    return 0

if __name__ == '__main__':
    sys.exit(main())
