import json,glob,os,re,sys
D=sys.argv[1]
MCU=re.compile(r'RP2350|RP2040|PGA2350|Pico 1/2|ESP32-D0WD')
sel=sys.argv[2] if len(sys.argv)>2 else None
for p in sorted(glob.glob(D+'/*.json')):
    d=json.load(open(p))
    if sel and d['board']!=sel: continue
    comps=d['components']; pm=d['pinmap']
    # net -> [(ref,pinfunc)]
    net2nodes={}
    for ref,pins in pm.items():
        for pf,nets in pins.items():
            for n in nets: net2nodes.setdefault(n,[]).append((ref,pf))
    print("="*78); print(d['board'].upper())
    for ref,c in comps.items():
        if not MCU.search(c['value'] or ''): continue
        gp={}
        for pf,nets in pm.get(ref,{}).items():
            m=re.match(r'^(?:GPIO|GP|IO)(\d+)',pf)
            if m: gp[int(m.group(1))]=nets[0]
        print(f"\n--- {ref} {c['value']}  ({c['sheet']})")
        for g in sorted(gp):
            n=gp[g]
            if n.startswith('unconnected-'): continue
            others=[(r,f) for (r,f) in net2nodes.get(n,[]) if r!=ref]
            desc=", ".join(f"{r}[{comps.get(r,{}).get('value','?')}].{f}" for r,f in others[:6])
            nn=n.replace('{slash}','/')
            print(f"  GPIO{g:<3d} {nn:<34s} -> {desc}")
