"""Mini-générateur de schémas KiCad 9 (format 20250114) à partir d'une description
netlist : chaque composant est placé sur une grille, chaque broche reçoit un bout de fil
et une étiquette globale (ou un symbole d'alimentation pour GND / rails)."""
import uuid, math
import symlib
from sexpdata import Symbol, dumps

GRID = 1.27
STUB = 2.54            # longueur du fil entre la broche et l'étiquette
POWER_NETS = {}        # net -> (lib, symbol)  ex. 'GND': ('power','GND')

def U(): return str(uuid.uuid4())

def snap(v): return round(round(v / GRID) * GRID, 2)

def q(s):
    return '"' + str(s).replace('\\', '\\\\').replace('"', '\\"') + '"'

# ---------------------------------------------------------------- lib_symbols
def symbol_text(lib, name, custom_libs):
    """retourne le texte S-expr du symbole pour lib_symbols, renommé lib:name"""
    if lib in custom_libs:
        sexp = custom_libs[lib][name]
    else:
        sexp = symlib.resolve(lib, name)
    import copy
    s = copy.deepcopy(sexp)
    full = f'{lib}:{name}'
    s[1] = full
    return dumps(s, true_as='yes', false_as='no')

def pins_of(lib, name, custom_libs):
    if lib in custom_libs:
        res = []
        def walk(e):
            if isinstance(e, list):
                if e and e[0] == Symbol('pin'):
                    d = {'type': str(e[1])}
                    for x in e[3:]:
                        if isinstance(x, list):
                            if x[0] == Symbol('at'): d['x'], d['y'], d['angle'] = float(x[1]), float(x[2]), float(x[3])
                            elif x[0] == Symbol('length'): d['length'] = float(x[1])
                            elif x[0] == Symbol('name'): d['name'] = str(x[1])
                            elif x[0] == Symbol('number'): d['number'] = str(x[1])
                    res.append(d)
                else:
                    for x in e: walk(x)
        walk(custom_libs[lib][name])
        return res
    return symlib.pins(lib, name)

# ---------------------------------------------------------------- composant
class Comp:
    def __init__(self, ref, lib_id, value, footprint, nets, desc='', dnp=False, rot=0):
        self.ref, self.lib_id, self.value, self.footprint = ref, lib_id, value, footprint
        self.nets = nets          # {pin_number: net or None(NC)}
        self.desc, self.dnp, self.rot = desc, dnp, rot
        self.uuid = U()
        self.x = self.y = 0.0

    def lib(self): return self.lib_id.split(':')[0]
    def name(self): return self.lib_id.split(':')[1]

