#include "night_sky.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "paths.h"

// TLDCoop_DLC: Better Night Sky (built in; MIT, WulfMarius and ds5678: third_party/better-night-sky; Karol 06.11:
// "zróbmy Better Night Sky"): TLD's star sphere swapped for a finer one, and a moon of its own that has its phases
// (one of 24 pictures over a 29-day cycle from the game's day). They fade in and out with TLD's own stars and moon
// (the time of day's m_StarsAlpha, m_MoonAlpha); the moon stands where TLD's moonlight comes from. Each player's own
// look (coop.ini [sky] enabled), nothing shared. Shooting stars and the night bloom tweak of the mod aren't in.
namespace night_sky {
namespace {

#include "night_sky_bundle.inc"

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr int kMoonCycleDays = 29, kMoonPhases = 24;

struct Color {
    float r, g, b, a;
};
struct Vector3 {
    float x, y, z;
};

struct Api {
    const MethodInfo* uni_storm = nullptr;      // static GameManager.GetUniStorm()
    ptrdiff_t star_sphere = -1;                 // UniStormWeatherSystem.m_StarSphere (GameObject)
    ptrdiff_t moon_light = -1;                  // UniStormWeatherSystem.m_MoonLight (Light)
    ptrdiff_t cycle_start = -1;                 // UniStormWeatherSystem.m_MoonCycleStartDay
    const MethodInfo* tod = nullptr;            // UniStormWeatherSystem.GetActiveTODState()
    const MethodInfo* day = nullptr;            // UniStormWeatherSystem.GetDayNumber()
    ptrdiff_t moon_alpha = -1, stars_alpha = -1;  // TODStateData.m_MoonAlpha / m_StarsAlpha
    const MethodInfo* load_bundle = nullptr;    // static AssetBundle.LoadFromFile(string)
    const MethodInfo* load_asset = nullptr;     // AssetBundle.LoadAsset(string, Type)
    Il2CppObject* t_go = nullptr, *t_tex = nullptr;  // typeof(GameObject), typeof(Texture2D)
    const MethodInfo* instantiate = nullptr;    // static Object.Instantiate(Object)
    const MethodInfo* destroy = nullptr;        // static Object.Destroy(Object)
    const MethodInfo* go_tf = nullptr;          // GameObject.get_transform()
    const MethodInfo* go_active = nullptr;      // GameObject.SetActive(bool)
    const MethodInfo* go_layer = nullptr, *go_set_layer = nullptr;  // GameObject.get_layer / set_layer
    const MethodInfo* comp_tf = nullptr;        // Component.get_transform()
    const MethodInfo* tf_parent = nullptr, *tf_set_parent = nullptr;  // Transform.get_parent / SetParent(Transform, bool)
    const MethodInfo* tf_set_local_euler = nullptr;  // Transform.set_localEulerAngles
    const MethodInfo* tf_forward = nullptr, *tf_set_forward = nullptr;  // Transform.get_forward / set_forward
    const MethodInfo* in_children = nullptr;    // GameObject.GetComponentInChildren(Type)
    Il2CppObject* t_renderer = nullptr;         // typeof(Renderer)
    const MethodInfo* material = nullptr;       // Renderer.get_material()
    const MethodInfo* get_color = nullptr, *set_color = nullptr;  // Material.GetColor(string) / SetColor(string, Color)
    const MethodInfo* set_texture = nullptr;    // Material.set_mainTexture(Texture)
} api;

bool g_ready = false, g_want = true, g_failed = false;
uintptr_t g_bundle = 0, g_stars = 0, g_moon = 0, g_stars_mat = 0, g_moon_mat = 0, g_storm = 0;
Color g_stars_base{}, g_moon_base{};
int g_phase = -1;
float g_stars_alpha = -1, g_moon_alpha = -1;

using InitFn = void (*)(Il2CppObject*, const MethodInfo*);
InitFn g_init = nullptr;

uintptr_t hold(Il2CppObject* o) { return o ? il2::il2cpp_gchandle_new(o, false) : 0; }
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
void drop(uintptr_t& h) {
    if (h) il2::il2cpp_gchandle_free(h);
    h = 0;
}
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }

Il2CppObject* bundle() {
    if (Il2CppObject* b = target(g_bundle); alive(b)) return b;
    drop(g_bundle);
    if (g_failed) return nullptr;
    const auto file = paths::instance_dir() / L"tldcoop_nightsky.bundle";
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) != kSkyBundleSize) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(kSkyBundle), static_cast<std::streamsize>(kSkyBundleSize));
        if (!out) {
            logx::warn("night sky: couldn't write %ls", file.c_str());
            g_failed = true;
            return nullptr;
        }
    }
    void* a[] = {il2::il2cpp_string_new(file.string().c_str())};
    Il2CppObject* b = il2::invoke(api.load_bundle, nullptr, a);
    if (!b) {
        logx::warn("night sky: the moon and stars' bundle didn't load (%ls)", file.c_str());
        g_failed = true;
        return nullptr;
    }
    g_bundle = hold(b);
    return b;
}

