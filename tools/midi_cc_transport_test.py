#!/usr/bin/env python3
"""MIDI CC control test (docs/11_midi.md §9). Sends on the OP-1's channel (COM: op_midi_channel, default 1).

  python3 tools/midi_cc_transport_test.py [--ch 1] [--port OP-1]            # walk-through of every mapped CC
  python3 tools/midi_cc_transport_test.py --key 26                          # tap one key CC (127, then 0)
  python3 tools/midi_cc_transport_test.py --enc 0 --steps -3                # blue encoder 3 detents down
"""
import argparse, sys, time
try:
    import mido
except ImportError:
    sys.exit("need: pip install mido python-rtmidi")

ap = argparse.ArgumentParser()
ap.add_argument('--ch', type=int, default=1, help='MIDI channel 1-16')
ap.add_argument('--port', default='OP-1')
ap.add_argument('--key', type=int, help='key CC to tap')
ap.add_argument('--enc', type=int, help='encoder 0..3 (blue green white red) = CC 102..105')
ap.add_argument('--steps', type=int, default=1)
a = ap.parse_args()
name = next((p for p in mido.get_output_names() if a.port in p), None) or sys.exit(f'no port with "{a.port}"')
out = mido.open_output(name)

def cc(n, v): out.send(mido.Message('control_change', channel=a.ch - 1, control=n, value=v)); time.sleep(0.03)
def tap(n): cc(n, 127); cc(n, 0)
def enc(k, steps): cc(102 + k, steps & 0x7F)          # 7-bit two's complement
def step(label, f): input(f'Enter -> {label} '); f()

if a.key is not None: tap(a.key); sys.exit()
if a.enc is not None: enc(a.enc, a.steps); sys.exit()

# every mapped CC (docs/11 §9); order keeps the OP-1 in a sane state (help / metronome / COM are left again)
KEYS = [(7, 'synth'), (8, 'drum'), (9, 'tape'), (10, 'mixer'), (7, 'synth (back)'),
        (11, 'T1'), (12, 'T2'), (13, 'T3'), (14, 'T4'),
        (21, 'key 1? (0x10)'), (22, 'key 2? (0x11)'), (23, 'key 3? (0x12)'), (24, 'key 4? (0x13)'),
        (25, 'key 5? (0x14)'), (50, 'key 6? (0x2D)'), (51, 'key 7? (0x2E)'), (52, 'key 8? (0x2F)'),
        (26, 'sequencer'), (7, 'synth (back)'),
        (5, 'help'), (5, 'help (off)'), (6, 'metronome'), (7, 'synth (back)'),
        (48, 'mic / input'), (48, 'mic / input (again)'), (49, 'COM'), (7, 'synth (back)'),
        (9, 'tape'), (41, 'left'), (42, 'right'),
        (15, 'lift'), (16, 'drop'), (17, 'scissors (tape: cut)'),
        (39, 'PLAY'), (40, 'STOP'), (40, 'STOP again')]
for n, label in KEYS:
    step(f'CC {n}: {label}', lambda n=n: tap(n))
step('CC 43 SHIFT held + CC 11 T1, then release', lambda: (cc(43, 127), tap(11), cc(43, 0)))
step('hold CC 38 REC + tap CC 39 PLAY (records), 2 s, CC 40 STOP',
     lambda: (cc(38, 127), tap(39), cc(38, 0), time.sleep(2), tap(40)))
step('CC 7: synth screen for the encoders', lambda: tap(7))
for k, col in enumerate(['blue', 'green', 'white', 'red']):
    step(f'CC {102 + k}: {col} encoder +4 detents', lambda k=k: enc(k, 4))
    step(f'CC {102 + k}: {col} encoder -4 detents', lambda k=k: enc(k, -4))
step('CC 43 SHIFT held + CC 102 blue +2, then release', lambda: (cc(43, 127), enc(0, 2), cc(43, 0)))
def sweep(n, a, b, steps=16):
    for i in range(steps + 1): cc(n, round(a + (b - a) * i / steps)); time.sleep(0.03)
step('CC 10: mixer screen', lambda: tap(10))
for t in range(4):
    step(f'CC {106 + t}: track {t + 1} volume 127 -> 0 -> 100', lambda t=t: (sweep(106 + t, 127, 0), sweep(106 + t, 0, 100)))
for t in range(4):
    step(f'CC {110 + t}: track {t + 1} pan L -> R -> centre', lambda t=t: (sweep(110 + t, 0, 127), cc(110 + t, 64)))
for t in range(4):
    step(f'CC {114 + t}: track {t + 1} mute on, 1 s, off', lambda t=t: (cc(114 + t, 127), time.sleep(1), cc(114 + t, 0)))
step('CC 9: tape screen', lambda: tap(9))
step('CC 118: input (rec) level 0 -> 127 -> 90 (red slider)', lambda: (sweep(118, 0, 127), sweep(118, 127, 90)))
step('negative check: CC 1 / 64 / 102=0 do nothing visible', lambda: (cc(1, 100), cc(64, 127), cc(64, 0), cc(102, 0)))
print('done')
