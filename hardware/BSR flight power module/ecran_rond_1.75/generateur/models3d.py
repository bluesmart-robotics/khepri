"""Modèles 3D STEP simplifiés (cotes constructeur) pour les 5 empreintes sans modèle dans la
bibliothèque KiCad 9. Repère KiCad : x à droite, y vers le HAUT (inverse de l'empreinte), z vers le haut."""
import cadquery as cq, os, sys

OUT = sys.argv[1] if len(sys.argv) > 1 else 'ecran_rond.3dshapes'
os.makedirs(OUT, exist_ok=True)
NOIR, METAL, OR, GRIS, BEIGE = (cq.Color(0.15, 0.15, 0.15), cq.Color(0.82, 0.82, 0.85), cq.Color(0.85, 0.7, 0.3),
                                cq.Color(0.55, 0.55, 0.58), cq.Color(0.9, 0.9, 0.85))

def save(asm, name):
    asm.save(os.path.join(OUT, name + '.step'))
    print('écrit', name)

def box(x, y, z, cx=0, cy=0, z0=0):
    return cq.Workplane('XY').box(x, y, z, centered=(True, True, False)).translate((cx, cy, z0))

def qfn(name, bx, by, h, pins, ep, pin1):
    """pins : liste (x, y, lx, ly) en repère empreinte ; converties en repère 3D (y inversé)"""
    a = cq.Assembly(name=name)
    a.add(box(bx, by, h, z0=0.02), name='corps', color=NOIR)
    for i, (x, y, lx, ly) in enumerate(pins):
        a.add(box(lx, ly, 0.2, x, -y, 0), name=f'p{i}', color=OR)
    if ep: a.add(box(ep[0], ep[1], 0.05, 0, 0, 0), name='ep', color=OR)
    px, py = pin1
    a.add(cq.Workplane('XY').circle(0.15).extrude(0.01).translate((px, -py, h + 0.02)), name='repere', color=BEIGE)
    return a

# U4 : QFN-20 3x3 mm, pas 0,4, hauteur 0,75 (ES8311)
pins = []
for i in range(5):
    v = -0.8 + 0.4 * i
    pins += [(-1.3, v, 0.4, 0.2), (v, 1.3, 0.2, 0.4), (1.3, -v, 0.4, 0.2), (-v, -1.3, 0.2, 0.4)]
save(qfn('QFN-20_3x3', 3.0, 3.0, 0.75, pins, (1.65, 1.65), (-1.1, -1.1)), 'QFN-20-1EP_3x3mm_P0.4mm_EP1.65x1.65mm')

# U7 : DFN-10 2,6x2,6 mm, pas 0,5, hauteur 0,75 (PCF85063ATL)
pins = []
for i in range(5):
    v = -1.0 + 0.5 * i
    pins += [(-1.1, v, 0.4, 0.25), (1.1, v, 0.4, 0.25)]
save(qfn('DFN-10_2.6x2.6', 2.6, 2.6, 0.75, pins, (1.3, 2.2), (-0.95, -1.0)), 'DFN-10-1EP_2.6x2.6mm_P0.5mm_EP1.3x2.2mm')

# L1 : Taiyo-Yuden NR40xx, 4,0 x 4,0 x 1,8 mm
a = cq.Assembly(name='NR4018')
a.add(box(4.0, 4.0, 1.8).edges('|Z').fillet(0.3), name='ferrite', color=GRIS)
a.add(cq.Workplane('XY').circle(1.6).extrude(0.02).translate((0, 0, 1.8)), name='bobine', color=cq.Color(0.6, 0.35, 0.15))
for s in (-1, 1):
    a.add(box(0.9, 3.4, 0.12, s * 1.55, 0, 0), name=f'b{s}', color=OR)
save(a, 'L_Taiyo-Yuden_NR-40xx')

# U8 : Quectel LC76G, 10,1 x 9,7 x 2,4 mm (capot métallique, demi-trous)
a = cq.Assembly(name='LC76G')
a.add(box(10.1, 9.7, 0.8), name='pcb', color=cq.Color(0.1, 0.35, 0.15))
a.add(box(9.5, 9.1, 1.6, z0=0.8), name='blindage', color=METAL)
for i in range(9):
    y = -4.4 + 1.1 * i
    for s in (-1, 1):
        a.add(box(0.6, 0.7, 0.81, s * 4.75, y, 0), name=f'c{s}{i}', color=OR)
a.add(cq.Workplane('XY').circle(0.3).extrude(0.01).translate((-4.0, 3.9, 2.4)), name='repere', color=NOIR)
save(a, 'Quectel_LC76G')

# J1 : HRO TYPE-C-31-M-12, 8,94 x 7,3 x 3,16 mm, ouverture vers +y empreinte (−y 3D)
body = box(8.94, 7.3, 3.16, 0, -0.5, 0).edges('|Y').fillet(1.2)
slot = cq.Workplane('XZ').center(0, 1.58).slot2D(8.3, 2.5).extrude(6.3).translate((0, 2.0, 0))  # XZ : normale -y, ouverture côté -y 3D
shell = body.cut(slot)
a = cq.Assembly(name='USB_C_HRO')
a.add(shell, name='coque', color=METAL)
a.add(box(6.6, 5.5, 0.7, 0, -1.2, 1.23), name='langue', color=NOIR)
for x in (-3.25, -2.45, -1.75, -1.25, -0.75, -0.25, 0.25, 0.75, 1.25, 1.75, 2.45, 3.25):
    a.add(box(0.25 if abs(x) < 2 else 0.5, 1.0, 0.15, x, 4.1, 0), name=f'b{x}', color=OR)
for x, y, ly in ((-4.32, 3.13, 1.7), (4.32, 3.13, 1.7), (-4.32, -1.05, 1.2), (4.32, -1.05, 1.2)):
    a.add(box(0.6, ly, 1.8, x, y, -1.6), name=f'pt{x}{y}', color=METAL)
save(a, 'USB_C_Receptacle_HRO_TYPE-C-31-M-12')
