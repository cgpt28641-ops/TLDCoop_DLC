#include "dynamic_trees.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <random>
#include <unordered_map>

#include "coop_features.h"
#include "game_state.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "paths.h"
#include "trees_math.h"

namespace dynamic_trees {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr double kScanAt[] = {1000, 6000};  // after a scene comes in: look for the trees' materials (late ones too)
constexpr double kSaveMs = 30000;

struct Api {
    bool ok = false;
    Il2CppObject* material_type = nullptr, *texture_type = nullptr, *terrain_type = nullptr;
    const MethodInfo* find_all = nullptr;       // static Resources.FindObjectsOfTypeAll(Type)
    const MethodInfo* find_objects = nullptr;   // static Object.FindObjectsOfType(Type)
    const MethodInfo* object_name = nullptr;
    const MethodInfo* get_main = nullptr, *set_main = nullptr;  // Material.get/set_mainTexture
    const MethodInfo* load_bundle = nullptr;    // static AssetBundle.LoadFromFile(string)
    const MethodInfo* load_asset = nullptr;     // AssetBundle.LoadAsset(string, Type)
    const MethodInfo* terrain_data = nullptr;   // Terrain.get_terrainData()
    const MethodInfo* refresh = nullptr;        // TerrainData.RefreshPrototypes()
    const MethodInfo* get_tod = nullptr, *hours_played = nullptr;  // GameManager.GetTimeOfDayComponent, TimeOfDay.GetHoursPlayedNotPaused
    const MethodInfo* get_weather = nullptr, *weather_stage = nullptr, *indoor = nullptr;  // Weather.GetWeatherStage, IsIndoorScene
    const MethodInfo* get_wind = nullptr;
    ptrdiff_t wind_strength = -1;               // Wind.m_CurrentStrength
} api;

uintptr_t g_bundle = 0;
bool g_bundle_tried = false;
std::unordered_map<std::string, uintptr_t> g_textures;  // ours, by name (from the bundle)

struct TreeMat {
    uintptr_t material = 0, original = 0;  // the material, and TLD's texture on it
    bool pine = true;
    float r = 0;      // its random neighbour pick (trees::texture_index)
    int shown = -1;   // the index it shows now
};
std::unordered_map<Il2CppObject*, TreeMat> g_mats;
std::mt19937 g_rng{std::random_device{}()};

std::string g_scene;
double g_scene_since = 0;
int g_scans_done = 0;
float g_snow = 0;
bool g_snow_known = false;
std::string g_save;  // the save the snow belongs to
double g_last_save = 0;
float g_last_hours = -1;
int g_state = -1;
bool g_applied = false;  // our textures are on

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
uintptr_t hold(Il2CppObject* o) { return o ? il2::il2cpp_gchandle_new(o, false) : 0; }
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
std::string name_of(Il2CppObject* o) {
    Il2CppObject* s = o ? il2::invoke(api.object_name, o) : nullptr;
    return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
}

bool active() { return coop_features::on(net::kFeatureDynamicTrees); }

// --- the snow, kept per save -------------------------------------------------------------------------------------------

std::filesystem::path store() { return paths::instance_dir() / L"dynamic_trees.ini"; }
std::wstring key_of(const std::string& save) {
    std::wstring k;
    for (char c : save) k += (isalnum(static_cast<unsigned char>(c)) ? static_cast<wchar_t>(c) : L'_');
    return k;
}
bool load_snow(const std::string& save, float& out) {
    wchar_t v[64];
    GetPrivateProfileStringW(L"snow", key_of(save).c_str(), L"", v, 64, store().c_str());
    if (!v[0]) return false;
    out = std::clamp(static_cast<float>(_wtof(v)), 0.0f, 100.0f);
    return true;
}
void save_snow() {
    if (g_save.empty() || !g_snow_known) return;
    wchar_t v[32];
    swprintf(v, 32, L"%.2f", g_snow);
    WritePrivateProfileStringW(L"snow", key_of(g_save).c_str(), v, store().c_str());
}

// --- the game ----------------------------------------------------------------------------------------------------------

int weather_now() {
    int32_t w = 4;
    if (Il2CppObject* weather = il2::invoke(api.get_weather, nullptr)) il2::invoke_value(api.weather_stage, weather, w);
    return w;
}
int wind_now() {
    Il2CppObject* wind = il2::invoke(api.get_wind, nullptr);
    return wind ? il2::read_field<int32_t>(wind, api.wind_strength) : 0;
}
bool indoors() {
    bool in = false;
    if (Il2CppObject* weather = il2::invoke(api.get_weather, nullptr)) il2::invoke_value(api.indoor, weather, in);
    return in;
}
float hours_played() {
    float h = -1;
    if (Il2CppObject* tod = il2::invoke(api.get_tod, nullptr)) il2::invoke_value(api.hours_played, tod, h);
    return h;
}

Il2CppObject* bundle() {
    if (Il2CppObject* b = target(g_bundle)) return b;
    if (g_bundle_tried) return nullptr;
    g_bundle_tried = true;
    const auto file = paths::mod_dir() / L"tldcoop_trees.bundle";
    if (!std::filesystem::exists(file)) {
        logx::warn("trees: %ls not found; Dynamic Trees has no textures (it comes in the TLDCoop_DLC zip)", file.c_str());
        return nullptr;
    }
    const std::u8string u8 = file.u8string();
    void* a[] = {il2::il2cpp_string_new(reinterpret_cast<const char*>(u8.c_str()))};
    Il2CppObject* b = il2::invoke(api.load_bundle, nullptr, a);
    if (!b) {
        logx::error("trees: could not load %ls", file.c_str());
        return nullptr;
    }
    g_bundle = hold(b);
    logx::info("trees: textures loaded");
    return b;
}

Il2CppObject* texture(const char* name) {
    if (auto it = g_textures.find(name); it != g_textures.end()) return target(it->second);
    Il2CppObject* b = bundle();
    if (!b) return nullptr;
    void* a[] = {il2::il2cpp_string_new(name), api.texture_type};
    Il2CppObject* t = il2::invoke(api.load_asset, b, a);
    g_textures[name] = hold(t);
    if (!t) logx::warn("trees: no texture %s in the bundle", name);
    return t;
}

int set_index(const char* tex_name, bool& pine) {
    for (int i = 0; i < 9; ++i) {
        if (!std::strcmp(tex_name, trees::kPine[i])) return pine = true, i;
        if (!std::strcmp(tex_name, trees::kCedar[i])) return pine = false, i;
    }
    return -1;
}

// Every material showing a pine's or a cedar's bark texture (TLD's or one of ours): instanced trees, Unity terrain
// trees' prefabs and placed trees alike share them.
void scan() {
    void* a[] = {api.material_type};
    Il2CppObject* all = il2::invoke(api.find_all, nullptr, a);
    int found = 0;
    for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
        Il2CppObject* m = il2::array_object_at(all, i);
        if (!alive(m) || g_mats.count(m)) continue;
        Il2CppObject* tex = il2::invoke(api.get_main, m);
        if (!tex) continue;
        bool pine = true;
        if (set_index(name_of(tex).c_str(), pine) < 0) continue;
        TreeMat t;
        t.material = hold(m);
        t.original = hold(tex);
        t.pine = pine;
        t.r = std::uniform_real_distribution<float>(0, 1)(g_rng);
        g_mats[m] = t;
        ++found;
    }
    if (found) logx::info("trees: %d tree materials found (%zu in all)", found, g_mats.size());
}