Il2CppObject* asset(const char* name, Il2CppObject* type) {
    Il2CppObject* b = bundle();
    if (!b) return nullptr;
    void* a[] = {il2::il2cpp_string_new(name), type};
    return il2::invoke(api.load_asset, b, a);
}

Il2CppObject* tf_of(Il2CppObject* go) { return go ? il2::invoke(api.go_tf, go) : nullptr; }
void set_active(Il2CppObject* go, bool on) {
    void* a[] = {&on};
    if (go) il2::invoke(api.go_active, go, a);
}
Il2CppObject* material_of(Il2CppObject* go) {
    void* a[] = {api.t_renderer};
    Il2CppObject* r = go ? il2::invoke(api.in_children, go, a) : nullptr;
    return r ? il2::invoke(api.material, r) : nullptr;
}
Color tint(Il2CppObject* mat) {
    Color c{1, 1, 1, 1};
    void* a[] = {il2::il2cpp_string_new("_TintColor")};
    il2::invoke_value(api.get_color, mat, c, a);
    return c;
}
void set_tint(Il2CppObject* mat, Color c) {
    void* a[] = {il2::il2cpp_string_new("_TintColor"), &c};
    il2::invoke(api.set_color, mat, a);
}

Il2CppObject* place(const char* prefab, Il2CppObject* parent, int layer) {
    Il2CppObject* p = asset(prefab, api.t_go);
    if (!p) {
        logx::warn("night sky: no %s in the bundle", prefab);
        return nullptr;
    }
    void* a[] = {p};
    Il2CppObject* go = il2::invoke(api.instantiate, nullptr, a);
    if (!go) return nullptr;
    bool keep_world = true;
    void* pa[] = {parent, &keep_world};
    il2::invoke(api.tf_set_parent, tf_of(go), pa);
    void* la[] = {&layer};
    il2::invoke(api.go_set_layer, go, la);
    return go;
}

void remove(Il2CppObject* storm) {
    for (uintptr_t* h : {&g_stars, &g_moon})
        if (Il2CppObject* go = target(*h); alive(go)) {
            void* a[] = {go};
            il2::invoke(api.destroy, nullptr, a);
        }
    drop(g_stars), drop(g_moon), drop(g_stars_mat), drop(g_moon_mat);
    if (storm) set_active(il2::read_field<Il2CppObject*>(storm, api.star_sphere), true);
    g_phase = -1;
}

