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

// How far from the skin a kind of clothing lies: at least min (pushed out), at most max (pulled in below the
// shoulders; 0: no limit), so the layers stack (base layer, shirt or sweater, trousers, vest, coat) without crossing.
[Serializable] class Layer { public string[] items; public float min; public float max; }

[Serializable] class HideRule { public string[] under; public string[] words; public string[] regions; }

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
    // The bodies cut into regions (bodycut/<mesh>.bodycut, from tools/bodycut.py): "<character>:<its mesh>".
    public string[] body_meshes;
    // Pieces kept off the body: every vertex at least wrap_clearance outside its torso, arms, neck and legs (a flat
    // cloak given volume, a sleeve never under the skin).
    public string[] wrap_items; public float wrap_clearance = 0.012f;
    // Hats kept off the head and its hair the same way (the fringe never through a beanie).
    public string[] hat_wrap_items; public float hat_wrap_clearance = 0.006f;
    public Layer[] layers;
    // One sock in the catalog: made a pair (mirrored to the other foot, its bones swapped Left/Right).
    public string[] mirror_items;
    // The base every character wears under everything (their own clothes hidden): these items, and a long-sleeved top
    // made from base_top_from in undyed grey (TLDCoop_BaseTop). base_hide: the body's regions never drawn.
    public string[] base_items; public string base_top_from; public string[] base_hide;
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
        var opaque = Shader.Find("TLDCoop/Cloth");
        var twoSided = opaque;  // every piece of clothing: TLDCoop/Cloth (two sided, normal map, baked occlusion)
        if (!opaque) throw new Exception("the TLDCoop cloth shader didn't compile");

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

        int cut = 0;
        foreach (string entry in list.body_meshes ?? new string[0]) {
            string[] kv = entry.Split(':');
            if (kv.Length != 2 || !keep.Contains(kv[0])) continue;
            string src = $"bodycut/{kv[1]}.bodycut";
            if (!File.Exists(src)) { Debug.LogWarning($"[Clothes] no {src}: {kv[0]} keeps its whole body"); continue; }
            BodyCut(src, kv[0], kv[1]);
            cut++;
        }
        // The base layer first (TLDCoop_BaseTop from base_top_from in undyed grey, then base_items), kept as the
        // catalog fitted them: every other piece is laid over its surface (BaseSurface), not over the hidden body.
        foreach (string character in Characters(list, chars).OrderBy(s => s)) {
            var baseList = new List<(string item, string file, bool grey)>();
            if (!string.IsNullOrEmpty(list.base_top_from))
                baseList.Add(("TLDCoop_BaseTop", Path.Combine(chars, character, list.base_top_from + ".gltf"), true));
            foreach (string bi in list.base_items ?? new string[0]) baseList.Add((bi, Path.Combine(chars, character, bi + ".gltf"), false));
            foreach (var (bItem, bFile, bGrey) in baseList) {
                if (!File.Exists(bFile)) continue;
                bool bMirror = list.mirror_items != null && list.mirror_items.Contains(bItem);
                triangles += Convert(bFile, character, bItem, materials, opaque, twoSided, 0, 0, -1, float.NaN, false, false, null, 0,
                                     new string[0], -1, false, 0, bMirror, bGrey, true);
                meshes++;
            }
        }
        foreach (string character in Characters(list, chars).OrderBy(s => s)) {
            foreach (string item in list.items) {
                if (list.base_items != null && list.base_items.Contains(item)) continue;  // built above
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
                var cutRegions = (list.hide_rules ?? new HideRule[0]).Where(r => r.under != null && r.regions != null && r.under.Contains(item))
                    .SelectMany(r => r.regions).Distinct().ToArray();
                float wrap = list.wrap_items != null && list.wrap_items.Contains(item) ? list.wrap_clearance : -1, wrapMax = 0;
                var layer = list.layers?.FirstOrDefault(x => x.items != null && x.items.Contains(item));
                if (layer != null) { wrap = layer.min; wrapMax = layer.max; }
                bool onHead = list.hat_wrap_items != null && list.hat_wrap_items.Contains(item);
                if (onHead) { wrap = list.hat_wrap_clearance; wrapMax = 0; }
                bool mirror = list.mirror_items != null && list.mirror_items.Contains(item);
                var result = Convert(file, character, item, materials, opaque, twoSided, lift, inflate, shorten, collar, turn, beanie, reshape, grow, cutRegions, wrap, onHead, wrapMax, mirror, false);
                triangles += result;
                meshes++;
            }
        }
        Skins.Clear();
        BaseSurface.Clear();
        foreach (var b in OcclusionBodies.Values) if (b) UnityEngine.Object.DestroyImmediate(b);
        OcclusionBodies.Clear();
        // What the mod hides under which items: one TextAsset for the whole pack.
        // One line per rule: "hide <item>,<item>... : <word>,<word>..."
        // and "cut <item>,<item>... : <region>,<region>..." (the body's regions not drawn under them).
        File.WriteAllText(OutDir + "/rules.txt", string.Concat((list.hide_rules ?? new HideRule[0]).Select(r =>
            (r.words != null && r.words.Length > 0
                ? "hide " + string.Join(",", r.under ?? new string[0]) + " : " + string.Join(",", r.words) + "\n" : "") +
            (r.regions != null && r.regions.Length > 0
                ? "cut " + string.Join(",", r.under ?? new string[0]) + " : " + string.Join(",", r.regions) + "\n" : ""))));
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
        Debug.Log($"[Clothes] {cut} bodies cut into regions");
        Debug.Log($"[Clothes] built {meshes} meshes ({triangles} triangles, {materials.Count} materials, {skipped} " +
                  $"missing in the catalog), bundle {size / 1048576.0:F1} MB");
    }

    // --- one glTF -> Mesh + TextAsset -----------------------------------------------------------------------------

    static long Convert(string file, string character, string item, Dictionary<string, Material> materials,
                        Shader opaque, Shader twoSided, float lift, float inflate, float shorten, float collar,
                        bool turn, bool beanie, Reshape reshape, float grow, string[] cutRegions, float wrap, bool onHead, float wrapMax, bool mirror, bool grey,
                        bool isBase = false) {
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
            matNames.Add(MaterialFor(g, file, p.material, materials, opaque, twoSided, grey));
        }
        if (mirror) MirrorPair(g, skin, verts, normals, uvs, weights, submeshes, $"{character}/{item}");

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

        if (wrap > 0) {
            float neckY = JointPos(g, bin, skin, "Neck_jnt").y;
            Wrap(verts, submeshes, character, wrap, $"{character}/{item}", onHead, wrapMax, float.IsNaN(neckY) ? 1.5f : neckY - 0.06f);
        }
        if (isBase) {  // the surface the other pieces lie over
            if (!BaseSurface.TryGetValue(character, out var surf)) BaseSurface[character] = surf = (new List<Vector3>(), new List<Vector3>());
            surf.p.AddRange(verts);
            surf.n.AddRange(normals.Select(x => x.normalized));
        }
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
        mesh.colors = BakeOcclusion(verts, normals, submeshes, character);
        AssetDatabase.CreateAsset(mesh, $"{OutDir}/{character}/{item}.asset");

        string bones = string.Join(",", skin.joints.Select(j => g.nodes[j].name));
        // The cut body's regions not drawn under it: those of its rules, and of the torso's bands those its hem reaches 3 cm
        // below (its lowest point near the middle, sleeves left out).
        string cutLine = "";
        if (cutRegions.Length > 0) {
            var mid = verts.Where(p => Mathf.Abs(p.x) < 0.2f).ToList();
            float hem = mid.Count > 0 ? mid.Min(p => p.y) : float.MaxValue;
            var cuts = cutRegions.ToList();
            if (cuts.Contains("torso"))
                for (int k = 0; k < 15; k++)
                    if (BandTop - BandStep * (k + 1) >= hem + 0.03f) cuts.Add("band" + k);
            cutLine = "cut " + string.Join(",", cuts) + "\n";
            Debug.Log($"[Clothes] {character}/{item}: hem at {hem:F2} m, hides {string.Join(",", cuts)}");
        }
        File.WriteAllText($"{OutDir}/{character}/{item}.txt", "bones " + bones + "\nmaterials " + string.Join(",", matNames) + "\n" + cutLine);
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

    // As tools/bodycut/cut.py names them: the torso's lower part in 3 cm bands from BandTop down.
    static readonly string[] Regions = new[] { "head", "neck", "torso", "arms", "hands", "hips", "legs", "feet" }
        .Concat(Enumerable.Range(0, 15).Select(k => "band" + k)).ToArray();
    const float BandTop = 1.25f, BandStep = 0.03f;

    // A body's mesh again, the same vertices and bones, its triangles split by region inside each of its submeshes:
    // <character>/BodyCut.asset, and Body.txt: "mesh <the body's own mesh name>", "parts <submesh>:<region>,..." (one per
    // submesh of the cut mesh, in order: which of the body's materials draws it and which region it is).
    static void BodyCut(string file, string character, string meshName) {
        using var br = new BinaryReader(File.OpenRead(file));
        if (new string(br.ReadChars(8)) != "TLDCUT1\0") throw new Exception(file + ": not a body cut");
        int n = br.ReadInt32(), subs = br.ReadInt32(), boneCount = br.ReadInt32(), matCount = br.ReadInt32();
        var v = new Vector3[n]; var nr = new Vector3[n]; var tg = new Vector4[n]; var uv = new Vector2[n]; var w = new float[n * 4];
        for (int i = 0; i < n; i++) {
            v[i] = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
            nr[i] = new Vector3(br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
            tg[i] = new Vector4(br.ReadSingle(), br.ReadSingle(), br.ReadSingle(), br.ReadSingle());
            uv[i] = new Vector2(br.ReadSingle(), br.ReadSingle());
            for (int k = 0; k < 4; k++) w[i * 4 + k] = br.ReadSingle();
        }
        var bw = new BoneWeight[n];
        for (int i = 0; i < n; i++)
            bw[i] = new BoneWeight {
                boneIndex0 = br.ReadInt32(), boneIndex1 = br.ReadInt32(), boneIndex2 = br.ReadInt32(), boneIndex3 = br.ReadInt32(),
                weight0 = w[i * 4], weight1 = w[i * 4 + 1], weight2 = w[i * 4 + 2], weight3 = w[i * 4 + 3],
            };
        var bind = new Matrix4x4[boneCount];
        for (int b = 0; b < boneCount; b++) {
            var m = new Matrix4x4();
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) m[r, c] = br.ReadSingle();
            bind[b] = m;
        }
        var parts = new List<string>(); var tris = new List<int[]>();
        for (int sIndex = 0; sIndex < subs; sIndex++) {
            int count = br.ReadInt32();
            var byRegion = new List<int>[Regions.Length];
            for (int t = 0; t < count; t++) {
                int a = br.ReadInt32(), b = br.ReadInt32(), c = br.ReadInt32(), reg = br.ReadInt32();
                (byRegion[reg] ??= new List<int>()).AddRange(new[] { a, b, c });
            }
            for (int reg = 0; reg < Regions.Length; reg++)
                if (byRegion[reg] != null) { parts.Add($"{sIndex}:{Regions[reg]}"); tris.Add(byRegion[reg].ToArray()); }
        }
        var mesh = new Mesh { name = "BodyCut_" + character, indexFormat = UnityEngine.Rendering.IndexFormat.UInt32 };
        mesh.vertices = v; mesh.normals = nr; mesh.tangents = tg; mesh.uv = uv; mesh.boneWeights = bw; mesh.bindposes = bind;
        mesh.subMeshCount = tris.Count;
        for (int i = 0; i < tris.Count; i++) mesh.SetTriangles(tris[i], i);
        mesh.RecalculateBounds();
        AssetDatabase.CreateAsset(mesh, $"{OutDir}/{character}/BodyCut.asset");
        File.WriteAllText($"{OutDir}/{character}/BodyCut.txt", $"mesh {meshName}\nparts {string.Join(",", parts)}\n");
        AssetDatabase.ImportAsset($"{OutDir}/{character}/BodyCut.txt");
        Debug.Log($"[Clothes] {character}: body {meshName} cut into {parts.Count} parts ({n} vertices, {boneCount} bones)");
    }

    // The preview's body on its cut mesh (once): each part drawn by the body's own material for its submesh.
    static readonly Dictionary<int, string[]> CutParts = new Dictionary<int, string[]>();
    static void EnsureCut(GameObject body, string character) {
        if (CutParts.ContainsKey(body.GetInstanceID())) return;
        var mesh = AssetDatabase.LoadAssetAtPath<Mesh>($"{OutDir}/{character}/BodyCut.asset");
        var info = AssetDatabase.LoadAssetAtPath<TextAsset>($"{OutDir}/{character}/BodyCut.txt");
        if (!mesh || !info) return;
        string[] lines = info.text.Split('\n');
        string meshName = lines[0].Substring("mesh ".Length).Trim();
        string[] parts = lines[1].Substring("parts ".Length).Trim().Split(',');
        foreach (var r in body.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
            if (!r.sharedMesh || r.sharedMesh.name != meshName) continue;
            var own = r.sharedMaterials;
            r.sharedMesh = mesh;
            var baseHide = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json")).base_hide ?? new string[0];
            var hiddenMat = AssetDatabase.LoadAssetAtPath<Material>(OutDir + "/Materials/TLDCoop_Hidden.mat");
            r.sharedMaterials = parts.Select(p => baseHide.Contains(p.Split(':')[1]) ? hiddenMat
                                                  : own[Math.Min(int.Parse(p.Split(':')[0]), own.Length - 1)]).ToArray();
            CutParts[body.GetInstanceID()] = parts;
            Debug.Log($"[Clothes] preview {character}: on the cut body ({parts.Length} parts)");
            return;
        }
        Debug.LogWarning($"[Clothes] preview {character}: no renderer with mesh {meshName}");
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
        var knitNormal = NormalFromColour(texPath, $"{OutDir}/Textures/Knit_{item}_N.png", 6f);
        if (knitNormal) mat.SetTexture("_Normal", knitNormal);
        EditorUtility.SetDirty(mat);
        matNames.Add(matName);
        Debug.Log($"[Clothes] {item}: a fitted beanie (colour {colour}) instead of the catalog's folded model");
    }

    // An undyed grey copy of a colour texture (a heather grey from its brightness): Textures/<name>_grey.png.
    static string GreyCopy(string texName) {
        string src = $"{OutDir}/Textures/{texName}.png", name = texName + "_grey", dst = $"{OutDir}/Textures/{name}.png";
        if (AssetDatabase.LoadAssetAtPath<Texture2D>(dst)) return name;
        var t = new Texture2D(2, 2);
        if (!File.Exists(src) || !t.LoadImage(File.ReadAllBytes(src))) { UnityEngine.Object.DestroyImmediate(t); return texName; }
        var px = t.GetPixels32();
        for (int i = 0; i < px.Length; i++) {
            float lum = (0.3f * px[i].r + 0.59f * px[i].g + 0.11f * px[i].b) / 255f;
            byte v = (byte)Mathf.Clamp(255 * (0.38f + 0.5f * lum), 0, 255);
            px[i] = new Color32(v, v, (byte)Mathf.Min(255, v + 4), 255);
        }
        t.SetPixels32(px);
        File.WriteAllBytes(dst, t.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(t);
        AssetDatabase.ImportAsset(dst);
        if (AssetImporter.GetAtPath(dst) is TextureImporter imp) { imp.maxTextureSize = 1024; imp.SaveAndReimport(); }
        return name;
    }

    // One sock made a pair: every vertex copied to the other side (x mirrored, the winding turned), skinned to the
    // matching bones (Left_ for Right_).
    static void MirrorPair(Gltf g, GSkin skin, List<Vector3> verts, List<Vector3> normals, List<Vector2> uvs,
                           List<BoneWeight> weights, List<int[]> submeshes, string what) {
        var names = skin.joints.Select(j => g.nodes[j].name).ToArray();
        int Other(int b) {
            string n = names[b], o = n.StartsWith("Left") ? "Right" + n.Substring(4) : n.StartsWith("Right") ? "Left" + n.Substring(5) : n;
            int k = Array.IndexOf(names, o);
            return k >= 0 ? k : b;
        }
        int count = verts.Count;
        float side = verts.Average(v => v.x);
        for (int i = 0; i < count; i++) {
            verts.Add(new Vector3(-verts[i].x, verts[i].y, verts[i].z));
            normals.Add(new Vector3(-normals[i].x, normals[i].y, normals[i].z));
            uvs.Add(uvs[i]);
            var w = weights[i];
            weights.Add(new BoneWeight {
                boneIndex0 = Other(w.boneIndex0), boneIndex1 = Other(w.boneIndex1), boneIndex2 = Other(w.boneIndex2), boneIndex3 = Other(w.boneIndex3),
                weight0 = w.weight0, weight1 = w.weight1, weight2 = w.weight2, weight3 = w.weight3,
            });
        }
        for (int s = 0; s < submeshes.Count; s++) {
            var t = submeshes[s];
            var both = new int[t.Length * 2];
            Array.Copy(t, both, t.Length);
            for (int i = 0; i + 2 < t.Length; i += 3) {
                both[t.Length + i] = t[i] + count;
                both[t.Length + i + 1] = t[i + 2] + count;
                both[t.Length + i + 2] = t[i + 1] + count;
            }
            submeshes[s] = both;
        }
        Debug.Log($"[Clothes] {what}: one piece (at x {side:F2}) made a pair");
    }

    // A tangent-space normal map from a colour texture: its brightness taken as height (dark seams and folds lie lower),
    // softened, sloped into normals. Cached: made once per texture.
    static Texture2D NormalFromColour(string colourPath, string normalPath, float strength = 2.5f) {
        var made = AssetDatabase.LoadAssetAtPath<Texture2D>(normalPath);
        if (made) return made;
        string full = Path.GetFullPath(colourPath);
        if (!File.Exists(full)) return null;
        var src = new Texture2D(2, 2);
        if (!src.LoadImage(File.ReadAllBytes(full))) { UnityEngine.Object.DestroyImmediate(src); return null; }
        int step = Mathf.Max(1, src.width / 1024), w = src.width / step, h = src.height / step;
        var px = src.GetPixels32();
        var lum = new float[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                var c = px[(y * step) * src.width + x * step];
                lum[y * w + x] = (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255f;
            }
        UnityEngine.Object.DestroyImmediate(src);
        var blur = new float[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float sum = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        sum += lum[((y + dy + h) % h) * w + (x + dx + w) % w];
                blur[y * w + x] = sum / 9f;
            }
        var outPx = new Color32[w * h];
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float l = blur[y * w + (x + w - 1) % w], r = blur[y * w + (x + 1) % w];
                float d = blur[((y + h - 1) % h) * w + x], u = blur[((y + 1) % h) * w + x];
                var n = new Vector3((l - r) * strength, (d - u) * strength, 1).normalized;
                outPx[y * w + x] = new Color32((byte)((n.x * 0.5f + 0.5f) * 255), (byte)((n.y * 0.5f + 0.5f) * 255),
                                               (byte)((n.z * 0.5f + 0.5f) * 255), 255);
            }
        var tex = new Texture2D(w, h, TextureFormat.RGBA32, false);
        tex.SetPixels32(outPx);
        File.WriteAllBytes(normalPath, tex.EncodeToPNG());
        UnityEngine.Object.DestroyImmediate(tex);
        AssetDatabase.ImportAsset(normalPath);
        if (AssetImporter.GetAtPath(normalPath) is TextureImporter imp) {
            imp.textureType = TextureImporterType.NormalMap;
            imp.maxTextureSize = 1024;
            imp.SaveAndReimport();
        }
        return AssetDatabase.LoadAssetAtPath<Texture2D>(normalPath);
    }

    // The body's skin that clothes must stay off (torso, arms, neck, hips, legs, the bands), at rest, with a 3 cm grid.
    class Skin { public Vector3[] p, n; public Dictionary<Vector3Int, List<int>> grid = new Dictionary<Vector3Int, List<int>>(); }
    static readonly Dictionary<string, Skin> Skins = new Dictionary<string, Skin>();
    static readonly Dictionary<string, (List<Vector3> p, List<Vector3> n)> BaseSurface = new Dictionary<string, (List<Vector3> p, List<Vector3> n)>();
    const float SkinCell = 0.03f;
    static Vector3Int Cell(Vector3 p) => new Vector3Int(Mathf.FloorToInt(p.x / SkinCell), Mathf.FloorToInt(p.y / SkinCell), Mathf.FloorToInt(p.z / SkinCell));
    static Skin SkinOf(string character, bool head) {
        string key = character + (head ? "/head" : "");
        if (Skins.TryGetValue(key, out var known)) return known;
        Skin skin = null;
        var mesh = AssetDatabase.LoadAssetAtPath<Mesh>($"{OutDir}/{character}/BodyCut.asset");
        var info = AssetDatabase.LoadAssetAtPath<TextAsset>($"{OutDir}/{character}/BodyCut.txt");
        if (mesh && info) {
            string[] parts = info.text.Split('\n')[1].Substring("parts ".Length).Trim().Split(',');
            var use = new HashSet<int>();
            for (int i = 0; i < parts.Length && i < mesh.subMeshCount; i++) {
                string region = parts[i].Split(':')[1];
                if (head ? region != "head" : region == "head") continue;
                foreach (int v in mesh.GetTriangles(i)) use.Add(v);
            }
            var all = mesh.vertices; var normals = mesh.normals;
            if (!head && BaseSurface.TryGetValue(character, out var surf)) {
                // over the base layer, and the body's own neck and hands (all that shows of it below the head)
                var shown = new HashSet<int>();
                for (int i = 0; i < parts.Length && i < mesh.subMeshCount; i++) {
                    string region = parts[i].Split(':')[1];
                    if (region == "neck" || region == "hands") foreach (int v in mesh.GetTriangles(i)) shown.Add(v);
                }
                skin = new Skin { p = surf.p.Concat(shown.Select(i => all[i])).ToArray(),
                                  n = surf.n.Concat(shown.Select(i => normals[i].normalized)).ToArray() };
            } else
            skin = new Skin { p = use.Select(i => all[i]).ToArray(), n = use.Select(i => normals[i].normalized).ToArray() };
            for (int i = 0; i < skin.p.Length; i++) {
                var c = Cell(skin.p[i]);
                if (!skin.grid.TryGetValue(c, out var l)) skin.grid[c] = l = new List<int>();
                l.Add(i);
            }
        }
        return Skins[key] = skin;
    }

    // Every vertex pushed out to at least `clearance` above the nearest skin (along the skin's normal there), the pushes
    // then smoothed over the piece's own triangles so it bends round the body instead of denting.
    static void Wrap(List<Vector3> verts, List<int[]> submeshes, string character, float clearance, string what, bool onHead,
                     float most, float pullBelow) {
        var skin = SkinOf(character, onHead);
        if (skin == null) return;
        var push = new Vector3[verts.Count];
        var pull = new Vector3[verts.Count];
        int moved = 0, pulled = 0, reachCells = most > 0 ? 2 : 1;
        for (int i = 0; i < verts.Count; i++) {
            var c = Cell(verts[i]);
            int best = -1; float bestD = float.MaxValue;
            for (int dx = -reachCells; dx <= reachCells; dx++) for (int dy = -reachCells; dy <= reachCells; dy++) for (int dz = -reachCells; dz <= reachCells; dz++)
                if (skin.grid.TryGetValue(new Vector3Int(c.x + dx, c.y + dy, c.z + dz), out var l))
                    foreach (int k in l) {
                        float d = (skin.p[k] - verts[i]).sqrMagnitude;
                        if (d < bestD) { bestD = d; best = k; }
                    }
            if (best < 0) continue;
            float above = Vector3.Dot(verts[i] - skin.p[best], skin.n[best]);
            if (above < clearance) { push[i] = skin.n[best] * (clearance - above); moved++; }
            else if (most > 0 && above > most && verts[i].y < pullBelow) { pull[i] = -skin.n[best] * (above - most); pulled++; }
        }
        // Layers move together: a coat's lining lies a few millimetres inside its shell, and pushing only the lining would
        // send it through the shell. Each vertex takes the biggest push within 2.5 cm (pointing its way).
        {
            const float reach = 0.025f;
            var grid = new Dictionary<Vector3Int, List<int>>();
            Vector3Int G(Vector3 v) => new Vector3Int(Mathf.FloorToInt(v.x / reach), Mathf.FloorToInt(v.y / reach), Mathf.FloorToInt(v.z / reach));
            for (int i = 0; i < verts.Count; i++)
                if (push[i] != Vector3.zero) {
                    var c = G(verts[i]);
                    if (!grid.TryGetValue(c, out var l)) grid[c] = l = new List<int>();
                    l.Add(i);
                }
            var widened = new Vector3[push.Length];
            for (int i = 0; i < verts.Count; i++) {
                var best = push[i];
                var c = G(verts[i]);
                for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++)
                    if (grid.TryGetValue(new Vector3Int(c.x + dx, c.y + dy, c.z + dz), out var l))
                        foreach (int k in l)
                            if (push[k].sqrMagnitude > best.sqrMagnitude && (verts[k] - verts[i]).sqrMagnitude < reach * reach &&
                                (best == Vector3.zero || Vector3.Dot(push[k].normalized, best.normalized) > 0.5f))
                                best = push[k];
                widened[i] = best;
            }
            push = widened;
        }
        // smooth the pushes twice over the piece's edges (a vertex keeps at least its own push)
        var neighbours = new List<int>[verts.Count];
        foreach (var tris in submeshes)
            for (int t = 0; t + 2 < tris.Length; t += 3)
                for (int e = 0; e < 3; e++) {
                    int a = tris[t + e], b = tris[t + (e + 1) % 3];
                    (neighbours[a] ??= new List<int>()).Add(b);
                    (neighbours[b] ??= new List<int>()).Add(a);
                }
        for (int pass = 0; pass < 2; pass++) {
            var next = new Vector3[push.Length];
            for (int i = 0; i < push.Length; i++) {
                if (neighbours[i] == null) { next[i] = push[i]; continue; }
                var sum = push[i];
                foreach (int k in neighbours[i]) sum += push[k];
                var avg = sum / (neighbours[i].Count + 1);
                next[i] = avg.sqrMagnitude > push[i].sqrMagnitude ? avg : push[i];
            }
            push = next;
        }
        // Pulled in (too loose under a layer that goes over it): the pulls smoothed over the piece's edges, two passes.
        if (pulled > 0)
            for (int pass = 0; pass < 2; pass++) {
                var next = new Vector3[pull.Length];
                for (int i = 0; i < pull.Length; i++) {
                    if (neighbours[i] == null) { next[i] = pull[i]; continue; }
                    var acc = pull[i];
                    foreach (int k in neighbours[i]) acc += pull[k];
                    next[i] = acc / (neighbours[i].Count + 1);
                }
                pull = next;
            }
        float farthest = 0, total = 0;
        for (int i = 0; i < verts.Count; i++) {
            verts[i] += push[i] + pull[i];
            farthest = Mathf.Max(farthest, push[i].magnitude);
            total += push[i].magnitude;
        }
        Debug.Log($"[Clothes] QA {what}: kept {clearance * 100:F1} cm off the body; {moved} of {verts.Count} vertices were too close " +
                  $"or under the skin, pushed out by {total / Mathf.Max(1, verts.Count) * 100:F1} cm on average, {farthest * 100:F1} cm at most; " +
                  $"{pulled} pulled in to {most * 100:F1} cm");
    }

    // Ambient occlusion per vertex, into the vertex colour: rays over the half of the sky its normal faces, blocked by
    // the piece itself and by the character's body (its cut mesh, at rest). 1 open, darker where folds and the body close in.
    static readonly Dictionary<string, GameObject> OcclusionBodies = new Dictionary<string, GameObject>();
    static Vector3[] occlusionRays;
    static Color[] BakeOcclusion(List<Vector3> verts, List<Vector3> normals, List<int[]> submeshes, string character) {
        if (occlusionRays == null) {  // 32 directions round +z, cosine weighted (a golden-angle spiral)
            const int count = 32;
            occlusionRays = new Vector3[count];
            for (int i = 0; i < count; i++) {
                float r = Mathf.Sqrt((i + 0.5f) / count), a = i * 2.39996323f;
                occlusionRays[i] = new Vector3(r * Mathf.Cos(a), r * Mathf.Sin(a), Mathf.Sqrt(1 - r * r));
            }
        }
        if (!OcclusionBodies.ContainsKey(character)) {
            var bodyMesh = AssetDatabase.LoadAssetAtPath<Mesh>($"{OutDir}/{character}/BodyCut.asset");
            GameObject b = null;
            if (bodyMesh) {
                b = new GameObject("TLDCoop_OcclusionBody_" + character);
                var still = new Mesh { indexFormat = UnityEngine.Rendering.IndexFormat.UInt32, vertices = bodyMesh.vertices };
                still.SetTriangles(Enumerable.Range(0, bodyMesh.subMeshCount).SelectMany(i => bodyMesh.GetTriangles(i)).ToArray(), 0);
                b.AddComponent<MeshCollider>().sharedMesh = still;
            }
            OcclusionBodies[character] = b;
        }
        var go = new GameObject("TLDCoop_OcclusionPiece");
        var piece = new Mesh { indexFormat = UnityEngine.Rendering.IndexFormat.UInt32 };
        piece.SetVertices(verts);
        piece.SetTriangles(submeshes.SelectMany(t => t).ToArray(), 0);
        go.AddComponent<MeshCollider>().sharedMesh = piece;
        Physics.SyncTransforms();
        bool backfaces = Physics.queriesHitBackfaces;
        Physics.queriesHitBackfaces = true;
        const float reach = 0.25f;
        var colours = new Color[verts.Count];
        for (int i = 0; i < verts.Count; i++) {
            Vector3 n = normals[i].sqrMagnitude > 0 ? normals[i].normalized : Vector3.up;
            Vector3 t = Vector3.Cross(n, Mathf.Abs(n.y) < 0.9f ? Vector3.up : Vector3.right).normalized, b = Vector3.Cross(n, t);
            Vector3 from = verts[i] + n * 0.003f;
            float blocked = 0;
            foreach (var d in occlusionRays)
                if (Physics.Raycast(from, t * d.x + b * d.y + n * d.z, out var hit, reach)) blocked += 1 - hit.distance / reach;
            float open = Mathf.Clamp(1 - 1.4f * blocked / occlusionRays.Length, 0.3f, 1);
            colours[i] = new Color(open, open, open, 1);
        }
        Physics.queriesHitBackfaces = backfaces;
        UnityEngine.Object.DestroyImmediate(go);
        UnityEngine.Object.DestroyImmediate(piece);
        return colours;
    }

    static Matrix4x4 Mirror(Matrix4x4 m) {
        var s = Matrix4x4.Scale(new Vector3(-1, 1, 1));
        return s * m * s;
    }

    static string MaterialFor(Gltf g, string file, int index, Dictionary<string, Material> materials, Shader opaque, Shader twoSided,
                              bool grey = false) {
        if (index < 0 || g.materials == null || index >= g.materials.Length) return "";
        var gm = g.materials[index];
        string texName = "";
        var pbr = gm.pbrMetallicRoughness;
        if (pbr != null && pbr.baseColorTexture != null && pbr.baseColorTexture.index >= 0) {
            var img = g.images[g.textures[pbr.baseColorTexture.index].source];
            texName = Path.GetFileNameWithoutExtension(Uri.UnescapeDataString(img.uri));
        }
        string name = (gm.name ?? "mat") + (gm.doubleSided ? "_2s" : "") + "_" + texName.GetHashCode().ToString("x8") + (grey ? "_grey" : "");
        foreach (char c in Path.GetInvalidFileNameChars()) name = name.Replace(c, '_');
        if (materials.ContainsKey(name)) return name;
        var mat = new Material(gm.doubleSided ? twoSided : opaque) { name = name };
        if (texName.Length > 0) {
            if (grey) texName = GreyCopy(texName);
            var tex = AssetDatabase.LoadAssetAtPath<Texture2D>($"{OutDir}/Textures/{texName}.png");
            if (tex) mat.SetTexture("_MainTex", tex);
            var normal = NormalFromColour($"{OutDir}/Textures/{texName}.png", $"{OutDir}/Textures/{texName}_N.png");
            if (normal) { mat.SetTexture("_Normal", normal); mat.SetFloat("_NormalMapStrength", 0.8f); }
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
        const int cellW = 400, cellH = 600;  // one picture per outfit and side, put together in a sheet per character
        var rt = new RenderTexture(cellW, cellH, 24);
        var camGo = new GameObject("PreviewCamera");
        var cam = camGo.AddComponent<Camera>();
        cam.targetTexture = rt;
        cam.clearFlags = CameraClearFlags.SolidColor;
        cam.backgroundColor = new Color(0.12f, 0.14f, 0.18f);
        cam.fieldOfView = 26;
        // Lit like an overcast day outside: a soft warm key from above at the front, a cool fill, a rim from behind, a
        // sky above and snow below for the ambient, and real shadows (folds, under the hat brim, the arms on the body).
        var lightGo = new GameObject("PreviewLight");
        Light AddLight(string name, Color colour, float intensity, Vector3 euler, bool shadows) {
            var go = name == "Key" ? lightGo : new GameObject("Preview" + name);
            if (go != lightGo) go.transform.SetParent(lightGo.transform, false);
            var l = go.AddComponent<Light>();
            l.type = LightType.Directional;
            l.color = colour;
            l.intensity = intensity;
            l.shadows = shadows ? LightShadows.Soft : LightShadows.None;
            l.shadowStrength = 0.75f;
            l.shadowBias = 0.02f;
            l.shadowNormalBias = 0.2f;
            go.transform.rotation = Quaternion.Euler(euler);
            return l;
        }
        AddLight("Key", new Color(1f, 0.96f, 0.9f), 0.8f, new Vector3(40, 150, 0), true);
        AddLight("Fill", new Color(0.75f, 0.85f, 1f), 0.28f, new Vector3(15, -130, 0), false);
        AddLight("Rim", new Color(0.9f, 0.95f, 1f), 0.25f, new Vector3(25, 20, 0), false);
        RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Trilight;
        RenderSettings.ambientSkyColor = new Color(0.3f, 0.33f, 0.39f);
        RenderSettings.ambientEquatorColor = new Color(0.3f, 0.31f, 0.34f);
        RenderSettings.ambientGroundColor = new Color(0.36f, 0.37f, 0.4f);
        QualitySettings.shadows = ShadowQuality.All;
        QualitySettings.shadowResolution = ShadowResolution.VeryHigh;
        QualitySettings.shadowDistance = 20;
        QualitySettings.antiAliasing = 4;
        rt.antiAliasing = 4;
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
                var close = new Texture2D(cellW * outfits.Count, cellH * yaws.Length, TextureFormat.RGB24, false);  // head and shoulders
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
                    // the base layer under every outfit (TLDCoop_BaseTop first: the long johns' waist goes over it)
                    // (a base sock or long johns left off when the outfit has its own)
                    bool Same(string sx, string sy) => (list.mirror_items != null && list.mirror_items.Contains(sx) && list.mirror_items.Contains(sy)) ||
                                                       (sx.StartsWith("GEAR_LongUnderwear") && sy.StartsWith("GEAR_LongUnderwear"));
                    var under = (list.base_items ?? new string[0]).Where(bi => !outfits[o].Any(ox => Same(ox, bi)));
                    foreach (string item in new[] { "TLDCoop_BaseTop" }.Concat(under).Concat(outfits[o]))
                        if (Dress(body, character, item)) dressed.Add(item);
                    var headPos = body.GetComponentsInChildren<Transform>(true).FirstOrDefault(t => t.name == "Head_jnt");
                    float headY = headPos ? headPos.position.y + 0.08f : 1.65f;
                    for (int pass = 0; pass < 2; pass++)
                        for (int y = 0; y < yaws.Length; y++) {
                            float yaw = yaws[y];
                            var aim = pass == 0 ? new Vector3(0, 1.0f, 0) : new Vector3(0, headY - 0.12f, 0);
                            float dist = pass == 0 ? 5.2f : 1.9f;  // the whole body, or head and shoulders
                            cam.transform.position = Quaternion.Euler(0, yaw, 0) * new Vector3(0, 0, dist) + aim;
                            cam.transform.LookAt(aim);
                            cam.Render();
                            RenderTexture.active = rt;
                            var tex = new Texture2D(rt.width, rt.height, TextureFormat.RGB24, false);
                            tex.ReadPixels(new Rect(0, 0, rt.width, rt.height), 0, 0);
                            tex.Apply();
                            (pass == 0 ? sheet : close).SetPixels(o * cellW, (yaws.Length - 1 - y) * cellH, cellW, cellH, tex.GetPixels());
                            UnityEngine.Object.DestroyImmediate(tex);
                            shots++;
                        }
                    Debug.Log($"[Clothes] preview {character} outfit {o + 1}: {string.Join(", ", dressed)}");
                    UnityEngine.Object.DestroyImmediate(body);
                }
                sheet.Apply();
                File.WriteAllBytes($"previews/sheet_{character}.jpg", sheet.EncodeToJPG(88));
                close.Apply();
                File.WriteAllBytes($"previews/close_{character}.jpg", close.EncodeToJPG(88));
                UnityEngine.Object.DestroyImmediate(close);
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
        // Under a coat: the body's own torso and arms (cut body), and its jacket / sweater submeshes, aren't drawn.
        var list = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json"));
        EnsureCut(body, character);
        string cutText = lines.FirstOrDefault(l => l.StartsWith("cut "));
        if (cutText != null && CutParts.TryGetValue(body.GetInstanceID(), out var cutParts))
            foreach (var regions in new[] { cutText.Substring(4).Trim().Split(',') }) {
                var hiddenMat = AssetDatabase.LoadAssetAtPath<Material>(OutDir + "/Materials/TLDCoop_Hidden.mat");
                foreach (var r in body.GetComponentsInChildren<SkinnedMeshRenderer>(true)) {
                    if (!r.sharedMesh || r.sharedMesh.name != "BodyCut") continue;
                    var mats = r.sharedMaterials;
                    for (int i = 0; i < mats.Length && i < cutParts.Length; i++)
                        if (regions.Contains(cutParts[i].Split(':')[1])) mats[i] = hiddenMat;
                    r.sharedMaterials = mats;
                }
            }
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
