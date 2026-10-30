#include "ambient_lights.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ambient_math.h"
#include "coop_features.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"

namespace ambient_lights {
namespace {

using ambient::Rgb;

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;   // List<T>._items, _size
constexpr ptrdiff_t kArrayFirst = 0x20;                    // T[]: the first element
constexpr ptrdiff_t kCachedPtr = 0x10;                     // UnityEngine.Object.m_CachedPtr: null once destroyed
constexpr size_t kLightDefSize = 40;                        // DarkLightingManager.LightDef: Light, float, Color, Vector3
constexpr double kSettleMs = 4000;   // no InteriorLightingManager by then (dark interiors): set up anyway
constexpr double kLookMs = 100;      // how often to look for the scene's InteriorLightingManager until it's there
constexpr int kPointLight = 2, kSpotLight = 0;              // UnityEngine.LightType

struct Vector3 {
    float x, y, z;
};

struct Api {
    bool ok = false;
    // UnityEngine
    Il2CppObject* light_type = nullptr;      // System.Type of Light
    Il2CppObject* renderer_type = nullptr;
    Il2CppObject* ilm_type = nullptr;        // InteriorLightingManager
    Il2CppClass* go_class = nullptr;
    const MethodInfo* find_objects = nullptr;  // static Object.FindObjectsOfType(Type)
    const MethodInfo* destroy = nullptr;       // static Object.Destroy(Object)
    const MethodInfo* object_name = nullptr;   // Object.get_name()
    const MethodInfo* go_ctor = nullptr;       // GameObject(string)
    const MethodInfo* add_component = nullptr; // GameObject.AddComponent(Type)
    const MethodInfo* go_transform = nullptr;  // GameObject.get_transform()
    const MethodInfo* component_go = nullptr;  // Component.get_gameObject()
    const MethodInfo* set_position = nullptr;  // Transform.set_position(Vector3)
    const MethodInfo* set_enabled = nullptr;   // Behaviour.set_enabled(bool)
    const MethodInfo* light_get_type = nullptr, *light_get_color = nullptr, *light_set_color = nullptr,
                      *light_get_intensity = nullptr, *light_set_intensity = nullptr, *light_set_range = nullptr,
                      *light_get_cookie = nullptr;
    const MethodInfo* get_materials = nullptr;  // Renderer.get_materials()
    const MethodInfo* mat_get_color = nullptr, *mat_set_color = nullptr, *mat_has = nullptr, *mat_get_float = nullptr,
                      *mat_set_float = nullptr;  // by property id
    int emissive_id = -1;                        // Shader.PropertyToID("_EmissiveStrength")
    // TLD
    FieldInfo* paused = nullptr;                 // static GameManager.m_IsPaused
    const MethodInfo* get_unistorm = nullptr, *get_aurora = nullptr, *get_weather = nullptr, *get_wind = nullptr;
    const MethodInfo* tod_state = nullptr;       // UniStormWeatherSystem.GetActiveTODState()
    const MethodInfo* blend_state = nullptr;     // GetTODBlendState()
    const MethodInfo* blend_pct = nullptr;       // GetTODBlendPercent(TODBlendState)
    ptrdiff_t prev_stage = -1, since_change = -1, transition = -1;  // m_PreviousWeatherStage, m_SecondsSince..., m_WeatherTransitionTime
    ptrdiff_t sun = -1, fog = -1;                // TODStateData.m_SunLight, m_FogColor
    const MethodInfo* weather_stage = nullptr;   // Weather.GetWeatherStage()
    const MethodInfo* wind_mph = nullptr;        // Wind.GetSpeedMPH()
    const MethodInfo* aurora_alpha = nullptr, *aurora_colour = nullptr, *aurora_cinematic = nullptr;
    ptrdiff_t aurora_active = -1, aurora_full = -1;  // m_NormalizedActive, m_FullyActiveValue
    ptrdiff_t ilm_initialized = -1;              // InteriorLightingManager.m_Initialized
    ptrdiff_t ilm_groups = -1, ilm_loose = -1, ilm_loose_midday = -1;  // InteriorLightingManager lists
    ptrdiff_t group_lights = -1, group_glow_mats = -1;  // InteriorLightingGroup.m_Lights, m_GlowObjectMaterialsToUpdate
    ptrdiff_t amb_day = -1, amb_night = -1;      // TodAmbientLight.m_AmbientIndoorsDay / Night
    ptrdiff_t gimble_light = -1, shaft_light = -1;  // LightShaftGimble.m_Light, LightShaftTod.m_Light
    ptrdiff_t dark_constant = -1, dark_coloured = -1, dark_nontod = -1;  // DarkLightingManager lists of LightDef
} api;

// --- state ---------------------------------------------------------------------------------------------------------

ambient::Levels g_levels;

std::string g_scene;
double g_scene_since = 0;
double g_last_look = 0;
const ambient::SceneLights* g_cfg = nullptr;  // this scene's emitters; null: the game's own lighting
bool g_set_up = false;
bool g_ready = false;           // lights set up and a light set computed: the hooks act
ambient::LightSet g_set;
int g_blend_state = 6;
float g_blend_pct = 1;

struct OurLight {
    uintptr_t go = 0, light = 0;
    const ambient::Emitter* emitter = nullptr;
};
std::vector<OurLight> g_lights;
struct FillLight {
    uintptr_t light = 0;
    Rgb original;
};
std::vector<FillLight> g_fill;
struct Window {
    uintptr_t material = 0;
    Rgb colour;
    float emissive = 0;
    bool has_emissive = false;
    bool tld_updates = false;  // TLD sets its glow every frame (an InteriorLightingGroup's): ours goes on top
};
std::vector<Window> g_windows;
struct AmbientDefaults {
    Rgb day, night;
};
std::unordered_map<Il2CppObject*, AmbientDefaults> g_ambient;  // TLD's indoor ambient light, as it was

// The mod's cloud flicker (LightConfig.GetFlickeringMod).
struct Flicker {
    bool change = true;
    float chance = 0, position = -1, start = 0, target = 0, inc = 0;
    std::mt19937 rng{std::random_device{}()};
    float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }
} g_flick;

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
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, const T& v) {
    if (o && off >= 0) std::memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}
