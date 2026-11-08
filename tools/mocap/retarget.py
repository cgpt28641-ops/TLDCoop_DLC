# Retargets a CMU BVH clip onto the body pack's rig (rig.py): each bone turned so it points where the actor's does
# (swing from its rest, the least twist), the hips turned as the actor's (from the hips' left-right and up), heading
# taken out (the avatar's root turns it), horizontal travel taken out (the network moves it), the hips' height as the
# actor's, scaled by leg length. Output: local rotations of the posed bones, and the hips' local position, per frame.
import numpy as np
from rig import qmul, qconj, qrot, qnorm, from_to

# target bone: (source joint, source joint it points to); the target child it points to in the rig
MAP = [
    ('Spine1_jnt', 'LowerBack', 'Spine', 'Spine2_jnt'),
    ('Spine2_jnt', 'Spine', 'Spine1', 'Spine3_jnt'),
    ('Spine3_jnt', 'Spine1', 'Neck', 'Neck_jnt'),
    ('Neck_jnt', 'Neck', 'Head', 'Head_jnt'),
    ('LeftShoulder_jnt', 'LeftShoulder', 'LeftArm', 'LeftUpArm_jnt'),
    ('LeftUpArm_jnt', 'LeftArm', 'LeftForeArm', 'LeftElbow_jnt'),
    ('LeftElbow_jnt', 'LeftForeArm', 'LeftHand', 'LeftHand_jnt'),
    ('LeftHand_jnt', 'LeftHand', 'LeftHandIndex1', 'LeftMiddle1_jnt'),
    ('RightShoulder_jnt', 'RightShoulder', 'RightArm', 'RightUpArm_jnt'),
    ('RightUpArm_jnt', 'RightArm', 'RightForeArm', 'RightElbow_jnt'),
    ('RightElbow_jnt', 'RightForeArm', 'RightHand', 'RightHand_jnt'),
    ('RightHand_jnt', 'RightHand', 'RightHandIndex1', 'RightMiddle1_jnt'),
    ('LeftHip_jnt', 'LeftUpLeg', 'LeftLeg', 'LeftKnee_jnt'),
    ('LeftKnee_jnt', 'LeftLeg', 'LeftFoot', 'LeftFoot_jnt'),
    ('LeftFoot_jnt', 'LeftFoot', 'LeftToeBase', 'LeftToe_jnt'),
    ('RightHip_jnt', 'RightUpLeg', 'RightLeg', 'RightKnee_jnt'),
    ('RightKnee_jnt', 'RightLeg', 'RightFoot', 'RightFoot_jnt'),
    ('RightFoot_jnt', 'RightFoot', 'RightToeBase', 'RightToe_jnt'),
]
POSED = ['Hips_jnt'] + [m[0] for m in MAP]

def frame_of(left, up):
    x = left / np.linalg.norm(left)
    y = up - x * np.dot(up, x); y /= np.linalg.norm(y)
    z = np.cross(x, y)
    return np.stack([x, y, z], 1)  # columns

def mat_to_quat(m):
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1) * 2; w = 0.25 * s
        x = (m[2, 1] - m[1, 2]) / s; y = (m[0, 2] - m[2, 0]) / s; z = (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w = (m[2, 1] - m[1, 2]) / s; x = 0.25 * s; y = (m[0, 1] + m[1, 0]) / s; z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w = (m[0, 2] - m[2, 0]) / s; x = (m[0, 1] + m[1, 0]) / s; y = 0.25 * s; z = (m[1, 2] + m[2, 1]) / s
    else:
        s = np.sqrt(1 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w = (m[1, 0] - m[0, 1]) / s; x = (m[0, 2] + m[2, 0]) / s; y = (m[1, 2] + m[2, 1]) / s; z = 0.25 * s
    return qnorm(np.array([x, y, z, w]))

def yaw_rot(deg):
    a = np.radians(deg)
    return np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])

