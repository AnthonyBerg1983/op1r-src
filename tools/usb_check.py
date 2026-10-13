#!/usr/bin/env python3
"""OP-1 USB check (Linux): device, descriptors, ALSA audio channels, a short recording per channel, MIDI ports.

  python3 tools/usb_check.py              # everything (records 3 s from the OP-1, play something / run the tape)
  python3 tools/usb_check.py --seconds 5  # longer recording
  python3 tools/usb_check.py --no-record  # descriptors / ALSA / MIDI only
  python3 tools/usb_check.py --midi-in 5  # also listen 5 s for MIDI from the OP-1 (press keys)

Needs: lsusb, arecord (alsa-utils); numpy; python-rtmidi for the MIDI part. Nothing is sent to the OP-1.
Expected with the 6-channel firmware (docs/21): capture 6 channels, EP 0x86 wMaxPacketSize 828; channels 1/2 = main
L / R, 3..6 = tape tracks 1..4 (silent in step a).
"""
import argparse, os, re, subprocess, sys, tempfile, time, wave

VID = '2367'
ok = lambda m: print('  ok   ' + m)
bad = lambda m: print('  FAIL ' + m)
info = lambda m: print('       ' + m)


def run(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=20).stdout
    except (OSError, subprocess.TimeoutExpired) as e:
        return ''


def check_usb():
    print('USB device')
    lst = run(['lsusb', '-d', VID + ':'])
    if not lst.strip():
        bad('no Teenage Engineering device (VID 2367) on USB'); return None
    ok(lst.strip())
    desc = run(['lsusb', '-v', '-d', VID + ':'])
    if not desc:
        info('lsusb -v gave nothing (try with sudo for full descriptors)'); return lst
    chans = [int(x) for x in re.findall(r'bNrChannels\s+(\d+)', desc)]
    info('bNrChannels in the descriptors: %s' % chans)
    m = re.search(r'bEndpointAddress\s+0x86.*?wMaxPacketSize\s+0x([0-9a-f]+)', desc, re.S)
    if m:
        mp = int(m.group(1), 16) & 0x7FF
        info('EP 0x86 (OP-1 -> host) wMaxPacketSize %d' % mp)
        (ok if mp in (276, 828) else bad)('packet size %d (stock 276, 6-channel 828)' % mp)
    for pat, name in ((r'MIDIStreaming|MIDI Streaming', 'MIDI interface'), (r'bInterfaceClass\s+8 ', 'mass storage')):
        info('%s: %s' % (name, 'present' if re.search(pat, desc) else 'not present (normal outside disk mode)'
                         if 'storage' in name else 'MISSING'))
    return lst


def find_card():
    cards = open('/proc/asound/cards').read() if os.path.exists('/proc/asound/cards') else ''
    for line in cards.splitlines():
        m = re.match(r'\s*(\d+)\s+\[(\S+)\s*\]', line)
        if m and re.search(r'OP-?1', line, re.I):
            return int(m.group(1)), m.group(2)
    return None, None


def check_alsa():
    print('ALSA audio')
    n, cid = find_card()
    if n is None:
        bad('no OP-1 sound card in /proc/asound/cards'); return None
    ok('card %d (%s)' % (n, cid))
    stream = ''
    for f in sorted(os.listdir('/proc/asound/card%d' % n)):
        if f.startswith('stream'):
            stream += open('/proc/asound/card%d/%s' % (n, f)).read()
    cap = stream.split('Capture:')[1] if 'Capture:' in stream else ''
    pb = stream.split('Playback:')[1].split('Capture:')[0] if 'Playback:' in stream else ''
    c_ch = re.findall(r'Channels:\s*(\d+)', cap); p_ch = re.findall(r'Channels:\s*(\d+)', pb)
    c_fmt = re.findall(r'Format:\s*(\S+)', cap); c_rate = re.findall(r'Rates:\s*([^\n]+)', cap)
    info('capture (OP-1 -> PC): channels %s, format %s, rates %s' % (c_ch, c_fmt, [r.strip() for r in c_rate]))
    info('playback (PC -> OP-1): channels %s' % p_ch)
    if c_ch: (ok if int(c_ch[0]) in (2, 6) else bad)('%s capture channels (stock 2, track-out firmware 6)' % c_ch[0])
    if not p_ch: bad('no playback stream')
    return n, (int(c_ch[0]) if c_ch else 2), (c_fmt[0] if c_fmt else 'S24_3LE')


