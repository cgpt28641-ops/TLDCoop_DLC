// Builds tldcoop_clothes: the clothing a friend wears, fitted to each TLDCoop body, for their avatar (TLDCoop_DLC).
//
//   Unity.exe -batchmode -nographics -quit -projectPath <project> -executeMethod TLDCoop.BuildClothes.Build
//   Unity.exe -batchmode -quit -projectPath <project> -executeMethod TLDCoop.BuildClothes.Preview   (needs graphics)
//
// Inputs (project root): clothes.json (which items), catalog.txt (the folder of the clothing catalog: characters/,
// textures/), bodies.txt (tldcoop_bodies.bundle, for the preview). Output: Build/tldcoop_clothes and previews/*.png.
//
// Each catalog item is a glTF skinned to a TLDCoop body's own rig (right handed, Y up, metres). It becomes, per
// character and item:
//   Assets/Clothes/<character>/<item>.asset      the Mesh (Unity's left handed space: x mirrored, winding flipped;
//                                               bind poses from the glTF's inverse bind matrices, mirrored too)
//   Assets/Clothes/<character>/<item>.txt        TextAsset: "bones" line (the skin's joint names, in the mesh's bone
//                                               order) and "materials" line (material names, one per submesh)
// and the materials (TLDCoop/Body, or Body2Sided for double sided ones) under Assets/Clothes/Materials. The mod puts a
// SkinnedMeshRenderer with the mesh on the body, its bones the body's transforms of those names.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using UnityEditor;
using UnityEngine;

namespace TLDCoop {

// An item the catalog fitted the wrong way round (the balaclava upside down and back to front, the ear warmer standing
// up round the face): turned about the x axis through its middle, scaled, its middle put at Head_jnt + centre (times
// the head's size), and skinned anew: to the head ("head") or the head above the neck ("headneck").
[Serializable] class Reshape { public string item; public float rotate_x; public float scale = 1; public float[] centre; public string weights; }

[Serializable] class HideRule { public string[] under; public string[] words; }

[Serializable] class ClothesList {
    public string[] items;
    public string[] preview_characters;
    public string[] preview_outfits_flat;  // (unused: JsonUtility can't read nested arrays; see ReadOutfits)
    // Fit fixes: an item moved up (+) or down (-) this many metres (hats), or pushed out along its normals (coats
    // over the body's own clothes).
    public string[] lift_items; public float[] lift_metres;
    public string[] inflate_items; public float[] inflate_metres;
    // Coats too long: the hem raised to this far from the knees (0) to the hips (1), what's under the hips squeezed up.
    public string[] shorten_items; public float[] shorten_to;
    // Under the items of a rule a body's own submeshes whose material name has one of its words are not drawn (its
    // jacket under a coat, its hair or own hat under a hat).
    public HideRule[] hide_rules;
    // Only these character folders go in the pack (empty: all of them).
    public string[] characters;
    // Collars that rose up the face: the top brought down to this many metres from Head_jnt (the jaw's height), the
    // front a little lower, and kept off the neck.
    public string[] collar_items; public float[] collar_metres;
    // Items that came in back to front (the balaclava's face on the back of the head): turned half round the head.
    public string[] turn_items;
    // Knitted hats whose catalog model is the folded one lying on the ground: a worn beanie is made instead, fitted to
    // the head, in the item's colour.
    public string[] beanie_items;
    public Reshape[] reshape;
    // A bigger beanie for characters with something of their own on the head (Mackenzie's own hat): this much bigger.
    public string[] beanie_grow_characters; public float[] beanie_grow;
}

// --- the glTF we read (only what the catalog uses) ---------------------------------------------------------------
[Serializable] class GAttributes { public int POSITION = -1, NORMAL = -1, TEXCOORD_0 = -1, JOINTS_0 = -1, WEIGHTS_0 = -1; }
[Serializable] class GPrimitive { public GAttributes attributes; public int indices = -1; public int material = -1; public int mode = 4; }
[Serializable] class GMesh { public string name; public GPrimitive[] primitives; }
[Serializable] class GNode {
    public string name; public int mesh = -1; public int skin = -1; public int[] children;
    public float[] translation; public float[] rotation; public float[] scale; public float[] matrix;
}
[Serializable] class GSkin { public int[] joints; public int inverseBindMatrices = -1; public int skeleton = -1; }
[Serializable] class GAccessor { public int bufferView = -1; public int byteOffset; public int componentType; public int count; public string type; public bool normalized; }
[Serializable] class GBufferView { public int buffer; public int byteOffset; public int byteLength; public int byteStride; }
[Serializable] class GBuffer { public string uri; }
[Serializable] class GTextureRef { public int index = -1; }
[Serializable] class GPbr { public GTextureRef baseColorTexture; public float[] baseColorFactor; }
[Serializable] class GMaterial { public string name; public bool doubleSided; public GPbr pbrMetallicRoughness; public GTextureRef normalTexture; }
[Serializable] class GTexture { public int source = -1; }
[Serializable] class GImage { public string uri; }
[Serializable] class Gltf {
    public GNode[] nodes; public GMesh[] meshes; public GSkin[] skins; public GAccessor[] accessors;
    public GBufferView[] bufferViews; public GBuffer[] buffers; public GMaterial[] materials; public GTexture[] textures;
    public GImage[] images;
}

public static class BuildClothes {
    const string OutDir = "Assets/Clothes";
    const string BundleName = "tldcoop_clothes";
    const string BuildDir = "Build";

