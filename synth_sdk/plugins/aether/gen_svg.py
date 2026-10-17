#!/usr/bin/env python3
"""Generate aether.svg + aether_geom.h from orbit-1.svg (graphics by DAN76/Rackes) (satellite dish, 4 dials + sliders).

The OP-1's SVG parser (docs/16) has no CSS, no stroke-dasharray, no rotate() transforms and no usable text, so the
drawing is flattened here: class styles become attributes, opacity is baked into the colour (on black), transforms
and rounded rects are baked into the coordinates, paths are rewritten absolute with 2 decimals, the dotted signal
waves become rows of small dots, the labels become stroke-font paths (seq/seq_text.h glyphs) and everything moves
up by Y_SHIFT (content above ~y 140). Moving / code-drawn parts:
  dish     group: the dish and its feed, waves: the radio waves (under the right signal's mask); aether.c rotates both about the hinge (elevation, front /
           back tilt) and squeezes it horizontally (azimuth, side tilt)
  ndl0..3  dial needles, rotated by the knobs (blue ORBIT, green FORM, white SPACE, red DEPTH)
  sld0..2  slider handles, moved by the page-2 words (red has none)
  the dial value arcs are drawn by aether.c (geometry in aether_geom.h)
"""
import math, os, re, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(os.path.dirname(HERE))
SRC = os.path.join(HERE, 'orbit-1.svg')
Y_SHIFT = -4.0
NS = '{http://www.w3.org/2000/svg}'
DOT_STEP = 2.68                                   # the source's dotted stroke (dasharray 0 2.68, round caps)

# ---------------- source: styles ----------------
src = open(SRC).read()
rules = {}
for sel, body in re.findall(r'([^{}]+)\{([^}]*)\}', re.search(r'<style>(.*?)</style>', src, re.S).group(1)):
    props = dict((k.strip(), v.strip()) for k, v in (p.split(':', 1) for p in body.split(';') if ':' in p))
    for c in sel.split(','):
        rules.setdefault(c.strip().lstrip('.'), {}).update(props)
root = ET.fromstring(src)


def style(el):
    st = {}
    for c in (el.get('class') or '').split(): st.update(rules.get(c, {}))
    return st


def col(v, op):
    v = v.lstrip('#')
    if len(v) == 3: v = ''.join(c * 2 for c in v)
    return '#%02X%02X%02X' % tuple(round(int(v[i:i + 2], 16) * op) for i in (0, 2, 4))


def matrix(t):                                    # transform attribute -> (a b c d e f)
    m = (1, 0, 0, 1, 0, 0)
    for fn, args in re.findall(r'(\w+)\(([^)]*)\)', t or ''):
        a = [float(x) for x in re.split(r'[ ,]+', args.strip())]
        if fn == 'translate': n = (1, 0, 0, 1, a[0], a[1] if len(a) > 1 else 0)
        elif fn == 'rotate':
            r = math.radians(a[0]); n = (math.cos(r), math.sin(r), -math.sin(r), math.cos(r), 0, 0)
        else: raise ValueError(fn)
        m = (m[0] * n[0] + m[2] * n[1], m[1] * n[0] + m[3] * n[1], m[0] * n[2] + m[2] * n[3],
             m[1] * n[2] + m[3] * n[3], m[0] * n[4] + m[2] * n[5] + m[4], m[1] * n[4] + m[3] * n[5] + m[5])
    return m


def path_ops(d):                                  # absolute M / L / C / Z (harness/render.py's parser)
    import sys; sys.path.insert(0, os.path.join(SDK, 'harness'))
    import render
    return render.path_ops(d)


def f2(v): return ('%.2f' % v).rstrip('0').rstrip('.') if abs(v) >= 0.005 else '0'


# ---------------- flatten: every drawable -> (kind, points / ops, attrs) in screen coordinates ----------------
items = []                                         # (tag, data, stroke, fill, width, group)