std::string name_of(Il2CppObject* o) {
    Il2CppObject* s = o ? il2::invoke(api.object_name, o) : nullptr;
    return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}
// List<T> of objects: each element.
template <typename F>
void each(Il2CppObject* list, F&& f) {
    Il2CppObject* items = il2::read_field<Il2CppObject*>(list, kListItems);
    const int32_t n = il2::read_field<int32_t>(list, kListSize);
    if (!items || n <= 0) return;
    const size_t len = il2::array_length(items);
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < len; ++i)
        if (Il2CppObject* o = il2::array_object_at(items, static_cast<size_t>(i))) f(o);
}
// List<DarkLightingManager.LightDef>: each LightDef's m_Light.
template <typename F>
void each_light_def(Il2CppObject* list, F&& f) {
    Il2CppObject* items = il2::read_field<Il2CppObject*>(list, kListItems);
    const int32_t n = il2::read_field<int32_t>(list, kListSize);
    if (!items || n <= 0) return;
    const size_t len = il2::array_length(items);
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < len; ++i) {
        Il2CppObject* light = nullptr;
        std::memcpy(&light, reinterpret_cast<const char*>(items) + kArrayFirst + i * kLightDefSize, sizeof(light));
        if (alive(light)) f(light);
    }
}

float light_intensity(Il2CppObject* l) {
    float v = 0;
    il2::invoke_value(api.light_get_intensity, l, v);
    return v;
}
void set_intensity(Il2CppObject* l, float v) {
    void* a[] = {&v};
    il2::invoke(api.light_set_intensity, l, a);
}
Rgb light_colour(Il2CppObject* l) {
    Rgb c;
    il2::invoke_value(api.light_get_color, l, c);
    return c;
}
void set_light_colour(Il2CppObject* l, Rgb c) {
    void* a[] = {&c};
    il2::invoke(api.light_set_color, l, a);
}

bool paused() {
    bool p = false;
    if (api.paused) il2::il2cpp_field_static_get_value(api.paused, &p);
    return p;
}

bool active() { return coop_features::on(net::kFeatureAmbientLights); }

bool acting() { return g_ready && active() && !paused(); }

// --- the game's time, weather and colours ----------------------------------------------------------------------------

float flicker(const char* weather, float wind_mph) {
    if (!std::strcmp(weather, "clear") || !std::strcmp(weather, "aurora") || !std::strcmp(weather, "lightfog") ||
        !std::strcmp(weather, "densefog"))
        return 0;
    Flicker& f = g_flick;
    if (f.change) {
        f.change = false;
        f.position = 0;
        f.start = f.target;
        f.inc = wind_mph / 2000.0f;
        if (!std::strcmp(weather, "partlycloudy")) {
            f.target = f.target <= 0 ? f.rnd(0.5f, 1) * 0.4f : f.rnd(0.5f, 1) * -0.2f;
            f.inc *= f.rnd(0.05f, 0.8f);
            f.chance = 0.0005f;
        } else if (!std::strcmp(weather, "cloudy") || !std::strcmp(weather, "lightsnow") ||
                   !std::strcmp(weather, "heavysnow")) {
            f.target = f.target <= 0 ? f.rnd(0.5f, 1) * 0.1f : f.rnd(0.5f, 1) * -0.1f;
            f.inc *= f.rnd(0.5f, 1);
            f.chance = 0.002f;
        } else if (!std::strcmp(weather, "blizzard")) {
            f.target = f.target <= 0 ? f.rnd(0.5f, 1) * 0.04f : f.rnd(0.7f, 1) * -0.04f;
            f.inc *= f.rnd(0.3f, 2);
            f.chance = 0.75f;
        }
    }
    if (f.position < 1) {
        const float v = ambient::lerp(f.start, f.target, f.position);
        f.position += f.inc;
        return v;
    }
    if (f.rnd(0, 1) < f.chance) f.change = true;
    return f.target;
}