def record(card, ch, fmt, seconds):
    print('Recording %g s, %d channels (play something on the OP-1 / run the tape)' % (seconds, ch))
    import numpy as np
    alsa_fmt = {'S24_3LE': 'S24_3LE', 'S16_LE': 'S16_LE', 'S32_LE': 'S32_LE'}.get(fmt, fmt)
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as t: path = t.name
    r = subprocess.run(['arecord', '-q', '-D', 'hw:%d' % card, '-c', str(ch), '-f', alsa_fmt, '-r', '44100',
                        '-d', str(int(seconds + 0.999)), path], capture_output=True, text=True)
    if r.returncode or not os.path.getsize(path):
        bad('arecord failed: ' + r.stderr.strip()); return
    with wave.open(path) as w:
        n, width, raw = w.getnchannels(), w.getsampwidth(), w.readframes(w.getnframes())
    os.unlink(path)
    b = np.frombuffer(raw, np.uint8)
    if width == 3:
        b = b[:len(b) // 3 * 3].reshape(-1, 3).astype(np.int32)
        x = (b[:, 0] | b[:, 1] << 8 | b[:, 2] << 16); x = np.where(x & 0x800000, x - 0x1000000, x) / 2.0 ** 23
    else:
        x = np.frombuffer(raw, '<i%d' % width) / 2.0 ** (8 * width - 1)
    x = x[:len(x) // n * n].reshape(-1, n)
    ok('%d frames recorded (%.2f s)' % (len(x), len(x) / 44100))
    names = ['main L', 'main R'] + ['track %d' % (i + 1) for i in range(n - 2)]
    for c in range(n):
        s = x[:, c]
        pk = np.max(np.abs(s)); rms = np.sqrt(np.mean(s ** 2))
        db = lambda v: '%6.1f dB' % (20 * np.log10(v)) if v > 0 else '  -inf dB'
        # dropouts: runs of >= 32 exact zeros inside a signal that is otherwise non-zero
        z = (s == 0).astype(np.int8); runs = 0
        if rms > 1e-4:
            d = np.diff(np.concatenate(([0], z, [0]))); st, en = np.where(d == 1)[0], np.where(d == -1)[0]
            long = (en - st) >= 32; runs = int(np.sum(long))
            gaps = ', '.join('%.3f s (%d smp)' % (a / 44100, b - a) for a, b in list(zip(st[long], en[long]))[:5])
        print('  ch %d %-8s peak %s  rms %s  %s' % (c + 1, names[c] if c < len(names) else '', db(pk), db(rms),
              'SILENT' if pk == 0 else ('%d zero gaps: %s' % (runs, gaps) if runs else 'signal')))


def check_midi(listen):
    print('MIDI')
    try:
        import rtmidi
    except ImportError:
        bad('python-rtmidi not installed'); return
    ins = [p for p in rtmidi.MidiIn().get_ports() if re.search(r'OP-?1', p, re.I)]
    outs = [p for p in rtmidi.MidiOut().get_ports() if re.search(r'OP-?1', p, re.I)]
    (ok if ins else bad)('MIDI in ports: %s' % ins); (ok if outs else bad)('MIDI out ports: %s' % outs)
    if listen and ins:
        mi = rtmidi.MidiIn(); names = mi.get_ports(); mi.open_port(names.index(ins[0])); mi.ignore_types(False, False, False)
        print('  listening %g s, press keys on the OP-1' % listen)
        t0, n, kinds = time.time(), 0, {}
        while time.time() - t0 < listen:
            m = mi.get_message()
            if m: n += 1; k = m[0][0] & 0xF0 if m[0][0] < 0xF0 else m[0][0]; kinds[hex(k)] = kinds.get(hex(k), 0) + 1
            else: time.sleep(0.002)
        (ok if n else bad)('%d messages %s' % (n, kinds))
        mi.close_port()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seconds', type=float, default=3)
    ap.add_argument('--no-record', action='store_true')
    ap.add_argument('--midi-in', type=float, default=0, help='listen N seconds for MIDI from the OP-1')
    a = ap.parse_args()
    if not check_usb(): sys.exit(1)
    al = check_alsa()
    if al and not a.no_record: record(al[0], al[1], al[2], a.seconds)
    check_midi(a.midi_in)


if __name__ == '__main__':
    main()
