#!/usr/bin/env python3
"""Builds the Docker Desktop for air/OS application icon (HVIF).

    python3 tools/make-icon.py resources/docker-desktop.hvif [preview.png]

The optional preview shows the icon at 64, 32 and 16 px on a light and on a
dark background (shapes hidden by their level of detail are left out at the
sizes where Haiku would hide them).

Source geometry
---------------
The whale is the Docker logo ("Moby"), a trademark of Docker, Inc., taken
from the single colour Docker mark published by simple-icons
(simple-icons@13.21.0, icons/docker.svg, 24 x 24 view box). WHALE below is
that file's whale outline, copied verbatim; its nine containers (rows of
five, three and one boxes of about 2.1 x 2.3 units on a 2.95 x 2.72 grid)
are redrawn rather than copied.

Adaptation to the Haiku icon guidelines
---------------------------------------
(https://www.haiku-os.org/development/icon-guidelines/)
* Scale: the whale is scaled by SCALE (24 units -> 56.4 canvas units) so it
  fills the canvas width, leaving room at the right for the cast shadow.
* Containers: nine boxes are unreadable at 32 x 32, so the stack keeps the
  logo's stepped pyramid with four, two and one boxes (ROWS) on an 8 unit
  grid (4 px at 32 x 32): 6 x 6 unit faces on a navy backing that forms the
  2 unit (1 px) outline and the gaps between the boxes. All edges sit on
  even canvas coordinates, so they are crisp at 32 px.
* 16 x 16: level of detail swaps in a second stack whose 1 x 1 px faces on
  a 1 px grid stay distinct at that size, and hides the eye.
* Lighting from the top left: the whale has a diagonal gradient from a
  pastel light blue through the dominant blue to a saturated deep blue;
  each row of boxes has a vertical gradient (lighter at the top), the upper
  rows a little lighter than the lower ones. No gloss.
* Colours: a mixed, slightly desaturated version of Docker blue (#2496ED)
  instead of the pure brand colour; the outline is a dark navy shade of the
  same blue instead of black.
* Outline: a 2 unit stroke around the whale, the navy backing around the
  stack.
* Shadow: the whale and stack silhouettes (one shape, so they do not
  darken where they overlap), translated to the lower right in translucent
  navy.
* Eye: the single colour mark has none; a small navy eye, as on Docker's
  earlier full colour logo, makes the whale read as a whale at 32 px.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hvif  # noqa: E402

# The whale of simple-icons' docker.svg (24 x 24 view box), verbatim.
WHALE = ('M23.763 9.89c-.065-.051-.672-.51-1.954-.51-.338.001-.676.03-1.01.087'
         '-.248-1.7-1.653-2.53-1.716-2.566l-.344-.199-.226.327c-.284.438-.49.922'
         '-.612 1.43-.23.97-.09 1.882.403 2.661-.595.332-1.55.413-1.744.42H.751'
         'a.751.751 0 00-.75.748 11.376 11.376 0 00.692 4.062c.545 1.428 1.355 '
         '2.48 2.41 3.124 1.18.723 3.1 1.137 5.275 1.137.983.003 1.963-.086 '
         '2.93-.266a12.248 12.248 0 003.823-1.389c.98-.567 1.86-1.288 2.61-2.136 '
         '1.252-1.418 1.998-2.997 2.553-4.4h.221c1.372 0 2.215-.549 2.68-1.009'
         '.309-.293.55-.65.707-1.046l.098-.288Z')
DECK_Y = 11.54          # the whale's back (the deck the containers sit on)

SCALE = 2.35            # 24 view box units -> 56.4 canvas units
ORIGIN_X = 2.0          # canvas x of the view box's x = 0
DECK = 34.0             # canvas y of the deck
ORIGIN_Y = DECK - DECK_Y * SCALE

GRID = 8                # container pitch (4 px at 32 x 32)
BOX = 6                 # container face; the 2 unit gaps are the outline
STACK_X = 8             # left edge of the first container face
# Container rows from the bottom: the columns (0..3) that hold a box.
ROWS = [(0, 1, 2, 3), (1, 2), (2,)]

SHADOW = (3, 2)         # cast shadow offset, to the lower right
SHADOW_ALPHA = 72
EYE, EYE_RADIUS = (12, 42), 2  # canvas units: a 2 x 2 px dot at 32 x 32

NAVY = hvif.hex_color('0c3468')
WHALE_STOPS = [(0.0, hvif.hex_color('9fd0f6')), (0.32, hvif.hex_color('4ea6ee')),
               (0.68, hvif.hex_color('2580d8')), (1.0, hvif.hex_color('1859ad'))]
ROW_STOPS = [  # (top, bottom) of a box face, per row from the bottom
    (hvif.hex_color('6fb8f2'), hvif.hex_color('2a7fd2')),
    (hvif.hex_color('84c4f4'), hvif.hex_color('3189d8')),
    (hvif.hex_color('9bd0f6'), hvif.hex_color('3891dd')),
]
HIDE_AT_16 = (0.3, 4.0)  # level of detail: shown from 19 px up
ONLY_AT_16 = (0.0, 0.3)  # ... and its replacement below that


def canvas(x, y):
    return (round(ORIGIN_X + x * SCALE, 2), round(ORIGIN_Y + y * SCALE, 2))


def arc_to_cubics(p0, rx, ry, phi, large, sweep, p1):
    """SVG elliptical arc to cubic segments (only rx == ry, phi == 0 used)."""
    (x1, y1), (x2, y2) = p0, p1
    dx, dy = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    r = rx
    lam = (dx * dx + dy * dy) / (r * r)
    if lam > 1:
        r *= math.sqrt(lam)
    sq = max(0.0, (r * r * r * r - r * r * (dx * dx + dy * dy)) / (r * r * (dx * dx + dy * dy)))
    coef = math.sqrt(sq) * (-1 if large == sweep else 1)
    cxp, cyp = coef * dy, -coef * dx
    cx, cy = cxp + (x1 + x2) / 2.0, cyp + (y1 + y2) / 2.0
    a1 = math.atan2(y1 - cy, x1 - cx)
    a2 = math.atan2(y2 - cy, x2 - cx)
    delta = a2 - a1
    if sweep and delta < 0:
        delta += 2 * math.pi
    elif not sweep and delta > 0:
        delta -= 2 * math.pi
    n = max(1, int(math.ceil(abs(delta) / (math.pi / 2))))
    step = delta / n
    k = 4.0 / 3.0 * math.tan(step / 4.0)
    result = []
    for i in range(n):
        t1, t2 = a1 + i * step, a1 + (i + 1) * step
        c1 = (cx + r * (math.cos(t1) - k * math.sin(t1)), cy + r * (math.sin(t1) + k * math.cos(t1)))
        c2 = (cx + r * (math.cos(t2) + k * math.sin(t2)), cy + r * (math.sin(t2) - k * math.cos(t2)))
        result.append((c1, c2, (cx + r * math.cos(t2), cy + r * math.sin(t2))))
    return result


def parse_path(d):
    """SVG path data (M, L/l, H/h, C/c, A/a, Z) to HVIF points in canvas units."""
    tokens = re.findall(r'[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?', d)
    nodes = []
    i = 0
    command = None
    current = (0.0, 0.0)

    def number():
        nonlocal i
        value = float(tokens[i])
        i += 1
        return value

    def flag():
        # Arc flags may be packed without separators ("00-.75").
        nonlocal i
        token = tokens[i]
        if len(token) > 1 and token[0] in '01':
            tokens[i] = token[1:]
            return int(token[0])
        i += 1
        return int(token)

    def add(point, handle_in=None):
        nodes.append({'p': point, 'in': handle_in or point, 'out': point})

    while i < len(tokens):
        if re.match(r'[A-Za-z]', tokens[i]):
            command = tokens[i]
            i += 1
            if command in 'Zz':
                if len(nodes) > 1 and math.dist(nodes[-1]['p'], nodes[0]['p']) < 1e-6:
                    nodes[0]['in'] = nodes[-1]['in']
                    nodes.pop()
                continue
        rel = command.islower()
        ox, oy = current if rel else (0.0, 0.0)
        c = command.upper()
        if c == 'M':
            current = (ox + number(), oy + number())
            add(current)
            command = 'l' if rel else 'L'
        elif c == 'L':
            current = (ox + number(), oy + number())
            add(current)
        elif c == 'H':
            current = ((current[0] if rel else 0.0) + number(), current[1])
            add(current)
        elif c == 'C':
            v = [number() for _ in range(6)]
            nodes[-1]['out'] = (ox + v[0], oy + v[1])
            current = (ox + v[4], oy + v[5])
            add(current, (ox + v[2], oy + v[3]))
        elif c == 'A':
            rx, ry, phi = number(), number(), number()
            large, sweep = flag(), flag()
            end = (ox + number(), oy + number())
            for c1, c2, p in arc_to_cubics(current, rx, ry, phi, large, sweep, end):
                nodes[-1]['out'] = c1
                add(p, c2)
            current = end
        else:
            raise ValueError('unsupported path command %r' % command)
    return [(canvas(*n['p']), canvas(*n['in']), canvas(*n['out'])) for n in nodes]


def rect(x0, y0, x1, y1):
    return {'closed': True, 'points': [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]}


def faces(fx, fw, fy, fh):
    """Container faces per row: within each GRID cell the face spans
    fx..fx+fw horizontally and fy..fy+fh down from the cell's top."""
    rows = []
    for r, cols in enumerate(ROWS):
        cell_top = DECK - (r + 1) * GRID
        rows.append([(STACK_X + c * GRID + fx, cell_top + fy,
                      STACK_X + c * GRID + fx + fw, cell_top + fy + fh) for c in cols])
    return rows


