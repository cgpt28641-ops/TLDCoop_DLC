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

[Serializable] class ClothesList {
    public string[] items;
    public string[] preview_characters;
    public string[] preview_outfits_flat;  // (unused: JsonUtility can't read nested arrays; see ReadOutfits)
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

    static string Catalog() => File.ReadAllText("catalog.txt").Trim();

    static void Run() {
        var list = JsonUtility.FromJson<ClothesList>(File.ReadAllText("clothes.json"));
        string catalog = Catalog();
        string chars = Path.Combine(catalog, "characters");
        if (!Directory.Exists(chars)) throw new Exception("no characters folder in the catalog: " + chars);
        Directory.CreateDirectory(OutDir);
        Directory.CreateDirectory(OutDir + "/Materials");
        Directory.CreateDirectory(OutDir + "/Textures");
        foreach (string character in Directory.GetDirectories(chars).Select(Path.GetFileName))
            Directory.CreateDirectory($"{OutDir}/{character}");
        AssetDatabase.Refresh();  // the folders, known to Unity before assets go in them
        var opaque = Shader.Find("TLDCoop/Body");
        var twoSided = Shader.Find("TLDCoop/Body2Sided");
        if (!opaque || !twoSided) throw new Exception("the TLDCoop body shaders didn't compile");

        var materials = new Dictionary<string, Material>();
        int meshes = 0, skipped = 0;
        long triangles = 0;
        AssetDatabase.StartAssetEditing();
        var pendingTextures = new List<string>();
        try {
            // Textures first (copied in, imported once), then materials and meshes.
            foreach (string character in Directory.GetDirectories(chars).Select(Path.GetFileName).OrderBy(s => s))
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

        foreach (string character in Directory.GetDirectories(chars).Select(Path.GetFileName).OrderBy(s => s)) {
            foreach (string item in list.items) {
                string file = Path.Combine(chars, character, item + ".gltf");
                if (!File.Exists(file)) { skipped++; continue; }
                var result = Convert(file, character, item, materials, opaque, twoSided);
                triangles += result;
                meshes++;
            }
        }
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
                        Shader opaque, Shader twoSided) {
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
                verts.Add(new Vector3(-P[i].x, P[i].y, P[i].z));
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
        var rt = new RenderTexture(640, 960, 24);
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
        try {
            foreach (string character in list.preview_characters) {
                var prefab = bodies.LoadAsset<GameObject>("Body_" + character);
                if (prefab == null) { Debug.LogWarning("[Clothes] no body " + character); continue; }
                for (int o = 0; o < outfits.Count; o++) {
                    var body = (GameObject)UnityEngine.Object.Instantiate(prefab);
                    var anim = body.GetComponentInChildren<Animation>();
                    if (anim) anim.enabled = false;  // the rig's own pose, as the clothes were fitted
                    var dressed = new List<string>();
                    foreach (string item in outfits[o])
                        if (Dress(body, character, item)) dressed.Add(item);
                    foreach (float yaw in new[] { 0f, 90f, 180f }) {  // the body faces +z: front, side, back
                        cam.transform.position = Quaternion.Euler(0, yaw, 0) * new Vector3(0, 0, 5.2f) + new Vector3(0, 1.0f, 0);
                        cam.transform.LookAt(new Vector3(0, 1.0f, 0));
                        cam.Render();
                        RenderTexture.active = rt;
                        var tex = new Texture2D(rt.width, rt.height, TextureFormat.RGB24, false);
                        tex.ReadPixels(new Rect(0, 0, rt.width, rt.height), 0, 0);
                        tex.Apply();
                        File.WriteAllBytes($"previews/{character}_{o + 1}_{(int)yaw}.png", tex.EncodeToPNG());
                        UnityEngine.Object.DestroyImmediate(tex);
                        shots++;
                    }
                    Debug.Log($"[Clothes] preview {character} outfit {o + 1}: {string.Join(", ", dressed)}");
                    UnityEngine.Object.DestroyImmediate(body);
                }
            }
        } finally {
            RenderTexture.active = null;
            UnityEngine.Object.DestroyImmediate(camGo);
            UnityEngine.Object.DestroyImmediate(lightGo);
            bodies.Unload(true);
        }
        Debug.Log($"[Clothes] {shots} preview pictures in previews/");
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
        return true;
    }
}

}  // namespace TLDCoop