bool read_now(ambient::Now& n) {
    Il2CppObject* storm = il2::invoke(api.get_unistorm, nullptr);
    Il2CppObject* weather = il2::invoke(api.get_weather, nullptr);
    if (!storm || !weather) return false;
    Il2CppObject* state = il2::invoke(api.tod_state, storm);
    if (!state) return false;
    il2::invoke_value(api.blend_state, storm, n.blend_state);
    int32_t bs = n.blend_state;
    void* a[] = {&bs};
    il2::invoke_value(api.blend_pct, storm, n.blend_pct, a);
    n.sun = il2::read_field<Rgb>(state, api.sun);
    n.fog = il2::read_field<Rgb>(state, api.fog);
    il2::invoke_value(api.weather_stage, weather, n.weather);
    n.previous_weather = il2::read_field<int32_t>(storm, api.prev_stage);
    const float t = il2::read_field<float>(storm, api.transition), e = il2::read_field<float>(storm, api.since_change);
    n.weather_pct = t > 0 ? std::clamp(e / t, 0.0f, 1.0f) : 1.0f;
    if (Il2CppObject* aurora = il2::invoke(api.get_aurora, nullptr)) {
        il2::invoke_value(api.aurora_alpha, aurora, n.aurora_fade);
        if (std::fabs(n.aurora_fade) > 0.0001f) {
            il2::invoke_value(api.aurora_colour, aurora, n.aurora_colour);
            il2::invoke_value(api.aurora_cinematic, aurora, n.aurora_cinematic);
            const float full = il2::read_field<float>(aurora, api.aurora_full);
            n.aurora_level = full > 0 ? std::clamp(il2::read_field<float>(aurora, api.aurora_active) / full, 0.0f, 1.0f) : 0;
        }
    }
    float mph = 0;
    if (Il2CppObject* wind = il2::invoke(api.get_wind, nullptr)) il2::invoke_value(api.wind_mph, wind, mph);
    n.flicker = flicker(ambient::weather_of_stage(n.weather), mph);
    return true;
}

// --- the scene's lights ------------------------------------------------------------------------------------------------

Il2CppObject* find_all(Il2CppObject* type) {
    void* a[] = {type};
    return type ? il2::invoke(api.find_objects, nullptr, a) : nullptr;
}

void add_our_lights() {
    for (int i = 0; i < g_cfg->count; ++i) {
        const ambient::Emitter& e = ambient::kEmitters[g_cfg->first + i];
        Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
        static const char* kNames[] = {"XPZ_Light_north", "XPZ_Light_south", "XPZ_Light_east", "XPZ_Light_west",
                                       "XPZ_Light_"};
        void* name[] = {il2::il2cpp_string_new(kNames[e.orientation >= 0 && e.orientation <= 4 ? e.orientation : 4])};
        il2::invoke(api.go_ctor, go, name);
        void* type[] = {api.light_type};
        Il2CppObject* light = il2::invoke(api.add_component, go, type);
        if (!light) {
            void* d[] = {go};
            il2::invoke(api.destroy, nullptr, d);
            continue;
        }
        Vector3 p{e.x, e.y, e.z};
        void* pa[] = {&p};
        il2::invoke(api.set_position, il2::invoke(api.go_transform, go), pa);
        set_intensity(light, 0);
        bool off = false;
        void* ea[] = {&off};
        il2::invoke(api.set_enabled, light, ea);
        g_lights.push_back({hold(go), hold(light), &e});
    }
}

