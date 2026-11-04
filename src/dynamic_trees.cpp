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
#include <unordered_set>
#include <vector>

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
constexpr size_t kPerFrame = 300;  // TLDCoop_DLC: materials looked at a frame (the whole list at once was a 35-77 ms frame)

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
    const MethodInfo* tree_protos = nullptr;    // TerrainData.get_treePrototypes()
    const MethodInfo* proto_prefab = nullptr;   // TreePrototype.get_prefab()
    const MethodInfo* children = nullptr;       // GameObject.GetComponentsInChildren(Type, bool)
    const MethodInfo* shared_mats = nullptr;    // Renderer.get_sharedMaterials()
    Il2CppObject* renderer_type = nullptr;
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
    bool terrain = false;  // TLDCoop_DLC: one of a terrain's tree prototypes' (the terrain is refreshed for it)
};
std::unordered_map<Il2CppObject*, TreeMat> g_mats;
// TLDCoop_DLC: the scan spread over frames (FindObjectsOfTypeAll's array, held), what it has seen in this scene, and
// the scene's terrains with tree prototypes.
uintptr_t g_sweep = 0;
size_t g_sweep_at = 0;
int g_found = 0;
std::unordered_set<void*> g_seen;           // materials (native) looked at in this scene
std::unordered_map<void*, int> g_tex_kind;  // texture (native) -> 0..8 pine, 9..17 cedar, -1 none
std::vector<uintptr_t> g_terrains;
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
// trees' prefabs and placed trees alike share them. TLDCoop_DLC: a few hundred a frame, each material asked for its
// texture once a scene, each texture for its name once.
void end_sweep() {
    if (g_sweep) il2::il2cpp_gchandle_free(g_sweep);
    g_sweep = 0;
    g_sweep_at = 0;
}

void start_sweep() {
    end_sweep();
    g_found = 0;
    void* a[] = {api.material_type};
    Il2CppObject* all = il2::invoke(api.find_all, nullptr, a);
    if (all) g_sweep = hold(all);
}

// True once the scan is through.
bool sweep_step() {
    Il2CppObject* all = target(g_sweep);
    const size_t n = il2::array_length(all);
    const size_t stop = std::min(n, g_sweep_at + kPerFrame);
    for (; g_sweep_at < stop; ++g_sweep_at) {
        Il2CppObject* m = il2::array_object_at(all, g_sweep_at);
        if (!alive(m) || g_mats.count(m) || !g_seen.insert(il2::read_field<void*>(m, kCachedPtr)).second) continue;
        Il2CppObject* tex = il2::invoke(api.get_main, m);
        if (!alive(tex)) continue;
        void* native = il2::read_field<void*>(tex, kCachedPtr);
        auto kind = g_tex_kind.find(native);
        if (kind == g_tex_kind.end()) {
            bool pine = true;
            const int i = set_index(name_of(tex).c_str(), pine);
            kind = g_tex_kind.emplace(native, i < 0 ? -1 : (pine ? i : 9 + i)).first;
        }
        if (kind->second < 0) continue;
        TreeMat t;
        t.material = hold(m);
        t.original = hold(tex);
        t.pine = kind->second < 9;
        t.r = std::uniform_real_distribution<float>(0, 1)(g_rng);
        g_mats[m] = t;
        ++g_found;
    }
    if (g_sweep_at < n) return false;
    end_sweep();
    if (g_found) logx::info("trees: %d tree materials found (%zu in all)", g_found, g_mats.size());
    return true;
}

// TLDCoop_DLC: the scene's terrains whose tree prototypes use one of the tree materials (only those are refreshed, and
// only when one of their materials changed).
void survey_terrains() {
    for (uintptr_t h : g_terrains) il2::il2cpp_gchandle_free(h);
    g_terrains.clear();
    if (!api.terrain_type || !api.terrain_data || !api.refresh || !api.tree_protos || !api.proto_prefab ||
        !api.children || !api.shared_mats || !api.renderer_type)
        return;
    void* a[] = {api.terrain_type};
    Il2CppObject* terrains = il2::invoke(api.find_objects, nullptr, a);
    for (size_t i = 0, n = il2::array_length(terrains); i < n; ++i) {
        Il2CppObject* terrain = il2::array_object_at(terrains, i);
        Il2CppObject* data = alive(terrain) ? il2::invoke(api.terrain_data, terrain) : nullptr;
        Il2CppObject* protos = data ? il2::invoke(api.tree_protos, data) : nullptr;
        bool ours = false;
        for (size_t p = 0, np = il2::array_length(protos); p < np; ++p) {
            Il2CppObject* proto = il2::array_object_at(protos, p);
            Il2CppObject* prefab = proto ? il2::invoke(api.proto_prefab, proto) : nullptr;
            if (!alive(prefab)) continue;
            bool include_inactive = true;
            void* ca[] = {api.renderer_type, &include_inactive};
            Il2CppObject* renderers = il2::invoke(api.children, prefab, ca);
            for (size_t r = 0, nr = il2::array_length(renderers); r < nr; ++r) {
                Il2CppObject* rend = il2::array_object_at(renderers, r);
                Il2CppObject* mats = alive(rend) ? il2::invoke(api.shared_mats, rend) : nullptr;
                for (size_t k = 0, nk = il2::array_length(mats); k < nk; ++k)
                    if (auto it = g_mats.find(il2::array_object_at(mats, k)); it != g_mats.end()) {
                        it->second.terrain = true;
                        ours = true;
                    }
            }
        }
        if (ours) g_terrains.push_back(hold(terrain));
    }
    if (!g_terrains.empty()) logx::info("trees: %zu terrain(s) with tree prototypes of ours", g_terrains.size());
}

