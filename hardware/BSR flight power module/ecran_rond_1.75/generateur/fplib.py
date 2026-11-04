"""Lecture des empreintes KiCad (.kicad_mod) : emprise de courtyard, pads, et ré-émission
dans un .kicad_pcb avec position / rotation / nets."""
import re, uuid, math, os
import sexpdata
from sexpdata import Symbol

FPDIR = '/mnt/user-data/uploads/footprints'
_cache = {}

def load(lib_id):
    if lib_id not in _cache:
        lib, name = lib_id.split(':')
        p = f'{FPDIR}/{lib}.pretty/{name}.kicad_mod'
        if not os.path.exists(p): p = f'/home/claude/work/out/ecran_rond_1.75/lib/{lib}.pretty/{name}.kicad_mod'
        with open(p, encoding='utf-8') as f:
            _cache[lib_id] = sexpdata.loads(f.read())
    return _cache[lib_id]

def _is(e, tag): return isinstance(e, list) and e and e[0] == Symbol(tag)

def courtyard(lib_id):
    """bbox (xmin, ymin, xmax, ymax) du F.CrtYd, sinon de tous les éléments graphiques + pads"""
    s = load(lib_id)
    xs, ys = [], []
    def coords(e, only_crtyd):
        if not isinstance(e, list): return
        if e and e[0] in (Symbol('fp_line'), Symbol('fp_rect'), Symbol('fp_poly'), Symbol('fp_circle'), Symbol('fp_arc')):
            layer = [x[1] for x in e if _is(x, 'layer')]
            if only_crtyd and (not layer or layer[0] != 'F.CrtYd'): return
            if not only_crtyd and layer and layer[0] in ('F.Fab', 'F.CrtYd', 'F.SilkS', 'B.CrtYd', 'B.Fab', 'B.SilkS'):
                pass
            if e[0] == Symbol('fp_circle'):
                c = [x for x in e if _is(x, 'center')][0]; en = [x for x in e if _is(x, 'end')][0]
                r = math.hypot(float(en[1]) - float(c[1]), float(en[2]) - float(c[2]))
                xs.extend([float(c[1]) - r, float(c[1]) + r]); ys.extend([float(c[2]) - r, float(c[2]) + r])
                return
            for x in e:
                if _is(x, 'start') or _is(x, 'end') or _is(x, 'center'):
                    xs.append(float(x[1])); ys.append(float(x[2]))
                if _is(x, 'pts'):
                    for p in x[1:]:
                        if _is(p, 'xy'): xs.append(float(p[1])); ys.append(float(p[2]))
            return
        if e and e[0] == Symbol('pad') and not only_crtyd:
            at = [x for x in e if _is(x, 'at')][0]; size = [x for x in e if _is(x, 'size')][0]
            xs.extend([float(at[1]) - float(size[1]) / 2, float(at[1]) + float(size[1]) / 2])
            ys.extend([float(at[2]) - float(size[2]) / 2, float(at[2]) + float(size[2]) / 2])
            return
        for x in e: coords(x, only_crtyd)
    coords(s, True)
    if not xs: coords(s, False)
    return (min(xs), min(ys), max(xs), max(ys))

def pads(lib_id):
    s = load(lib_id)
    res = []
    for e in s:
        if _is(e, 'pad'):
            at = [x for x in e if _is(x, 'at')][0]
            res.append((str(e[1]), float(at[1]), float(at[2])))
    return res

def _rot_angle(at, rot):
    a = float(at[3]) if len(at) > 3 else 0.0
    return (a + rot) % 360

def emit(lib_id, ref, value, x, y, rot, netmap, path, sheetname, sheetfile, desc=''):
    """netmap : {pad_number: (net_index, net_name)} ; retourne le texte (footprint ...)"""
    import copy
    s = copy.deepcopy(load(lib_id))
    out = [Symbol('footprint'), lib_id]
    out.append([Symbol('layer'), 'F.Cu'])
    out.append([Symbol('uuid'), str(uuid.uuid4())])
    out.append([Symbol('at'), x, y, rot] if rot else [Symbol('at'), x, y])
    for e in s[2:]:
        if _is(e, 'version') or _is(e, 'generator') or _is(e, 'generator_version') or _is(e, 'layer'):
            continue
        if _is(e, 'property'):
            name = e[1]
            if name == 'Reference': e[2] = ref
            elif name == 'Value': e[2] = value
            elif name == 'Footprint': e[2] = lib_id
            elif name == 'Description' and desc: e[2] = desc
            for x in e:
                if _is(x, 'at') and rot:
                    while len(x) < 4: x.append(0)
                    x[3] = _rot_angle(x, rot)
            if not any(_is(x, 'uuid') for x in e): e.append([Symbol('uuid'), str(uuid.uuid4())])
            out.append(e); continue
        if _is(e, 'pad'):
            num = str(e[1])
            for x in e:
                if _is(x, 'at') and rot:
                    while len(x) < 4: x.append(0)
                    x[3] = _rot_angle(x, rot)
            if num in netmap and netmap[num] is not None:
                idx, nm = netmap[num]
                # insérer (net ...) avant uuid
                e.append([Symbol('net'), idx, nm])
            if not any(_is(x, 'uuid') for x in e): e.append([Symbol('uuid'), str(uuid.uuid4())])
            out.append(e); continue
        if _is(e, 'fp_text'):
            for x in e:
                if _is(x, 'at') and rot:
                    while len(x) < 4: x.append(0)
                    x[3] = _rot_angle(x, rot)
            if not any(_is(x, 'uuid') for x in e): e.append([Symbol('uuid'), str(uuid.uuid4())])
            out.append(e); continue
        if e and e[0] in (Symbol('fp_line'), Symbol('fp_rect'), Symbol('fp_poly'), Symbol('fp_circle'), Symbol('fp_arc')):
            if not any(_is(x, 'uuid') for x in e): e.append([Symbol('uuid'), str(uuid.uuid4())])
            out.append(e); continue
        if _is(e, 'attr') or _is(e, 'descr') or _is(e, 'tags') or _is(e, 'model') or _is(e, 'solder_mask_margin') or _is(e, 'solder_paste_margin') or _is(e, 'solder_paste_margin_ratio') or _is(e, 'clearance') or _is(e, 'zone_connect') or _is(e, 'thermal_width') or _is(e, 'thermal_gap') or _is(e, 'embedded_fonts') or _is(e, 'tstamp'):
            if _is(e, 'tstamp'): continue
            out.append(e); continue
        out.append(e)
    # path / sheet
    out.insert(5, [Symbol('path'), path])
    out.insert(6, [Symbol('sheetname'), sheetname])
    out.insert(7, [Symbol('sheetfile'), sheetfile])
    return sexpdata.dumps(out, true_as='yes', false_as='no')
