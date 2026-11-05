# A BVH reader (CMU's, as converted to BVH): joint world positions per frame, in Unity's frame (the left side at -x,
# facing +z, y up; the BVH's x mirrored).
import numpy as np

def euler(order, angles):
    m = np.eye(3)
    for axis, a in zip(order, angles):
        a = np.radians(a); c, s = np.cos(a), np.sin(a)
        r = {'X': np.array([[1, 0, 0], [0, c, -s], [0, s, c]]),
             'Y': np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]]),
             'Z': np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])}[axis]
        m = m @ r
    return m

class BVH:
    def __init__(self, path):
        lines = open(path).read().split('\n')
        self.names, self.parent, self.offset, self.channels, self.ends = [], [], [], [], {}
        stack = []; i = 0; cur = -1
        while not lines[i].strip().startswith('MOTION'):
            t = lines[i].split()
            if not t: i += 1; continue
            if t[0] in ('ROOT', 'JOINT'):
                self.names.append(t[1]); self.parent.append(stack[-1] if stack else -1)
                self.offset.append(None); self.channels.append([]); cur = len(self.names) - 1
            elif t[0] == 'End':
                cur = ('end', stack[-1])
            elif t[0] == '{':
                stack.append(cur) if not isinstance(cur, tuple) else stack.append(cur)
            elif t[0] == '}':
                stack.pop()
            elif t[0] == 'OFFSET':
                v = np.array([float(x) for x in t[1:4]])
                if isinstance(cur, tuple): self.ends[cur[1]] = v
                else: self.offset[cur] = v
            elif t[0] == 'CHANNELS':
                self.channels[cur] = t[2:]
            i += 1
        # stack pops leave end entries as tuples: fix parents to ints
        self.parent = [p if isinstance(p, int) else -1 for p in self.parent]
        self.frames = int(lines[i + 1].split()[1]); self.dt = float(lines[i + 2].split()[2])
        data = []
        for l in lines[i + 3:]:
            if l.strip(): data.append([float(x) for x in l.split()])
        self.data = np.array(data)
        self.index = {n: k for k, n in enumerate(self.names)}

    def positions(self):
        """(frames, joints, 3) world positions, and the end sites' {joint: (frames, 3)}"""
        F, J = len(self.data), len(self.names)
        P = np.zeros((F, J, 3)); R = np.zeros((F, J, 3, 3)); E = {j: np.zeros((F, 3)) for j in self.ends}
        for f in range(F):
            row = self.data[f]; c = 0
            for j in range(J):
                ch = self.channels[j]; vals = row[c:c + len(ch)]; c += len(ch)
                pos = self.offset[j].copy(); rot_axes, rot_vals = [], []
                for name, v in zip(ch, vals):
                    if name.endswith('position'): pos[{'X': 0, 'Y': 1, 'Z': 2}[name[0]]] = v
                    else: rot_axes.append(name[0]); rot_vals.append(v)
                local = euler(rot_axes, rot_vals)
                p = self.parent[j]
                if p < 0:
                    R[f, j] = local; P[f, j] = pos
                else:
                    R[f, j] = R[f, p] @ local; P[f, j] = P[f, p] + R[f, p] @ self.offset[j]
                if j in self.ends: E[j][f] = P[f, j] + R[f, j] @ self.ends[j]
        mirror = np.array([-1.0, 1.0, 1.0])
        return P * mirror, {self.names[j]: e * mirror for j, e in E.items()}
