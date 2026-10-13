#!/usr/bin/env python3
"""Preview of the USB out page (screens/usbout.c via harness/usbout_test.c) -> display/usbout_preview.png.
Text in a plain font as a stand-in for the firmware font (position = top left)."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); SDK = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import render as R
sys.path.insert(0, os.path.join(SDK, 'display'))
_OPF = os.path.join(SDK, '..', 'op1_246', 'content', 'display', 'opfont.svg')
FONT = FONTW = None
if os.path.exists(_OPF) and not os.environ.get('NO_OPFONT'):   # public screenshots: plain stand-in font
    import svgfont, gen_oblique as G
    FONT = svgfont.load(_OPF); FONTW = G.widths(_OPF)

def main(out=None, name='usbout'):
    exe = os.path.join(HERE, 'build', name + '_test')
    subprocess.run(['cc', '-O1', '-Wall', '-DOP1_HOST', '-fsanitize=address,undefined', '-I', SDK,
                    os.path.join(HERE, name + '_test.c'), '-o', exe, '-lm'], check=True)
    print(subprocess.run([exe], check=True, capture_output=True, text=True).stdout.strip())
    frames = [json.loads(l) for l in subprocess.run([exe, 'render'], check=True, capture_output=True, text=True).stdout.splitlines() if l.startswith('{')]
    if os.environ.get('SINGLE'):                       # one frame, no label, 3x (README screenshots)
        frames = [frames[int(os.environ['SINGLE'])]]
    cairo = R.cairo; S, W, H, pad, lab = R.SCALE, 320 * R.SCALE, 160 * R.SCALE, 12, 22
    if os.environ.get('SINGLE'): S, W, H, pad, lab = 3, 960, 480, 0, 0
    cols, rows = len(frames), 1
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, cols * (W + pad) + pad, rows * (H + pad + lab) + pad)
    ctx = cairo.Context(surf); ctx.set_source_rgb(0.25, 0.25, 0.27); ctx.paint()
    for n, fr in enumerate(frames):
        x0, y0 = pad + (n % cols) * (W + pad), pad + (n // cols) * (H + pad + lab)
        if lab: ctx.save(); ctx.set_source_rgb(1, 1, 1); ctx.set_font_size(14); ctx.move_to(x0, y0 + 15); ctx.show_text(fr['label']); ctx.restore()
        ctx.save(); ctx.translate(x0, y0 + lab); ctx.rectangle(0, 0, W, H); ctx.clip()
        ctx.set_source_rgb(0, 0, 0); ctx.paint(); ctx.scale(S, S)
        for x1, y1, x2, y2, w, c in fr['lines']:
            ctx.set_source_rgba(*(v / 255 for v in c)); ctx.set_line_width(w / 1024); ctx.set_line_cap(cairo.LINE_CAP_BUTT)
            ctx.move_to(x1 / 1024, y1 / 1024); ctx.line_to(x2 / 1024, y2 / 1024); ctx.stroke()
        ctx.select_font_face('DejaVu Sans', 0, 1); ctx.set_font_size(9)
        for t in fr['text']:
            x, y, c, s = t[:4]
            ctx.set_source_rgb(*(v / 255 for v in c))
            if FONT:                                   # stock opfont strokes (local preview only), oblique layout model
                k = G.TEXT_H / G.CELL; xs = []; xx = 0.0
                for ch in s: xs.append(xx); xx += (G.SPACE if ch == ' ' else FONTW.get(ch, G.SPACE)) + G.GAP
                x0 = x / 1024 - (xx - G.GAP) / 2 if len(t) > 4 and t[4] == 1 else x / 1024
                ctx.set_line_width(1.5)
                for ch, dx in zip(s, xs):
                    polys = FONT.get(ch) or []; pts = [p for poly in polys for p in poly]
                    if not pts: continue
                    gx0 = min(p[0] for p in pts); top = max(r for r in [-142.4, -57.4, 27.6, 112.7, 197.7, 282.8] if r <= min(p[1] for p in pts) + 1.0)
                    for poly in polys:
                        ctx.move_to(x0 + dx + (poly[0][0] - gx0) * k, y + (poly[0][1] - top) * k)
                        for px, py in poly[1:]: ctx.line_to(x0 + dx + (px - gx0) * k, y + (py - top) * k)
                        ctx.stroke()
                continue
            if len(t) > 4 and t[4] == 1: x -= ctx.text_extents(s)[4] * 1024 / 2      # centred on x
            ctx.move_to(x / 1024, y + 9); ctx.show_text(s)
        ctx.restore()
    out = out or os.path.join(SDK, 'display', 'usbout_preview.png')
    surf.write_to_png(out); print('->', out)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None, sys.argv[2] if len(sys.argv) > 2 else 'usbout')