def walk(el, m, op, grp):
    tag = el.tag.replace(NS, '')
    st = style(el)
    op *= float(st.get('opacity', 1))
    m2 = m
    if el.get('transform'):
        a = matrix(el.get('transform'))
        m2 = (m[0] * a[0] + m[2] * a[1], m[1] * a[0] + m[3] * a[1], m[0] * a[2] + m[2] * a[3],
              m[1] * a[2] + m[3] * a[3], m[0] * a[4] + m[2] * a[5] + m[4], m[1] * a[4] + m[3] * a[5] + m[5])
    P = lambda x, y: (m2[0] * x + m2[2] * y + m2[4], m2[1] * x + m2[3] * y + m2[5] + Y_SHIFT)
    if tag in ('svg', 'g'):
        for ch in el: walk(ch, m2, op, grp)
        return
    if tag in ('defs', 'style', 'text') or (tag == 'rect' and not el.get('class')): return   # bg rect, labels: own
    f = lambda k: float(el.get(k, 0))
    stroke = st.get('stroke'); fill = st.get('fill', '#000' if 'fill' not in st else st['fill'])
    if fill == 'none': fill = None
    stroke = col(stroke, op) if stroke and stroke != 'none' else None
    fill = col(fill, op) if fill else None
    w = float(st.get('stroke-width', '1').replace('px', ''))
    dotted = 'stroke-dasharray' in st
    ops = []
    if tag == 'line': ops = [('M', *P(f('x1'), f('y1'))), ('L', *P(f('x2'), f('y2')))]
    elif tag == 'polyline':
        p = [float(v) for v in el.get('points').split()]
        ops = [('M' if i == 0 else 'L', *P(p[i], p[i + 1])) for i in range(0, len(p), 2)]
    elif tag == 'rect':                            # rounded corners dropped (1 px at this size)
        x, y, wd, ht = f('x'), f('y'), f('width'), f('height')
        ops = [('M', *P(x, y)), ('L', *P(x + wd, y)), ('L', *P(x + wd, y + ht)), ('L', *P(x, y + ht)), ('Z',)]
    elif tag in ('circle', 'ellipse'):
        cx, cy = f('cx'), f('cy'); rx = f('r') or f('rx'); ry = f('r') or f('ry')
        k = 0.5523
        q = [(cx + rx, cy), (cx, cy + ry), (cx - rx, cy), (cx, cy - ry)]
        ops = [('M', *P(*q[0]))]
        for i in range(4):                         # 4 cubic quarter arcs
            (x0, y0), (x1, y1) = q[i], q[(i + 1) % 4]
            c1 = (x0 + (0 if i % 2 else 0) - (k * rx if i == 1 else 0) + (0), 0)
            if i == 0: c1, c2 = (cx + rx, cy + k * ry), (cx + k * rx, cy + ry)
            elif i == 1: c1, c2 = (cx - k * rx, cy + ry), (cx - rx, cy + k * ry)
            elif i == 2: c1, c2 = (cx - rx, cy - k * ry), (cx - k * rx, cy - ry)
            else: c1, c2 = (cx + k * rx, cy - ry), (cx + rx, cy - k * ry)
            ops.append(('C', *P(*c1), *P(*c2), *P(x1, y1)))
        ops.append(('Z',))
    elif tag == 'path':
        for o in path_ops(el.get('d')):
            if o[0] == 'Z': ops.append(o)
            else: ops.append((o[0], *sum((P(o[i], o[i + 1]) for i in range(1, len(o), 2)), ())))
    items.append(dict(tag=tag, ops=ops, stroke=stroke, fill=fill, w=w, dotted=dotted, grp=grp, el=el))


# group membership by source structure: the dish = the group holding the ellipse + the waves + dish ribs / rim
def classify():
    kids = list(root)
    for i, ch in enumerate(kids):
        tag = ch.tag.replace(NS, '')
        grp = None
        if tag == 'g':
            inner = [c.tag.replace(NS, '') for c in ch]
            if 'ellipse' in inner: grp = 'dish'
            elif all(c.get('class') == 'cls-6' for c in ch): grp = 'waves'         # the radio waves (moved with the dish)
        elif tag in ('path', 'line') and ch.get('class') == 'cls-5':
            grp = 'dish'                                                            # ribs / rim
        walk(ch, (1, 0, 0, 1, 0, 0), 1.0, grp)


classify()

# ---------------- dials / sliders (source layout: left blue / white, right green / red) ----------------
DIALS = [  # knob, centre, colour, label, slider x, slider y0 .. y1, handle centre in the source
    dict(k=0, cx=55.26, cy=31.29, c='#4D9EFF', lab='ORBIT', sx=22.29, sy0=18.75, sy1=52.43),
    dict(k=1, cx=267.85, cy=31.29, c='#00ED95', lab='FORM', sx=297.85, sy0=18.75, sy1=52.43),
    dict(k=2, cx=55.26, cy=90.85, c='#FFFFFF', lab='SPACE', sx=22.29, sy0=78.31, sy1=112.0),
    dict(k=3, cx=267.85, cy=90.85, c='#FF3A5D', lab='DEPTH', sx=297.85, sy0=78.31, sy1=112.0),
]
R_ARC = 13.07                                     # value arc radius (the bright arc of the source)
A0 = math.radians(135)                            # arc start (lower left), 270 degrees clockwise