    public static void Build() {
        try {
            Run();
        } catch (Exception e) {
            Debug.LogError("[Clothes] FAILED: " + e);
            EditorApplication.Exit(1);
        }
    }

    public static void Preview() {
        try {
            RunPreview();
        } catch (Exception e) {
            Debug.LogError("[Clothes] PREVIEW FAILED: " + e);
            EditorApplication.Exit(1);
        }
    }

    [MenuItem("TLDCoop/Build clothes")] static void BuildMenu() => Run();
    [MenuItem("TLDCoop/Preview clothes")] static void PreviewMenu() => RunPreview();

    static IEnumerable<string> Characters(ClothesList list, string chars) =>
        Directory.GetDirectories(chars).Select(Path.GetFileName)
            .Where(c => list.characters == null || list.characters.Length == 0 || list.characters.Contains(c));

    static string Catalog() => File.ReadAllText("catalog.txt").Trim();

    static void Run() {
        var list = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json"));
        string catalog = Catalog();
        string chars = Path.Combine(catalog, "characters");
        if (!Directory.Exists(chars)) throw new Exception("no characters folder in the catalog: " + chars);
        Directory.CreateDirectory(OutDir);
        Directory.CreateDirectory(OutDir + "/Materials");
        Directory.CreateDirectory(OutDir + "/Textures");
        foreach (string character in Characters(list, chars))
            Directory.CreateDirectory($"{OutDir}/{character}");
        var keep = new HashSet<string>(Characters(list, chars));  // characters left out: out of the pack too
        foreach (string dir in Directory.GetDirectories(OutDir).Select(Path.GetFileName))
            if (dir != "Materials" && dir != "Textures" && !keep.Contains(dir)) AssetDatabase.DeleteAsset($"{OutDir}/{dir}");
        AssetDatabase.Refresh();  // the folders, known to Unity before assets go in them
        var opaque = Shader.Find("TLDCoop/Body");
        var twoSided = Shader.Find("TLDCoop/Body2Sided");
        if (!opaque || !twoSided) throw new Exception("the TLDCoop body shaders didn't compile");

        var hiddenShader = Shader.Find("TLDCoop/Hidden");
        if (!hiddenShader) throw new Exception("the TLDCoop hidden shader didn't compile");
        if (!AssetDatabase.LoadAssetAtPath<Material>(OutDir + "/Materials/TLDCoop_Hidden.mat"))
            AssetDatabase.CreateAsset(new Material(hiddenShader) { name = "TLDCoop_Hidden" }, OutDir + "/Materials/TLDCoop_Hidden.mat");
        var materials = new Dictionary<string, Material>();
        int meshes = 0, skipped = 0;
        long triangles = 0;
        AssetDatabase.StartAssetEditing();
        var pendingTextures = new List<string>();
        try {
            // Textures first (copied in, imported once), then materials and meshes.
            foreach (string character in Characters(list, chars).OrderBy(s => s))
                foreach (string item in list.items) {
                    string file = Path.Combine(chars, character, item + ".gltf");
                    if (!File.Exists(file)) continue;
                    var g = JsonUtility.FromJson<Gltf>(File.ReadAllText(file));
                    if (g.images == null) continue;
                    foreach (var img in g.images) {
                        string src = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(file), Uri.UnescapeDataString(img.uri)));
                        string dst = OutDir + "/Textures/" + Path.GetFileName(src);
                        if (!File.Exists(dst) && File.Exists(src)) {
                            File.Copy(src, dst);
                            pendingTextures.Add(dst);
                        }
                    }
                }
        } finally {
            AssetDatabase.StopAssetEditing();
        }
        AssetDatabase.Refresh();
        foreach (string t in pendingTextures) {  // a sane size for a friend's clothes
            var imp = AssetImporter.GetAtPath(t) as TextureImporter;
            if (imp == null) continue;
            imp.maxTextureSize = 1024;
            imp.textureCompression = TextureImporterCompression.Compressed;
            imp.mipmapEnabled = true;
            imp.SaveAndReimport();
        }

        foreach (string character in Characters(list, chars).OrderBy(s => s)) {
            foreach (string item in list.items) {
                string file = Path.Combine(chars, character, item + ".gltf");
                if (!File.Exists(file)) { skipped++; continue; }
                float lift = 0, inflate = 0;
                if (list.lift_items != null)
                    for (int i = 0; i < list.lift_items.Length && i < list.lift_metres.Length; i++)
                        if (list.lift_items[i] == item) lift = list.lift_metres[i];
                if (list.inflate_items != null)
                    for (int i = 0; i < list.inflate_items.Length && i < list.inflate_metres.Length; i++)
                        if (list.inflate_items[i] == item) inflate = list.inflate_metres[i];
                float shorten = -1;
                if (list.shorten_items != null)
                    for (int i = 0; i < list.shorten_items.Length && i < list.shorten_to.Length; i++)
                        if (list.shorten_items[i] == item) shorten = list.shorten_to[i];
                float collar = float.NaN;
                if (list.collar_items != null)
                    for (int i = 0; i < list.collar_items.Length && i < list.collar_metres.Length; i++)
                        if (list.collar_items[i] == item) collar = list.collar_metres[i];
                bool turn = list.turn_items != null && list.turn_items.Contains(item);
                bool beanie = list.beanie_items != null && list.beanie_items.Contains(item);
                var reshape = list.reshape?.FirstOrDefault(r => r.item == item);
                float grow = 0;
                if (list.beanie_grow_characters != null)
                    for (int i = 0; i < list.beanie_grow_characters.Length && i < list.beanie_grow.Length; i++)
                        if (list.beanie_grow_characters[i] == character) grow = list.beanie_grow[i];
                var result = Convert(file, character, item, materials, opaque, twoSided, lift, inflate, shorten, collar, turn, beanie, reshape, grow);
                triangles += result;
                meshes++;
            }
        }
        // What the mod hides under which items: one TextAsset for the whole pack.
        // One line per rule: "hide <item>,<item>... : <word>,<word>..."
        File.WriteAllText(OutDir + "/rules.txt", string.Concat((list.hide_rules ?? new HideRule[0]).Select(r =>
            "hide " + string.Join(",", r.under ?? new string[0]) + " : " + string.Join(",", r.words ?? new string[0]) + "\n")));
        AssetDatabase.ImportAsset(OutDir + "/rules.txt");
        AssetDatabase.SaveAssets();

