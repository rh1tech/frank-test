import re, sys, os, glob, json

def sexp(s):
    toks = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', s)
    def parse(i):
        out=[]
        while i < len(toks):
            t=toks[i]
            if t=='(':
                sub,i=parse(i+1); out.append(sub)
            elif t==')':
                return out,i+1
            else:
                out.append(t[1:-1] if t.startswith('"') else t); i+=1
        return out,i
    r,_=parse(0)
    return r[0]

def get(node,key):
    for c in node:
        if isinstance(c,list) and c and c[0]==key: return c
    return None
def getall(node,key):
    return [c for c in node if isinstance(c,list) and c and c[0]==key]
def val(node,key,default=None):
    c=get(node,key)
    return c[1] if c and len(c)>1 else default

for path in sorted(glob.glob(sys.argv[1]+'/*.net')):
    board=os.path.basename(path)[:-4]
    root=sexp(open(path).read())
    comps={}
    cs=get(root,'components')
    for c in getall(cs,'comp'):
        ref=val(c,'ref'); comps[ref]={'value':val(c,'value'),'fp':val(c,'footprint'),
            'sheet':val(get(c,'sheetpath') or ['sheetpath'],'names','')}
    # pin map
    pinmap={}   # ref -> {pinfunction: net}
    nets=get(root,'nets')
    for n in getall(nets,'net'):
        name=val(n,'name')
        for nd in getall(n,'node'):
            r=val(nd,'ref'); pf=val(nd,'pinfunction') or ('pin'+val(nd,'pin'))
            pinmap.setdefault(r,{}).setdefault(pf,[]).append(name)
    out={'board':board,'components':comps,'pinmap':pinmap}
    json.dump(out,open(path[:-4]+'.json','w'))
    # print summary of ICs
    print(f"===== {board} =====")
    for ref,c in sorted(comps.items(), key=lambda kv:(kv[1]['value'] or '')):
        v=c['value'] or ''
        if re.match(r'^(R|C|L|D|Q|F|FB|TP|H|J|S|SW|LD|Y|MH)\d', ref): continue
        print(f"  {ref:6s} {v:28s} {c['sheet']}")