def is_value_part(it):
    """the parts aether.c draws / moves: bright value arcs, needles, slider handles"""
    ops = it['ops']
    if it['tag'] == 'rect' and it['el'].get('class') in ('cls-4', 'cls-7', 'cls-11', 'cls-16'): return 'handle'
    if it['tag'] == 'line' and it['el'].get('class') in ('cls-5', 'cls-6', 'cls-2', 'cls-8') and it['grp'] is None:
        x0, y0 = ops[0][1:3]
        for d in DIALS:
            if abs(x0 - d['cx']) < 0.6 and abs(y0 - d['cy'] - Y_SHIFT) < 0.6: return 'needle'
    if it['tag'] == 'path' and it['grp'] is None:
        x0, y0 = ops[0][1:3]
        for d in DIALS:                                                            # partial arc from the start
            if abs(x0 - (d['cx'] - 9.29)) < 0.6 and abs(y0 - (d['cy'] + 9.19 + Y_SHIFT)) < 0.6 \
                    and it['stroke'] and it['stroke'].upper() == d['c'] and len(ops) < 5:
                return 'arc'
    return None


def ops_d(ops):
    out = []
    for o in ops:
        out.append(o[0] if o[0] == 'Z' else o[0] + ' '.join(f2(v) for v in o[1:]))
    return ''.join(out)


def dots(ops, step):                              # points along a path every `step` px
    pts, (px, py) = [], ops[0][1:3]
    acc = step; out = [(px, py)]
    for o in ops[1:]:
        if o[0] == 'L': seg = [(o[1], o[2])]
        elif o[0] == 'C':
            seg = []
            for t in range(1, 17):
                t /= 16; u = 1 - t
                seg.append((u**3 * px + 3*u*u*t*o[1] + 3*u*t*t*o[3] + t**3*o[5], u**3 * py + 3*u*u*t*o[2] + 3*u*t*t*o[4] + t**3*o[6]))
        else: continue
        for x, y in seg:
            d = math.hypot(x - px, y - py)
            while d >= acc:
                r = acc / d; px, py = px + (x - px) * r, py + (y - py) * r
                out.append((px, py)); d = math.hypot(x - px, y - py); acc = step
            acc -= d; px, py = x, y
    return out


# ---------------- stroke-font labels (seq/seq_text.h) ----------------
def glyphs():
    h = open(os.path.join(SDK, 'seq', 'seq_text.h')).read()
    fc = re.search(r'sq_fc\[\] = "([^"]*)"', h).group(1)
    num = lambda name: [int(v, 0) for v in re.search(name + r'\[\] = \{([^}]*)\}', h).group(1).replace('\n', ' ').split(',') if v.strip()]
    fo, fn, seg = num('sq_fo'), num('sq_fn'), num('sq_seg')
    g = {}
    for k, ch in enumerate(fc):
        g[ch] = [((seg[2 * j] >> 4, seg[2 * j] & 15), (seg[2 * j + 1] >> 4, seg[2 * j + 1] & 15)) for j in range(fo[k], fo[k] + fn[k])]
    return g


def label(text, cx, top, s, colour, o):
    g = glyphs()
    wch = 4 * s + 1.6 * s                         # glyph 4 units + gap
    x = cx - (len(text) * wch - 1.6 * s) / 2
    d = []
    for ch in text:
        for (x0, y0), (x1, y1) in g.get(ch, []):
            d.append('M%s %sL%s %s' % (f2(x + x0 * s), f2(top + y0 * s), f2(x + x1 * s), f2(top + y1 * s)))
        x += wch
    o.append(f'<path fill="none" stroke="{colour}" stroke-width="1.3" stroke-linecap="square" d="{"".join(d)}"/>')


# ---------------- output ----------------
DIM = 0.35                                        # the inactive page's dial / slider


def near_dial(it, d):                             # a static part of dial d (ring track, centre circle)
    xs = [v for o in it['ops'] for v in o[1::2]]; ys = [v for o in it['ops'] for v in o[2::2]]
    cy = d['cy'] + Y_SHIFT
    return xs and min(xs) > d['cx'] - 16 and max(xs) < d['cx'] + 16 and min(ys) > cy - 16 and max(ys) < cy + 16