// TLD's lights the mod tones (GameLights.AddGameLights): every point light, and spot lights but the windows' shafts,
// that isn't one of an InteriorLightingGroup's window lights (TLD drives those itself) or ours.
void find_game_lights(Il2CppObject* manager) {
    std::unordered_set<Il2CppObject*> group_lights;
    std::unordered_set<Il2CppObject*> tld_glow;
    if (manager)
        each(il2::read_field<Il2CppObject*>(manager, api.ilm_groups), [&](Il2CppObject* group) {
            each(il2::read_field<Il2CppObject*>(group, api.group_lights), [&](Il2CppObject* l) { group_lights.insert(l); });
            each(il2::read_field<Il2CppObject*>(group, api.group_glow_mats), [&](Il2CppObject* m) { tld_glow.insert(m); });
        });
    std::unordered_set<Il2CppObject*> ours;
    for (const OurLight& l : g_lights) ours.insert(target(l.light));

    Il2CppObject* lights = find_all(api.light_type);
    for (size_t i = 0, n = il2::array_length(lights); i < n; ++i) {
        Il2CppObject* l = il2::array_object_at(lights, i);
        if (!alive(l) || ours.count(l) || group_lights.count(l)) continue;
        int32_t type = -1;
        il2::invoke_value(api.light_get_type, l, type);
        if (type == kSpotLight) {
            Il2CppObject* cookie = il2::invoke(api.light_get_cookie, l);
            if (cookie && name_of(cookie).find("Window") != std::string::npos) continue;  // a window's shaft
        } else if (type != kPointLight) {
            continue;
        }
        g_fill.push_back({hold(l), light_colour(l)});
    }

    // Windows: renderers with a glowing material (not a truck's, car's or lamp's).
    Il2CppObject* renderers = find_all(api.renderer_type);
    for (size_t i = 0, n = il2::array_length(renderers); i < n; ++i) {
        Il2CppObject* r = il2::array_object_at(renderers, i);
        if (!alive(r)) continue;
        const std::string go_name = lower(name_of(il2::invoke(api.component_go, r)));
        if (go_name.find("truck") != std::string::npos || go_name.find("car") != std::string::npos ||
            go_name.find("lamp") != std::string::npos)
            continue;
        Il2CppObject* mats = il2::invoke(api.get_materials, r);
        const size_t mn = il2::array_length(mats);
        bool glow = false;
        for (size_t k = 0; k < mn && !glow; ++k)
            glow = lower(name_of(il2::array_object_at(mats, k))).find("glow") != std::string::npos;
        if (!glow) continue;
        for (size_t k = 0; k < mn; ++k) {
            Il2CppObject* m = il2::array_object_at(mats, k);
            if (!alive(m)) continue;
            Window w;
            w.material = hold(m);
            il2::invoke_value(api.mat_get_color, m, w.colour);
            int id = api.emissive_id;
            void* ia[] = {&id};
            il2::invoke_value(api.mat_has, m, w.has_emissive, ia);
            if (w.has_emissive) il2::invoke_value(api.mat_get_float, m, w.emissive, ia);
            w.tld_updates = tld_glow.count(m) != 0;
            g_windows.push_back(w);
        }
    }
}

// The MelonLoader mod itself, if a player still has it: its lights are named XPZ_Light_<orientation>.
bool melon_mod_here() {
    Il2CppObject* lights = find_all(api.light_type);
    for (size_t i = 0, n = il2::array_length(lights); i < n; ++i) {
        Il2CppObject* l = il2::array_object_at(lights, i);
        if (alive(l) && name_of(il2::invoke(api.component_go, l)).rfind("XPZ_Light_", 0) == 0) return true;
    }
    return false;
}

void set_up(const std::string& scene) {
    g_cfg = ambient::find_scene(scene.c_str());
    g_set_up = true;
    if (!g_cfg) {
        logx::info("ambient: no light data for %s; TLD's own lighting", scene.c_str());
        return;
    }
    if (melon_mod_here()) {
        static bool told = false;
        if (!told) logx::warn("ambient: the MelonLoader mod AmbientLights is installed too; it lights this, not us "
                              "(remove it from Mods to use ours and the host's ON / OFF)");
        told = true;
        g_cfg = nullptr;
        return;
    }
    Il2CppObject* managers = find_all(api.ilm_type);
    Il2CppObject* manager = il2::array_length(managers) ? il2::array_object_at(managers, 0) : nullptr;
    add_our_lights();
    find_game_lights(manager);
    logx::info("ambient: %s: %zu window lights, %zu of TLD's lights toned, %zu window materials", scene.c_str(),
               g_lights.size(), g_fill.size(), g_windows.size());
}

