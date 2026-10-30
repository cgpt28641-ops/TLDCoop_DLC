#include "custom_pictures.h"

#include <windows.h>

#include <cstdint>
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
    const unsigned char* rgb;  // w*h RGB, rows top-down
};
#include "pictures_data.inc"

constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr double kScanAt[] = {1000, 6000};  // after a scene comes in
constexpr double kRescanMs = 60000;          // and now and then after (frames hung up later)
constexpr int kRGBA32 = 4;                   // TextureFormat.RGBA32
constexpr int kDontUnloadUnusedAsset = 32;   // HideFlags

struct Rect { float x, y, w, h; };

struct Api {
    bool ok = false;
    Il2CppObject* material_type = nullptr;
    Il2CppClass* texture2d = nullptr;
    const MethodInfo* find_all = nullptr, *object_name = nullptr, *set_name = nullptr, *hide_flags = nullptr;
    const MethodInfo* get_main = nullptr, *set_main = nullptr;
    const MethodInfo* rt_get = nullptr, *rt_release = nullptr, *rt_active = nullptr, *rt_set_active = nullptr;
    const MethodInfo* blit = nullptr;
    const MethodInfo* t2d_ctor = nullptr, *read_pixels = nullptr, *get_pixels = nullptr, *set_pixels = nullptr,
                      *apply = nullptr;
} api;

std::unordered_map<std::string, uintptr_t> g_ours;  // atlas name -> our texture
std::unordered_set<std::string> g_failed;
std::string g_scene;
double g_scene_since = 0, g_last_scan = 0;
int g_scans_done = 0;

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
                const unsigned char* src = p.rgb + (static_cast<size_t>(j) * p.w + i) * 3;
                uint8_t* dst = px + (static_cast<size_t>(row) * size + col) * 4;
                dst[0] = src[0], dst[1] = src[1], dst[2] = src[2], dst[3] = 255;
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

void scan() {
    void* a[] = {api.material_type};
    Il2CppObject* all = il2::invoke(api.find_all, nullptr, a);
    int changed = 0;
    for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
        Il2CppObject* m = il2::array_object_at(all, i);
        if (!alive(m)) continue;
        Il2CppObject* tex = il2::invoke(api.get_main, m);
        if (!tex) continue;
        const std::string name = name_of(tex);
        bool ours = false;
        for (const Picture& p : kPictures)
            if (name == p.atlas) ours = true;
        if (!ours || g_failed.count(name)) continue;
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
        ++changed;
    }
    if (changed) logx::info("pictures: %d material(s) show our pictures", changed);
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
    return api.find_all && api.object_name && api.set_name && api.hide_flags && api.get_main && api.set_main &&
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
    }
    if (!in_game) return;
    bool due = false;
    while (g_scans_done < 2 && now - g_scene_since >= kScanAt[g_scans_done]) {
        ++g_scans_done;
        due = true;
    }
    if (g_scans_done >= 2 && now - g_last_scan >= kRescanMs) due = true;
    if (!due) return;
    g_last_scan = now;
    scan();
}

}  // namespace custom_pictures
