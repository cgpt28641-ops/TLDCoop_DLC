#include "custom_pictures.h"

#include <windows.h>

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "il2cpp_api.h"
#include "log.h"

namespace custom_pictures {
namespace {

struct Picture {
    const char* atlas;   // TLD's texture name
    int size;            // the size we build our copy of the atlas at (square)
    int x, y, w, h;      // where ours goes, in that size, top-left origin
    const unsigned char* rgb;  // w*h pixels, rows top-down
    int channels;              // 3 RGB, or 4 RGBA (alpha blends it over TLD's: its frame's brushed edge stays)
};
#include "pictures_data.inc"

constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
// TLDCoop_DLC: looked at after a scene comes in (late materials, frames hung up later), only materials not seen yet in
// this scene, a few hundred a frame: the whole list at once, every minute, was a 35-77 ms frame.
constexpr double kScanAt[] = {1000, 6000, 20000, 60000};
constexpr int kScans = static_cast<int>(sizeof(kScanAt) / sizeof(kScanAt[0]));
constexpr size_t kPerFrame = 300;
constexpr int kRGBA32 = 4;                   // TextureFormat.RGBA32
constexpr int kDontUnloadUnusedAsset = 32;   // HideFlags

struct Rect { float x, y, w, h; };

struct Api {
    bool ok = false;
    Il2CppObject* material_type = nullptr;
    Il2CppClass* texture2d = nullptr;
    const MethodInfo* find_all = nullptr, *object_name = nullptr, *set_name = nullptr, *hide_flags = nullptr;
    const MethodInfo* destroy = nullptr;  // static Object.Destroy(Object)
    const MethodInfo* get_main = nullptr, *set_main = nullptr;
    const MethodInfo* rt_get = nullptr, *rt_release = nullptr, *rt_active = nullptr, *rt_set_active = nullptr;
    const MethodInfo* blit = nullptr;
    const MethodInfo* t2d_ctor = nullptr, *read_pixels = nullptr, *get_pixels = nullptr, *set_pixels = nullptr,
                      *apply = nullptr;
} api;

std::unordered_map<std::string, uintptr_t> g_ours;  // atlas name -> our texture
std::unordered_set<std::string> g_failed;
std::string g_scene;
double g_scene_since = 0;
int g_scans_done = 0;
// The scan under way, spread over frames: Resources.FindObjectsOfTypeAll's array (held), and where we are in it.
uintptr_t g_sweep = 0;
size_t g_sweep_at = 0;
int g_changed = 0;
std::unordered_set<void*> g_seen;            // materials (native) looked at in this scene
std::unordered_map<void*, int> g_tex_kind;   // texture (native) -> 1 one of the atlases, 0 not

double now_ms() {
    static const double per_ms = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / per_ms;
}
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
std::string name_of(Il2CppObject* o) {
    Il2CppObject* s = o ? il2::invoke(api.object_name, o) : nullptr;
    return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
}

// TLD's atlas -> a readable copy of ours at `size`, with every picture of ours for it painted in.
Il2CppObject* build(Il2CppObject* tld, const std::string& atlas) {
    int size = 0;
    for (const Picture& p : kPictures)
        if (atlas == p.atlas) size = p.size;
    if (!size) return nullptr;

    int32_t s = size, depth = 0;
    void* ga[] = {&s, &s, &depth};
    Il2CppObject* rt = il2::invoke(api.rt_get, nullptr, ga);
    if (!rt) return nullptr;
    void* ba[] = {tld, rt};
    il2::invoke(api.blit, nullptr, ba);
    Il2CppObject* prev = il2::invoke(api.rt_active, nullptr);
    void* aa[] = {rt};
    il2::invoke(api.rt_set_active, nullptr, aa);

    Il2CppObject* tex = il2::il2cpp_object_new(api.texture2d);
    int32_t fmt = kRGBA32;
    bool mips = true;
    void* ca[] = {&s, &s, &fmt, &mips};
    il2::invoke(api.t2d_ctor, tex, ca);
    Rect r{0, 0, static_cast<float>(size), static_cast<float>(size)};
    int32_t zero = 0;
    void* ra[] = {&r, &zero, &zero};
    il2::invoke(api.read_pixels, tex, ra);

    void* pa[] = {prev};
    il2::invoke(api.rt_set_active, nullptr, pa);
    void* rel[] = {rt};
    il2::invoke(api.rt_release, nullptr, rel);

    void* la[] = {&zero};
    Il2CppObject* pixels = il2::invoke(api.get_pixels, tex, la);  // Color32[], rows bottom-up
    if (il2::array_length(pixels) != static_cast<size_t>(size) * size) {
        logx::error("pictures: could not read %s back (%zu pixels)", atlas.c_str(), il2::array_length(pixels));
        void* da[] = {tex};
        il2::invoke(api.destroy, nullptr, da);  // TLDCoop_DLC: not left behind (a texture the size of the atlas)
        return nullptr;
    }
    uint8_t* px = reinterpret_cast<uint8_t*>(pixels) + 0x20;
    int painted = 0;
    for (const Picture& p : kPictures) {
        if (atlas != p.atlas) continue;
        for (int j = 0; j < p.h; ++j) {
            const int row = size - 1 - (p.y + j);
            if (row < 0 || row >= size) continue;
            for (int i = 0; i < p.w; ++i) {
                const int col = p.x + i;
                if (col < 0 || col >= size) continue;
                const unsigned char* src = p.rgb + (static_cast<size_t>(j) * p.w + i) * p.channels;
                uint8_t* dst = px + (static_cast<size_t>(row) * size + col) * 4;
                const int a = p.channels == 4 ? src[3] : 255;
                for (int k = 0; k < 3; ++k) dst[k] = static_cast<uint8_t>((src[k] * a + dst[k] * (255 - a) + 127) / 255);
                dst[3] = 255;
            }
        }
        ++painted;
    }
    void* sa[] = {pixels, &zero};
    il2::invoke(api.set_pixels, tex, sa);
    bool update_mips = true, no_longer_readable = true;
    void* ap[] = {&update_mips, &no_longer_readable};
    il2::invoke(api.apply, tex, ap);

    const std::string ours = atlas + "_tldcoop";
    void* na[] = {il2::il2cpp_string_new(ours.c_str())};
    il2::invoke(api.set_name, tex, na);
    int32_t flags = kDontUnloadUnusedAsset;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, tex, fa);
    logx::info("pictures: %s built (%dx%d, %d picture(s) of ours)", ours.c_str(), size, size, painted);
    return tex;
}

void end_sweep() {
    if (g_sweep) il2::il2cpp_gchandle_free(g_sweep);
    g_sweep = 0;
    g_sweep_at = 0;
    if (g_changed) logx::info("pictures: %d material(s) show our pictures", g_changed);
    g_changed = 0;
}

void start_sweep() {
    end_sweep();
    void* a[] = {api.material_type};
    Il2CppObject* all = il2::invoke(api.find_all, nullptr, a);
    if (all) g_sweep = il2::il2cpp_gchandle_new(all, false);
}

bool is_atlas(const std::string& name) {
    for (const Picture& p : kPictures)
        if (name == p.atlas) return true;
    return false;
}

// One frame's share of the scan: a material is asked for its texture once a scene, a texture for its name once.
void sweep_step() {
    Il2CppObject* all = target(g_sweep);
    const size_t n = il2::array_length(all);
    const size_t stop = std::min(n, g_sweep_at + kPerFrame);
    for (; g_sweep_at < stop; ++g_sweep_at) {
        Il2CppObject* m = il2::array_object_at(all, g_sweep_at);
        if (!alive(m) || !g_seen.insert(il2::read_field<void*>(m, kCachedPtr)).second) continue;
        Il2CppObject* tex = il2::invoke(api.get_main, m);
        if (!alive(tex)) continue;
        void* native = il2::read_field<void*>(tex, kCachedPtr);
        auto kind = g_tex_kind.find(native);
        if (kind == g_tex_kind.end()) kind = g_tex_kind.emplace(native, is_atlas(name_of(tex)) ? 1 : 0).first;
        if (!kind->second) continue;
        const std::string name = name_of(tex);
        if (g_failed.count(name)) continue;
        Il2CppObject* mine = target(g_ours[name]);
        if (!alive(mine)) {
            mine = build(tex, name);
            if (!mine) {
                g_failed.insert(name);
                logx::warn("pictures: %s left as TLD's", name.c_str());
                continue;
            }
            if (g_ours[name]) il2::il2cpp_gchandle_free(g_ours[name]);
            g_ours[name] = il2::il2cpp_gchandle_new(mine, false);
        }
        void* sa[] = {mine};
        il2::invoke(api.set_main, m, sa);
        ++g_changed;
    }
    if (g_sweep_at >= n) end_sweep();
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* resources = il2::find_class(kCore, "UnityEngine", "Resources");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* rt = il2::find_class(kCore, "UnityEngine", "RenderTexture");
    Il2CppClass* graphics = il2::find_class(kCore, "UnityEngine", "Graphics");
    api.texture2d = il2::find_class(kCore, "UnityEngine", "Texture2D");
    if (!object || !resources || !material || !rt || !graphics || !api.texture2d) return false;
    api.material_type = il2::type_object(material);
    api.find_all = il2::find_method_params(resources, "FindObjectsOfTypeAll", {"System.Type"});
    api.object_name = il2::find_method(object, "get_name", 0);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    api.get_main = il2::find_method(material, "get_mainTexture", 0);
    api.set_main = il2::find_method(material, "set_mainTexture", 1);
    api.rt_get = il2::find_method_params(rt, "GetTemporary", {"System.Int32", "System.Int32", "System.Int32"});
    api.rt_release = il2::find_method(rt, "ReleaseTemporary", 1);
    api.rt_active = il2::find_method(rt, "get_active", 0);
    api.rt_set_active = il2::find_method(rt, "set_active", 1);
    api.blit = il2::find_method_params(graphics, "Blit", {"UnityEngine.Texture", "UnityEngine.RenderTexture"});
    api.t2d_ctor = il2::find_method_params(api.texture2d, ".ctor",
                                           {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"});
    api.read_pixels = il2::find_method_params(api.texture2d, "ReadPixels", {"UnityEngine.Rect", "System.Int32", "System.Int32"});
    api.get_pixels = il2::find_method_params(api.texture2d, "GetPixels32", {"System.Int32"});
    api.set_pixels = il2::find_method_params(api.texture2d, "SetPixels32", {"UnityEngine.Color32[]", "System.Int32"});
    api.apply = il2::find_method_params(api.texture2d, "Apply", {"System.Boolean", "System.Boolean"});
    return api.find_all && api.object_name && api.set_name && api.hide_flags && api.destroy && api.get_main && api.set_main &&
           api.rt_get && api.rt_release && api.rt_active && api.rt_set_active && api.blit && api.t2d_ctor &&
           api.read_pixels && api.get_pixels && api.set_pixels && api.apply;
}

}  // namespace

void init() {
    api.ok = resolve();
    logx::info("pictures: %s", api.ok ? "ready" : "unavailable (Unity's calls missing)");
}

void on_frame(const std::string& scene, bool in_game) {
    if (!api.ok) return;
    const double now = now_ms();
    if (scene != g_scene) {
        g_scene = scene;
        g_scene_since = now;
        g_scans_done = 0;
        end_sweep();
        g_seen.clear();      // native pointers of the scene left behind can come back as others
        g_tex_kind.clear();
    }
    if (!in_game) return;
    if (g_sweep) {
        sweep_step();
        return;
    }
    bool due = false;
    while (g_scans_done < kScans && now - g_scene_since >= kScanAt[g_scans_done]) {
        ++g_scans_done;
        due = true;
    }
    if (!due) return;
    start_sweep();
    sweep_step();
}

}  // namespace custom_pictures
