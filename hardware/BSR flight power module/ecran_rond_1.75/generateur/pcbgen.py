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
near(['C13', 'C18', 'C19', 'R7', 'R8', 'TP1', 'TP3', 'TP4'], 'U1', 'F')
near(['R11', 'R12'], 'J3', 'B')
near(['R5', 'C20'], 'SW1', 'F'); near(['R6', 'C21'], 'SW2', 'F')
near(['R1', 'R2', 'D2'], 'J1', 'F'); near(['D1'], (13, -2), 'F')
near(['D3', 'D4', 'C1', 'C2', 'R3', 'C3', 'L1', 'C4', 'C5', 'C6'], 'U2', 'F')
near(['R13', 'C28', 'C29', 'C30', 'C31', 'C32', 'C33', 'C34', 'C35', 'C36', 'C37', 'C38', 'R14', 'R15', 'FB1', 'C7', 'C8', 'NT1'], 'U4', 'auto')
near(['R16', 'R17', 'C39', 'C40', 'C41', 'C42'], 'U5', 'auto')
near(['D5', 'C9'], 'BT1', 'auto')
near(['C25', 'C26', 'C27'], 'J3', 'B')
near(['C43'], 'U6', 'auto')
near(['Y2', 'C44', 'C45', 'C46', 'R18'], 'U7', 'auto')
near(['R19', 'R20', 'R21', 'C47', 'C48'], 'J6', 'B')
near(['C49'], 'J7', 'auto')

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
for h in ('J1', 'J2'):    # trous traversants : occupent les deux faces
    x, y, rot, b, side = placed[h]
    placed[h] = (x, y, rot, b, 'both')
# zone d'antenne du module : interdite à tout composant, sur les deux faces
ANT_KEEPOUT = (-24.0, -30.0, 24.0, -6.75)   # = courtyard bibliothèque : 15 mm de part et d'autre de l'antenne (Espressif)
_ux, _uy, _urot = FIXED['U1'][0], FIXED['U1'][1], FIXED['U1'][2]
_a = math.radians(_urot)
_ant_pts = [(_ux + px * math.cos(_a) + py * math.sin(_a), _uy - px * math.sin(_a) + py * math.cos(_a))
            for px, py in ((ANT_KEEPOUT[0], ANT_KEEPOUT[1]), (ANT_KEEPOUT[2], ANT_KEEPOUT[1]), (ANT_KEEPOUT[2], ANT_KEEPOUT[3]), (ANT_KEEPOUT[0], ANT_KEEPOUT[3]))]
placed['_ANT'] = (0, 0, 0, (min(p[0] for p in _ant_pts), min(p[1] for p in _ant_pts), max(p[0] for p in _ant_pts), max(p[1] for p in _ant_pts)), 'both')
# vias thermiques du pad central du module (traversants) : rien côté B dessous
EP_VIAS = (-3.35, 0.6, 0.35, 4.3)
_ep_pts = [(_ux + px * math.cos(_a) + py * math.sin(_a), _uy - px * math.sin(_a) + py * math.cos(_a))
           for px, py in ((EP_VIAS[0], EP_VIAS[1]), (EP_VIAS[2], EP_VIAS[1]), (EP_VIAS[2], EP_VIAS[3]), (EP_VIAS[0], EP_VIAS[3]))]
placed['_EP'] = (0, 0, 0, (min(p[0] for p in _ep_pts), min(p[1] for p in _ep_pts), max(p[0] for p in _ep_pts), max(p[1] for p in _ep_pts)), 'both')

# ---- trous de fixation M2 (traversants, 2 faces) : on cherche les 3 emplacements libres près du bord
# qui maximisent l'écartement mutuel (triangle le plus grand possible)
_cands = []
for k in range(0, 360, 3):
    for rr in (20.0, 19.5, 20.5, 19.0, 18.5, 18.0, 17.0, 16.0, 15.0):
        a = math.radians(k)
        x, y = round(rr * math.cos(a), 2), round(rr * math.sin(a), 2)
        hb = (x - 2.3, y - 2.3, x + 2.3, y + 2.3)
        if any(overlap(hb, b2) for (_, _, _, b2, s2) in placed.values()): continue
        _cands.append((x, y, rr))
_best = None
if all(h in FIXED for h in ('H1', 'H2', 'H3')): _cands = []
_pts = _cands[::2] if len(_cands) > 400 else _cands
for i in range(len(_pts)):
    for j in range(i + 1, len(_pts)):
        dij = math.hypot(_pts[i][0] - _pts[j][0], _pts[i][1] - _pts[j][1])
        if dij < 6: continue
        for k in range(j + 1, len(_pts)):
            dik = math.hypot(_pts[i][0] - _pts[k][0], _pts[i][1] - _pts[k][1])
            djk = math.hypot(_pts[j][0] - _pts[k][0], _pts[j][1] - _pts[k][1])
            score = min(dij, dik, djk) + 0.3 * (_pts[i][2] + _pts[j][2] + _pts[k][2])
            if _best is None or score > _best[0]: _best = (score, _pts[i], _pts[j], _pts[k])