def stack_outline(rows, grow):
    """The outer edge of the faces grown by grow: the stack's backing. Each
    row is at most as wide as the one below it (a staircase)."""
    left, right = [], []
    below = None
    for boxes in rows:
        top = min(b[1] for b in boxes) - grow
        bottom = max(b[3] for b in boxes) + grow if below is None else below
        left.append((min(b[0] for b in boxes) - grow, bottom, top))
        right.append((max(b[2] for b in boxes) + grow, bottom, top))
        below = top
    points = []
    for x, bottom, top in left:
        points += [(x, bottom), (x, top)]
    for x, bottom, top in reversed(right):
        points += [(x, top), (x, bottom)]
    # Drop duplicate and collinear points.
    clean = []
    for p in points:
        if not clean or p != clean[-1]:
            clean.append(p)
    result = []
    n = len(clean)
    for k, p in enumerate(clean):
        a, b = clean[k - 1], clean[(k + 1) % n]
        if (a[0] == p[0] == b[0]) or (a[1] == p[1] == b[1]):
            continue
        result.append(p)
    return {'closed': True, 'points': result}


def signed_area(points):
    pts = [hvif.normalize_point(p)[0] for p in points]
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1])) / 2.0


def orient(path, clockwise=True):
    if (signed_area(path['points']) > 0) != clockwise:
        path['points'] = [(p, o, i) for p, i, o in
                          (hvif.normalize_point(q) for q in reversed(path['points']))]
    return path