        foreach (var other in AssetDatabase.GetAllAssetBundleNames())
            if (other != BundleName) AssetDatabase.RemoveAssetBundleName(other, true);
        AssetImporter.GetAtPath(OutDir).assetBundleName = BundleName;
        AssetDatabase.RemoveUnusedAssetBundleNames();
        if (Directory.Exists(BuildDir)) Directory.Delete(BuildDir, true);
        Directory.CreateDirectory(BuildDir);
        var manifest = BuildPipeline.BuildAssetBundles(BuildDir,
            BuildAssetBundleOptions.ChunkBasedCompression | BuildAssetBundleOptions.StrictMode,
            BuildTarget.StandaloneWindows64);
        if (manifest == null) throw new Exception("BuildAssetBundles returned null");
        var size = new FileInfo(Path.Combine(BuildDir, BundleName)).Length;
        Debug.Log($"[Clothes] built {meshes} meshes ({triangles} triangles, {materials.Count} materials, {skipped} " +
                  $"missing in the catalog), bundle {size / 1048576.0:F1} MB");
    }

    // --- one glTF -> Mesh + TextAsset -----------------------------------------------------------------------------

    static long Convert(string file, string character, string item, Dictionary<string, Material> materials,
                        Shader opaque, Shader twoSided, float lift, float inflate, float shorten, float collar,
                        bool turn, bool beanie, Reshape reshape, float grow) {
        var g = JsonUtility.FromJson<Gltf>(File.ReadAllText(file));
        byte[] bin = File.ReadAllBytes(Path.Combine(Path.GetDirectoryName(file), Uri.UnescapeDataString(g.buffers[0].uri)));
        int meshNode = Array.FindIndex(g.nodes, n => n.mesh >= 0);
        if (meshNode < 0) throw new Exception(file + ": no mesh");
        var node = g.nodes[meshNode];
        var gmesh = g.meshes[node.mesh];
        var skin = node.skin >= 0 ? g.skins[node.skin] : null;
        if (skin == null) throw new Exception(file + ": not skinned");

        var verts = new List<Vector3>();
        var normals = new List<Vector3>();
        var uvs = new List<Vector2>();
        var weights = new List<BoneWeight>();
        var submeshes = new List<int[]>();
        var matNames = new List<string>();
        foreach (var p in gmesh.primitives) {
            int baseIndex = verts.Count;
            Vector3[] P = ReadVec3(g, bin, p.attributes.POSITION);
            Vector3[] N = p.attributes.NORMAL >= 0 ? ReadVec3(g, bin, p.attributes.NORMAL) : null;
            float[] T = p.attributes.TEXCOORD_0 >= 0 ? ReadFloats(g, bin, p.attributes.TEXCOORD_0) : null;
            float[] J = ReadFloats(g, bin, p.attributes.JOINTS_0);
            float[] W = ReadFloats(g, bin, p.attributes.WEIGHTS_0);
            for (int i = 0; i < P.Length; i++) {
                Vector3 nrm = N != null ? new Vector3(-N[i].x, N[i].y, N[i].z) : Vector3.zero;
                verts.Add(new Vector3(-P[i].x, P[i].y + lift, P[i].z) + nrm.normalized * inflate);
                normals.Add(N != null ? new Vector3(-N[i].x, N[i].y, N[i].z) : Vector3.up);
                uvs.Add(T != null ? new Vector2(T[i * 2], 1f - T[i * 2 + 1]) : Vector2.zero);
                var bw = new BoneWeight {
                    boneIndex0 = (int)J[i * 4], boneIndex1 = (int)J[i * 4 + 1], boneIndex2 = (int)J[i * 4 + 2], boneIndex3 = (int)J[i * 4 + 3],
                    weight0 = W[i * 4], weight1 = W[i * 4 + 1], weight2 = W[i * 4 + 2], weight3 = W[i * 4 + 3],
                };
                float sum = bw.weight0 + bw.weight1 + bw.weight2 + bw.weight3;
                if (sum > 1e-6f) { bw.weight0 /= sum; bw.weight1 /= sum; bw.weight2 /= sum; bw.weight3 /= sum; }
                weights.Add(bw);
            }
            float[] I = ReadFloats(g, bin, p.indices);
            var tris = new int[I.Length];
            for (int i = 0; i + 2 < I.Length; i += 3) {  // mirrored: the winding turns
                tris[i] = baseIndex + (int)I[i];
                tris[i + 1] = baseIndex + (int)I[i + 2];
                tris[i + 2] = baseIndex + (int)I[i + 1];
            }
            submeshes.Add(tris);
            matNames.Add(MaterialFor(g, file, p.material, materials, opaque, twoSided));
        }

        if (reshape != null) DoReshape(g, bin, skin, reshape, verts, normals, weights, $"{character}/{item}");
        if (beanie) Beanie(g, bin, skin, file, item, grow, verts, normals, uvs, weights, submeshes, matNames, twoSided);
        if (turn) {  // half round the head's vertical axis
            Vector3 head = JointPos(g, bin, skin, "Head_jnt");
            if (!float.IsNaN(head.x)) {
                for (int i = 0; i < verts.Count; i++) {
                    verts[i] = new Vector3(2 * head.x - verts[i].x, verts[i].y, 2 * head.z - verts[i].z);
                    normals[i] = new Vector3(-normals[i].x, normals[i].y, -normals[i].z);
                }
                Debug.Log($"[Clothes] {character}/{item}: turned round to face forward");
            }
        }
        if (shorten >= 0) Shorten(g, bin, skin, verts, shorten, $"{character}/{item}");
        if (!float.IsNaN(collar)) Collar(g, bin, skin, verts, collar, $"{character}/{item}");

        var mesh = new Mesh { name = $"{character}_{item}" };
        if (verts.Count > 65535) mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
        mesh.SetVertices(verts);
        mesh.SetNormals(normals);
        mesh.SetUVs(0, uvs);
        mesh.boneWeights = weights.ToArray();
        mesh.subMeshCount = submeshes.Count;
        for (int s = 0; s < submeshes.Count; s++) mesh.SetTriangles(submeshes[s], s);
        // Bind poses: glTF's inverse bind matrices, mirrored as the vertices are (S * M * S, S = diag(-1, 1, 1, 1)).
        float[] ibm = ReadFloats(g, bin, skin.inverseBindMatrices);
        var bind = new Matrix4x4[skin.joints.Length];
        for (int j = 0; j < bind.Length; j++) {
            var m = new Matrix4x4();
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++) m[r, c] = ibm[j * 16 + c * 4 + r];  // glTF: column major
            bind[j] = Mirror(m);
        }
        mesh.bindposes = bind;
        mesh.RecalculateTangents();
        mesh.RecalculateBounds();
        AssetDatabase.CreateAsset(mesh, $"{OutDir}/{character}/{item}.asset");

        string bones = string.Join(",", skin.joints.Select(j => g.nodes[j].name));
        File.WriteAllText($"{OutDir}/{character}/{item}.txt", "bones " + bones + "\nmaterials " + string.Join(",", matNames) + "\n");
        AssetDatabase.ImportAsset($"{OutDir}/{character}/{item}.txt");
        long triangles = 0;
        foreach (var s in submeshes) triangles += s.Length / 3;
        return triangles;
    }

    // The hem up to `to` (0 the knees, 1 the hips): everything under the hips squeezed towards them. The joints' heights
    // come from the skin's inverse bind matrices.
    static void Shorten(Gltf g, byte[] bin, GSkin skin, List<Vector3> verts, float to, string what) {
        float[] ibm = ReadFloats(g, bin, skin.inverseBindMatrices);
        float JointY(string name) {
            int j = Array.FindIndex(skin.joints, k => g.nodes[k].name == name);
            if (j < 0) return float.NaN;
            var m = new Matrix4x4();
            for (int c = 0; c < 4; c++)
                for (int r = 0; r < 4; r++) m[r, c] = ibm[j * 16 + c * 4 + r];
            return m.inverse.GetColumn(3).y;
        }
        float hips = JointY("Hips_jnt"), knees = JointY("LeftKnee_jnt");
        if (float.IsNaN(hips) || float.IsNaN(knees) || hips <= knees) return;
        float bottom = verts.Min(v => v.y), target = knees + Mathf.Clamp01(to) * (hips - knees);
        if (bottom >= target) return;
        float k = (hips - target) / (hips - bottom);
        for (int i = 0; i < verts.Count; i++) {
            var v = verts[i];
            if (v.y < hips) verts[i] = new Vector3(v.x, hips - (hips - v.y) * k, v.z);
        }
        Debug.Log($"[Clothes] {what}: hem from {bottom:F2} m up to {target:F2} m (hips {hips:F2}, knees {knees:F2})");
    }

    // A joint's rest position in Unity space (x mirrored), from the skin's inverse bind matrices; NaN if not in it.
    static Vector3 JointPos(Gltf g, byte[] bin, GSkin skin, string name) {
        float[] ibm = ReadFloats(g, bin, skin.inverseBindMatrices);
        int j = Array.FindIndex(skin.joints, k => g.nodes[k].name == name);
        if (j < 0) return new Vector3(float.NaN, float.NaN, float.NaN);
        var m = new Matrix4x4();
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) m[r, c] = ibm[j * 16 + c * 4 + r];
        Vector3 p = m.inverse.GetColumn(3);
        return new Vector3(-p.x, p.y, p.z);
    }

    // The collar down from the face: everything above the base of the neck squeezed so the top is at Head_jnt + `top`
    // at the back and 4 cm lower at the front (the chin), and no closer than 7.5 cm to the neck's axis.
    static void Collar(Gltf g, byte[] bin, GSkin skin, List<Vector3> verts, float top, string what) {
        Vector3 neck = JointPos(g, bin, skin, "Neck_jnt"), head = JointPos(g, bin, skin, "Head_jnt");
        if (float.IsNaN(neck.y) || float.IsNaN(head.y) || head.y <= neck.y) return;
        float from = neck.y - 0.03f, highest = verts.Max(v => v.y), back = head.y + top;
        if (highest <= back) return;
        const float frontDrop = 0.04f, minRadius = 0.075f;
        int moved = 0;
        for (int i = 0; i < verts.Count; i++) {
            var v = verts[i];
            if (v.y <= from) continue;
            var d = new Vector2(v.x - neck.x, v.z - neck.z);
            float r = d.magnitude;
            float front = r > 1e-4f ? Mathf.Clamp01(d.y / r) : 0;  // +z is the front
            float to = back - frontDrop * front;
            float y = from + (v.y - from) / (highest - from) * (to - from);
            if (r < minRadius) d = r > 1e-4f ? d / r * minRadius : new Vector2(0, -minRadius);
            verts[i] = new Vector3(neck.x + d.x, y, neck.z + d.y);
            moved++;
        }
        Debug.Log($"[Clothes] {what}: collar from {highest:F2} m down to {back:F2} m (neck {neck.y:F2}, head {head.y:F2}), {moved} vertices");
    }

    static void DoReshape(Gltf g, byte[] bin, GSkin skin, Reshape r, List<Vector3> verts, List<Vector3> normals,
                          List<BoneWeight> weights, string what) {
        Vector3 head = JointPos(g, bin, skin, "Head_jnt"), neck = JointPos(g, bin, skin, "Neck_jnt");
        int hi = Array.FindIndex(skin.joints, k => g.nodes[k].name == "Head_jnt");
        int ni = Array.FindIndex(skin.joints, k => g.nodes[k].name == "Neck_jnt");
        if (float.IsNaN(head.y) || float.IsNaN(neck.y) || hi < 0 || ni < 0) return;
        float s = Mathf.Clamp((head.y - neck.y) / 0.083f, 0.8f, 1.2f);
        Vector3 min = verts[0], max = verts[0];
        foreach (var v in verts) { min = Vector3.Min(min, v); max = Vector3.Max(max, v); }
        Vector3 mid = (min + max) / 2;
        var c = r.centre != null && r.centre.Length == 3 ? new Vector3(r.centre[0], r.centre[1], r.centre[2]) : Vector3.zero;
        Vector3 to = head + c * s;
        var rot = Quaternion.Euler(r.rotate_x, 0, 0);
        for (int i = 0; i < verts.Count; i++) {
            verts[i] = to + rot * (verts[i] - mid) * r.scale * s;
            normals[i] = rot * normals[i];
            float y = verts[i].y;
            float w = r.weights == "headneck" ? Mathf.Clamp01((y - neck.y) / Mathf.Max(0.01f, head.y - neck.y)) : 1;
            weights[i] = w >= 0.999f ? new BoneWeight { boneIndex0 = hi, weight0 = 1 }
                                     : new BoneWeight { boneIndex0 = hi, weight0 = w, boneIndex1 = ni, weight1 = 1 - w };
        }
        Debug.Log($"[Clothes] {what}: turned {r.rotate_x} degrees, scaled {r.scale * s:F2}, put at the head");
    }

    // A worn knitted hat in place of the item's own (folded) model: half an egg over the skull from the forehead up,
    // a turned-up cuff at the bottom, ribbed, all of it on Head_jnt; sized by the length of the neck.
    static void Beanie(Gltf g, byte[] bin, GSkin skin, string file, string item, float grow, List<Vector3> verts,
                       List<Vector3> normals, List<Vector2> uvs, List<BoneWeight> weights, List<int[]> submeshes,
                       List<string> matNames, Shader twoSided) {
        Vector3 head = JointPos(g, bin, skin, "Head_jnt"), neck = JointPos(g, bin, skin, "Neck_jnt");
        int headIndex = Array.FindIndex(skin.joints, k => g.nodes[k].name == "Head_jnt");
        if (float.IsNaN(head.y) || float.IsNaN(neck.y) || headIndex < 0) return;
        float s = Mathf.Clamp((head.y - neck.y) / 0.083f, 0.8f, 1.2f) * (1 + grow);
        var c = head + new Vector3(0, 0.115f, 0.012f - 0.1f * grow) * s;
        float rx = 0.104f * s, ry = 0.13f * s, rz = 0.116f * s;
        const int around = 40, up = 14;
        verts.Clear(); normals.Clear(); uvs.Clear(); weights.Clear(); submeshes.Clear(); matNames.Clear();
        for (int j = 0; j <= up; j++)
            for (int i = 0; i <= around; i++) {
                float th = i * 2 * Mathf.PI / around;  // 0: the front (+z)
                float front = Mathf.Max(0, Mathf.Cos(th));
                float rimY = head.y + (0.085f + 0.03f * front) * s;  // lower at the back of the head
                float eRim = Mathf.Asin(Mathf.Clamp((rimY - c.y) / ry, -1, 1));
                float t = j / (float)up;
                float e = Mathf.Lerp(eRim, Mathf.PI / 2, t);
                float cuff = t < 0.22f ? 1.05f : 1f;  // the turned-up band
                var dir = new Vector3(Mathf.Cos(e) * Mathf.Sin(th), Mathf.Sin(e), Mathf.Cos(e) * Mathf.Cos(th));
                var p = c + new Vector3(dir.x * rx * cuff, dir.y * ry, dir.z * rz * cuff);
                verts.Add(p);
                normals.Add(new Vector3((p.x - c.x) / (rx * rx), (p.y - c.y) / (ry * ry), (p.z - c.z) / (rz * rz)).normalized);
                uvs.Add(new Vector2(i / (float)around * 12f, t));  // 12 rib repeats round the head
                weights.Add(new BoneWeight { boneIndex0 = headIndex, weight0 = 1 });
            }
        var tris = new List<int>();
        void Tri(int a, int b, int d) {  // facing out (Unity: clockwise seen from the front)
            var n = Vector3.Cross(verts[b] - verts[a], verts[d] - verts[a]);
            if (Vector3.Dot(n, normals[a] + normals[b] + normals[d]) < 0) { int x = b; b = d; d = x; }
            tris.Add(a); tris.Add(b); tris.Add(d);
        }
        for (int j = 0; j < up; j++)
            for (int i = 0; i < around; i++) {
                int a = j * (around + 1) + i, b = a + 1, d = a + around + 1, e = d + 1;
                Tri(a, b, e); Tri(a, e, d);
            }
        submeshes.Add(tris.ToArray());

        // Its colour: the item's own texture, averaged; ribs knitted into a small tiling texture.
        var colour = new Color(0.45f, 0.18f, 0.16f);
        try {
            var img = g.images[g.textures[g.materials[0].pbrMetallicRoughness.baseColorTexture.index].source];
            var src = new Texture2D(2, 2);
            if (src.LoadImage(File.ReadAllBytes(Path.Combine(Path.GetDirectoryName(file), Uri.UnescapeDataString(img.uri))))) {
                var px = src.GetPixels32(); double r = 0, gg = 0, b = 0;
                foreach (var q in px) { r += q.r; gg += q.g; b += q.b; }
                colour = new Color((float)(r / px.Length / 255), (float)(gg / px.Length / 255), (float)(b / px.Length / 255));
            }
            UnityEngine.Object.DestroyImmediate(src);
        } catch (Exception e) { Debug.LogWarning($"[Clothes] {item}: no colour from its texture ({e.Message})"); }
        string texPath = $"{OutDir}/Textures/Knit_{item}.png";
        var knit = new Texture2D(32, 32, TextureFormat.RGB24, false);
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                float rib = 0.82f + 0.18f * Mathf.Abs(Mathf.Sin(x * Mathf.PI / 32f * 2));  // two ribs a tile
                float stitch = 0.94f + 0.06f * Mathf.Abs(Mathf.Sin(y * Mathf.PI / 4f));
                knit.SetPixel(x, y, colour * rib * stitch);
            }
        File.WriteAllBytes(texPath, knit.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(knit);
        AssetDatabase.ImportAsset(texPath);
        string matName = "Beanie_" + item;
        string matPath = $"{OutDir}/Materials/{matName}.mat";
        var mat = AssetDatabase.LoadAssetAtPath<Material>(matPath);
        if (!mat) { mat = new Material(twoSided) { name = matName }; AssetDatabase.CreateAsset(mat, matPath); }
        mat.SetTexture("_MainTex", AssetDatabase.LoadAssetAtPath<Texture2D>(texPath));
        mat.SetColor("_Color", Color.white);
        EditorUtility.SetDirty(mat);
        matNames.Add(matName);
        Debug.Log($"[Clothes] {item}: a fitted beanie (colour {colour}) instead of the catalog's folded model");
    }

    static Matrix4x4 Mirror(Matrix4x4 m) {
        var s = Matrix4x4.Scale(new Vector3(-1, 1, 1));
        return s * m * s;
    }

    static string MaterialFor(Gltf g, string file, int index, Dictionary<string, Material> materials, Shader opaque, Shader twoSided) {
        if (index < 0 || g.materials == null || index >= g.materials.Length) return "";
        var gm = g.materials[index];
        string texName = "";
        var pbr = gm.pbrMetallicRoughness;
        if (pbr != null && pbr.baseColorTexture != null && pbr.baseColorTexture.index >= 0) {
            var img = g.images[g.textures[pbr.baseColorTexture.index].source];
            texName = Path.GetFileNameWithoutExtension(Uri.UnescapeDataString(img.uri));
        }
        string name = (gm.name ?? "mat") + (gm.doubleSided ? "_2s" : "") + "_" + texName.GetHashCode().ToString("x8");
        foreach (char c in Path.GetInvalidFileNameChars()) name = name.Replace(c, '_');
        if (materials.ContainsKey(name)) return name;
        var mat = new Material(gm.doubleSided ? twoSided : opaque) { name = name };
        if (texName.Length > 0) {
            var tex = AssetDatabase.LoadAssetAtPath<Texture2D>($"{OutDir}/Textures/{texName}.png");
            if (tex) mat.SetTexture("_MainTex", tex);
        }
        if (pbr != null && pbr.baseColorFactor != null && pbr.baseColorFactor.Length == 4)
            mat.SetColor("_Color", new Color(pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], 1));
        AssetDatabase.CreateAsset(mat, $"{OutDir}/Materials/{name}.mat");
        materials[name] = mat;
        return name;
    }

    // --- glTF accessors -------------------------------------------------------------------------------------------

    static int Components(string type) => type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 16;

    static float[] ReadFloats(Gltf g, byte[] bin, int accessor) {
        var a = g.accessors[accessor];
        var v = g.bufferViews[a.bufferView];
        int n = Components(a.type);
        int size = a.componentType == 5126 || a.componentType == 5125 ? 4 : a.componentType == 5123 || a.componentType == 5122 ? 2 : 1;
        int stride = v.byteStride > 0 ? v.byteStride : n * size;
        var outv = new float[a.count * n];
        int start = v.byteOffset + a.byteOffset;
        for (int i = 0; i < a.count; i++)
            for (int c = 0; c < n; c++) {
                int o = start + i * stride + c * size;
                float x;
                switch (a.componentType) {
                    case 5126: x = BitConverter.ToSingle(bin, o); break;
                    case 5125: x = BitConverter.ToUInt32(bin, o); break;
                    case 5123: x = BitConverter.ToUInt16(bin, o); if (a.normalized) x /= 65535f; break;
                    case 5122: x = BitConverter.ToInt16(bin, o); if (a.normalized) x = Mathf.Max(x / 32767f, -1); break;
                    case 5121: x = bin[o]; if (a.normalized) x /= 255f; break;
                    default: x = (sbyte)bin[o]; if (a.normalized) x = Mathf.Max(x / 127f, -1); break;
                }
                outv[i * n + c] = x;
            }
        return outv;
    }

    static Vector3[] ReadVec3(Gltf g, byte[] bin, int accessor) {
        float[] f = ReadFloats(g, bin, accessor);
        var v = new Vector3[f.Length / 3];
        for (int i = 0; i < v.Length; i++) v[i] = new Vector3(f[i * 3], f[i * 3 + 1], f[i * 3 + 2]);
        return v;
    }

    // --- the preview: bodies from tldcoop_bodies wearing outfits, as the mod dresses them ---------------------------

    static List<string[]> ReadOutfits() {
        // "preview_outfits": [["a","b"],["c"]] (JsonUtility can't read nested arrays: pulled out by hand)
        string text = File.ReadAllText("clothes.json");
        int at = text.IndexOf("\"preview_outfits\"", StringComparison.Ordinal);
        var outfits = new List<string[]>();
        if (at < 0) return outfits;
        int open = text.IndexOf('[', at), depth = 0, start = -1;
        for (int i = open; i < text.Length; i++) {
            if (text[i] == '[') { depth++; if (depth == 2) start = i; }
            else if (text[i] == ']') {
                if (depth == 2) {
                    string inner = text.Substring(start + 1, i - start - 1);
                    outfits.Add(inner.Split(',').Select(s => s.Trim().Trim('"')).Where(s => s.Length > 0).ToArray());
                }
                depth--;
                if (depth == 0) break;
            }
        }
        return outfits;
    }

    static void RunPreview() {
        var list = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json"));
        var outfits = ReadOutfits();
        string bodiesPath = File.ReadAllText("bodies.txt").Trim();
        var bodies = AssetBundle.LoadFromFile(bodiesPath);
        if (bodies == null) throw new Exception("couldn't load " + bodiesPath);
        Directory.CreateDirectory("previews");
        const int cellW = 320, cellH = 480;  // one picture per outfit and side, put together in a sheet per character
        var rt = new RenderTexture(cellW, cellH, 24);
        var camGo = new GameObject("PreviewCamera");
        var cam = camGo.AddComponent<Camera>();
        cam.targetTexture = rt;
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.12f, 0.14f, 0.18f);
        cam.fieldOfView = 26;
        var lightGo = new GameObject("PreviewLight");
        var light = lightGo.AddComponent<Light>();
        light.type = LightType.Directional;
        light.intensity = 1.2f;
        lightGo.transform.rotation = Quaternion.Euler(35, -30, 0);
        RenderSettings.ambientLight = new Color(0.45f, 0.45f, 0.5f);
        int shots = 0;
        foreach (string name in bodies.GetAllAssetNames().Where(n => n.EndsWith(".prefab"))) {  // every body's parts
            var p = bodies.LoadAsset<GameObject>(name);
            foreach (var r in p.GetComponentsInChildren<SkinnedMeshRenderer>(true))
                Debug.Log($"[Clothes] parts {Path.GetFileNameWithoutExtension(name)}: {r.name}: " +
                          string.Join("/", r.sharedMaterials.Select(x => x ? x.name : "-")));
        }
        try {
            foreach (string character in list.preview_characters) {
                var prefab = bodies.LoadAsset<GameObject>("Body_" + character);
                if (prefab == null) { Debug.LogWarning("[Clothes] no body " + character); continue; }
                float[] yaws = { 0f, 90f, 180f };  // the body faces +z: front, side, back
                var sheet = new Texture2D(cellW * outfits.Count, cellH * yaws.Length, TextureFormat.RGB24, false);
                for (int o = 0; o < outfits.Count; o++) {
                    var body = (GameObject)UnityEngine.Object.Instantiate(prefab);
                    var anim = body.GetComponentInChildren<Animation>();
                    if (anim) anim.enabled = false;  // the rig's own pose, as the clothes were fitted
                    if (o == 0)  // what the body is made of (can its own clothes be hidden under ours?)
                        foreach (var r in body.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                            var m = r.sharedMesh;
                            Debug.Log($"[Clothes] body {character}: renderer {r.name}, mesh {(m ? m.name : "none")}, " +
                                      $"readable {(m ? m.isReadable : false)}, {(m ? m.subMeshCount : 0)} submeshes, " +
                                      $"{(m ? m.vertexCount : 0)} vertices, materials " +
                                      string.Join("/", r.sharedMaterials.Select(x => x ? x.name : "-")));
                        }
                    var dressed = new List<string>();
                    foreach (string item in outfits[o])
                        if (Dress(body, character, item)) dressed.Add(item);
                    for (int y = 0; y < yaws.Length; y++) {
                        float yaw = yaws[y];
                        cam.transform.position = Quaternion.Euler(0, yaw, 0) * new Vector3(0, 0, 5.2f) + new Vector3(0, 1.0f, 0);
                        cam.transform.LookAt(new Vector3(0, 1.0f, 0));
                        cam.Render();
                        RenderTexture.active = rt;
                        var tex = new Texture2D(rt.width, rt.height, TextureFormat.RGB24, false);
                        tex.ReadPixels(new Rect(0, 0, rt.width, rt.height), 0, 0);
                        tex.Apply();
                        sheet.SetPixels(o * cellW, (yaws.Length - 1 - y) * cellH, cellW, cellH, tex.GetPixels());
                        UnityEngine.Object.DestroyImmediate(tex);
                        shots++;
                    }
                    Debug.Log($"[Clothes] preview {character} outfit {o + 1}: {string.Join(", ", dressed)}");
                    UnityEngine.Object.DestroyImmediate(body);
                }
                sheet.Apply();
                File.WriteAllBytes($"previews/sheet_{character}.jpg", sheet.EncodeToJPG(88));
                UnityEngine.Object.DestroyImmediate(sheet);
            }
        } finally {
            RenderTexture.active = null;
            UnityEngine.Object.DestroyImmediate(camGo);
            UnityEngine.Object.DestroyImmediate(lightGo);
            bodies.Unload(true);
        }
        Debug.Log($"[Clothes] {shots} preview pictures in previews/sheet_<character>.jpg (columns: the outfits; rows: front, side, back)");
    }

    // As the mod does it: a SkinnedMeshRenderer with the item's mesh under the body, its bones the body's transforms
    // of the item's bone names.
    static bool Dress(GameObject body, string character, string item) {
        var mesh = AssetDatabase.LoadAssetAtPath<Mesh>($"{OutDir}/{character}/{item}.asset");
        var info = AssetDatabase.LoadAssetAtPath<TextAsset>($"{OutDir}/{character}/{item}.txt");
        if (mesh == null || info == null) { Debug.LogWarning($"[Clothes] {character}/{item} not built"); return false; }
        var byName = new Dictionary<string, Transform>();
        foreach (var t in body.GetComponentsInChildren<Transform>(true))
            if (!byName.ContainsKey(t.name)) byName[t.name] = t;
        string[] lines = info.text.Split('\n');
        string[] boneNames = lines[0].Substring("bones ".Length).Split(',');
        string[] matNames = lines.Length > 1 && lines[1].StartsWith("materials ") ? lines[1].Substring("materials ".Length).Split(',') : new string[0];
        var bones = new Transform[boneNames.Length];
        int missing = 0;
        for (int i = 0; i < bones.Length; i++) {
            if (!byName.TryGetValue(boneNames[i].Trim(), out bones[i])) { bones[i] = body.transform; missing++; }
        }
        var go = new GameObject("TLDCoop_" + item);
        go.transform.SetParent(body.transform, false);
        var smr = go.AddComponent<SkinnedMeshRenderer>();
        smr.sharedMesh = mesh;
        smr.bones = bones;
        smr.rootBone = byName.TryGetValue("Hips_jnt", out var hips) ? hips : body.transform;
        smr.updateWhenOffscreen = true;
        smr.sharedMaterials = matNames.Select(n => AssetDatabase.LoadAssetAtPath<Material>($"{OutDir}/Materials/{n.Trim()}.mat")).ToArray();
        if (missing > 0) Debug.LogWarning($"[Clothes] {character}/{item}: {missing} of {bones.Length} bones not on the body");
        // Under a coat: the body's own jacket / sweater submeshes aren't drawn.
        var list = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json"));
        foreach (var rule in list.hide_rules ?? new HideRule[0]) {
            if (rule.under == null || rule.words == null || !rule.under.Contains(item)) continue;
            var hidden = AssetDatabase.LoadAssetAtPath<Material>(OutDir + "/Materials/TLDCoop_Hidden.mat");
            foreach (var r in body.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                if (r.gameObject == go) continue;
                var mats = r.sharedMaterials;
                bool changed = false;
                for (int i = 0; i < mats.Length; i++)
                    if (mats[i] && mats[i] != hidden && rule.words.Any(w => mats[i].name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0)) {
                        Debug.Log($"[Clothes] {character}/{item}: the body's {mats[i].name} hidden");
                        mats[i] = hidden;
                        changed = true;
                    }
                if (changed) r.sharedMaterials = mats;
            }
        }
        return true;
    }
}

}  // namespace TLDCoop
