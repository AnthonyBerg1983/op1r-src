#!/usr/bin/env python3
"""Mock-up of the mixer T1 page with FX2 = aux (docs/54): the stock page (mixer.svg) with the volume digits at half
size and an unlabelled send knob under each pan knob. Left: aux off (stock), right: aux on.
Usage: mixaux_preview.py <out.png>   (reads op1_246/content/display/mixer.svg + opfont.svg: keep the PNG local)"""
import math, os, sys
import xml.etree.ElementTree as ET
HERE = os.path.dirname(os.path.abspath(__file__)); SDK = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(SDK, 'harness')); sys.path.insert(0, HERE)
import render as R, svgfont, gen_oblique as G
cairo = R.cairo
DISP = os.path.join(SDK, '..', 'op1_246', 'content', 'display')
FONT = svgfont.load(os.path.join(DISP, 'opfont.svg')); FW = G.widths(os.path.join(DISP, 'opfont.svg'))
COLS = ['#698EFF', '#00ED95', '#FFFFFF', '#FF3A5D']; GREY = '#656579'
CX = [42.058, 122.058, 199.393, 276.727]                    # panring1..4 cx (mixer.svg)
VOL = [91, 99, 64, 99]; SEND = [0, 40, 100, 70]             # values shown

def rgb(h): return tuple(int(h[i:i + 2], 16) / 255 for i in (1, 3, 5))

def digits(ctx, s, x, ytop, h, col):                          # opfont digits, h = cap height in units
    k = h / G.CELL; xx = 0.0; ctx.set_source_rgb(*rgb(col)); ctx.set_line_width(1.5)
    for ch in s:
        polys = FONT.get(ch) or []; pts = [p for poly in polys for p in poly]
        gx0 = min(p[0] for p in pts); top = max(r for r in [-142.4, -57.4, 27.6, 112.7, 197.7, 282.8] if r <= min(p[1] for p in pts) + 1.0)
        for poly in polys:
            ctx.move_to(x + xx + (poly[0][0] - gx0) * k, ytop + (poly[0][1] - top) * k)
            for px, py in poly[1:]: ctx.line_to(x + xx + (px - gx0) * k, ytop + (py - top) * k)
            ctx.stroke()
        xx += (FW.get(ch, G.SPACE) + G.GAP) * k * G.CELL / G.TEXT_H

def width(s, h): return sum((FW.get(ch, G.SPACE) + G.GAP) for ch in s) * h / G.TEXT_H - G.GAP * h / G.TEXT_H

def send_knob(ctx, cx, cy, v, col, r=10.582):                 # ring (16 segments) + pointer -135..+135 deg
    ctx.set_line_width(1.5); ctx.set_line_cap(cairo.LINE_CAP_BUTT)
    ctx.set_source_rgb(*rgb(GREY))
    for i in range(16):
        a0, a1 = i / 16 * 2 * math.pi, (i + 1) / 16 * 2 * math.pi
        ctx.move_to(cx + r * math.sin(a0), cy - r * math.cos(a0)); ctx.line_to(cx + r * math.sin(a1), cy - r * math.cos(a1)); ctx.stroke()
    a = math.radians(-135 + 270 * v / 100)
    ctx.set_source_rgb(*rgb(col))
    ctx.move_to(cx, cy); ctx.line_to(cx + (r - 2.7) * math.sin(a), cy - (r - 2.7) * math.cos(a)); ctx.stroke()

def page(ctx, aux):
    svg = ET.parse(os.path.join(DISP, 'mixer.svg')).getroot()
    R.draw_svg(ctx, svg, {}, hidden={'lev1', 'lev2', 'lev3', 'lev4'})
    for t in range(4):
        s = str(VOL[t])
        if not aux:
            digits(ctx, s, CX[t] - 28.3, 85.3, 53.3 / 1.25 * 1.0, COLS[t])        # stock: lev box 85..139
        else:
            send_knob(ctx, CX[t], 90, SEND[t], COLS[t])
            h = 21.0; digits(ctx, s, CX[t] - width(s, h) / 2, 112, h, COLS[t])

def main(out):
    S = 3; W, H = 320 * S, 160 * S; pad = 12
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, 2 * W + 3 * pad, H + 2 * pad)
    ctx = cairo.Context(surf); ctx.set_source_rgb(0.25, 0.25, 0.27); ctx.paint()
    for n, aux in enumerate((False, True)):
        ctx.save(); ctx.translate(pad + n * (W + pad), pad); ctx.rectangle(0, 0, W, H); ctx.clip()
        ctx.set_source_rgb(0, 0, 0); ctx.paint(); ctx.scale(S, S); page(ctx, aux); ctx.restore()
    surf.write_to_png(out); print('->', out)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '/tmp/mixaux.png')
