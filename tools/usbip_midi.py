#!/usr/bin/env python3
"""Minimal USB/IP client for the op1emu emulator: imports the emulated OP-1 and talks USB-MIDI over its bulk
endpoints, without the kernel's vhci (no sudo, no audio driver). Used by op1push.py --usbip HOST:PORT.

  usbip_midi.py 127.0.0.1:3241 --identity      # sends an identity request, prints the reply"""
import socket, struct, sys, time

EP_OUT, EP_IN = 4, 3                              # OP-1 USB-MIDI bulk endpoints (from the emulator log)


class UsbipMidi:
    def __init__(self, addr):
        host, port = addr.split(':')
        self.s = socket.create_connection((host, int(port)))
        self.s.sendall(struct.pack('>HHI32s', 0x0111, 0x8003, 0, b'1-1'))
        self._recv(320)                           # OP_REP_IMPORT
        self.seq = 0; self.rx = bytearray(); self.pending = None
        self.control(0x00, 0x09, 1, 0)            # SET_CONFIGURATION 1

    def _recv(self, n):
        b = b''
        while len(b) < n:
            c = self.s.recv(n - len(b))
            if not c: raise EOFError('emulator closed the connection')
            b += c
        return b

    def _submit(self, ep, direction, length, setup=b'\0' * 8, data=b''):
        self.seq += 1
        self.s.sendall(struct.pack('>IIIIIIIIII', 1, self.seq, 0x10002, direction, ep, 0, length, 0, 0, 0) + setup + data)
        return self.seq

    def _ret(self):
        h = self._recv(48)
        cmd, seq, _, _, _, status, alen = struct.unpack('>IIIIIiI', h[:28])
        data = self._recv(alen) if cmd == 3 and alen and self.dirs.get(seq) else b''
        return seq, status, data

    def control(self, rt, req, val, idx, data=b'', inlen=0):
        self.dirs = getattr(self, 'dirs', {})
        setup = struct.pack('<BBHHH', rt, req, val, idx, inlen or len(data))
        q = self._submit(0, 1 if rt & 0x80 else 0, inlen or len(data), setup, data if not rt & 0x80 else b'')
        self.dirs[q] = bool(rt & 0x80)
        while True:
            seq, st, d = self._ret()
            if seq == q: return st, d
            self._other(seq, st, d)

    def _other(self, seq, st, d):
        if seq == self.pending:
            self.pending = None
            for i in range(0, len(d) - 3, 4):     # USB-MIDI packets: CIN 4..7 = SysEx, F = single byte
                cin = d[i] & 15; n = {4: 3, 5: 1, 6: 2, 7: 3, 15: 1}.get(cin, 0)
                self.rx += bytes(b for b in d[i + 1:i + 1 + n] if b < 0xF8)

    def send(self, msg):
        pk = bytearray()
        for i in range(0, len(msg), 3):
            c = msg[i:i + 3]; end = i + 3 >= len(msg)
            pk += bytes([(4 + len(c)) if end else 4]) + c + b'\0' * (3 - len(c))
        q = self._submit(EP_OUT, 0, len(pk), data=bytes(pk)); self.dirs[q] = False
        while True:
            seq, st, d = self._ret()
            if seq == q: return
            self._other(seq, st, d)

    def poll(self, timeout):
        """collect bulk IN data until timeout; returns the bytes received so far (realtime bytes dropped)"""
        end = time.time() + timeout
        while time.time() < end:
            if self.pending is None:
                self.pending = self._submit(EP_IN, 1, 512); self.dirs[self.pending] = True
            self.s.settimeout(max(end - time.time(), 0.01))
            try:
                seq, st, d = self._ret()
            except socket.timeout:
                break
            finally:
                self.s.settimeout(None)
            self._other(seq, st, d)
            if self.rx.find(0xF7) >= 0: break
        return self.rx


if __name__ == '__main__':
    u = UsbipMidi(sys.argv[1])
    u.send(bytes([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7]))
    print(u.poll(10).hex(' '))
