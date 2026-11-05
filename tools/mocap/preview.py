# Stick-figure previews of a retargeted clip on the rig: a strip of frames, side and front.
import sys, numpy as np
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
LINES = [('Hips_jnt', 'Spine1_jnt'), ('Spine1_jnt', 'Spine2_jnt'), ('Spine2_jnt', 'Spine3_jnt'), ('Spine3_jnt', 'Neck_jnt'),
         ('Neck_jnt', 'Head_jnt'), ('Spine3_jnt', 'LeftShoulder_jnt'), ('LeftShoulder_jnt', 'LeftUpArm_jnt'),
         ('LeftUpArm_jnt', 'LeftElbow_jnt'), ('LeftElbow_jnt', 'LeftHand_jnt'), ('LeftHand_jnt', 'LeftMiddle1_jnt'),
         ('Spine3_jnt', 'RightShoulder_jnt'), ('RightShoulder_jnt', 'RightUpArm_jnt'), ('RightUpArm_jnt', 'RightElbow_jnt'),
         ('RightElbow_jnt', 'RightHand_jnt'), ('RightHand_jnt', 'RightMiddle1_jnt'), ('Hips_jnt', 'LeftHip_jnt'),
         ('LeftHip_jnt', 'LeftKnee_jnt'), ('LeftKnee_jnt', 'LeftFoot_jnt'), ('LeftFoot_jnt', 'LeftToe_jnt'),
         ('Hips_jnt', 'RightHip_jnt'), ('RightHip_jnt', 'RightKnee_jnt'), ('RightKnee_jnt', 'RightFoot_jnt'),
         ('RightFoot_jnt', 'RightToe_jnt')]

def strip(rig, frames, path, n=8, title=''):
    sel = np.linspace(0, len(frames) - 1, n).astype(int)
    fig, axes = plt.subplots(2, n, figsize=(2 * n, 5))
    for k, fi in enumerate(sel):
        local, hp = frames[fi]
        W, P = rig.fk(local, root_pos=hp)
        for row, (a, b) in enumerate([(2, 1), (0, 1)]):  # side (z,y), front (x,y)
            ax = axes[row, k]
            for u, v in LINES:
                p, q = P[rig.index[u]], P[rig.index[v]]
                col = 'r' if u.startswith('Left') or v.startswith('Left') else ('b' if 'Right' in u + v else 'k')
                ax.plot([p[a], q[a]], [p[b], q[b]], col, lw=1.5)
            ax.set_xlim(-0.9, 0.9); ax.set_ylim(-0.05, 1.9); ax.set_aspect('equal'); ax.set_xticks([]); ax.set_yticks([])
            ax.axhline(0, color='g', lw=0.5)
            if row == 0: ax.set_title(f'{fi}', fontsize=7)
    fig.suptitle(title + '   (top: side, +z right; bottom: front, red = left)')
    plt.tight_layout(); plt.savefig(path, dpi=60); plt.close()
