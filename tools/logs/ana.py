import json,re,collections,os,sys
logs=json.load(open('/root/logs/uniq_logs.json'))
norm=lambda s: re.sub(r'([0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12})|[0-9a-f]{8}|#[0-9]+|-?[0-9]+(\.[0-9]+)?','N',s)
rows=[]
glob=collections.Counter(); globex={}
for p in logs:
    L=open(p,'rb').read().decode('utf-8','replace').splitlines()
    if not L: continue
    build=next((re.search(r'loader (.*?), proxy',l).group(1) for l in L if 'TLDCoop loader' in l),'?')
    name=next((re.search(r'name=(\S+)',l).group(1) for l in L if 'settings: mode=' in l),'?')
    mode=next((re.search(r'mode=(\S+)',l).group(1) for l in L if 'settings: mode=' in l),'?')
    last=L[-1][:15]
    c=collections.Counter()
    for l in L:
        if '[ERROR]' in l or '[WARN]' in l or 'threw' in l:
            k=norm(re.sub(r'^\[[^\]]*\] ','',l))[:170]
            c[k]+=1; glob[k]+=1; globex.setdefault(k,(p,l[:300]))
    crash=[l for l in L if re.search(r'EXCEPTION_|stack overflow|access violation|unhandled|crash',l,re.I)][:3]
    slow=[int(m.group(1)) for l in L for m in [re.search(r'frame: (\d+) ms since',l)] if m]
    ended_ok=any('MainMenu' in l for l in L[-80:]) or any('quit' in l.lower() for l in L[-20:])
    rows.append((L[0][1:13],last,build,name,mode,len(L),sum(c.values()),len(slow),max(slow) if slow else 0,crash,os.path.relpath(p,'/root/logs/all2')))
rows.sort()
for r in rows:
    print(f"{r[0]}-{r[1]} build={r[2]} {r[3]} {r[4]} lines={r[5]} err/warn={r[6]} slow={r[7]} max={r[8]}ms  {r[10]}")
    for x in r[9]: print('   CRASH?',x[:200])
print('\n==== all warnings/errors (count, kind) ====')
for k,v in glob.most_common(80): print(v,k)
