import struct
COMMON=b"AABB\0AnimationClip\0AnimationCurve\0AnimationState\0Array\0Base\0BitField\0bitset\0bool\0char\0ColorRGBA\0Component\0data\0deque\0double\0dynamic_array\0FastPropertyName\0first\0float\0Font\0GameObject\0Generic Mono\0GradientNEW\0GUID\0GUIStyle\0int\0list\0long long\0map\0Matrix4x4f\0MdFour\0MonoBehaviour\0MonoScript\0m_ByteSize\0m_Curve\0m_EditorClassIdentifier\0m_EditorHideFlags\0m_Enabled\0m_ExtensionPtr\0m_GameObject\0m_Index\0m_IsArray\0m_IsStatic\0m_MetaFlag\0m_Name\0m_ObjectHideFlags\0m_PrefabInternal\0m_PrefabParentObject\0m_Script\0m_StaticEditorFlags\0m_Type\0m_Version\0Object\0pair\0PPtr<Component>\0PPtr<GameObject>\0PPtr<Material>\0PPtr<MonoBehaviour>\0PPtr<MonoScript>\0PPtr<Object>\0PPtr<Prefab>\0PPtr<Sprite>\0PPtr<TextAsset>\0PPtr<Texture>\0PPtr<Texture2D>\0PPtr<Transform>\0Prefab\0Quaternionf\0Rectf\0RectInt\0RectOffset\0second\0set\0short\0size\0SInt16\0SInt32\0SInt64\0SInt8\0staticvector\0string\0TextAsset\0TextMesh\0Texture\0Texture2D\0Transform\0TypelessData\0UInt16\0UInt32\0UInt64\0UInt8\0unsigned int\0unsigned long long\0unsigned short\0vector\0Vector2f\0Vector3f\0Vector4f\0m_ScriptingClassIdentifier\0Gradient\0Type*\0int2_storage\0int3_storage\0BoundsInt\0m_CorrespondingSourceObject\0m_PrefabInstance\0m_PrefabAsset\0FileSize\0Hash128\0RenderingLayerMask\0"
def cstr(buf,o):
    e=buf.index(b'\0',o); return buf[o:e].decode('latin1')
class Node:
    __slots__=('type','name','size','level','flags','meta','children')
class SF:
    def __init__(s,b):
        s.b=b
        ver=struct.unpack('>I',b[8:12])[0]; s.ver=ver
        assert ver>=22, ver
        s.endian=b[16]
        msize,fsize,doff=struct.unpack('>Iqq',b[20:40]); s.doff=doff
        o=48
        s.E='<' if s.endian==0 else '>'
        s.uv=cstr(b,o); o+=len(s.uv)+1
        s.platform,=struct.unpack(s.E+'i',b[o:o+4]); o+=4
        tt=b[o]; o+=1
        n,=struct.unpack(s.E+'i',b[o:o+4]); o+=4
        s.types=[]
        for i in range(n):
            cid,=struct.unpack(s.E+'i',b[o:o+4]); o+=4
            o+=1; o+=2
            if cid==114: o+=16
            o+=16
            root=None
            if tt:
                nc,sb=struct.unpack(s.E+'ii',b[o:o+8]); o+=8
                raw=b[o:o+nc*32]; o+=nc*32
                strs=b[o:o+sb]; o+=sb
                nodes=[]
                for k in range(nc):
                    v,lvl,fl,to,no,bs,idx,mf,h=struct.unpack(s.E+'HBBIIiiiQ',raw[k*32:k*32+32])
                    nd=Node(); nd.level=lvl; nd.flags=fl; nd.size=bs; nd.meta=mf; nd.children=[]
                    nd.type=cstr(COMMON,to&0x7fffffff) if to&0x80000000 else cstr(strs,to)
                    nd.name=cstr(COMMON,no&0x7fffffff) if no&0x80000000 else cstr(strs,no)
                    nodes.append(nd)
                st=[]
                for nd in nodes:
                    while st and st[-1].level>=nd.level: st.pop()
                    if st: st[-1].children.append(nd)
                    st.append(nd)
                root=nodes[0]
                dn,=struct.unpack(s.E+'i',b[o:o+4]); o+=4+4*dn
            s.types.append((cid,root))
        on,=struct.unpack(s.E+'i',b[o:o+4]); o+=4
        s.objects=[]
        for i in range(on):
            o=(o+3)//4*4
            pid,start,size,ti=struct.unpack(s.E+'qqIi',b[o:o+24]); o+=24
            s.objects.append((pid,start+doff,size,s.types[ti][0],s.types[ti][1]))
    def read(s,obj):
        pid,start,size,cid,root=obj
        s.p=start
        return s._node(root)
    def _node(s,nd):
        b=s.b; E=s.E
        if nd.flags&1:  # array
            n,=struct.unpack(E+'i',b[s.p:s.p+4]); s.p+=4
            el=nd.children[1]
            if el.size>0 and not el.children:
                fmt={1:'B',2:'H',4:'I',8:'Q'}
                t=el.type
                if t in('float',): f='f'
                elif t=='double': f='d'
                elif t in ('SInt32','int'): f='i'
                elif t in('SInt16','short'): f='h'
                elif t in('SInt8',): f='b'
                elif t in('SInt64','long long'): f='q'
                else: f=fmt[el.size]
                if el.size==1 and f=='B': v=bytes(b[s.p:s.p+n])
                else: v=list(struct.unpack(E+f*n,b[s.p:s.p+n*el.size]))
                s.p+=n*el.size
            else:
                v=[s._node(el) for _ in range(n)]
            r=v
        elif nd.type=='string':
            n,=struct.unpack(E+'i',b[s.p:s.p+4]); s.p+=4
            r=bytes(b[s.p:s.p+n]).decode('utf8','replace'); s.p+=n
            s.p=(s.p+3)//4*4
            return r
        elif nd.type=='TypelessData':
            n,=struct.unpack(E+'i',b[s.p:s.p+4]); s.p+=4
            r=bytes(b[s.p:s.p+n]); s.p+=n
        elif not nd.children:
            t=nd.type; sz=nd.size; raw=b[s.p:s.p+sz]; s.p+=sz
            if t in('float',): r=struct.unpack(E+'f',raw)[0]
            elif t=='double': r=struct.unpack(E+'d',raw)[0]
            elif t=='bool': r=raw[0]!=0
            elif sz==1: r=raw[0] if t!='SInt8' else struct.unpack('b',raw)[0]
            elif sz==2: r=struct.unpack(E+('h' if t in('SInt16','short') else 'H'),raw)[0]
            elif sz==4: r=struct.unpack(E+('i' if t in('int','SInt32') else 'I'),raw)[0]
            elif sz==8: r=struct.unpack(E+('q' if t in('long long','SInt64') else 'Q'),raw)[0]
            else: r=raw
        else:
            r={}
            for c in nd.children: r[c.name]=s._node(c)
        if nd.meta&0x4000: s.p=(s.p+3)//4*4
        return r