def vertical(top, bottom, stops):
    return hvif.linear_gradient((0, top), (0, bottom), stops)


def circle(cx, cy, r):
    k = 0.5523 * r
    return {'closed': True, 'points': [
        ((cx, cy - r), (cx - k, cy - r), (cx + k, cy - r)),
        ((cx + r, cy), (cx + r, cy - k), (cx + r, cy + k)),
        ((cx, cy + r), (cx + k, cy + r), (cx - k, cy + r)),
        ((cx - r, cy), (cx - r, cy + k), (cx - r, cy - k))]}


def build():
    icon = {'styles': [], 'paths': [], 'shapes': []}

    def style(s):
        icon['styles'].append(s)
        return len(icon['styles']) - 1

    def path(p):
        icon['paths'].append(p)
        return len(icon['paths']) - 1

    def shape(style_index, paths, **extra):
        icon['shapes'].append(dict({'style': style_index, 'paths': list(paths)}, **extra))

    left, _ = canvas(0, 0)
    right, _ = canvas(24, 0)
    top = DECK - len(ROWS) * GRID
    bottom = canvas(0, 20.61)[1]
    shadow = style({'color': NAVY[:3] + (SHADOW_ALPHA,)})
    body = style(hvif.linear_gradient((left + 6, top + 4), (right - 10, bottom + 4), WHALE_STOPS))
    navy = style({'color': NAVY})
    rows = []
    for r, (light, dark) in enumerate(ROW_STOPS[:len(ROWS)]):
        cell_top = DECK - (r + 1) * GRID
        rows.append(style(vertical(cell_top, cell_top + BOX, [(0.0, light), (1.0, dark)])))

    whale = path(orient({'closed': True, 'points': parse_path(WHALE)}))
    big = faces(0, BOX, 0, BOX)              # 32 px and up: 3 x 3 px faces
    small = faces(0, 4, 2, 4)                # 16 px: 1 x 1 px faces
    stack = path(orient(stack_outline(big, 2)))
    stack16 = path(orient(stack_outline(small, 4)))

    shape(shadow, [whale, stack], matrix=[1.0, 0.0, 0.0, 1.0, float(SHADOW[0]), float(SHADOW[1])])
    shape(body, [whale])
    shape(navy, [whale], transformers=[{'type': 'stroke', 'width': 2, 'join': 2, 'cap': 2}])
    shape(navy, [stack], lod=HIDE_AT_16)
    for r, boxes in enumerate(big):
        shape(rows[r], [path(rect(*b)) for b in boxes], lod=HIDE_AT_16)
    shape(navy, [stack16], lod=ONLY_AT_16)
    for r, boxes in enumerate(small):
        shape(rows[r], [path(rect(*b)) for b in boxes], lod=ONLY_AT_16)
    shape(navy, [path(circle(*EYE, EYE_RADIUS))], lod=HIDE_AT_16)
    return icon