void refresh_terrains() {
    void* a[] = {api.terrain_type};
    Il2CppObject* terrains = api.terrain_type ? il2::invoke(api.find_objects, nullptr, a) : nullptr;
    for (size_t i = 0, n = il2::array_length(terrains); i < n; ++i)
        if (Il2CppObject* data = il2::invoke(api.terrain_data, il2::array_object_at(terrains, i)))
            il2::invoke(api.refresh, data);
}

void apply() {
    int changed = 0;
    for (auto& [ptr, t] : g_mats) {
        Il2CppObject* m = target(t.material);
        if (!alive(m)) continue;
        const int idx = trees::texture_index(g_snow, t.r);
        if (idx == t.shown) continue;
        Il2CppObject* tex = texture(t.pine ? trees::kPine[idx] : trees::kCedar[idx]);
        if (!tex) continue;
        void* a[] = {tex};
        il2::invoke(api.set_main, m, a);
        t.shown = idx;
        ++changed;
    }
    if (changed) {
        refresh_terrains();
        logx::info("trees: snow %.1f (state %d): %d materials changed", g_snow, trees::state_of(g_snow), changed);
    }
    g_applied = true;
}

void restore() {
    for (auto& [ptr, t] : g_mats) {
        Il2CppObject* m = target(t.material);
        Il2CppObject* tex = target(t.original);
        if (alive(m) && tex && t.shown >= 0) {
            void* a[] = {tex};
            il2::invoke(api.set_main, m, a);
        }
        t.shown = -1;
    }
    refresh_terrains();
    g_applied = false;
    logx::info("trees: TLD's own tree textures back");
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* resources = il2::find_class(kCore, "UnityEngine", "Resources");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* texture_c = il2::find_class(kCore, "UnityEngine", "Texture");
    Il2CppClass* bundle_c = il2::find_class("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
    Il2CppClass* terrain = il2::find_class("UnityEngine.TerrainModule.dll", "UnityEngine", "Terrain");
    Il2CppClass* terrain_data = il2::find_class("UnityEngine.TerrainModule.dll", "UnityEngine", "TerrainData");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* tod = il2::find_class(kGame, "", "TimeOfDay");
    Il2CppClass* weather = il2::find_class(kGame, "", "Weather");
    Il2CppClass* wind = il2::find_class(kGame, "", "Wind");
    if (!object || !resources || !material || !texture_c || !bundle_c || !gm || !tod || !weather || !wind) return false;
    api.material_type = il2::type_object(material);
    api.texture_type = il2::type_object(texture_c);
    api.terrain_type = terrain ? il2::type_object(terrain) : nullptr;
    api.find_all = il2::find_method_params(resources, "FindObjectsOfTypeAll", {"System.Type"});
    api.find_objects = il2::find_method_params(object, "FindObjectsOfType", {"System.Type"});
    api.object_name = il2::find_method(object, "get_name", 0);
    api.get_main = il2::find_method(material, "get_mainTexture", 0);
    api.set_main = il2::find_method(material, "set_mainTexture", 1);
    api.load_bundle = il2::find_method_params(bundle_c, "LoadFromFile", {"System.String"});
    api.load_asset = il2::find_method_params(bundle_c, "LoadAsset", {"System.String", "System.Type"});
    if (terrain) api.terrain_data = il2::find_method(terrain, "get_terrainData", 0);
    if (terrain_data) api.refresh = il2::find_method(terrain_data, "RefreshPrototypes", 0);
    api.get_tod = il2::find_method(gm, "GetTimeOfDayComponent", 0);
    api.hours_played = il2::find_method(tod, "GetHoursPlayedNotPaused", 0);
    api.get_weather = il2::find_method(gm, "GetWeatherComponent", 0);
    api.weather_stage = il2::find_method(weather, "GetWeatherStage", 0);
    api.indoor = il2::find_method(weather, "IsIndoorScene", 0);
    api.get_wind = il2::find_method(gm, "GetWindComponent", 0);
    api.wind_strength = il2::field_offset(wind, "m_CurrentStrength");
    return api.find_all && api.find_objects && api.object_name && api.get_main && api.set_main && api.load_bundle &&
           api.load_asset && api.get_tod && api.hours_played && api.get_weather && api.weather_stage && api.indoor &&
           api.get_wind && api.wind_strength >= 0;
}

}  // namespace