void restore_and_drop() {
    for (const FillLight& f : g_fill)
        if (Il2CppObject* l = target(f.light); alive(l)) set_light_colour(l, f.original);
    for (const Window& w : g_windows)
        if (Il2CppObject* m = target(w.material); alive(m)) {
            Rgb c = w.colour;
            void* ca[] = {&c};
            il2::invoke(api.mat_set_color, m, ca);
            if (w.has_emissive && !w.tld_updates) {
                int id = api.emissive_id;
                float v = w.emissive;
                void* fa[] = {&id, &v};
                il2::invoke(api.mat_set_float, m, fa);
            }
        }
    for (OurLight& l : g_lights) {
        if (Il2CppObject* go = target(l.go); alive(go)) {
            void* d[] = {go};
            il2::invoke(api.destroy, nullptr, d);
        }
        il2::il2cpp_gchandle_free(l.go);
        il2::il2cpp_gchandle_free(l.light);
    }
    for (const FillLight& f : g_fill) il2::il2cpp_gchandle_free(f.light);
    for (const Window& w : g_windows) il2::il2cpp_gchandle_free(w.material);
    g_lights.clear();
    g_fill.clear();
    g_windows.clear();
    g_ambient.clear();
    g_cfg = nullptr;
    g_set_up = false;
    g_ready = false;
}

void update_our_lights() {
    for (const OurLight& l : g_lights) {
        Il2CppObject* light = target(l.light);
        if (!alive(light)) continue;
        const ambient::WindowLight w = ambient::window_light(g_set, g_levels, *g_cfg, *l.emitter, g_blend_state, g_blend_pct);
        bool on = w.on;
        void* ea[] = {&on};
        il2::invoke(api.set_enabled, light, ea);
        set_intensity(light, w.intensity);
        float r = w.range;
        void* ra[] = {&r};
        il2::invoke(api.light_set_range, light, ra);
        set_light_colour(light, w.colour);
    }
}

// --- TLD's own lights, toned where TLD updates them ----------------------------------------------------------------

void tone_fill_and_windows() {
    for (const FillLight& f : g_fill)
        if (Il2CppObject* l = target(f.light); alive(l)) set_light_colour(l, ambient::fill_colour(f.original, g_levels));
    const Rgb wc = g_set.window;
    for (const Window& w : g_windows) {
        Il2CppObject* m = target(w.material);
        if (!alive(m)) continue;
        Rgb c = wc;
        void* ca[] = {&c};
        il2::invoke(api.mat_set_color, m, ca);
        if (!w.has_emissive) continue;
        int id = api.emissive_id;
        float v = w.emissive;
        void* ia[] = {&id};
        if (w.tld_updates) il2::invoke_value(api.mat_get_float, m, v, ia);  // TLD's value of this frame
        v *= g_set.window_mod;
        void* fa[] = {&id, &v};
        il2::invoke(api.mat_set_float, m, fa);
    }
}

using VoidFn = void (*)(Il2CppObject*, const MethodInfo*);
using AmbientFn = void (*)(Il2CppObject*, float, float, const MethodInfo*);
using ShaftFn = void (*)(Il2CppObject*, float, bool, const MethodInfo*);
using FloatFn = void (*)(Il2CppObject*, float, const MethodInfo*);
AmbientFn g_set_ambient = nullptr;
VoidFn g_ilm_update = nullptr, g_shaft_tod_update = nullptr, g_dark_constant = nullptr, g_dark_nontod = nullptr,
       g_dark_coloured = nullptr;
ShaftFn g_ilm_shaft = nullptr;
FloatFn g_gimble_light = nullptr;

// GameLights.UpdateAmbience: TLD's indoor ambient light, quieter and in the light set's colours.
void set_ambient_detour(Il2CppObject* self, float tod, float multiplier, const MethodInfo* method) {
    if (self && api.amb_day >= 0) {
        if (acting() && g_cfg) {
            if (!g_ambient.count(self))
                g_ambient[self] = {il2::read_field<Rgb>(self, api.amb_day), il2::read_field<Rgb>(self, api.amb_night)};
            multiplier *= ambient::ambience_multiplier(g_set, g_levels, *g_cfg, g_blend_state, g_blend_pct);
            put(self, api.amb_day, g_set.ambient_day);
            put(self, api.amb_night, g_set.ambient_night);
        } else if (auto it = g_ambient.find(self); it != g_ambient.end()) {
            put(self, api.amb_day, it->second.day);
            put(self, api.amb_night, it->second.night);
            g_ambient.erase(it);
        }
    }
    g_set_ambient(self, tod, multiplier, method);
}

void ilm_update_detour(Il2CppObject* self, const MethodInfo* method) {
    g_ilm_update(self, method);
    if (acting() && g_cfg) tone_fill_and_windows();
}