// TLD's sky: ours in place of its star sphere (under the same parent), the moon a level up (as the mod has them).
void install(Il2CppObject* storm) {
    if (!storm) return;
    Il2CppObject* original = il2::read_field<Il2CppObject*>(storm, api.star_sphere);
    if (!alive(original)) return;
    Il2CppObject* parent = il2::invoke(api.tf_parent, tf_of(original));
    Il2CppObject* grand = parent ? il2::invoke(api.tf_parent, parent) : nullptr;
    int32_t layer = 0;
    il2::invoke_value(api.go_layer, original, layer);
    Il2CppObject* stars = place("assets/StarSphere.prefab", parent, layer);
    Il2CppObject* moon = stars ? place("assets/Moon.prefab", grand ? grand : parent, layer) : nullptr;
    if (!stars || !moon) {
        if (stars) {
            void* a[] = {stars};
            il2::invoke(api.destroy, nullptr, a);
        }
        return;
    }
    Vector3 euler{0, 90, 0};
    void* ea[] = {&euler};
    il2::invoke(api.tf_set_local_euler, tf_of(stars), ea);
    Il2CppObject* sm = material_of(stars);
    Il2CppObject* mm = material_of(moon);
    g_stars = hold(stars), g_moon = hold(moon), g_stars_mat = hold(sm), g_moon_mat = hold(mm);
    if (sm) g_stars_base = tint(sm);
    if (mm) g_moon_base = tint(mm);
    g_stars_alpha = g_moon_alpha = -1;
    g_phase = -1;
    set_active(original, false);
    logx::info("night sky: Better Night Sky's stars and moon up%s", sm && mm ? "" : " (a material missing: they won't fade)");
}

void init_detour(Il2CppObject* self, const MethodInfo* m) {
    g_init(self, m);
    if (!g_ready || !self) return;
    if (target(g_storm) == self && alive(target(g_stars))) return;  // (the same sky again: ours are still up)
    drop(g_storm);
    g_storm = hold(self);
    drop(g_stars), drop(g_moon), drop(g_stars_mat), drop(g_moon_mat);  // (a new sky: the old ones went with it)
    if (g_want) install(self);
}

}  // namespace

