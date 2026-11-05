import json,struct,sys,io,numpy as np
from PIL import Image
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
b=open(sys.argv[1],'rb').read(); off=12; ch=[]
while off<len(b):
    l,t=struct.unpack('<I4s',b[off:off+8]); ch.append(b[off+8:off+8+l]); off+=8+l
j=json.loads(ch[0]); bin_=ch[1]
CT={5126:('f',4),5123:('H',2),5125:('I',4),5121:('B',1)}; NC={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}
def acc(i):
    a=j['accessors'][i]; bv=j['bufferViews'][a['bufferView']]; f,s=CT[a['componentType']]; n=NC[a['type']]
    st=bv.get('byteStride',s*n); start=bv.get('byteOffset',0)+a.get('byteOffset',0)
    dt=np.dtype('<'+f)
    arr=np.ndarray((a['count'],n),dtype=dt,buffer=bin_,offset=start,strides=(st,s)).astype(np.float64)
    if a.get('normalized') or a['componentType'] in (5121,5123) and 'COLOR' in str(i): pass
    return arr
imgs=[]
for im in j.get('images',[]):
    bv=j['bufferViews'][im['bufferView']]; o=bv.get('byteOffset',0)
    imgs.append(np.asarray(Image.open(io.BytesIO(bin_[o:o+bv['byteLength']])).convert('RGB')).astype(float)/255)
tris=[];cols=[]
for n in j['nodes']:
    if 'mesh' not in n: continue
    M=np.array(n.get('matrix',np.eye(4).flatten().tolist())).reshape(4,4).T
    for p in j['meshes'][n['mesh']]['primitives']:
        P=acc(p['attributes']['POSITION']); P=(np.c_[P,np.ones(len(P))]@M.T)[:,:3]
        idx=acc(p['indices']).astype(int).reshape(-1,3)
        if 'COLOR_0' in p['attributes']:
            C=acc(p['attributes']['COLOR_0'])[:,:3]
            if C.max()>1.5: C=C/(255 if C.max()<=255 else 65535)
            fc=C[idx].mean(1)
        elif 'TEXCOORD_0' in p['attributes'] and 'material' in p:
            mat=j['materials'][p['material']]; ti=mat['pbrMetallicRoughness']['baseColorTexture']['index']; img=imgs[j['textures'][ti]['source']]
            UV=acc(p['attributes']['TEXCOORD_0'])[idx].mean(1); h,w=img.shape[:2]
            fc=img[np.clip((UV[:,1]%1)*h,0,h-1).astype(int),np.clip((UV[:,0]%1)*w,0,w-1).astype(int)]
        else:
            mat=j['materials'][p['material']]; fc=np.tile(np.array(mat.get('emissiveFactor',[0.8,0.8,0.8])),(len(idx),1))
        tris.append(P[idx]); cols.append(fc)
T=np.concatenate(tris); C=np.clip(np.concatenate(cols),0,1)
fig=plt.figure(figsize=(12,6))
for k,(el,az) in enumerate([(10,-90),(20,-55),(25,-125),(15,90)]):
    ax=fig.add_subplot(1,4,k+1,projection='3d')
    # matplotlib z-up: map (x,y,z)->(x,-z,y)
    V=T[:,:,[0,2,1]].copy(); V[:,:,1]*=-1
    pc=Poly3DCollection(V,facecolors=C,edgecolors='none'); ax.add_collection3d(pc)
    ax.set_xlim(-0.3,0.3);ax.set_ylim(-0.3,0.3);ax.set_zlim(0,0.7); ax.view_init(el,az); ax.set_box_aspect((0.6,0.6,0.7)); ax.axis('off')
plt.tight_layout(); plt.savefig(sys.argv[2],dpi=110)
for i,im in enumerate(imgs): Image.fromarray((im*255).astype(np.uint8)).save(sys.argv[2].replace('.png',f'_tex{i}.png'))
