#!/usr/bin/env python3
"""Render the drum+ pages from drumplus/dp_ui.c (host build) to a PNG: T1, T1 + Shift, T2, T2 + Shift.
Usage: render_dpui.py <op1_246/content/display/opfont.svg> <out.png> (font read at render time only; the PNG shows stock glyphs: write it to build/, never commit)."""
import json, os, subprocess, sys
import cairocffi as cairo
HERE = os.path.dirname(os.path.abspath(__file__)); SDK = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(SDK, 'display'))
import svgfont
ROWS = (-142.4, -57.4, 27.6, 112.7, 197.7, 282.8)
TEXT_H = 8.0                                         # cap height (px) of size-12 text (estimate)

def main(font, out):
    exe = os.path.join(HERE, 'build', 'dp_ui_test')
    subprocess.run(['cc', '-O1', '-DOP1_HOST', '-I', SDK, os.path.join(HERE, 'dp_ui_test.c'),
                    os.path.join(SDK, 'drumplus', 'dp_plus.c'), os.path.join(SDK, 'drumplus', 'dp_voice.c'), '-lm', '-o', exe], check=True)
    frames = [json.loads(l) for l in subprocess.run([exe, 'render'], check=True, capture_output=True, text=True).stdout.splitlines()]
    G = svgfont.load(font)
    S, W, H, pad, lab = 2, 640, 320, 12, 20
    names = ['t1 play', 't1 + shift', 't2 envelope and tone', 't2 + shift']
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, 2 * (W + pad) + pad, 2 * (H + pad + lab) + pad)
    c = cairo.Context(surf); c.set_source_rgb(0.25, 0.25, 0.27); c.paint()
    for n, fr in enumerate(frames):
        x0, y0 = pad + (n % 2) * (W + pad), pad + (n // 2) * (H + pad + lab)
        c.save(); c.set_source_rgb(1, 1, 1); c.set_font_size(14); c.move_to(x0, y0 + 15); c.show_text(names[n]); c.restore()
        c.save(); c.translate(x0, y0 + lab); c.rectangle(0, 0, W, H); c.clip(); c.set_source_rgb(0, 0, 0); c.paint(); c.scale(S, S)
        c.set_line_cap(cairo.LINE_CAP_BUTT)
        for a, b, x1, y1, w, col in fr['lines']:
            c.set_source_rgb(*(v / 255 for v in col[:3])); c.set_line_width(w / 1024)
            c.move_to(a / 1024, b / 1024); c.line_to(x1 / 1024, y1 / 1024); c.stroke()
        k = TEXT_H / 56.7
        for t in fr['text']:
            c.set_source_rgb(*(v / 255 for v in t['c'])); c.set_line_width(1.0)
            x = t['x'] / 1024
            for ch in t['s']:
                pts = [p for poly in G.get(ch, []) for p in poly]
                if not pts: x += 4; continue
                gx0 = min(p[0] for p in pts); top = max(r for r in ROWS if r <= min(p[1] for p in pts) + 1.0)
                for poly in G[ch]:
                    if len(poly) < 2: continue
                    c.move_to(x + (poly[0][0] - gx0) * k, t['y'] + (poly[0][1] - top) * k)
                    for px, py in poly[1:]: c.line_to(x + (px - gx0) * k, t['y'] + (py - top) * k)
                    c.stroke()
                x += (max(p[0] for p in pts) - gx0) * k + 2.2
        c.restore()
    surf.write_to_png(out); print('->', out)

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
