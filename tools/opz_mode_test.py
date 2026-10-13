#!/usr/bin/env python3
"""
Test the (re-enabled) hidden OP-Z controller mode of OP-1 firmware R246.
See docs/11_midi.md §7 / docs/12_opz_mode_patch.md.

Firmware built with `synth_sdk/make_firmware.py --opz`: COM -> CTRL starts in the normal CTRL mode;
this script switches the OP-Z mode on with the host-mode SysEx F0 00 20 76 00 01 03 F7
(the old always-on byte patch 019e0cfc -> return 1 needs no SysEx).

  pip install mido python-rtmidi
  python3 tools/opz_mode_test.py            # auto-detect "OP-1" port, run all steps
  python3 tools/opz_mode_test.py --list
  python3 tools/opz_mode_test.py --monitor  # just decode traffic

On the OP-1: COM -> CTRL (T2), then the script sends 00 01 03.  Expected with the patch:
  * keep-alive SysEx F0 00 20 76 00 05 F7 roughly every 960 sequencer ticks  <- proof patch is active
  * CTRL keys send Program Change / CC53 / CC110=118 pairs / FA FC instead of plain "CC key+5"
  * encoders use per-knob CC/channel map (opzcontroller_temp) instead of CC1-4
"""
import argparse, sys, time

try:
    import mido
except ImportError:
    sys.exit("need: pip install mido python-rtmidi")

TE = [0x00, 0x20, 0x76]           # Teenage Engineering manufacturer ID
KEEPALIVE = TE + [0x00, 0x05]


def te_sysex(cmd, payload=()):
    return mido.Message('sysex', data=TE + [0x00, cmd] + list(payload))


def pack32(v):  # 5 x 7-bit, little endian (as parsed by midi_rx_parse)
    return [(v >> s) & 0x7F for s in (0, 7, 14, 21, 28)]


def describe(m):
    if m.type == 'sysex':
        d = list(m.data)
        if d == KEEPALIVE:
            return 'SYSEX OP-Z KEEPALIVE  -> patch ACTIVE'
        if d[:4] == [0x7E, 0x00, 0x06, 0x02]:
            ver = ''.join(str(x) for x in reversed(d[-4:]))
            return f'SYSEX identity reply, mfr={d[4:7]} fam={d[7:9]} model={d[9:11]} fw={ver}'
        if d[:4] == [0x7E, 0x7F, 0x06, 0x01]:
            return 'SYSEX identity REQUEST (OP-1 entered CTRL)'
        if d[:3] == TE:
            return f'SYSEX TE cmd={d[4] if len(d) > 4 else "?"} data={bytes(d[5:]).hex(" ")}'
        return 'SYSEX ' + bytes(d).hex(' ')
    if m.type == 'control_change':
        tag = {53: ' (OP-Z mute)', 110: ' (OP-Z bank/handshake)', 56: ' (pattern hi)', 57: ' (pattern lo)'}.get(m.control, '')
        return f'CC ch{m.channel+1} #{m.control}={m.value}{tag}'
    if m.type == 'program_change':
        return f'PROGRAM ch{m.channel+1} {m.program}  (OP-Z pattern select)'
    if m.type == 'clock':
        return None
    return str(m)


def find_port(names, hint):
    for n in names:
        if hint.lower() in n.lower():
            return n
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default='OP-1')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--monitor', action='store_true')
    ap.add_argument('--wait', type=float, default=20.0, help='seconds to wait for keepalive')
    ap.add_argument('--off', action='store_true', help='only send 00 01 00 (leave OP-Z mode) and exit')
    a = ap.parse_args()

    ins, outs = mido.get_input_names(), mido.get_output_names()
    if a.list:
        print('IN :', ins); print('OUT:', outs); return
    pin, pout = find_port(ins, a.port), find_port(outs, a.port)
    if not pin or not pout:
        sys.exit(f'OP-1 port not found. IN={ins} OUT={outs}')
    print(f'using IN={pin!r} OUT={pout!r}')

    with mido.open_input(pin) as inp, mido.open_output(pout) as out:
        def drain(sec, quiet=False):
            got, end = [], time.time() + sec
            while time.time() < end:
                for m in inp.iter_pending():
                    got.append(m)
                    s = describe(m)
                    if s and not quiet:
                        print(f'  <- {s}')
                time.sleep(0.005)
            return got

        if a.monitor:
            print('monitoring, Ctrl-C to quit'); drain(1e9)

        # 1. identity
        print('\n[1] identity request')
        out.send(mido.Message('sysex', data=[0x7E, 0x7F, 0x06, 0x01]))
        drain(1.0)

        if a.off:
            out.send(te_sysex(0x01, [0x00])); print('sent host mode 00 (OP-Z mode off)'); return

        # 2. enter CTRL (OP-1 sends an identity request), then switch on OP-Z host mode
        print('\n[2] put OP-1 in COM -> CTRL (T2) now (waiting for its identity request, max 30s)...')
        end = time.time() + 30
        while time.time() < end and not any(
                m.type == 'sysex' and list(m.data)[:4] == [0x7E, 0x7F, 0x06, 0x01] for m in drain(0.2, True)):
            pass
        print('  -> sending host-mode SysEx F0 00 20 76 00 01 03 F7 (OP-Z mode on)')
        out.send(te_sysex(0x01, [0x03]))

        # keep-alive = proof that midi_opz_mode_enabled() returns 1
        print(f'  waiting {a.wait:.0f}s for OP-Z keep-alive...')
        got = drain(a.wait)
        ok = any(m.type == 'sysex' and list(m.data) == KEEPALIVE for m in got)
        print('  RESULT:', 'PATCH ACTIVE (keep-alive seen)' if ok else
              'no keep-alive (patch not active, not in CTRL, or tempo not running)')

        # 3. feed OP-Z style state to the OP-1 (affects CTRL/OP-Z screen)
        print('\n[3] sending OP-Z feedback: name, track table, mutes, pattern')
        name = b'HELLO OPZ'
        out.send(te_sysex(0x03, [len(name)] + list(name)))                     # -> ff907d08
        n = 4
        out.send(te_sysex(0x04, [n] + [x & 0x7F for i in range(n) for x in (i, 10 * i, 127 - i)]))
        for ch in range(4):                                                    # CC53: 0 off,1 mute,2 solo
            out.send(mido.Message('control_change', channel=ch, control=53, value=ch % 3))
        out.send(mido.Message('control_change', channel=0, control=55, value=3))
        out.send(mido.Message('control_change', channel=0, control=56, value=5))   # pattern (14-bit-ish)
        out.send(mido.Message('control_change', channel=0, control=57, value=1))
        out.send(mido.Message('control_change', channel=0, control=111, value=42))
        for cc in range(1, 17):                                                # knob value feedback
            out.send(mido.Message('control_change', channel=0, control=cc, value=cc * 7))
        out.send(mido.Message('program_change', channel=0, program=2))
        drain(0.5)
        print('  check the OP-1 screen for changes')

        # 4. interactive: press CTRL keys / turn knobs
        print('\n[4] press keys / turn encoders on the OP-1 (30s). '
              'OP-Z mode => PROGRAM / CC53 / CC110=118 / start-stop; stock => CC key+5 only')
        drain(30)


if __name__ == '__main__':
    main()
