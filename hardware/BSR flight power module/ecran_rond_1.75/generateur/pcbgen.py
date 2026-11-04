"""Génère ecran_rond_1.75.kicad_pcb : contour rond Ø46, 4 couches, empreintes avec nets,
placement : gros composants à position fixe, le reste par recherche gloutonne près de son "parent"."""
import json, math, sys, os, uuid
import fplib

OUT = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/out/ecran_rond_1.75'
d = json.load(open(os.path.join(OUT, 'design_export.json'), encoding='utf-8'))
CX, CY = 100.0, 100.0          # centre de la carte dans la feuille
RADIUS = 23.0                  # Ø46
EDGE_MARGIN = 0.6
CLEAR = 0.25                   # espace entre courtyards

comps = {}
for sh in d['sheets']:
    for c in sh['comps']:
        if c['ref'].startswith('#'): continue
        c['sheet'] = sh; comps[c['ref']] = c

# ---- nets : numérotation
net_names = sorted(n for n in d['nets'])
net_idx = {n: i + 1 for i, n in enumerate(net_names)}

# ---- placement fixe : (x, y, rot, face) ; coordonnées relatives au centre, y vers le bas
# Face F = côté composants (dos de l'instrument), face B = côté écran (composants bas sous la dalle)
PLACEMENT_JSON = sys.argv[2] if len(sys.argv) > 2 else '/home/claude/work/out/placement_big.json'
_pb = json.load(open(PLACEMENT_JSON))
_pb = _pb.get('placement', _pb)
FIXED = {r: tuple(v) for r, v in _pb.items()}
EDGE_OK = {'J1', 'H1', 'H2', 'H3'}      # peuvent dépasser le cercle (connecteur en bord)
SIDE = {}
# ancre (ref ou position) pour les autres, et face
ANCHOR = {}
def near(refs, anchor, side):
    for r in refs: ANCHOR[r] = anchor; SIDE[r] = side
near(['C10'], 'U3', 'B'); near(['Y1', 'C11', 'C12', 'R7', 'R8', 'TP1', 'TP2', 'TP3', 'TP4'] + [f'C{i}' for i in range(13, 20)], 'U1', 'F')
near(['R11', 'R12'], 'J3', 'B')
near(['R5', 'C20'], 'SW1', 'F'); near(['R6', 'C21'], 'SW2', 'F')
near(['C22', 'C23', 'C24', 'R9', 'R10'], 'E1', 'F')
near(['R1', 'R2', 'D2'], 'J1', 'F'); near(['D1'], (13, -2), 'F')
near(['D3', 'D4', 'C1', 'C2', 'R3', 'C3', 'L1', 'C4', 'C5', 'C6'], 'U2', 'F')
near(['R13', 'C28', 'C29', 'C30', 'C31', 'C32', 'C33', 'C34', 'C35', 'C36', 'C37', 'C38', 'R14', 'R15', 'FB1', 'C7', 'C8', 'NT1'], 'U4', 'B')
near(['R16', 'R17', 'C39', 'C40', 'C41', 'C42'], 'U5', 'F')
near(['D5', 'C9'], 'BT1', 'F')
near(['C25', 'C26', 'C27'], 'J3', 'B')
near(['C43', 'TP5', 'TP6', 'TP7', 'TP8', 'TP9', 'TP10'], 'U6', 'F')
near(['Y2', 'C44', 'C45', 'C46', 'R18'], 'U7', 'F')
near(['R19', 'R20', 'R21', 'C47', 'C48'], 'J6', 'B')
near(['R22', 'C49', 'C50', 'R23', 'C51', 'R24', 'C52', 'C53', 'L2', 'TP11', 'TP12', 'TP13'], 'U8', 'B')

# ---- géométrie
_cy = {}
def crt(fp):
    if fp not in _cy: _cy[fp] = fplib.courtyard(fp)
    return _cy[fp]
def bbox_at(fp, x, y, rot, side='F'):
    x0, y0, x1, y1 = crt(fp)
    if side == 'B': y0, y1 = -y1, -y0          # retourné haut/bas autour de l'ancre
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    a = math.radians(rot)
    rp = [(x + px * math.cos(a) + py * math.sin(a), y - px * math.sin(a) + py * math.cos(a)) for px, py in pts]
    xs = [p[0] for p in rp]; ys = [p[1] for p in rp]
    return (min(xs), min(ys), max(xs), max(ys))

def inside(b):
    r = RADIUS - EDGE_MARGIN
    return all(math.hypot(px, py) <= r for px in (b[0], b[2]) for py in (b[1], b[3]))

def overlap(a, b):
    return not (a[2] + CLEAR <= b[0] or b[2] + CLEAR <= a[0] or a[3] + CLEAR <= b[1] or b[3] + CLEAR <= a[1])

placed = {}   # ref -> (x, y, rot, bbox, side)
for ref, (x, y, rot, side) in FIXED.items():
    c = comps[ref]
    b = bbox_at(c['footprint'], x, y, rot, side)
    if not inside(b) and ref not in EDGE_OK: print('ATTENTION hors contour :', ref, [round(v, 1) for v in b])
    for r2, (_, _, _, b2, s2) in placed.items():
        if s2 == side and overlap(b, b2) and not ref.startswith('H') and not r2.startswith('H'): print('ATTENTION chevauchement :', ref, r2)
    placed[ref] = (x, y, rot, b, side)
