import sexpdata, re, sys
from sexpdata import Symbol
LIBDIR='/mnt/user-data/uploads/symbols'
_cache={}
def load(lib):
    if lib not in _cache:
        with open(f'{LIBDIR}/{lib}.kicad_sym',encoding='utf-8') as f:
            _cache[lib]=sexpdata.loads(f.read())
    return _cache[lib]
def find(lib,name):
    for e in load(lib):
        if isinstance(e,list) and len(e)>1 and e[0]==Symbol('symbol') and e[1]==name:
            return e
    raise KeyError(f'{lib}:{name}')
def resolve(lib,name):
    """return flattened symbol sexp (handles extends)"""
    s=find(lib,name)
    ext=[e for e in s if isinstance(e,list) and e[0]==Symbol('extends')]
    if not ext: return s
    parent=resolve(lib,ext[0][1])
    import copy
    p=copy.deepcopy(parent)
    # replace name and properties
    p[1]=name
    props={e[1]:e for e in s if isinstance(e,list) and e[0]==Symbol('property')}
    out=[]
    for e in p:
        if isinstance(e,list) and e[0]==Symbol('property') and e[1] in props:
            out.append(props[e[1]])
        else: out.append(e)
    # rename sub-units parent_0_1 -> name_0_1
    for e in out:
        if isinstance(e,list) and e[0]==Symbol('symbol'):
            e[1]=re.sub(r'^'+re.escape(parent[1]),name,e[1])
    return out
def pins(lib,name):
    s=resolve(lib,name)
    res=[]
    def walk(e):
        if isinstance(e,list):
            if e and e[0]==Symbol('pin'):
                d={'type':str(e[1]),'style':str(e[2])}
                for x in e[3:]:
                    if isinstance(x,list):
                        if x[0]==Symbol('at'): d['x'],d['y'],d['angle']=float(x[1]),float(x[2]),float(x[3])
                        elif x[0]==Symbol('length'): d['length']=float(x[1])
                        elif x[0]==Symbol('name'): d['name']=str(x[1])
                        elif x[0]==Symbol('number'): d['number']=str(x[1])
                res.append(d)
            else:
                for x in e: walk(x)
    walk(s)
    return res
if __name__=='__main__':
    lib,name=sys.argv[1].split(':')
    for p in pins(lib,name): print(p['number'],p['name'],p['type'],p['x'],p['y'],p['angle'])
