"""Placement des gros composants par recuit simulé sur la carte ronde Ø46.
Retourne un dict ref -> (x, y, rot, face) utilisable comme FIXED dans pcbgen."""
import math, random, json
import fplib

RADIUS = 23.0
R_IN = RADIUS - 0.6

# ref: (face, rotations permises, marge autour du courtyard pour laisser la place aux passifs, "bord" souhaité)
BIG = {
    'U1': ('F', [0], 1.3, None), 'U5': ('F', [0, 90], 0.6, None), 'U2': ('F', [0, 90], 0.8, None), 'L1': ('F', [0], 0.3, None),
    'J5': ('F', [0, 90, 180, 270], 0.3, 'rim'), 'J4': ('F', [0, 90, 180, 270], 0.3, 'rim'), 'SW1': ('F', [0, 90], 0.3, 'rim'),
    'SW2': ('F', [0, 90], 0.3, 'rim'), 'J2': ('both', [0, 90, 180, 270], 0.3, 'rim'), 'BT1': ('both', [0, 90], 0.3, None),
    'D3': ('F', [0, 90], 0.3, None), 'D4': ('F', [0, 90], 0.3, None),
    'J3': ('B', [0, 90, 180, 270], 0.6, None), 'U8': ('B', [0, 90], 0.5, None), 'J6': ('B', [0, 90, 180, 270], 0.3, 'slot'),
    'U4': ('B', [0, 90], 0.9, None), 'U6': ('F', [0, 90], 0.6, None), 'U7': ('F', [0, 90], 0.6, None),
    'U3': ('B', [0, 90], 0.5, None), 'J7': ('B', [0, 90, 180, 270], 0.3, 'rim'),
}
FIX = {'E1': (0, -21.1, 0, 'F'), 'J1': (19.5, 0, 90, 'both')}
FIX_MARGIN = 0.3
IGNORE_NETS = {'GND', 'AGND', '+3V3', '+5V', 'A3V3', 'VCC_RTC', 'VBUS', '+5V_IN'}

def load_design(path):
    d = json.load(open(path, encoding='utf-8'))
    comps = {}
    for sh in d['sheets']:
        for c in sh['comps']:
            if not c['ref'].startswith('#'): comps[c['ref']] = c
    return d, comps

def rot_bbox(b, x, y, rot, side):
    x0, y0, x1, y1 = b
    if side == 'B': y0, y1 = -y1, -y0
    a = math.radians(rot)
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    rp = [(x + px * math.cos(a) + py * math.sin(a), y - px * math.sin(a) + py * math.cos(a)) for px, py in pts]
    xs = [p[0] for p in rp]; ys = [p[1] for p in rp]
    return (min(xs), min(ys), max(xs), max(ys))

