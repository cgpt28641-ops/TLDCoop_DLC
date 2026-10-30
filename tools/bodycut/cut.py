# Extracts body meshes from tldcoop_bodies (its unpacked serialized file) and splits every submesh by body region,
# from the bones that carry each triangle. Output per mesh: <name>.bodycut (see write()).
import struct,sys,re,json,os
import numpy as np
from sf import SF
REGIONS=['head','neck','torso','arms','hands','hips','legs','feet']+['band%d'%k for k in range(15)]+['eyes']
# The torso's lower part in 3 cm bands from BAND_TOP down, so a coat hides only what its hem reaches.
BAND_TOP=1.25; BAND_STEP=0.03
def region_of(b):
    if b in('Origin_jnt','Hips_jnt'): return 'hips'
    if b.startswith('Spine') or 'Shoulder' in b: return 'torso'
    if b.startswith('Neck'): return 'neck'
    if 'eye' in b.lower(): return 'eyes'  # the eyes and their lids: what a balaclava leaves showing
    if b=='Head_jnt' or not b[0].isupper(): return 'head'
    if re.search('UpArm|Bicep|Elbow|Forearm',b): return 'arms'
    if re.search('Hand|Pinky|Ring|Middle|Index|Thumb',b): return 'hands'
    if re.search('Foot|Toe',b): return 'feet'
    if re.search('Hip|Thigh|Knee|Shin',b): return 'legs'
    return 'torso'
FMT={0:('f',4),1:('e',2),2:('B',1),3:('b',1),4:('H',2),5:('h',2),6:('B',1),7:('b',1),8:('H',2),9:('h',2),10:('I',4),11:('i',4)}
def vertex_arrays(vd):
    ch=vd['m_Channels']['Array']; n=vd['m_VertexCount']; data=vd['m_DataSize']
    streams={}
    for c in ch:
        if c['dimension']==0: continue
        sz=FMT[c['format']][1]*(c['dimension']&0xf)
        st=streams.setdefault(c['stream'],0); streams[c['stream']]=max(st,c['offset']+sz)
    off=0; base={}
    for sidx in sorted(streams):
        base[sidx]=off; off+=streams[sidx]*n; off=(off+15)//16*16
    out={}
    for i,c in enumerate(ch):
        if c['dimension']==0: continue
        f,sz=FMT[c['format']]; dim=c['dimension']&0xf; stride=streams[c['stream']]
        dt=np.dtype('<'+f)
        a=np.ndarray((n,dim),dtype=dt,buffer=data,offset=base[c['stream']]+c['offset'],strides=(stride,sz)).astype(np.float64 if f in 'fe' else np.int64)
        if c['format'] in(2,4): a=a/ (255 if c['format']==2 else 65535)
        out[i]=a
    return out
def main(cab,outdir,names):
    s=SF(open(cab,'rb').read()); byid={o[0]:o for o in s.objects}; cache={}
    def R(pid):
        if pid not in cache: cache[pid]=s.read(byid[pid])
        return cache[pid]
    os.makedirs(outdir,exist_ok=True)
    for o in s.objects:
        if o[3]!=137: continue
        d=s.read(o); mesh=R(d['m_Mesh']['m_PathID'])
        if mesh['m_Name'] not in names: continue
        bones=[R(R(b['m_PathID'])['m_GameObject']['m_PathID'])['m_Name'] for b in d['m_Bones']['Array']]
        va=vertex_arrays(mesh['m_VertexData'])
        P,N,T,UV,W,J=va[0],va[1],va[2],va[4],va[12],va[13]
        ib=mesh['m_IndexBuffer']; ib=ib['Array'] if isinstance(ib,dict) else ib
        idx=np.frombuffer(ib,dtype='<u4' if mesh['m_IndexFormat']==1 else '<u2').astype(np.int64)
        breg=np.array([REGIONS.index(region_of(b)) for b in bones])
        bpm=mesh['m_BindPose']['Array'][bones.index('Neck_jnt')]
        M=np.array([[bpm['e%d%d'%(i,j)] for j in range(4)] for i in range(4)])
        neck_y=np.linalg.inv(M)[1,3]
        # each vertex's region: the region carrying most of its weight
        vr=np.zeros((len(P),len(REGIONS)))
        for k in range(4): np.add.at(vr,(np.arange(len(P)),breg[J[:,k]]),W[:,k])
        vreg=vr.argmax(1)
        mats=[R(m['m_PathID'])['m_Name'] for m in d['m_Materials']['Array']]
        subs=[]
        isz=4 if mesh['m_IndexFormat']==1 else 2
        for sm in mesh['m_SubMeshes']['Array']:
            first=sm['firstByte']//isz; tri=idx[first:first+sm['indexCount']].reshape(-1,3)+sm['baseVertex']
            r=vreg[tri]  # a triangle's region: its vertices' majority (ties: the lowest index, the most central)
            treg=np.array([np.bincount(x,minlength=len(REGIONS)).argmax() for x in r])
            cy=P[tri][:,:,1].mean(1)
            # (the neck region is what the Neck bone carries: the skin there and a collar round it)
            # the torso, hips and legs between BAND_TOP and the lowest band: what a coat's hem may or may not reach
            low=np.isin(treg,[2,5,6])&(cy<BAND_TOP)&(cy>=BAND_TOP-15*BAND_STEP)
            treg[low]=8+np.clip(((BAND_TOP-cy[low])/BAND_STEP).astype(int),0,14)
            subs.append((tri,treg))
        bp=np.array([[m['e%d%d'%(i,j)] for i in range(4) for j in range(4)] for m in mesh['m_BindPose']['Array']])
        write(os.path.join(outdir,mesh['m_Name']+'.bodycut'),P,N,T,UV,W,J,subs,bp,mats)
        counts={REGIONS[i]:int(sum((t[1]==i).sum() for t in subs)) for i in range(len(REGIONS))}
        print(mesh['m_Name'],len(P),'verts',len(subs),'subs',counts,'mats',mats)
        np.savez(os.path.join(outdir,mesh['m_Name']+'.npz'),P=P,vreg=vreg,tris=np.vstack([t[0] for t in subs]),treg=np.concatenate([t[1] for t in subs]))
def write(path,P,N,T,UV,W,J,subs,bp,mats):
    # little endian: "TLDCUT1\0", int vertexCount, int submeshCount, int boneCount, int materialCount,
    # then per vertex: pos3 normal3 tangent4 uv2 weight4 (floats) + bone4 (ints), bindposes (16 floats row major each),
    # per submesh: int triangleCount, then per triangle 3 ints + 1 int region; materials as int length + utf8.
    n=len(P)
    with open(path,'wb') as f:
        f.write(b'TLDCUT1\0'); f.write(struct.pack('<4i',n,len(subs),len(bp),len(mats)))
        V=np.hstack([P,N,T,UV[:,:2],W]).astype('<f4'); f.write(V.tobytes()); f.write(J.astype('<i4').tobytes())
        f.write(bp.astype('<f4').tobytes())
        for tri,reg in subs:
            f.write(struct.pack('<i',len(tri))); f.write(np.hstack([tri,reg[:,None]]).astype('<i4').tobytes())
        for m in mats:
            e=m.encode(); f.write(struct.pack('<i',len(e))); f.write(e)
main(sys.argv[1],sys.argv[2],sys.argv[3].split(','))