void init() {
    api.ok = resolve();
    logx::info("trees: Dynamic Trees %s", api.ok ? "ready" : "unavailable (TLD's calls missing)");
}

void set_settings(const config::Settings& s) {
    coop_features::set_own(net::kFeatureDynamicTrees, s.dynamic_trees);
    logx::info("trees: %s", s.dynamic_trees ? "on" : "off");
}

void on_frame(const std::string& scene, bool in_game) {
    if (!api.ok) return;
    const double now = now_ms();
    if (scene != g_scene) {
        if (g_snow_known) save_snow();
        g_scene = scene;
        g_scene_since = now;
        g_scans_done = 0;
        g_last_hours = -1;
        // materials of the scene left behind are gone; TLD's shared ones stay
        for (auto it = g_mats.begin(); it != g_mats.end();)
            if (!alive(target(it->second.material))) {
                il2::il2cpp_gchandle_free(it->second.material);
                il2::il2cpp_gchandle_free(it->second.original);
                it = g_mats.erase(it);
            } else {
                ++it;
            }
    }
    if (!in_game) return;
    if (!active()) {
        if (g_applied) restore();
        return;
    }

    // The snow: the host's for a friend; else ours, carried on from the save.
    const float host = coop_features::host_tree_snow();
    if (host >= 0) {
        g_snow = host;
        g_snow_known = true;
    } else {
        const std::string save = game::save_id();
        if (!save.empty() && save != g_save) {
            if (g_snow_known && !g_save.empty()) save_snow();
            g_save = save;
            float kept = 0;
            g_snow = load_snow(save, kept) ? kept : trees::starting(weather_now());
            g_snow_known = true;
            g_last_hours = -1;
            logx::info("trees: this save's trees carry snow %.1f", g_snow);
        }
        const float h = hours_played();
        if (g_snow_known && h >= 0) {
            if (g_last_hours >= 0 && h > g_last_hours && h - g_last_hours < 48)
                g_snow = trees::accumulate(g_snow, weather_now(), wind_now(), h - g_last_hours);
            g_last_hours = h;
        }
        coop_features::set_tree_snow(g_snow_known ? g_snow : -1);
        if (now - g_last_save > kSaveMs) {
            g_last_save = now;
            save_snow();
        }
    }
    if (!g_snow_known) return;

    // The textures: on a scene's arrival (twice: late materials) and whenever the snow crosses a threshold, outdoors.
    bool rescan = false;
    while (g_scans_done < 2 && now - g_scene_since >= kScanAt[g_scans_done]) {
        ++g_scans_done;
        rescan = true;
    }
    if (rescan) scan();
    const int state = trees::state_of(g_snow);
    if ((rescan || state != g_state || !g_applied) && !indoors()) {
        g_state = state;
        apply();
    }
}

}  // namespace dynamic_trees
