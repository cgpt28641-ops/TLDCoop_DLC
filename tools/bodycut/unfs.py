import struct,sys,os,subprocess,tempfile
def rstr(b,o):
    e=b.index(b'\0',o); return b[o:e].decode(),e+1
def lz4(data,usize):
    d=tempfile.mkdtemp(); a=os.path.join(d,'i'); b=os.path.join(d,'o')
    open(a,'wb').write(data); subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)),'lz4d'),a,b,str(usize)],check=True)
    return open(b,'rb').read()
def unpack(path,outdir):
    f=open(path,'rb'); b=f.read()
    o=0; sig,o=rstr(b,o); ver=struct.unpack('>I',b[o:o+4])[0]; o+=4
    uv,o=rstr(b,o); rev,o=rstr(b,o)
    size,csz,usz,flags=struct.unpack('>qIII',b[o:o+20]); o+=20
    print(sig,ver,uv,rev,size,csz,usz,hex(flags))
    if ver>=7: o=(o+15)//16*16
    if flags&0x80: info=b[len(b)-csz:]; 
    else: info=b[o:o+csz]; o+=csz
    comp=flags&0x3f
    if comp in(2,3): info=lz4(info,usz)
    elif comp!=0: raise Exception('comp %d'%comp)
    p=16; n=struct.unpack('>i',info[p:p+4])[0]; p+=4
    blocks=[]
    for i in range(n):
        u,c,fl=struct.unpack('>IIH',info[p:p+10]); p+=10; blocks.append((u,c,fl))
    nn=struct.unpack('>i',info[p:p+4])[0]; p+=4; nodes=[]
    for i in range(nn):
        off,sz,fl=struct.unpack('>qqI',info[p:p+20]); p+=20; name,p=rstr(info,p); nodes.append((off,sz,fl,name))
    if flags&0x200: o=(o+15)//16*16
    out=bytearray()
    # decompress blocks, batch lz4 via single calls
    for u,c,fl in blocks:
        data=b[o:o+c]; o+=c
        cm=fl&0x3f
        if cm==0: out+=data
        elif cm in(2,3): out+=lz4(data,u)
        else: raise Exception('block comp %d'%cm)
    os.makedirs(outdir,exist_ok=True)
    for off,sz,fl,name in nodes:
        open(os.path.join(outdir,name.replace('/','_')),'wb').write(out[off:off+sz]); print('node',name,sz)
unpack(sys.argv[1],sys.argv[2])
