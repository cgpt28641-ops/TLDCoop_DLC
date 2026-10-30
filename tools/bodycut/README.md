# bodycut

Cuts TLDCoop's story bodies into regions (head, neck, torso, arms, hands, hips, legs, feet) from the bones that carry
each triangle, so worn clothes can hide exactly what they cover (clothespack, `body_meshes` in clothes.json).

    gcc -O2 -o lz4d lz4d.c
    python3 unfs.py <TLDCoop>/tldcoop_bodies.bundle out            # the bundle's serialized file
    python3 cut.py out/CAB-<hash> cut Mackenzie_noJacket_LO,Astrid_Ep5,MESH_MOLLY

The .bodycut files hold Hinterland's meshes: keep them on your own PC (clothespack's `bodycut` folder), never publish them.
