#!/usr/bin/env python3
"""Preview of the synth randomiser panel (harness/rnd_render.c) -> display/rnd_preview.png, drawn as render_mseq.py."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); SDK = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import render as R
from render_mseq import text

PAGE = (0.3, 0.3, 0.36)                                  # stand-in page colour
def half565(c):                                          # rn_fade on one RGB565 colour: every channel at half
    r, g, b = (int(v * 255) for v in c)
    q = ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)) >> 1 & 0x7BEF
    return (q >> 11) / 31, ((q >> 5) & 63) / 63, (q & 31) / 31

def main(out=None):
    os.makedirs(os.path.join(HERE, 'build'), exist_ok=True)
    exe = os.path.join(HERE, 'build', 'rnd_render')
    subprocess.run(['cc', '-O1', '-Wall', '-DOP1_HOST', '-fsanitize=address,undefined', '-I', SDK,
                    os.path.join(HERE, 'rnd_render.c'), os.path.join(SDK, 'rnd', 'rn_draw.c'), '-o', exe], check=True)
    frames = [json.loads(l) for l in subprocess.run([exe], check=True, capture_output=True, text=True).stdout.splitlines()]
    cairo = R.cairo; S = 3; W, H, pad, lab = 320 * S, 160 * S, 12, 22
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, W + 2 * pad, len(frames) * (H + pad + lab) + pad)
    ctx = cairo.Context(surf); ctx.set_source_rgb(0.25, 0.25, 0.27); ctx.paint()
    for n, fr in enumerate(frames):
        x0, y0 = pad, pad + n * (H + pad + lab)
        ctx.save(); ctx.set_source_rgb(1, 1, 1); ctx.set_font_size(15); ctx.move_to(x0, y0 + 16); ctx.show_text(fr['label']); ctx.restore()
        ctx.save(); ctx.translate(x0, y0 + lab); ctx.rectangle(0, 0, W, H); ctx.clip()
        ctx.set_source_rgb(0, 0, 0); ctx.paint(); ctx.scale(S, S)
        ctx.set_source_rgb(*(half565(PAGE) if fr.get('dim') else PAGE)); ctx.set_line_width(1)   # stand-in page
        for k in range(-160, 320, 6): ctx.move_to(k, 0); ctx.line_to(k + 160, 160)
        ctx.stroke()
        for x1, y1, x2, y2, w, c in fr['lines']:
            ctx.set_source_rgba(*(v / 255 for v in c)); ctx.set_line_width(w / 1024); ctx.set_line_cap(cairo.LINE_CAP_BUTT)
            ctx.move_to(x1 / 1024, y1 / 1024); ctx.line_to(x2 / 1024, y2 / 1024); ctx.stroke()
        for x, y, c, st, al, sz in fr.get('text', []):
            ctx.save(); ctx.translate(x / 1024, y); ctx.scale(sz / 12, sz / 12); text(ctx, 0, 0, c, st, al); ctx.restore()
        ctx.restore()
    out = out or os.path.join(SDK, 'display', 'rnd_preview.png')
    surf.write_to_png(out); print('->', out)

if __name__ == '__main__':
    main(*sys.argv[1:2])