// Light shafts by the weather (the mod's lightshaftStr: none in fog or snow).
void ilm_shaft_detour(Il2CppObject* self, float tod, bool follow, const MethodInfo* method) {
    if (acting() && g_cfg) tod *= g_set.lightshaft;
    g_ilm_shaft(self, tod, follow, method);
}
void scale_light(Il2CppObject* owner, ptrdiff_t field) {
    Il2CppObject* l = il2::read_field<Il2CppObject*>(owner, field);
    if (alive(l)) set_intensity(l, light_intensity(l) * g_set.lightshaft);
}
void gimble_light_detour(Il2CppObject* self, float tod, const MethodInfo* method) {
    g_gimble_light(self, tod, method);
    if (acting() && g_cfg) scale_light(self, api.gimble_light);
}
void shaft_tod_update_detour(Il2CppObject* self, const MethodInfo* method) {
    g_shaft_tod_update(self, method);
    if (acting() && g_cfg) scale_light(self, api.shaft_light);
}

// DarkLightingManager (mines, dark interiors): its fill lights by the fill level, its coloured ones less coloured.
void dark_scale(Il2CppObject* self, ptrdiff_t list, bool colour) {
    if (!acting() || !g_cfg) return;
    each_light_def(il2::read_field<Il2CppObject*>(self, list), [&](Il2CppObject* l) {
        set_intensity(l, light_intensity(l) * g_levels.fill);
        if (colour) {
            ambient::Hsv h = ambient::to_hsv(light_colour(l));
            h.s *= g_levels.fill_color;
            set_light_colour(l, ambient::to_rgb(h));
        }
    });
}
void dark_constant_detour(Il2CppObject* self, const MethodInfo* m) {
    g_dark_constant(self, m);
    dark_scale(self, api.dark_constant, false);
}
void dark_nontod_detour(Il2CppObject* self, const MethodInfo* m) {
    g_dark_nontod(self, m);
    dark_scale(self, api.dark_nontod, false);
}
void dark_coloured_detour(Il2CppObject* self, const MethodInfo* m) {
    g_dark_coloured(self, m);
    dark_scale(self, api.dark_coloured, true);
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* behaviour = il2::find_class(kCore, "UnityEngine", "Behaviour");
    Il2CppClass* light = il2::find_class(kCore, "UnityEngine", "Light");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* storm = il2::find_class(kGame, "", "UniStormWeatherSystem");
    Il2CppClass* tod_state = il2::find_class(kGame, "", "TODStateData");
    Il2CppClass* weather = il2::find_class(kGame, "", "Weather");
    Il2CppClass* wind = il2::find_class(kGame, "", "Wind");
    Il2CppClass* aurora = il2::find_class(kGame, "", "AuroraManager");
    Il2CppClass* ilm = il2::find_class(kGame, "", "InteriorLightingManager");
    Il2CppClass* group = il2::find_class(kGame, "", "InteriorLightingGroup");
    Il2CppClass* amb = il2::find_class(kGame, "", "TodAmbientLight");
    Il2CppClass* gimble = il2::find_class(kGame, "", "LightShaftGimble");
    Il2CppClass* shaft = il2::find_class(kGame, "", "LightShaftTod");
    Il2CppClass* dark = il2::find_class(kGame, "", "DarkLightingManager");
    if (!object || !go || !component || !transform || !behaviour || !light || !renderer || !material || !shader || !gm ||
        !storm || !tod_state || !weather || !wind || !aurora || !ilm || !group || !amb) {
        logx::warn("ambient: some of TLD's lighting classes are missing; Ambient Lights is off");
        return false;
    }
    api.go_class = go;
    api.light_type = il2::type_object(light);
    api.renderer_type = il2::type_object(renderer);
    api.ilm_type = il2::type_object(ilm);
    api.find_objects = il2::find_method_params(object, "FindObjectsOfType", {"System.Type"});
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.object_name = il2::find_method(object, "get_name", 0);
    api.go_ctor = il2::find_method_params(go, ".ctor", {"System.String"});
    api.add_component = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_transform = il2::find_method(go, "get_transform", 0);
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.set_position = il2::find_method(transform, "set_position", 1);
    api.set_enabled = il2::find_method(behaviour, "set_enabled", 1);
    api.light_get_type = il2::find_method(light, "get_type", 0);
    api.light_get_color = il2::find_method(light, "get_color", 0);
    api.light_set_color = il2::find_method(light, "set_color", 1);
    api.light_get_intensity = il2::find_method(light, "get_intensity", 0);
    api.light_set_intensity = il2::find_method(light, "set_intensity", 1);
    api.light_set_range = il2::find_method(light, "set_range", 1);
    api.light_get_cookie = il2::find_method(light, "get_cookie", 0);
    api.get_materials = il2::find_method(renderer, "get_materials", 0);
    api.mat_get_color = il2::find_method(material, "get_color", 0);
    api.mat_set_color = il2::find_method(material, "set_color", 1);
    api.mat_has = il2::find_method_params(material, "HasProperty", {"System.Int32"});
    api.mat_get_float = il2::find_method_params(material, "GetFloat", {"System.Int32"});
    api.mat_set_float = il2::find_method_params(material, "SetFloat", {"System.Int32", "System.Single"});
    if (const MethodInfo* to_id = il2::find_method_params(shader, "PropertyToID", {"System.String"})) {
        void* a[] = {il2::il2cpp_string_new("_EmissiveStrength")};
        il2::invoke_value(to_id, nullptr, api.emissive_id, a);
    }
    api.paused = il2::il2cpp_class_get_field_from_name(gm, "m_IsPaused");
    api.get_unistorm = il2::find_method(gm, "GetUniStorm", 0);
    api.get_aurora = il2::find_method(gm, "GetAuroraManager", 0);
    api.get_weather = il2::find_method(gm, "GetWeatherComponent", 0);
    api.get_wind = il2::find_method(gm, "GetWindComponent", 0);
    api.tod_state = il2::find_method(storm, "GetActiveTODState", 0);
    api.blend_state = il2::find_method(storm, "GetTODBlendState", 0);
    api.blend_pct = il2::find_method(storm, "GetTODBlendPercent", 1);
    api.prev_stage = il2::field_offset(storm, "m_PreviousWeatherStage");
    api.since_change = il2::field_offset(storm, "m_SecondsSinceLastWeatherChange");
    api.transition = il2::field_offset(storm, "m_WeatherTransitionTime");
    api.sun = il2::field_offset(tod_state, "m_SunLight");
    api.fog = il2::field_offset(tod_state, "m_FogColor");
    api.weather_stage = il2::find_method(weather, "GetWeatherStage", 0);
    api.wind_mph = il2::find_method(wind, "GetSpeedMPH", 0);
    api.aurora_alpha = il2::find_method(aurora, "GetNormalizedAlphaSquare", 0);
    api.aurora_colour = il2::find_method(aurora, "GetAuroraColour", 0);
    api.aurora_cinematic = il2::find_method(aurora, "IsUsingCinematicColours", 0);
    api.aurora_active = il2::field_offset(aurora, "m_NormalizedActive");
    api.aurora_full = il2::field_offset(aurora, "m_FullyActiveValue");
    api.ilm_groups = il2::field_offset(ilm, "m_LightGroupList");
    api.ilm_initialized = il2::field_offset_quiet(ilm, "m_Initialized");
    api.ilm_loose = il2::field_offset(ilm, "m_LooseLightList");
    api.ilm_loose_midday = il2::field_offset(ilm, "m_LooseLightsMiddayList");
    api.group_lights = il2::field_offset(group, "m_Lights");
    api.group_glow_mats = il2::field_offset(group, "m_GlowObjectMaterialsToUpdate");
    api.amb_day = il2::field_offset(amb, "m_AmbientIndoorsDay");
    api.amb_night = il2::field_offset(amb, "m_AmbientIndoorsNight");
    if (gimble) api.gimble_light = il2::field_offset(gimble, "m_Light");
    if (shaft) api.shaft_light = il2::field_offset(shaft, "m_Light");
    if (dark) {
        api.dark_constant = il2::field_offset(dark, "m_ConstantLightList");
        api.dark_coloured = il2::field_offset(dark, "m_ColouredLightList");
        api.dark_nontod = il2::field_offset(dark, "m_NonTodLightList");
    }
    const bool core_ok = api.find_objects && api.destroy && api.object_name && api.go_ctor && api.add_component &&
                         api.go_transform && api.component_go && api.set_position && api.set_enabled &&
                         api.light_get_type && api.light_get_color && api.light_set_color && api.light_get_intensity &&
                         api.light_set_intensity && api.light_set_range && api.get_materials && api.mat_get_color &&
                         api.mat_set_color && api.mat_has && api.mat_get_float && api.mat_set_float &&
                         api.get_unistorm && api.get_weather && api.tod_state && api.blend_state && api.blend_pct &&
                         api.weather_stage && api.sun >= 0 && api.fog >= 0 && api.amb_day >= 0 && api.amb_night >= 0;
    if (!core_ok) {
        logx::warn("ambient: some of TLD's lighting calls are missing; Ambient Lights is off");
        return false;
    }

    auto hook = [](const char* label, Il2CppClass* k, const char* name, int args, void* detour, void** original) {
        const MethodInfo* m = k ? il2::find_method(k, name, args) : nullptr;
        return m && hooks::install_method(label, m, detour, original);
    };
    if (!hook("TodAmbientLight.SetAmbientLightValue", amb, "SetAmbientLightValue", 2,
              reinterpret_cast<void*>(&set_ambient_detour), reinterpret_cast<void**>(&g_set_ambient)) ||
        !hook("InteriorLightingManager.Update", ilm, "Update", 0, reinterpret_cast<void*>(&ilm_update_detour),
              reinterpret_cast<void**>(&g_ilm_update))) {
        logx::warn("ambient: couldn't hook TLD's indoor lighting; Ambient Lights is off");
        return false;
    }
    // The rest tone what they can; each is optional.
    hook("InteriorLightingManager.UpdateLightShaft", ilm, "UpdateLightShaft", 2,
         reinterpret_cast<void*>(&ilm_shaft_detour), reinterpret_cast<void**>(&g_ilm_shaft));
    if (api.gimble_light >= 0)
        hook("LightShaftGimble.UpdateLight", gimble, "UpdateLight", 1, reinterpret_cast<void*>(&gimble_light_detour),
             reinterpret_cast<void**>(&g_gimble_light));
    if (api.shaft_light >= 0)
        hook("LightShaftTod.Update", shaft, "Update", 0, reinterpret_cast<void*>(&shaft_tod_update_detour),
             reinterpret_cast<void**>(&g_shaft_tod_update));
    if (api.dark_constant >= 0)
        hook("DarkLightingManager.UpdateConstantLights", dark, "UpdateConstantLights", 0,
             reinterpret_cast<void*>(&dark_constant_detour), reinterpret_cast<void**>(&g_dark_constant));
    if (api.dark_nontod >= 0)
        hook("DarkLightingManager.UpdateNonTodLights", dark, "UpdateNonTodLights", 0,
             reinterpret_cast<void*>(&dark_nontod_detour), reinterpret_cast<void**>(&g_dark_nontod));
    if (api.dark_coloured >= 0)
        hook("DarkLightingManager.UpdateColouredLights", dark, "UpdateColouredLights", 0,
             reinterpret_cast<void*>(&dark_coloured_detour), reinterpret_cast<void**>(&g_dark_coloured));
    return true;
}

}  // namespace