# les trous traversent : les ajouter sur les deux faces pour les collisions
for h in ('J1', 'J2', 'BT1'):    # trous traversants : occupent les deux faces
    x, y, rot, b, side = placed[h]
    placed[h] = (x, y, rot, b, 'both')

# ---- trous de fixation M2 : 3 trous vers r = 19.5 mm, angles 90 / 210 / 330 deg, aux deux faces
for h, a0 in (('H1', 330), ('H2', 210), ('H3', 90)):
    done = False
    for da in [0, 5, -5, 10, -10, 15, -15, 20, -20, 25, -25, 30, -30, 40, -40, 50, -50, 60, -60, 70, -70, 80, -80]:
        for rr in (19.5, 19.0, 20.0, 18.5, 20.5):
            a = math.radians(a0 + da)
            x, y = round(rr * math.cos(a), 2), round(rr * math.sin(a), 2)
            hb = (x - 2.6, y - 2.6, x + 2.6, y + 2.6)
            if any(overlap(hb, b2) for (_, _, _, b2, s2) in placed.values()): continue
            placed[h] = (x, y, 0, hb, 'both'); done = True; break
        if done: break
    if not done: print('IMPOSSIBLE de placer le trou', h); placed[h] = (0, 0, 0, (-2.6, -2.6, 2.6, 2.6), 'both')

def anchor_pos(ref):
    a = ANCHOR.get(ref, 'U1')
    if isinstance(a, tuple): return a
    if a in placed: return placed[a][0], placed[a][1]
    return (0, 0)

def area(c):
    b = crt(c['footprint']); return (b[2] - b[0]) * (b[3] - b[1])

def collides(b, side):
    return any((s2 == side or s2 == 'both') and overlap(b, b2) for (_, _, _, b2, s2) in placed.values())

rest = [c for r, c in comps.items() if r not in FIXED and r not in placed]
rest.sort(key=lambda c: -area(c))
for c in rest:
    ax, ay = anchor_pos(c['ref']); side = SIDE.get(c['ref'], 'F')
    best = None
    for radius_step in range(0, 50):
        rr = radius_step * 0.5
        n = max(1, int(2 * math.pi * rr / 0.75)) if rr > 0 else 1
        for k in range(n):
            t = 2 * math.pi * k / n
            x, y = round(ax + rr * math.cos(t), 2), round(ay + rr * math.sin(t), 2)
            for rot in (0, 90):
                b = bbox_at(c['footprint'], x, y, rot, side)
                if not inside(b): continue
                if collides(b, side): continue
                best = (x, y, rot, b, side); break
            if best: break
        if best: break
    if not best:
        print('IMPOSSIBLE à placer :', c['ref']); best = (0, 30, 0, bbox_at(c['footprint'], 0, 30, 0), side)
    placed[c['ref']] = best

# ---- écriture
def q(s): return '"' + str(s).replace('"', '\\"') + '"'
L = []
L.append('(kicad_pcb (version 20241229) (generator "pcbnew") (generator_version "9.0")')
L.append('  (general (thickness 1.0) (legacy_teardrops no))')
L.append('  (paper "A4")')
L.append('  (title_block (title "Écran rond 1,75\\" KHEPRI") (company "Blue Smart Robotics") (comment 1 "Carte Ø46 mm dérivée de la Waveshare ESP32-S3-Touch-AMOLED-1.75-G ; composants côté F.Cu, écran AMOLED collé côté B.Cu"))')
L.append('''  (layers
    (0 "F.Cu" signal) (1 "In1.Cu" power) (2 "In2.Cu" power) (31 "B.Cu" signal)
    (32 "B.Adhes" user "B.Adhesive") (33 "F.Adhes" user "F.Adhesive") (34 "B.Paste" user) (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen") (38 "B.Mask" user) (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings") (41 "Cmts.User" user "User.Comments") (42 "Eco1.User" user "User.Eco1") (43 "Eco2.User" user "User.Eco2")
    (44 "Edge.Cuts" user) (45 "Margin" user) (46 "B.CrtYd" user "B.Courtyard") (47 "F.CrtYd" user "F.Courtyard") (48 "B.Fab" user) (49 "F.Fab" user)
    (50 "User.1" user) (51 "User.2" user) (52 "User.3" user) (53 "User.4" user) (54 "User.5" user) (55 "User.6" user) (56 "User.7" user) (57 "User.8" user) (58 "User.9" user)
  )''')