bool init() {
    Il2CppClass* storm = il2::find_class(kGame, "", "UniStormWeatherSystem");
    Il2CppClass* tod = il2::find_class(kGame, "", "TODStateData");
    Il2CppClass* bundle_c = il2::find_class("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* comp = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* mat = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* tex = il2::find_class(kCore, "UnityEngine", "Texture2D");
    if (!storm || !tod || !bundle_c || !object || !go || !comp || !tf || !renderer || !mat || !tex) return false;
    api.uni_storm = il2::find_method(il2::find_class(kGame, "", "GameManager"), "GetUniStorm", 0);
    api.star_sphere = il2::field_offset(storm, "m_StarSphere");
    api.moon_light = il2::field_offset(storm, "m_MoonLight");
    api.cycle_start = il2::field_offset(storm, "m_MoonCycleStartDay");
    api.tod = il2::find_method(storm, "GetActiveTODState", 0);
    api.day = il2::find_method(storm, "GetDayNumber", 0);
    api.moon_alpha = il2::field_offset(tod, "m_MoonAlpha");
    api.stars_alpha = il2::field_offset(tod, "m_StarsAlpha");
    api.load_bundle = il2::find_method_params(bundle_c, "LoadFromFile", {"System.String"});
    api.load_asset = il2::find_method_params(bundle_c, "LoadAsset", {"System.String", "System.Type"});
    api.t_go = il2::type_object(go);
    api.t_tex = il2::type_object(tex);
    api.t_renderer = il2::type_object(renderer);
    for (Il2CppObject* t : {api.t_go, api.t_tex, api.t_renderer})
        if (t) il2::il2cpp_gchandle_new(t, false);
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.go_tf = il2::find_method(go, "get_transform", 0);
    api.go_active = il2::find_method(go, "SetActive", 1);
    api.go_layer = il2::find_method(go, "get_layer", 0);
    api.go_set_layer = il2::find_method(go, "set_layer", 1);
    api.comp_tf = il2::find_method(comp, "get_transform", 0);
    api.tf_parent = il2::find_method(tf, "get_parent", 0);
    api.tf_set_parent = il2::find_method_params(tf, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    api.tf_set_local_euler = il2::find_method(tf, "set_localEulerAngles", 1);
    api.tf_forward = il2::find_method(tf, "get_forward", 0);
    api.tf_set_forward = il2::find_method(tf, "set_forward", 1);
    api.in_children = il2::find_method_params(go, "GetComponentInChildren", {"System.Type"});
    api.material = il2::find_method(renderer, "get_material", 0);
    api.get_color = il2::find_method_params(mat, "GetColor", {"System.String"});
    api.set_color = il2::find_method_params(mat, "SetColor", {"System.String", "UnityEngine.Color"});
    api.set_texture = il2::find_method(mat, "set_mainTexture", 1);
    const MethodInfo* storm_init = il2::find_method(storm, "Init", 0);
    g_ready = api.uni_storm && api.star_sphere >= 0 && api.moon_light >= 0 && api.cycle_start >= 0 && api.tod && api.day &&
              api.moon_alpha >= 0 && api.stars_alpha >= 0 && api.load_bundle && api.load_asset && api.t_go && api.t_tex &&
              api.instantiate && api.destroy && api.go_tf && api.go_active && api.go_layer && api.go_set_layer &&
              api.comp_tf && api.tf_parent && api.tf_set_parent && api.tf_set_local_euler && api.tf_forward &&
              api.tf_set_forward && api.in_children && api.material && api.get_color && api.set_color &&
              api.set_texture && storm_init &&
              hooks::install_method("UniStormWeatherSystem.Init (night sky)", storm_init,
                                    reinterpret_cast<void*>(&init_detour), reinterpret_cast<void**>(&g_init));
    logx::info("night sky: %s", g_ready ? "ready (coop.ini [sky] enabled)" : "INCOMPLETE (see warnings above)");
    return g_ready;
}

void set_settings(const config::Settings& s) {
    if (g_want == s.night_sky) return;
    g_want = s.night_sky;
    if (!g_ready) return;
    Il2CppObject* storm = target(g_storm);
    if (!alive(storm)) return;
    if (g_want) install(storm);
    else remove(storm);
}

void on_frame() {
    if (!g_ready) return;
    Il2CppObject* storm = target(g_storm);
    Il2CppObject* stars = target(g_stars);
    Il2CppObject* moon = target(g_moon);
    if (!alive(storm) || !alive(stars) || !alive(moon)) return;
    // fading with TLD's own stars and moon
    if (Il2CppObject* tod = il2::invoke(api.tod, storm)) {
        const float sa = il2::read_field<float>(tod, api.stars_alpha), ma = il2::read_field<float>(tod, api.moon_alpha);
        if (Il2CppObject* sm = target(g_stars_mat); sm && std::fabs(sa - g_stars_alpha) > 0.002f) {
            g_stars_alpha = sa;
            set_tint(sm, {g_stars_base.r * sa * sa, g_stars_base.g * sa * sa, g_stars_base.b * sa * sa, g_stars_base.a * sa * sa});
        }
        if (Il2CppObject* mm = target(g_moon_mat); mm && std::fabs(ma - g_moon_alpha) > 0.002f) {
            g_moon_alpha = ma;
            set_tint(mm, {g_moon_base.r * ma, g_moon_base.g * ma, g_moon_base.b * ma, g_moon_base.a * ma});
        }
    }
    // the moon where the moonlight comes from
    if (Il2CppObject* light = il2::read_field<Il2CppObject*>(storm, api.moon_light); alive(light)) {
        Vector3 f{};
        if (Il2CppObject* ltf = il2::invoke(api.comp_tf, light); ltf && il2::invoke_value(api.tf_forward, ltf, f)) {
            Vector3 back{-f.x, -f.y, -f.z};
            void* a[] = {&back};
            il2::invoke(api.tf_set_forward, tf_of(moon), a);
        }
    }
    // its phase: one of 24 pictures over the 29 days of the moon's cycle
    int32_t day = 0;
    il2::invoke_value(api.day, storm, day);
    const int phase = ((day + il2::read_field<int32_t>(storm, api.cycle_start)) % kMoonCycleDays + kMoonCycleDays) %
                      kMoonCycleDays * kMoonPhases / kMoonCycleDays;
    if (phase != g_phase) {
        if (Il2CppObject* mm = target(g_moon_mat)) {
            const std::string name = "assets/MoonPhase/Moon_" + std::to_string(phase) + ".png";
            if (Il2CppObject* tex = asset(name.c_str(), api.t_tex)) {
                void* a[] = {tex};
                il2::invoke(api.set_texture, mm, a);
            }
        }
        g_phase = phase;
    }
}

}  // namespace night_sky