# ---------------------------------------------------------------- feuille
class Sheet:
    def __init__(self, name, filename, project, root_uuid, paper='A3'):
        self.name, self.filename, self.project = name, filename, project
        self.uuid = U()
        self.root_uuid = root_uuid
        self.paper = paper
        self.comps = []
        self.items = []       # textes S-expr (wires, labels, power symbols…)
        self.libs_used = {}   # lib_id -> True
        self.texts = []
        self.power_count = 0

    def add(self, c):
        self.comps.append(c)
        return c

    def text(self, s, x, y, size=2.54):
        self.texts.append(f'(text {q(s)} (exclude_from_sim no) (at {x} {y} 0) (effects (font (size {size} {size}) (bold yes)) (justify left bottom)) (uuid "{U()}"))')

    # --- primitives
    def wire(self, x1, y1, x2, y2):
        self.items.append(f'(wire (pts (xy {x1} {y1}) (xy {x2} {y2})) (stroke (width 0) (type default)) (uuid "{U()}"))')

    def glabel(self, net, x, y, angle):
        just = 'left' if angle in (0, 90) else 'right'
        self.items.append(
            f'(global_label {q(net)} (shape input) (at {x} {y} {angle}) (fields_autoplaced yes) '
            f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{U()}") '
            f'(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))))')

    def no_connect(self, x, y):
        self.items.append(f'(no_connect (at {x} {y}) (uuid "{U()}"))')

    def power_symbol(self, net, x, y, custom_libs):
        lib, name = POWER_NETS[net]
        self.libs_used[f'{lib}:{name}'] = True
        self.power_count += 1
        ref = f'#PWR{self.power_count:03d}'
        rot = 0
        if name == 'GND':
            vx, vy = x, y + 2.54  # value en dessous
        else:
            vx, vy = x, y - 3.81
        c = Comp(ref, f'{lib}:{name}', net, '', {})
        self.items.append(
            f'(symbol (lib_id {q(lib+":"+name)}) (at {x} {y} {rot}) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{c.uuid}") '
            f'(property "Reference" {q(ref)} (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(property "Value" {q(net)} (at {vx} {vy} 0) (effects (font (size 1.27 1.27)))) '
            f'(property "Footprint" "" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(property "Datasheet" "" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(property "Description" "" (at {x} {y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(pin "1" (uuid "{U()}")) '
            f'(instances (project {q(self.project)} (path "/{self.root_uuid}/{self.uuid}" (reference {q(ref)}) (unit 1)))))')

    # --- placement + câblage d'un composant
    def emit_comp(self, c, custom_libs):
        pins = pins_of(c.lib(), c.name(), custom_libs)
        self.libs_used[c.lib_id] = True
        X, Y = c.x, c.y
        seen_pos = set()
        for p in pins:
            # position du point de connexion (coord. schéma, Y vers le bas)
            px, py = snap(X + p['x']), snap(Y - p['y'])
            a = p['angle'] % 360
            # direction "vers l'extérieur" (opposée au corps)
            d = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[int(a)]
            net = c.nets.get(p['number'], '__MISSING__')
            if net == '__MISSING__':
                raise KeyError(f'{c.ref}: broche {p["number"]} ({p["name"]}) sans net')
            if (px, py) in seen_pos:
                continue                      # broches empilées (même point)
            seen_pos.add((px, py))
            if net is None:
                self.no_connect(px, py)
                continue
            ex, ey = snap(px + d[0] * STUB), snap(py + d[1] * STUB)
            self.wire(px, py, ex, ey)
            if net in POWER_NETS:
                lib, name = POWER_NETS[net]
                down = (name == 'GND')
                want = (0, 1) if down else (0, -1)
                if d == want:
                    self.power_symbol(net, ex, ey, custom_libs)
                else:
                    # mauvaise orientation : étiquette globale (même net que le symbole d'alim)
                    ang = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[d]
                    self.glabel(net, ex, ey, ang)
            else:
                ang = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[d]
                self.glabel(net, ex, ey, ang)
        # symbole lui-même
        vx, vy = X, Y
        bbox = self.bbox(pins)
        rx, ry = X + bbox[2] + 1.27, Y - bbox[3] - 1.27
        self.items.append(
            f'(symbol (lib_id {q(c.lib_id)}) (at {X} {Y} {c.rot}) (unit 1) (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp {"yes" if c.dnp else "no"}) (uuid "{c.uuid}") '
            f'(property "Reference" {q(c.ref)} (at {rx} {ry} 0) (effects (font (size 1.27 1.27)) (justify left))) '
            f'(property "Value" {q(c.value)} (at {rx} {ry + 2.54} 0) (effects (font (size 1.27 1.27)) (justify left))) '
            f'(property "Footprint" {q(c.footprint)} (at {X} {Y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(property "Datasheet" "" (at {X} {Y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            f'(property "Description" {q(c.desc)} (at {X} {Y} 0) (effects (font (size 1.27 1.27)) (hide yes))) '
            + ' '.join(f'(pin {q(p["number"])} (uuid "{U()}"))' for p in pins) +
            f' (instances (project {q(self.project)} (path "/{self.root_uuid}/{self.uuid}" (reference {q(c.ref)}) (unit 1)))))')

    @staticmethod
    def bbox(pins):
        xs = [p['x'] for p in pins] or [0]; ys = [p['y'] for p in pins] or [0]
        return (min(xs), min(ys), max(xs), max(ys))   # coord. lib (Y vers le haut)

    # --- placement automatique en rangées
    def autoplace(self, custom_libs, x0=20.0, y0=30.0, xmax=400.0, margin_x=24.0, margin_y=12.0):
        x, y, rowh = x0, y0, 0.0
        for c in self.comps:
            pins = pins_of(c.lib(), c.name(), custom_libs)
            if not pins:
                w, h = 10, 10; cx, cy = 0, 0
            else:
                b = self.bbox(pins)
                w = (b[2] - b[0]) + 2 * margin_x
                h = (b[3] - b[1]) + 2 * margin_y
                cx, cy = -b[0] + margin_x, b[3] + margin_y   # décalage origine dans la cellule
            if x + w > xmax:
                x = x0; y += rowh; rowh = 0
            c.x, c.y = snap(x + cx), snap(y + cy)
            x += w
            rowh = max(rowh, h)
        return y + rowh

    def write(self, path, custom_libs, title):
        libs = ''.join(symbol_text(*k.split(':'), custom_libs) for k in sorted(self.libs_used))
        body = '\n'.join(self.items + self.texts)
        txt = (f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid "{self.uuid}") (paper "{self.paper}")\n'
               f'(title_block (title {q(title)}) (company "Blue Smart Robotics") (comment 1 "Écran rond 1,75\\" KHEPRI — d\'après Waveshare ESP32-S3-Touch-AMOLED-1.75-G"))\n'
               f'(lib_symbols {libs})\n{body}\n)\n')
        open(path, 'w', encoding='utf-8').write(txt)


def write_root(path, project, root_uuid, sheets, custom_libs, title):
    items = []
    x, y = 30.0, 30.0
    for i, sh in enumerate(sheets):
        items.append(
            f'(sheet (at {x} {y}) (size 50.8 20.32) (fields_autoplaced yes) (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0)) (uuid "{sh.uuid}") '
            f'(property "Sheetname" {q(sh.name)} (at {x} {y - 0.7} 0) (effects (font (size 1.27 1.27)) (justify left bottom))) '
            f'(property "Sheetfile" {q(sh.filename)} (at {x} {y + 20.32 + 0.6} 0) (effects (font (size 1.27 1.27)) (justify left top))) '
            f'(instances (project {q(project)} (path "/{root_uuid}" (page "{i + 2}")))))')
        x += 70
        if x > 250: x = 30.0; y += 40
    txt = (f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "9.0") (uuid "{root_uuid}") (paper "A4")\n'
           f'(title_block (title {q(title)}) (company "Blue Smart Robotics"))\n'
           f'(lib_symbols)\n' + '\n'.join(items) +
           f'\n(text "Écran rond 1,75\\" KHEPRI : ESP32-S3R8 + AMOLED CO5300 466x466, sans PMU/batterie, sans micros, sans IMU.\\nAlimentation 5 V par JST PH (ou USB-C), liaison UART vers la carte façade sur le même connecteur." (exclude_from_sim no) (at 30 120 0) (effects (font (size 2 2)) (justify left bottom)) (uuid "{U()}"))'
           f'\n(sheet_instances (path "/" (page "1")))\n)\n')
    open(path, 'w', encoding='utf-8').write(txt)
