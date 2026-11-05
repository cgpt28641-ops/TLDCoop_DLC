# The body pack's rig (tools/mocap/rig.json, dumped from tldcoop_bodies.bundle's Body_* prefabs): forward kinematics.
import json, numpy as np

def qmul(a, b):
    x1, y1, z1, w1 = a; x2, y2, z2, w2 = b
    return np.array([w1*x2 + x1*w2 + y1*z2 - z1*y2, w1*y2 - x1*z2 + y1*w2 + z1*x2,
                     w1*z2 + x1*y2 - y1*x2 + z1*w2, w1*w2 - x1*x2 - y1*y2 - z1*z2])
def qconj(q): return np.array([-q[0], -q[1], -q[2], q[3]])
def qrot(q, v):
    p = np.array([v[0], v[1], v[2], 0.0])
    return qmul(qmul(q, p), qconj(q))[:3]
def qnorm(q): return q / np.linalg.norm(q)
def from_to(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    c = np.cross(a, b); d = np.dot(a, b)
    if d < -0.999999:
        ax = np.cross(a, [1, 0, 0])
        if np.linalg.norm(ax) < 1e-6: ax = np.cross(a, [0, 1, 0])
        ax /= np.linalg.norm(ax); return np.array([ax[0], ax[1], ax[2], 0.0])
    return qnorm(np.array([c[0], c[1], c[2], 1 + d]))

class Rig:
    def __init__(self, bones):
        self.bones = bones
        self.index = {b['name']: i for i, b in enumerate(bones)}
        self.parent = [self.index.get(b['parent'], -1) if b['parent'] else -1 for b in bones]
        self.rest_rot = [np.array(b['rot'], float) for b in bones]
        self.rest_pos = [np.array(b['pos'], float) * np.array(b.get('scale', [1, 1, 1])) for b in bones]
    def fk(self, local_rot=None, root_pos=None):
        n = len(self.bones); W = [None]*n; P = [None]*n
        for i in range(n):
            lr = local_rot.get(self.bones[i]['name'], self.rest_rot[i]) if local_rot else self.rest_rot[i]
            lp = self.rest_pos[i]
            if root_pos is not None and self.bones[i]['name'] == 'Hips_jnt': lp = root_pos
            p = self.parent[i]
            if p < 0: W[i] = lr; P[i] = lp
            else: W[i] = qnorm(qmul(W[p], lr)); P[i] = P[p] + qrot(W[p], lp)
        return W, P

def load(path, body):
    return Rig(json.load(open(path))[body])