def smooth(a, n):
    if n <= 1: return a
    k = np.ones(n) / n
    pad = np.pad(a, ((n // 2, n - 1 - n // 2),) + ((0, 0),) * (a.ndim - 1), mode='edge')
    return np.stack([np.convolve(pad[:, i], k, mode='valid') for i in range(a.shape[1])], 1) if a.ndim > 1 else np.convolve(pad, k, mode='valid')

def retarget(rig, bvh, start, end, fps=30, heading='clip', travel='remove', keep_y=True, times=None):
    P, E = bvh.positions()
    src = lambda name: E[name] if name in ('LeftHandIndex1', 'RightHandIndex1') and name in E else P[:, bvh.index[name]]
    if times is None: times = np.arange(start, end, 1.0 / fps)
    idx = [int(round(t / bvh.dt)) for t in times]
    idx = [min(i, len(P) - 1) for i in idx]
    # the actor's leg length (hip to foot at the first frame of the file, ~T pose) against the rig's
    W0, P0 = rig.fk()
    rleg = np.linalg.norm(P0[rig.index['LeftHip_jnt']] - P0[rig.index['LeftKnee_jnt']]) + \
           np.linalg.norm(P0[rig.index['LeftKnee_jnt']] - P0[rig.index['LeftFoot_jnt']])
    sleg = np.linalg.norm(P[0, bvh.index['LeftUpLeg']] - P[0, bvh.index['LeftLeg']]) + \
           np.linalg.norm(P[0, bvh.index['LeftLeg']] - P[0, bvh.index['LeftFoot']])
    scale = rleg / sleg
    # the floor: the lowest toe or foot after the file's first (synthetic T pose) frames
    body = P[min(30, len(P) - 1):]
    ground = min(body[:, bvh.index[j], 1].min() for j in ('LeftToeBase', 'RightToeBase', 'LeftFoot', 'RightFoot', 'Hips'))
    # heading: the hips' forward (left x up), per frame, smoothed; 'clip': its mean over the clip
    fw = []
    for f in range(len(P)):
        left = P[f, bvh.index['LeftUpLeg']] - P[f, bvh.index['RightUpLeg']]
        up = P[f, bvh.index['Spine1']] - P[f, bvh.index['Hips']]
        F = frame_of(left, up); fw.append(-F[:, 2])  # (left x up points backwards)
    fw = np.array(fw)
    ang = np.degrees(np.arctan2(fw[:, 0], fw[:, 2]))
    ang = np.unwrap(np.radians(ang)); ang = np.degrees(ang)
    if heading == 'clip': head = np.full(len(P), np.mean(ang[idx]))
    elif heading == 'smooth': head = smooth(ang, int(1.0 / bvh.dt))
    else: head = np.zeros(len(P))
    hips_xz = P[:, bvh.index['Hips']][:, [0, 2]]
    trend = smooth(hips_xz, int(1.0 / bvh.dt)) if travel in ('remove', 'sway') else None
    rest_hips_local = rig.rest_pos[rig.index['Hips_jnt']].copy()
    rest_hips_world_y = P0[rig.index['Hips_jnt']][1]
    hips_parent = rig.parent[rig.index['Hips_jnt']]
    out = []
    for f in idx:
        Rh = yaw_rot(-head[f])
        S = lambda name: Rh @ (src(name)[f] - P[f, bvh.index['Hips']])
        # hips: turned as the actor's
        sF = frame_of(S('LeftUpLeg') - S('RightUpLeg'), S('Spine1') - S('Hips') if False else S('Spine1'))
        tl = P0[rig.index['LeftHip_jnt']] - P0[rig.index['RightHip_jnt']]
        tu = P0[rig.index['Spine2_jnt']] - P0[rig.index['Hips_jnt']]
        tF = frame_of(tl, tu)
        Rw = sF @ tF.T
        hips_world = qmul(mat_to_quat(Rw), W0[rig.index['Hips_jnt']])
        local = {}
        parent_world = W0[hips_parent] if hips_parent >= 0 else np.array([0, 0, 0, 1.0])
        local['Hips_jnt'] = qnorm(qmul(qconj(parent_world), hips_world))
        # the hips' position: the actor's height above the ground, scaled; travel taken out
        hy = (P[f, bvh.index['Hips'], 1] - ground) * scale
        hips_pos = rest_hips_local.copy()
        if keep_y: hips_pos[1] = rest_hips_local[1] + (hy - rest_hips_world_y)
        if travel == 'sway':  # the hips' sway about their path (side to side, back and forth in the stride)
            d = Rh @ np.array([hips_xz[f, 0] - trend[f, 0], 0, hips_xz[f, 1] - trend[f, 1]]) * scale
            hips_pos[0] += d[0]; hips_pos[2] += d[2]
        elif travel != 'remove':
            d = Rh @ np.array([hips_xz[f, 0] - hips_xz[idx[0], 0], 0, hips_xz[f, 1] - hips_xz[idx[0], 1]]) * scale
            hips_pos[0] += d[0]; hips_pos[2] += d[2]
        # the bones, in order (parents first): swing each onto the actor's direction
        W, Pt = rig.fk(local, root_pos=hips_pos)
        for tgt, s0, s1, tchild in MAP:
            d_src = S(s1) - S(s0)
            if np.linalg.norm(d_src) < 1e-6: continue
            ti, ci = rig.index[tgt], rig.index[tchild]
            W, Pt = rig.fk(local, root_pos=hips_pos)
            d_tgt = Pt[ci] - Pt[ti]
            q = from_to(d_tgt, d_src)
            new_world = qnorm(qmul(q, W[ti]))
            pw = W[rig.parent[ti]]
            local[tgt] = qnorm(qmul(qconj(pw), new_world))
        out.append((local, hips_pos))
    return out

# --- TLDCoop_DLC (Shatsca 08.11: knees bending backwards when breaking things down, a leg twisting out at the fire,
# arms tight to the body and short, "like a T-rex", when standing and walking) --------------------------------------
# After the retarget, each arm and leg is set again as a hinge: the upper bone turned so the joint bends the way a
# body's does (knees forward, elbows to the front of the arm), the lower bone turned with it (no twist between them);
# a knee bent backwards is put forwards (hip and foot where they were); the arms held a little out from the body and
# their elbows bent `elbow_scale` as much (the TLD bodies' thick clothes against the actor's slim arms). Hands and feet
# keep their turn in the world.
def _mat_from_frame(d, b):
    d = d / np.linalg.norm(d); b = b - d * np.dot(b, d); b /= np.linalg.norm(b)
    return np.stack([d, b, np.cross(d, b)], 1)

def _ortho(v, d):
    v = v - d * np.dot(v, d); n = np.linalg.norm(v)
    return v / n if n > 1e-6 else None

def _rotate(v, axis, deg):
    axis = axis / np.linalg.norm(axis); a = np.radians(deg)
    return v * np.cos(a) + np.cross(axis, v) * np.sin(a) + axis * np.dot(axis, v) * (1 - np.cos(a))

LIMBS = [  # upper, lower, end, leg?
    ('LeftHip_jnt', 'LeftKnee_jnt', 'LeftFoot_jnt', 'LeftToe_jnt', True),
    ('RightHip_jnt', 'RightKnee_jnt', 'RightFoot_jnt', 'RightToe_jnt', True),
    ('LeftUpArm_jnt', 'LeftElbow_jnt', 'LeftHand_jnt', 'LeftMiddle1_jnt', False),
    ('RightUpArm_jnt', 'RightElbow_jnt', 'RightHand_jnt', 'RightMiddle1_jnt', False),
]

def humanize(rig, frames, abduct_deg=10.0, elbow_scale=1.0):
    I = rig.index
    W0, P0 = rig.fk()
    fwd0 = P0[I['LeftToe_jnt']] - P0[I['LeftFoot_jnt']]; fwd0[1] = 0; fwd0 /= np.linalg.norm(fwd0)
    out = []
    for local, hp in frames:
        local = dict(local)
        W, P = rig.fk(local, root_pos=hp)
        hips_rot = qmul(W[I['Hips_jnt']], qconj(W0[I['Hips_jnt']]))
        fwd = qrot(hips_rot, fwd0)
        chest = P[I['Spine3_jnt']]
        for up, lo, end, tip, leg in LIMBS:
            W, P = rig.fk(local, root_pos=hp)
            end_world = W[I[end]]
            a, k, e = P[I[up]], P[I[lo]], P[I[end]]
            L1, L2 = np.linalg.norm(P0[I[lo]] - P0[I[up]]), np.linalg.norm(P0[I[end]] - P0[I[lo]])
            du, dl = (k - a) / np.linalg.norm(k - a), (e - k) / np.linalg.norm(e - k)
            if leg:
                # the knee to the front of the hip-foot line (a straight or backward knee: bent forward a little)
                he = e - a; dist = np.linalg.norm(he); h = he / dist
                fw = _ortho(fwd, h)
                off = k - (a + h * np.dot(k - a, h))
                if fw is not None and np.dot(off, fw) < 0.01:
                    dist = min(dist, (L1 + L2) * 0.995)
                    x = (L1 * L1 - L2 * L2 + dist * dist) / (2 * dist); y = np.sqrt(max(L1 * L1 - x * x, 0))
                    knee = a + h * x + fw * max(y, 0.02)
                    du = (knee - a) / np.linalg.norm(knee - a); dl = (a + h * dist - knee) / np.linalg.norm(a + h * dist - knee)
                bend_rest = -_ortho(fwd0, (P0[I[lo]] - P0[I[up]]) / L1)
                fallback = -_ortho(fwd, du) if _ortho(fwd, du) is not None else None
            else:
                # out from the body a little (round the body's forward axis, away from the chest)
                outward = _ortho(a - chest, du)
                if outward is not None and abduct_deg:
                    axis = np.cross(du, outward)
                    du = _rotate(du, axis, abduct_deg); dl = _rotate(dl, axis, abduct_deg)
                bend_rest = _ortho(fwd0, (P0[I[lo]] - P0[I[up]]) / L1)
                fallback = _ortho(fwd, du)
            # the bend: where the lower bone leaves the upper one's line
            c = np.clip(np.dot(du, dl), -1, 1); th = np.arccos(c)
            bb = _ortho(dl, du)
            if not leg and elbow_scale != 1.0 and bb is not None:
                th *= elbow_scale; dl = du * np.cos(th) + bb * np.sin(th)
            w = min(1.0, np.sin(th) / 0.3)
            b = fallback if bb is None else (bb if fallback is None else bb * w + fallback * (1 - w))
            if b is None or np.linalg.norm(b) < 1e-3: b = bb if bb is not None else fallback
            if b is None: continue
            b = _ortho(b, du)
            if b is None: continue
            bl = b * np.cos(th) - du * np.sin(th) if bb is not None else _ortho(b, dl)
            for bone, d_new, b_new, child in ((up, du, b, lo), (lo, dl, bl, end)):
                W, P = rig.fk(local, root_pos=hp)
                ti = I[bone]
                d_rest = P0[I[child]] - P0[ti]; d_rest /= np.linalg.norm(d_rest)
                b_rest = _ortho(bend_rest, d_rest)
                R = _mat_from_frame(d_new, b_new) @ _mat_from_frame(d_rest, b_rest).T
                new_world = qnorm(qmul(mat_to_quat(R), W0[ti]))
                local[bone] = qnorm(qmul(qconj(W[rig.parent[ti]]), new_world))
            # the hand or foot keeps its turn in the world
            W, P = rig.fk(local, root_pos=hp)
            local[end] = qnorm(qmul(qconj(W[rig.parent[I[end]]]), end_world))
        out.append((local, hp))
    return out