def visible(icon, size):
    scale = size / 64.0
    shapes = [s for s in icon['shapes']
              if s.get('lod', (0.0, 4.0))[0] <= scale <= s.get('lod', (0.0, 4.0))[1]]
    return dict(icon, shapes=shapes)


def preview_sheet(icon, out):
    from PIL import Image
    sizes = (64, 32, 16)
    pad = 12
    width = pad + sum(s + pad for s in sizes)
    sheet = Image.new('RGBA', (width, 2 * (64 + 2 * pad)), (0, 0, 0, 0))
    for row, bg in enumerate(((236, 236, 236, 255), (51, 51, 51, 255))):
        y = row * (64 + 2 * pad)
        sheet.paste(Image.new('RGBA', (width, 64 + 2 * pad), bg), (0, y))
        x = pad
        for size in sizes:
            image = hvif.preview(visible(icon, size), size)
            sheet.alpha_composite(image, (x, y + pad + (64 - size) // 2))
            x += size + pad
    sheet.save(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'docker-desktop.hvif'
    icon = build()
    data = hvif.encode(icon)
    hvif.decode(data)
    with open(out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes, %d shapes' % (out, len(data), len(icon['shapes'])))
    if len(sys.argv) > 2:
        preview_sheet(hvif.decode(data), sys.argv[2])


if __name__ == '__main__':
    main()
