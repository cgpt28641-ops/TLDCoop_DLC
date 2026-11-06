# Gait cycles from CMU walks and runs: one full stride (left heel strike to the next), found in a window of the take
# where the actor moves steadily; its speed (m/s on the rig), and the seam closed so it loops without a jump.
import numpy as np
from rig import qmul, qconj, qnorm
from retarget import retarget, smooth

def leg_scale(rig, bvh, P):
    W0, P0 = rig.fk()
    rleg = np.linalg.norm(P0[rig.index['LeftHip_jnt']] - P0[rig.index['LeftKnee_jnt']]) + \
           np.linalg.norm(P0[rig.index['LeftKnee_jnt']] - P0[rig.index['LeftFoot_jnt']])
    sleg = np.linalg.norm(P[0, bvh.index['LeftUpLeg']] - P[0, bvh.index['LeftLeg']]) + \
           np.linalg.norm(P[0, bvh.index['LeftLeg']] - P[0, bvh.index['LeftFoot']])
    return rleg / sleg

def heel_strikes(bvh, P, t0, t1):
    """Times in [t0, t1] where the left foot is furthest ahead of the hips along the travel (a heel strike)."""
    H = P[:, bvh.index['Hips']]
    trend = smooth(H[:, [0, 2]], int(0.5 / bvh.dt))
    vel = np.gradient(trend, axis=0)
    fw = vel / np.maximum(np.linalg.norm(vel, axis=1, keepdims=True), 1e-9)
    L = P[:, bvh.index['LeftFoot']][:, [0, 2]] - H[:, [0, 2]]
    f = np.sum(L * fw, axis=1)
    f = smooth(f, max(1, int(0.05 / bvh.dt)))
    a, b = int(t0 / bvh.dt), min(int(t1 / bvh.dt), len(P) - 2)
    return [i * bvh.dt for i in range(a + 1, b) if f[i] >= f[i - 1] and f[i] > f[i + 1]], H, trend

def pose_gap(bvh, P, i, j):
    """How different two frames' poses are (joints relative to the hips)."""
    A = P[i] - P[i, bvh.index['Hips']]
    B = P[j] - P[j, bvh.index['Hips']]
    return float(np.mean(np.linalg.norm(A - B, axis=1)))

def cycle(rig, bvh, t0, t1, fps=30, heading='smooth'):
    """The best full stride in [t0, t1]: retargeted frames (looping), its speed (m/s), its start and length (s)."""
    P, _ = bvh.positions()
    strikes, H, trend = heel_strikes(bvh, P, t0, t1)
    best = None
    for k in range(len(strikes) - 1):
        a, b = strikes[k], strikes[k + 1]
        if b - a < 0.4 or b - a > 2.0: continue
        gap = pose_gap(bvh, P, int(a / bvh.dt), int(b / bvh.dt))
        if best is None or gap < best[0]: best = (gap, a, b)
    if best is None: raise RuntimeError('no stride found in %.1f-%.1f s' % (t0, t1))
    gap, a, b = best
    n = max(8, int(round((b - a) * fps)))
    times = a + (b - a) * np.arange(n + 1) / n  # n + 1: the last is the next cycle's first (for the seam)
    frames = retarget(rig, bvh, a, b, fps=fps, heading=heading, travel='sway', keep_y=True, times=times)
    scale = leg_scale(rig, bvh, P)
    ia, ib = int(a / bvh.dt), int(b / bvh.dt)
    speed = np.linalg.norm(trend[ib] - trend[ia]) / (b - a) * scale
    return close_seam(frames), float(speed), a, b - a, gap * scale

def close_seam(frames):
    """n + 1 frames whose last should equal the first: the difference spread over the cycle, the last dropped."""
    n = len(frames) - 1
    first, last = frames[0], frames[n]
    out = []
    for i in range(n):
        w = i / n
        local, hips = frames[i]
        fixed = {}
        for name, q in local.items():
            qa, qb = first[0][name], last[0][name]
            if np.dot(qa, qb) < 0: qb = -qb
            d = qmul(qa, qconj(qb))  # turns the last onto the first
            if d[3] < 0: d = -d
            ang = 2 * np.arccos(min(1.0, d[3]))
            if ang < 1e-6: fixed[name] = q; continue
            axis = d[:3] / np.sin(ang / 2)
            part = np.concatenate([axis * np.sin(ang * w / 2), [np.cos(ang * w / 2)]])
            fixed[name] = qnorm(qmul(part, q))
        out.append((fixed, hips + (first[1] - last[1]) * w))
    return out