def solve(design_json, seed=1, iters=250000, verbose=True):
    d, comps = load_design(design_json)
    rnd = random.Random(seed)
    refs = list(BIG)
    crt = {r: fplib.courtyard(comps[r]['footprint']) for r in refs + list(FIX)}
    # nets entre gros composants (pour la longueur de fil)
    big_set = set(refs) | set(FIX)
    links = []
    for net, pins in d['nets'].items():
        if net in IGNORE_NETS: continue
        rs = {p.split('.')[0] for p in pins} & big_set
        rs = sorted(rs)
        for i in range(len(rs)):
            for j in range(i + 1, len(rs)):
                links.append((rs[i], rs[j]))
    # liaisons à travers des passifs (non vues par la netlist directe), pondérées
    EXTRA = [('U8', 'J7', 4), ('E1', 'J4', 3), ('U4', 'U5', 2), ('U2', 'L1', 3), ('U1', 'J3', 3), ('U1', 'U3', 2),
             ('U2', 'J2', 1), ('U2', 'J1', 1), ('U2', 'D3', 2), ('U2', 'D4', 2), ('U5', 'J5', 2), ('U7', 'BT1', 1), ('U1', 'SW1', 1), ('U1', 'SW2', 1)]
    for a, b2, w in EXTRA:
        links.extend([(a, b2)] * w)
    state = {}
    for r in refs:
        face, rots, m, pref = BIG[r]
        ang = rnd.uniform(0, 2 * math.pi); rr = rnd.uniform(0, 14)
        state[r] = [rr * math.cos(ang), rr * math.sin(ang), rnd.choice(rots)]
    for r, (x, y, rot, face) in FIX.items():
        state[r] = [x, y, rot]
    def face_of(r): return FIX[r][3] if r in FIX else BIG[r][0]
    def margin(r): return FIX_MARGIN if r in FIX else BIG[r][2]
    def bb(r):
        x, y, rot = state[r]; m = margin(r)
        b = crt[r]; b = (b[0] - m, b[1] - m, b[2] + m, b[3] + m)
        return rot_bbox(b, x, y, rot, face_of(r))
    def pair_cost(r1, r2, b1, b2):
        f1, f2 = face_of(r1), face_of(r2)
        if f1 != f2 and 'both' not in (f1, f2): return 0.0
        ox = min(b1[2], b2[2]) - max(b1[0], b2[0]); oy = min(b1[3], b2[3]) - max(b1[1], b2[1])
        if ox > 0 and oy > 0: return 30.0 * ox * oy + 20.0
        return 0.0
    def edge_cost(r, b):
        if r in ('J1',): return 0.0
        c = 0.0
        for px in (b[0], b[2]):
            for py in (b[1], b[3]):
                rr = math.hypot(px, py)
                if rr > R_IN: c += 40.0 * (rr - R_IN) ** 2 + 10.0
        return c
    def pref_cost(r, b):
        if r in FIX: return 0.0
        pref = BIG[r][3]
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        rr = math.hypot(cx, cy)
        if r == 'J3':                       # nappe écran : près du centre
            return 2.0 * max(0.0, rr - 6.0)
        if pref is None: return 0.0
        half = max(b[2] - b[0], b[3] - b[1]) / 2
        c = 1.5 * max(0.0, (R_IN - half - 1.0) - rr)
        if pref == 'slot':                  # microSD : fente vers l'extérieur, au bord
            rot = state[r][2]
            mx, my = {0: (0, 1), 90: (1, 0), 180: (0, -1), 270: (-1, 0)}[rot]
            dot = (mx * cx + my * cy) / rr if rr > 0.1 else 0.0
            c += 8.0 * (1.0 - dot)
            # bord côté fente : point le plus loin dans la direction de la fente
            far = max(px * mx + py * my for px in (b[0], b[2]) for py in (b[1], b[3]))
            c += 2.0 * abs((R_IN - 0.3) - far) if dot > 0.8 else 0.0
        return c
    def wire_cost():
        c = 0.0
        for a, b2 in links:
            xa, ya, _ = state[a]; xb, yb, _ = state[b2]
            c += 0.2 * (abs(xa - xb) + abs(ya - yb))
        return c
    def total():
        bbs = {r: bb(r) for r in state}
        c = wire_cost()
        rl = list(state)
        for i in range(len(rl)):
            c += edge_cost(rl[i], bbs[rl[i]]) + pref_cost(rl[i], bbs[rl[i]])
            for j in range(i + 1, len(rl)):
                c += pair_cost(rl[i], rl[j], bbs[rl[i]], bbs[rl[j]])
        return c
    cur = total(); best = cur; best_state = {r: list(v) for r, v in state.items()}
    T0, T1 = 30.0, 0.05
    for it in range(iters):
        T = T0 * (T1 / T0) ** (it / iters)
        r = rnd.choice(refs)
        touched = {r: list(state[r])}
        mv = rnd.random()
        if mv < 0.75:
            sg = 3.0 if T > 2 else 1.0
            state[r][0] += rnd.gauss(0, sg); state[r][1] += rnd.gauss(0, sg)
        elif mv < 0.9:
            state[r][2] = rnd.choice(BIG[r][1])
        else:
            r2 = rnd.choice(refs)
            if r2 != r and face_of(r2) == face_of(r):
                touched[r2] = list(state[r2])
                state[r][0], state[r2][0] = state[r2][0], state[r][0]
                state[r][1], state[r2][1] = state[r2][1], state[r][1]
        new = total()
        if new <= cur or rnd.random() < math.exp(-(new - cur) / T):
            cur = new
            if cur < best:
                best = cur; best_state = {k: list(v) for k, v in state.items()}
        else:
            for k, v in touched.items(): state[k] = v
    state = best_state
    if verbose:
        print('coût final', round(best, 1))
        bbs = {r: bb(r) for r in state}
        for r in refs:
            for r2 in refs:
                if r < r2 and pair_cost(r, r2, bbs[r], bbs[r2]) > 0: print('  chevauchement', r, r2)
            if edge_cost(r, bbs[r]) > 0: print('  hors contour', r)
    out = {}
    for r in refs:
        x, y, rot = state[r]
        out[r] = (round(x, 2), round(y, 2), rot, BIG[r][0] if BIG[r][0] != 'both' else 'F')
    for r, v in FIX.items():
        out[r] = (v[0], v[1], v[2], 'F')
    return out, best

if __name__ == '__main__':
    import sys
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    res, cost = solve(sys.argv[1], seed=seed, iters=int(sys.argv[3]) if len(sys.argv) > 3 else 250000)
    json.dump({'cost': cost, 'placement': res}, open(f'/home/claude/work/out/placement_big_{seed}.json', 'w'), indent=1)
    print('seed', seed, 'coût', round(cost, 1))