def near_slider(it, d):                           # a static part of slider d (bracket, ticks)
    xs = [v for o in it['ops'] for v in o[1::2]]; ys = [v for o in it['ops'] for v in o[2::2]]
    return xs and min(xs) > d['sx'] - 7 and max(xs) < d['sx'] + 7 and min(ys) > d['sy0'] + Y_SHIFT - 2 \
        and max(ys) < d['sy1'] + Y_SHIFT + 2


def is_star(it): return it['el'].get('class') == 'cls-14'


def star_geom(it):                                # centre, half width / height of a 4-point star path
    xs = [v for o in it['ops'] for v in o[1::2]]; ys = [v for o in it['ops'] for v in o[2::2]]
    return ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (max(xs) - min(xs)) / 2, (max(ys) - min(ys)) / 2)


STARS = []                                        # (x, y, hw, hh) in source order
DOTS = []                                         # the dotted signals: points
DOT_SPLIT = []                                    # end index of each signal


def svg():
    o = ['<?xml version="1.0" encoding="utf-8"?>',
         '<svg version="1.1" id="Layer_1" xmlns="http://www.w3.org/2000/svg" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">',
         '<!-- generated by gen_svg.py from orbit-1.svg. aether.c moves the dish + waves (wave0..3 shown by FORM), the',
         '     needles (ndl, nld) and handles (sld, sdd), shows the active page (dial d / dd, slider s / sd, bright / dim)',
         '     and draws the value arcs, the stars and the labels (stock font) -->']

    def emit(it, ident=None, dim=1.0):
        st_ = col(it['stroke'], dim) if it['stroke'] else None
        fi = col(it['fill'], dim) if it['fill'] else None
        a = f'stroke="{st_}" stroke-width="{f2(it["w"])}" stroke-linecap="round" stroke-linejoin="round"' if st_ else ''
        fl = f'fill="{fi}"' if fi else 'fill="none"'
        idt = f' id="{ident}"' if ident else ''
        if it['dotted']:                           # dotted stroke: dots drawn by aether.c (AGG: a path of 90 dashes
            DOTS.extend(dots(it['ops'], DOT_STEP))  # ran out of cell blocks, small circles did not show)
            DOT_SPLIT.append(len(DOTS))
            return
        o.append(f'<path{idt} {fl} {a} d="{ops_d(it["ops"])}"/>')

    dish = [it for it in items if it['grp'] == 'dish']
    rest = [it for it in items if it['grp'] != 'dish']   # source order: the waves come before the black mask under
                                                         # the right signal, so it cuts them as drawn
    handles, needles, dial_parts, slider_parts = {}, {}, {d['k']: [] for d in DIALS}, {d['k']: [] for d in DIALS}
    waves = [it for it in rest if it['grp'] == 'waves']
    first_wave = True
    for it in rest:
        if it['grp'] == 'waves':
            if first_wave:                         # each arc its own node: FORM shows 1..4 of them (inner first)
                o.append('<g id="waves">')
                for n, w in enumerate(waves): emit(w, f'wave{n}')
                o.append('</g>'); first_wave = False
            continue
        if is_star(it): STARS.append(star_geom(it)); continue   # drawn by aether.c (colour / brightness by knob)
        kind = is_value_part(it)
        if kind == 'arc': continue                 # drawn by aether.c
        ox, oy = it['ops'][0][1:3]
        if it['tag'] == 'path' and it['grp'] is None and it['stroke'] and len(it['ops']) >= 5 and any(
                abs(ox - (d['cx'] - 9.29)) < 0.6 and abs(oy - (d['cy'] + 9.19 + Y_SHIFT)) < 0.6
                and it['stroke'].upper() == d['c'] for d in DIALS):
            it = dict(it, stroke=col(it['stroke'], 0.4))   # a bright full ring (red dial): dim track like the others
        if kind == 'needle':
            k = next(d['k'] for d in DIALS if abs(it['ops'][0][1] - d['cx']) < 0.6 and abs(it['ops'][0][2] - d['cy'] - Y_SHIFT) < 0.6)
            needles[k] = it; continue
        if kind == 'handle':
            k = next(d['k'] for d in DIALS if abs((it['ops'][0][1] + it['ops'][1][1]) / 2 - d['sx']) < 2
                     and d['sy0'] < it['ops'][0][2] - Y_SHIFT < d['sy1'])
            handles[k] = it; continue
        if it['tag'] == 'line' and abs(ox - 288.75) < 0.1 and oy > 60: continue   # red: the divider of its (removed) slider
        dk = next((d['k'] for d in DIALS if near_dial(it, d)), None)
        if dk is not None: dial_parts[dk].append(it); continue
        sk = next((d['k'] for d in DIALS if near_slider(it, d)), None)
        if sk is not None: slider_parts[sk].append(it); continue
        emit(it)
    o.append('<g id="dish">')
    for it in dish: emit(it)
    o.append('</g>')
    for d in DIALS:
        k, cx, cy = d['k'], d['cx'], d['cy'] + Y_SHIFT
        ym = (d['sy0'] + d['sy1']) / 2 + Y_SHIFT
        for sfx, dm in (('', 1.0), ('d', DIM)):   # bright (this page active) and dim versions
            o.append(f'<g id="d{sfx}{k}">')
            for it in dial_parts[k]: emit(it, dim=dm)
            o.append(f'<line id="n{"l" if sfx else "d"}{k}" fill="none" stroke="{col(d["c"], dm)}" stroke-width="1.5" '
                     f'stroke-linecap="round" x1="{f2(cx)}" y1="{f2(cy)}" x2="{f2(cx)}" y2="{f2(cy - 6.5)}"/>')
            o.append('</g>')
            if k == 3: continue                    # red: no slider (no page 2)
            o.append(f'<g id="s{sfx}{k}">')
            for it in slider_parts[k]: emit(it, dim=dm)
            o.append(f'<path id="h{"d" if sfx else "b"}{k}" fill="{col(d["c"], dm)}" d="M{f2(d["sx"] - 5)} {f2(ym - 1.88)}'
                     f'H{f2(d["sx"] + 5)}V{f2(ym + 1.88)}H{f2(d["sx"] - 5)}Z"/>')
            o.append('</g>')
    o.append('</svg>')
    return '\n'.join(o) + '\n'