if all(h in FIXED for h in ('H1', 'H2', 'H3')):
    for h in ('H1', 'H2', 'H3'):
        x, y = FIXED[h][0], FIXED[h][1]; placed[h] = (x, y, 0, (x - 2.3, y - 2.3, x + 2.3, y + 2.3), 'both')
elif _best is None:
    print('IMPOSSIBLE de placer 3 trous')
else:
    for h, (x, y, rr) in zip(('H1', 'H2', 'H3'), _best[1:]):
        placed[h] = (x, y, 0, (x - 2.3, y - 2.3, x + 2.3, y + 2.3), 'both')

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
    if side == 'auto': side = placed[ANCHOR[c['ref']]][4] if placed[ANCHOR[c['ref']]][4] in ('F', 'B') else 'F'
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
    if ref.startswith('_'): continue
    c = comps[ref]
    netmap = {}
    for pin, net in c['nets'].items():
        if net: netmap[pin] = (net_idx[net], net)
    path = f"/{d['root_uuid']}/{c['sheet']['uuid']}/{c['uuid']}"
    L.append(fplib.emit(c['footprint'], ref, c['value'], round(CX + x, 3), round(CY + y, 3), rot, netmap, path, c['sheet']['name'], c['sheet']['file'], c['desc']))

# contour + zone de dégagement antenne + sérigraphie
L.append(f'  (gr_circle (center {CX} {CY}) (end {CX + RADIUS} {CY}) (stroke (width 0.1) (type default)) (fill no) (layer "Edge.Cuts") (uuid "{uuid.uuid4()}"))')
L.append(f'  (gr_text "KHEPRI ecran rond 1.75 v0.2" (at {CX} {CY + 24.5} 0) (layer "F.SilkS") (uuid "{uuid.uuid4()}") (effects (font (size 1 1) (thickness 0.15))))')
# zone sans cuivre sous et autour de l'antenne du module, toutes couches (recommandation Espressif)
_pts = ' '.join(f'(xy {round(CX + px, 3)} {round(CY + py, 3)})' for px, py in _ant_pts)
L.append(f'''  (zone (net 0) (net_name "") (layers "F&B.Cu" "In1.Cu" "In2.Cu") (uuid "{uuid.uuid4()}") (name "keepout_antenne_WROOM") (hatch edge 0.5)
    (keepout (tracks not_allowed) (vias not_allowed) (pads not_allowed) (copperpour not_allowed) (footprints allowed))
    (polygon (pts {_pts})))''')
# plans de masse : In1 = GND, In2 = GND, B.Cu = GND (l'écran est collé dessus)
for layer in ('In1.Cu', 'In2.Cu', 'B.Cu', 'F.Cu'):
    L.append(f'''  (zone (net {net_idx['GND']}) (net_name "GND") (layer "{layer}") (uuid "{uuid.uuid4()}") (name "GND_{layer}") (hatch edge 0.5) (priority 0)
    (connect_pads (clearance 0.25)) (min_thickness 0.2) (filled_areas_thickness no)
    (fill yes (thermal_gap 0.3) (thermal_bridge_width 0.3))
    (polygon (pts (xy {CX - 24} {CY - 24}) (xy {CX + 24} {CY - 24}) (xy {CX + 24} {CY + 24}) (xy {CX - 24} {CY + 24}))))''')
L.append('  (embedded_fonts no)')
L.append(')')
# modèles 3D absents de la bibliothèque KiCad : versions simplifiées du projet (models3d.py)
MODEL_REMAP = {
    '${KICAD9_3DMODEL_DIR}/RF_GPS.3dshapes/Quectel_L76.step': 'Quectel_LC76G.step',
    '${KICAD9_3DMODEL_DIR}/Connector_USB.3dshapes/USB_C_Receptacle_HRO_TYPE-C-31-M-12.step': 'USB_C_Receptacle_HRO_TYPE-C-31-M-12.step',
    '${KICAD9_3DMODEL_DIR}/Inductor_SMD.3dshapes/L_Taiyo-Yuden_NR-40xx.step': 'L_Taiyo-Yuden_NR-40xx.step',
    '${KICAD9_3DMODEL_DIR}/Package_DFN_QFN.3dshapes/QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm.step': 'QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm.step',
    '${KICAD9_3DMODEL_DIR}/Package_DFN_QFN.3dshapes/DFN-10-1EP_2.6x2.6mm_P0.5mm_EP1.3x2.2mm.step': 'DFN-10-1EP_2.6x2.6mm_P0.5mm_EP1.3x2.2mm.step'}
_txt = '\n'.join(L) + '\n'
for _a, _b in MODEL_REMAP.items():
    _txt = _txt.replace(_a, '${KIPRJMOD}/lib/ecran_rond.3dshapes/' + _b)
open(os.path.join(OUT, d['project'] + '.kicad_pcb'), 'w', encoding='utf-8').write(_txt)

# récapitulatif placement
with open(os.path.join(OUT, 'placement.csv'), 'w', encoding='utf-8') as f:
    f.write('ref;x_mm;y_mm;rot;face\n')
    for ref, (x, y, rot, b, side) in placed.items():
        if not ref.startswith('_'): f.write(f'{ref};{x};{y};{rot};{side}\n')
print('PCB écrit,', len(placed), 'empreintes')