void init() {
    api.ok = resolve();
    logx::info("ambient: Ambient Lights %s (%zu interiors with light data)", api.ok ? "ready" : "unavailable",
               sizeof(ambient::kScenes) / sizeof(ambient::kScenes[0]));
}

void set_settings(const config::Settings& s) {
    coop_features::set_own(net::kFeatureAmbientLights, s.ambient_lights);
    g_levels.intensity = s.ambient_intensity;
    g_levels.range = s.ambient_range;
    g_levels.night = s.ambient_night;
    g_levels.ambience = s.ambient_ambience;
    g_levels.fill = s.ambient_fill;
    g_levels.fill_color = s.ambient_fill_color;
    g_levels.aurora_intensity = s.ambient_aurora_intensity;
    g_levels.aurora_saturation = s.ambient_aurora_saturation;
    logx::info("ambient: %s (intensity %.2f range %.2f night %d ambience %.2f fill %.2f/%.2f)",
               s.ambient_lights ? "on" : "off", g_levels.intensity, g_levels.range, g_levels.night, g_levels.ambience,
               g_levels.fill, g_levels.fill_color);
}

void on_frame(const std::string& scene, bool in_game) {
    if (!api.ok) return;

    const double now = now_ms();
    if (scene != g_scene) {
        if (g_set_up) restore_and_drop();
        g_scene = scene;
        g_scene_since = now;
    }
    if (!in_game || !active()) {
        if (g_set_up) {
            logx::info("ambient: off in %s; TLD's own lighting back", g_scene.c_str());
            restore_and_drop();
        }
        return;
    }
    if (!g_set_up) {
        // As soon as TLD's own indoor lighting has started (its manager initialized: the scene's lights are all in),
        // so the window light is there when the scene shows; a scene without that manager after kSettleMs.
        if (now - g_last_look < kLookMs) return;
        g_last_look = now;
        bool lit = false;
        if (Il2CppObject* managers = find_all(api.ilm_type); il2::array_length(managers)) {
            Il2CppObject* m = il2::array_object_at(managers, 0);
            lit = alive(m) && (api.ilm_initialized < 0 || il2::read_field<bool>(m, api.ilm_initialized));
        }
        if (!lit && now - g_scene_since < kSettleMs) return;
        logx::info("ambient: %s set up %.1f s after it came in%s", scene.c_str(), (now - g_scene_since) / 1000.0,
                   lit ? "" : " (no indoor lighting manager)");
        set_up(scene);
    }
    if (!g_cfg || paused()) return;
    ambient::Now n;
    if (!read_now(n)) return;
    g_set = ambient::light_set(n, g_levels);
    g_blend_state = n.blend_state;
    g_blend_pct = n.blend_pct;
    update_our_lights();
    g_ready = true;
}

}  // namespace ambient_lights