def header():
    L = ['/* generated by gen_svg.py - geometry of aether.svg (from orbit-1.svg) */',
         '#define AE_HINGE_X_Q10 %d' % round(132.34 * 1024), '#define AE_HINGE_Y_Q10 %d' % round((106.68 + Y_SHIFT) * 1024),
         '#define AE_FEED_X_Q10 %d' % round(167.64 * 1024), '#define AE_FEED_Y_Q10 %d' % round((71.91 + Y_SHIFT) * 1024),
         '#define AE_ARC_R_Q10 %d' % round(R_ARC * 1024),
         '#define AE_SLD_HALF_Q10 %d   /* slider travel: +- from the middle */' % round((52.43 - 18.75) / 2 * 1024 - 2.5 * 1024),
         '#define AE_LABEL_DY %d        /* label top below the dial centre, px */' % round(17.5 - 2)]
    L.append('static const int32_t ae_dial_x[4] = { ' + ', '.join(str(round(d['cx'] * 1024)) for d in DIALS) + ' };   /* by knob */')
    L.append('static const int32_t ae_dial_y[4] = { ' + ', '.join(str(round((d['cy'] + Y_SHIFT) * 1024)) for d in DIALS) + ' };')
    L.append('static const char *const ae_label[4] = { ' + ', '.join(f'"{d["lab"].lower()}"' for d in DIALS) + ' };   /* stock font: lower case (upper = icons) */')
    L.append('/* stars (source order): centre, half width, half height (Q10) */')
    L.append('static const int32_t ae_star[%d][4] = { ' % len(STARS) + ', '.join(
        '{ %s }' % ', '.join(str(round(v * 1024)) for v in st) for st in STARS) + ' };')
    L.append('/* dotted signals: dot centres, x / y in 1/64 px */')
    ends = DOT_SPLIT; starts = [0] + ends[:-1]
    L.append('static const uint16_t ae_dot_end[%d] = { %s };   /* end of each signal in ae_dot */' % (len(ends), ', '.join(map(str, ends))))
    L.append('static const uint16_t ae_dot_cx[%d] = { %s };    /* its centre x, 1/64 px (stretch pivot) */' % (len(ends), ', '.join(
        str(round((min(x for x, _ in DOTS[a:b]) + max(x for x, _ in DOTS[a:b])) / 2 * 64)) for a, b in zip(starts, ends))))
    L.append('static const uint16_t ae_dot[%d][2] = { ' % len(DOTS) + ', '.join(
        '{ %d, %d }' % (round(x * 64), round(y * 64)) for x, y in DOTS) + ' };')
    return '\n'.join(L) + '\n'


if __name__ == '__main__':
    open(os.path.join(HERE, 'aether.svg'), 'w').write(svg())
    open(os.path.join(HERE, 'aether_geom.h'), 'w').write(header())
    print('aether.svg, aether_geom.h')