L.append('''  (setup (pad_to_mask_clearance 0) (allow_soldermask_bridges_in_footprints no)
    (stackup (layer "F.SilkS" (type "Top Silk Screen")) (layer "F.Paste" (type "Top Solder Paste")) (layer "F.Mask" (type "Top Solder Mask") (thickness 0.01))
      (layer "F.Cu" (type "copper") (thickness 0.035)) (layer "dielectric 1" (type "prepreg") (thickness 0.1) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "In1.Cu" (type "copper") (thickness 0.0175)) (layer "dielectric 2" (type "core") (thickness 0.565) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "In2.Cu" (type "copper") (thickness 0.0175)) (layer "dielectric 3" (type "prepreg") (thickness 0.1) (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
      (layer "B.Cu" (type "copper") (thickness 0.035)) (layer "B.Mask" (type "Bottom Solder Mask") (thickness 0.01)) (layer "B.Paste" (type "Bottom Solder Paste")) (layer "B.SilkS" (type "Bottom Silk Screen"))
      (copper_finish "ENIG") (dielectric_constraints no))
    (pcbplotparams (layerselection 0x00000000_00000000_55555555_5755f5ff) (plot_on_all_layers_selection 0x00000000_00000000_00000000_00000000) (disableapertmacros no) (usegerberextensions no) (usegerberattributes yes) (usegerberadvancedattributes yes) (creategerberjobfile yes) (dashed_line_dash_ratio 12.0) (dashed_line_gap_ratio 3.0) (svgprecision 4) (plotframeref no) (mode 1) (useauxorigin no) (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15.0) (pdf_front_fp_property_popups yes) (pdf_back_fp_property_popups yes) (pdf_metadata yes) (pdf_single_document no) (dxfpolygonmode yes) (dxfimperialunits yes) (dxfusepcbnewfont yes) (psnegative no) (psa4output no) (plot_black_and_white yes) (sketchpadsonfab no) (plotpadnumbers no) (hidednponfab no) (sketchdnponfab yes) (crossoutdnponfab yes) (subtractmaskfromsilk no) (outputformat 1) (mirror no) (drillshape 1) (scaleselection 1) (outputdirectory "production/"))
  )''')
L.append('  (net 0 "")')
for n in net_names:
    L.append(f'  (net {net_idx[n]} {q(n)})')

for ref, (x, y, rot, b, side) in placed.items():
    c = comps[ref]
    netmap = {}
    for pin, net in c['nets'].items():
        if net: netmap[pin] = (net_idx[net], net)
    path = f"/{d['root_uuid']}/{c['sheet']['uuid']}/{c['uuid']}"
    L.append(fplib.emit(c['footprint'], ref, c['value'], round(CX + x, 3), round(CY + y, 3), rot, netmap, path, c['sheet']['name'], c['sheet']['file'], c['desc']))

# contour + zone de dégagement antenne + sérigraphie
L.append(f'  (gr_circle (center {CX} {CY}) (end {CX + RADIUS} {CY}) (stroke (width 0.1) (type default)) (fill no) (layer "Edge.Cuts") (uuid "{uuid.uuid4()}"))')
L.append(f'  (gr_text "KHEPRI ecran rond 1.75 v0.2" (at {CX} {CY + 24.5} 0) (layer "F.SilkS") (uuid "{uuid.uuid4()}") (effects (font (size 1 1) (thickness 0.15))))')
# zone keepout sous l'antenne céramique (pas de cuivre)
ax, ay = CX + FIXED['E1'][0], CY + FIXED['E1'][1]
L.append(f'''  (zone (net 0) (net_name "") (layers "F&B.Cu" "In1.Cu" "In2.Cu") (uuid "{uuid.uuid4()}") (name "keepout_antenne") (hatch edge 0.5)
    (keepout (tracks allowed) (vias not_allowed) (pads allowed) (copperpour not_allowed) (footprints allowed))
    (polygon (pts (xy {ax - 6} {ay - 2.5}) (xy {ax + 6} {ay - 2.5}) (xy {ax + 6} {ay + 2.5}) (xy {ax - 6} {ay + 2.5}))))''')
# plans de masse : In1 = GND, In2 = GND, B.Cu = GND (l'écran est collé dessus)
for layer in ('In1.Cu', 'In2.Cu', 'B.Cu', 'F.Cu'):
    L.append(f'''  (zone (net {net_idx['GND']}) (net_name "GND") (layer "{layer}") (uuid "{uuid.uuid4()}") (name "GND_{layer}") (hatch edge 0.5) (priority 0)
    (connect_pads (clearance 0.25)) (min_thickness 0.2) (filled_areas_thickness no)
    (fill yes (thermal_gap 0.3) (thermal_bridge_width 0.3))
    (polygon (pts (xy {CX - 24} {CY - 24}) (xy {CX + 24} {CY - 24}) (xy {CX + 24} {CY + 24}) (xy {CX - 24} {CY + 24}))))''')
L.append('  (embedded_fonts no)')
L.append(')')
open(os.path.join(OUT, d['project'] + '.kicad_pcb'), 'w', encoding='utf-8').write('\n'.join(L) + '\n')

# récapitulatif placement
with open(os.path.join(OUT, 'placement.csv'), 'w', encoding='utf-8') as f:
    f.write('ref;x_mm;y_mm;rot;face\n')
    for ref, (x, y, rot, b, side) in placed.items(): f.write(f'{ref};{x};{y};{rot};{side}\n')
print('PCB écrit,', len(placed), 'empreintes')