void refresh_terrains() {
    for (uintptr_t h : g_terrains) {
        Il2CppObject* terrain = target(h);
        if (Il2CppObject* data = alive(terrain) ? il2::invoke(api.terrain_data, terrain) : nullptr)
            il2::invoke(api.refresh, data);
    }
}

void apply() {
    int changed = 0;
    bool terrain = false;
    for (auto& [ptr, t] : g_mats) {
        Il2CppObject* m = target(t.material);
        if (!alive(m)) continue;
        const int idx = trees::shown_index(g_snow, t.r);  // TLDCoop_DLC: capped (Karol: too white)
        if (idx == t.shown) continue;
        Il2CppObject* tex = texture(t.pine ? trees::kPine[idx] : trees::kCedar[idx]);
        if (!tex) continue;
        void* a[] = {tex};
        il2::invoke(api.set_main, m, a);
        t.shown = idx;
        terrain |= t.terrain;
        ++changed;
    }
    if (changed) {
        if (terrain) refresh_terrains();
        logx::info("trees: snow %.1f (state %d): %d materials changed", g_snow, trees::state_of(g_snow), changed);
    }
    g_applied = true;
}

void restore() {
    bool terrain = false;
    for (auto& [ptr, t] : g_mats) {
        Il2CppObject* m = target(t.material);
        Il2CppObject* tex = target(t.original);
        if (alive(m) && tex && t.shown >= 0) {
            void* a[] = {tex};
            il2::invoke(api.set_main, m, a);
            terrain |= t.terrain;
        }
        t.shown = -1;
    }
    if (terrain) refresh_terrains();
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
    if (terrain_data) api.tree_protos = il2::find_method(terrain_data, "get_treePrototypes", 0);
    if (Il2CppClass* proto = il2::find_class("UnityEngine.TerrainModule.dll", "UnityEngine", "TreePrototype"))
        api.proto_prefab = il2::find_method(proto, "get_prefab", 0);
    if (Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject"))
        api.children = il2::find_method_params(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    if (Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer")) {
        api.renderer_type = il2::type_object(renderer);
        api.shared_mats = il2::find_method(renderer, "get_sharedMaterials", 0);
    }
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
        end_sweep();
        g_seen.clear();  // native pointers of the scene left behind can come back as others
        g_tex_kind.clear();
        for (uintptr_t h : g_terrains) il2::il2cpp_gchandle_free(h);
        g_terrains.clear();
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
    // TLDCoop_DLC: the trees' textures loaded at the main menu (a few seconds in), not when a game's first scene comes
    // in: that took 1-2 s in one frame there (the log's slow frames), the game standing still. Whatever our own setting
    // says: a host's may turn the trees on for us.
    if (!in_game) {
        static double menu_since = 0;
        static bool preloaded = false;
        if (scene != "MainMenu") {
            menu_since = 0;
            return;
        }
        if (!menu_since) menu_since = now;
        if (!preloaded && now - menu_since > 8000) {
            preloaded = true;
            const double t0 = now_ms();
            int n = 0;
            for (int i = 0; i < 8; ++i) n += (texture(trees::kPine[i]) != nullptr) + (texture(trees::kCedar[i]) != nullptr);
            logx::info("trees: %d textures loaded at the main menu (%.0f ms)", n, now_ms() - t0);
        }
        return;
    }
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
    if (rescan) start_sweep();
    rescan = false;
    if (g_sweep && sweep_step()) {
        survey_terrains();
        rescan = true;
    }
    // TLDCoop_DLC: once a minute, what one tree material really shows (Karol: trees drawn flat cream, no texture)
    static double probe_at = 0;
    if (now - probe_at > 60000 && !g_mats.empty()) {
        probe_at = now;
        auto& [ptr, t] = *g_mats.begin();
        Il2CppObject* m = target(t.material);
        Il2CppObject* tex = alive(m) ? il2::invoke(api.get_main, m) : nullptr;
        logx::info("trees: a tree material shows '%s' (%s; ours #%d, snow %.1f, %s)", alive(tex) ? name_of(tex).c_str() : "-",
                   alive(m) ? "material alive" : "material gone", t.shown, g_snow, indoors() ? "indoors" : "outdoors");
    }
    const int state = trees::state_of(g_snow);
    if ((rescan || state != g_state || !g_applied) && !indoors()) {
        g_state = state;
        apply();
    }
}

}  // namespace dynamic_trees
