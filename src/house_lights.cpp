#include "house_lights.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "coop_features.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"
#include "paths.h"
#include "text_upper.h"

namespace house_lights {
namespace {

#include "house_lights_bundle.inc"

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;  // List<T>._items, _size
constexpr ptrdiff_t kCachedPtr = 0x10;                    // UnityEngine.Object.m_CachedPtr: null once destroyed
constexpr int kSwitchLayer = 19;                          // the mod's (TLD's interactive layer)
constexpr int kHoverCanInteract = 3;                      // HoverTextState.CanInteract
constexpr int kShadowsSoft = 2;                           // LightShadows.Soft
constexpr const char* kSwitchName = "MOD_HouseLightSwitch";
constexpr double kStateEveryMs = 5000;  // lights on in our scene: said again this often (a player coming in hears it)

struct Vector3 {
    float x, y, z;
};
struct Color {
    float r, g, b, a;
};

struct Api {
    bool ok = false;
    // Unity
    Il2CppClass* material_class = nullptr;
    Il2CppObject* transform_type = nullptr;
    Il2CppObject* go_type = nullptr;
    Il2CppObject* mesh_renderer_type = nullptr;
    Il2CppObject* collider_type = nullptr;
    Il2CppObject* box_type = nullptr;
    Il2CppObject* toggle_type = nullptr;   // AuroraActivatedToggle (a radio: left to TLD)
    Il2CppObject* screen_type = nullptr;   // AuroraScreenDisplay (a screen: left to TLD)
    const MethodInfo* find_objects = nullptr;    // static Object.FindObjectsOfType(Type)
    const MethodInfo* object_name = nullptr;     // Object.get_name()
    const MethodInfo* set_name = nullptr;        // Object.set_name(string)
    const MethodInfo* instantiate = nullptr;     // static Object.Instantiate(Object)
    const MethodInfo* component_go = nullptr;    // Component.get_gameObject()
    const MethodInfo* component_tf = nullptr;    // Component.get_transform()
    const MethodInfo* go_tf = nullptr;           // GameObject.get_transform()
    const MethodInfo* go_set_active = nullptr;   // GameObject.SetActive(bool)
    const MethodInfo* go_active_self = nullptr;  // GameObject.get_activeSelf()
    const MethodInfo* go_set_layer = nullptr;    // GameObject.set_layer(int)
    const MethodInfo* go_component = nullptr;    // GameObject.GetComponent(Type)
    const MethodInfo* go_add = nullptr;          // GameObject.AddComponent(Type)
    const MethodInfo* go_children = nullptr;     // GameObject.GetComponentsInChildren(Type, bool)
    const MethodInfo* go_parents = nullptr;      // GameObject.GetComponentsInParent(Type, bool)
    const MethodInfo* tf_find = nullptr;         // Transform.Find(string)
    const MethodInfo* tf_position = nullptr;     // Transform.get_position()
    const MethodInfo* tf_set_position = nullptr;
    const MethodInfo* tf_euler = nullptr;        // Transform.get_eulerAngles()
    const MethodInfo* tf_set_euler = nullptr;
    const MethodInfo* tf_scale = nullptr;        // Transform.get_localScale()
    const MethodInfo* tf_set_scale = nullptr;
    const MethodInfo* box_set_size = nullptr;    // BoxCollider.set_size(Vector3)
    const MethodInfo* renderer_material = nullptr;      // Renderer.get_material()
    const MethodInfo* renderer_set_material = nullptr;  // Renderer.set_material(Material)
    const MethodInfo* material_ctor = nullptr;   // Material(Shader)
    const MethodInfo* material_main = nullptr;   // Material.get_mainTexture()
    const MethodInfo* material_set_main = nullptr;
    const MethodInfo* shader_find = nullptr;     // static Shader.Find(string)
    const MethodInfo* load_bundle = nullptr;     // static AssetBundle.LoadFromFile(string)
    const MethodInfo* load_asset = nullptr;      // AssetBundle.LoadAsset(string, Type)
    const MethodInfo* light_range = nullptr, *light_set_range = nullptr;
    const MethodInfo* light_color = nullptr, *light_set_color = nullptr;
    const MethodInfo* light_set_shadows = nullptr;
    // TLD
    const MethodInfo* outdoors_scene = nullptr;  // static GameManager.IsOutDoorsScene(string)
    const MethodInfo* play_sound = nullptr;      // static GameAudioManager.PlaySound(string, GameObject)
    const MethodInfo* under_crosshairs = nullptr;  // PlayerManager.GetInteractiveObjectUnderCrosshairs(float)
    const MethodInfo* set_hover = nullptr;       // Panel_HUD.SetHoverText(string, GameObject, HoverTextState)
    const MethodInfo* weather_indoor = nullptr;  // Weather.IsIndoorScene()
    const MethodInfo* aurora_active = nullptr;   // AuroraManager.AuroraIsActive()
    ptrdiff_t aurora_normalized = -1;            // AuroraManager.m_NormalizedActive
    // AuroraModularElectrolizer
    ptrdiff_t me_lights = -1, me_flicker = -1;
    const MethodInfo *me_intensity = nullptr, *me_light = nullptr, *me_emissive = nullptr, *me_audio = nullptr,
                     *me_stop = nullptr;
    // AuroraLightingSimple
    ptrdiff_t ls_lights = -1, ls_intensity = -1;
    const MethodInfo *ls_update_intensity = nullptr, *ls_light = nullptr, *ls_emissive = nullptr, *ls_audio = nullptr,
                     *ls_stop = nullptr;
} api;

// Settings: the levels are each player's own; ON / OFF is the host's (coop_features).
struct Levels {
    float intensity = 2.0f, range = 1.4f, reach = 1.0f;
    bool white = false, no_flicker = true, shadows = false, audio = false, outside = false;
    int cull = 50;
    uint32_t generation = 0;  // changes with the settings: lights take their ranges and colours again
} g_lv;

bool active() { return api.ok && coop_features::on(net::kFeatureHouseLights); }

uintptr_t hold(Il2CppObject* o) { return o ? il2::il2cpp_gchandle_new(o, false) : 0; }
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
void drop(uintptr_t& h) {
    if (h) il2::il2cpp_gchandle_free(h);
    h = 0;
}
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
std::string name_of(Il2CppObject* o) {
    auto* s = o ? reinterpret_cast<Il2CppString*>(il2::invoke(api.object_name, o)) : nullptr;
    return s ? il2::to_utf8(s) : std::string();
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
Il2CppObject* go_of(Il2CppObject* component) { return component ? il2::invoke(api.component_go, component) : nullptr; }
Il2CppObject* tf_of_go(Il2CppObject* go) { return go ? il2::invoke(api.go_tf, go) : nullptr; }
void set_active(Il2CppObject* go, bool on) {
    if (!go) return;
    void* a[] = {&on};
    il2::invoke(api.go_set_active, go, a);
}
void set_vec(const MethodInfo* m, Il2CppObject* tf, Vector3 v) {
    if (!tf || !m) return;
    void* a[] = {&v};
    il2::invoke(m, tf, a);
}
Vector3 get_vec(const MethodInfo* m, Il2CppObject* tf) {
    Vector3 v{};
    if (tf && m) il2::invoke_value(m, tf, v);
    return v;
}
Il2CppObject* components(const MethodInfo* m, Il2CppObject* go, Il2CppObject* type) {
    if (!go || !type || !m) return nullptr;
    bool include_inactive = true;
    void* a[] = {type, &include_inactive};
    return il2::invoke(m, go, a);
}

// --- the scene ------------------------------------------------------------------------------------------------------

std::string g_scene;                     // the scene the switches and lights are for
std::map<std::string, bool> g_scene_on;  // lights on, by scene (ours, or as another player said)
bool g_on = false;                       // in our scene now (HouseLights.lightsOn)
double g_last_said = 0;

struct Switch {
    uintptr_t part = 0;  // the switch's SM_LightSwitchBlack (what the player looks at)
};
std::vector<Switch> g_switches;

struct Electro {
    uintptr_t h = 0;
    bool simple = false;  // AuroraLightingSimple (else AuroraModularElectrolizer)
    std::vector<float> ranges;
    std::vector<Color> colors;
    uint32_t applied = 0;  // g_lv.generation its lights took
    bool skip = false;     // an alarm, headlight, screen... (left as TLD has them)
};
std::vector<Electro> g_electros;

bool enabled_here(const std::string& scene) {
    if (scene.empty() || scene.find("MainMenu") != std::string::npos) return false;
    if (g_lv.outside || scene == "DamTransitionZone") return true;
    bool outdoors = false;
    void* a[] = {il2::il2cpp_string_new(scene.c_str())};
    il2::invoke_value(api.outdoors_scene, nullptr, outdoors, a);
    return !outdoors;
}

// --- the bundle (the switch models) -------------------------------------------------------------------------------

uintptr_t g_bundle = 0;
bool g_bundle_failed = false;
std::map<std::string, uintptr_t> g_prefabs;

Il2CppObject* bundle() {
    if (Il2CppObject* b = target(g_bundle)) return b;
    if (g_bundle_failed) return nullptr;
    const auto file = paths::instance_dir() / L"tldcoop_houselights.bundle";
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) != kBundleSize) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(kBundle), static_cast<std::streamsize>(kBundleSize));
        if (!out) {
            logx::warn("houselights: couldn't write %ls", file.c_str());
            g_bundle_failed = true;
            return nullptr;
        }
    }
    void* a[] = {il2::il2cpp_string_new(file.string().c_str())};
    Il2CppObject* b = il2::invoke(api.load_bundle, nullptr, a);
    if (!b) {
        logx::warn("houselights: the switch models' bundle didn't load (%ls)", file.c_str());
        g_bundle_failed = true;
        return nullptr;
    }
    g_bundle = hold(b);
    logx::info("houselights: switch models loaded");
    return b;
}

Il2CppObject* prefab(const char* name) {
    if (auto it = g_prefabs.find(name); it != g_prefabs.end()) return target(it->second);
    Il2CppObject* b = bundle();
    if (!b) return nullptr;
    void* a[] = {il2::il2cpp_string_new(name), api.go_type};
    Il2CppObject* p = il2::invoke(api.load_asset, b, a);
    g_prefabs[name] = hold(p);
    if (!p) logx::warn("houselights: no %s in the bundle", name);
    return p;
}

// HouseLightsUtils.InstantiateSwitch: variant 0 OBJ_SwitchHL, 1 ..HLB, 2 ..HLC, 3 ..HLD; its look with TLD's shader.
Il2CppObject* make_switch(Vector3 pos, Vector3 euler, int variant) {
    const char* names[] = {"OBJ_SwitchHL", "OBJ_SwitchHLB", "OBJ_SwitchHLC", "OBJ_SwitchHLD"};
    Il2CppObject* p = prefab(names[variant >= 0 && variant <= 3 ? variant : 0]);
    if (!p) return nullptr;
    void* a[] = {p};
    Il2CppObject* go = il2::invoke(api.instantiate, nullptr, a);
    if (!go) return nullptr;
    static Il2CppObject* shader = nullptr;
    if (!shader && api.shader_find) {
        void* sa[] = {il2::il2cpp_string_new("Shader Forge/TLD_StandardDiffuse")};
        shader = il2::invoke(api.shader_find, nullptr, sa);
        if (shader) il2::il2cpp_gchandle_new(shader, false);
    }
    if (shader && api.material_class && api.material_ctor) {
        Il2CppObject* rs = components(api.go_children, go, api.mesh_renderer_type);
        for (size_t i = 0, n = il2::array_length(rs); i < n; ++i) {
            Il2CppObject* r = il2::array_object_at(rs, i);
            Il2CppObject* old = il2::invoke(api.renderer_material, r);
            Il2CppObject* tex = old ? il2::invoke(api.material_main, old) : nullptr;
            Il2CppObject* mat = il2::il2cpp_object_new(api.material_class);
            void* ca[] = {shader};
            il2::invoke(api.material_ctor, mat, ca);
            void* ta[] = {tex};
            if (tex) il2::invoke(api.material_set_main, mat, ta);
            void* ma[] = {mat};
            il2::invoke(api.renderer_set_material, r, ma);
        }
    }
    Il2CppObject* tf = tf_of_go(go);
    set_vec(api.tf_set_euler, tf, euler);
    set_vec(api.tf_set_position, tf, pos);
    set_vec(api.tf_set_scale, tf, {1, 1, 1});
    return go;
}

// The switch's part the player looks at: named and layered so PlayerManager finds it, with a collider.
void register_switch(Il2CppObject* go) {
    void* fa[] = {il2::il2cpp_string_new("SM_LightSwitchBlack")};
    Il2CppObject* part_tf = il2::invoke(api.tf_find, tf_of_go(go), fa);
    Il2CppObject* part = part_tf ? go_of(part_tf) : nullptr;
    if (!part) return;
    int layer = kSwitchLayer;
    void* la[] = {&layer};
    il2::invoke(api.go_set_layer, part, la);
    void* na[] = {il2::il2cpp_string_new(kSwitchName)};
    il2::invoke(api.set_name, part, na);
    void* ca[] = {api.collider_type};
    if (!il2::invoke(api.go_component, part, ca)) {
        void* ba[] = {api.box_type};
        Il2CppObject* box = il2::invoke(api.go_add, part, ba);
        Vector3 size{0.1f, 0.1f, 0.1f};
        void* sa[] = {&size};
        if (box) il2::invoke(api.box_set_size, box, sa);
    }
    Switch s;
    s.part = hold(part);
    g_switches.push_back(s);
}

// HouseLights.InstantiateCustomSwitches: the mod's switches where the game has none.
struct Custom {
    const char* scene;
    Vector3 pos, euler;
    int variant;
};
constexpr Custom kCustom[] = {
    {"lakeregion", {791.93f, 214.38f, 965.76f}, {0, 265, 0}, 0},
    {"trailera", {-2.95f, 1.38f, 2.06f}, {0, 180, 0}, 0},
    {"communityhalla", {0.128f, 1.38f, 4.20f}, {0, 0, 0}, 1},
    {"communityhalla", {8.52f, 1.40f, 0.26f}, {0, 0, 0}, 1},
    {"communityhalla", {6.91f, 1.54f, -3.97f}, {0, 90, 0}, 1},
    {"communityhalla", {6.58f, 1.43f, 0.22f}, {0, 270, 0}, 3},
    {"communityhalla", {-9.2f, 2.24f, 3.16f}, {0, 180, 0}, 3},
    {"communityhalla", {9.05f, 1.41f, 0.16f}, {0, 180, 0}, 0},
    {"trailersshape", {7.20f, 1.50f, -10.12f}, {0, 0, 0}, 1},
    {"trailersshape", {0.59f, 1.55f, -5.92f}, {0, 0, 0}, 1},
    {"trailersshape", {-6.53f, 1.52f, 3.76f}, {0, 0, 0}, 1},
    {"trailerb", {3.88f, 1.34f, 2.07f}, {0, 180, 0}, 3},
    {"trailerc", {-0.88f, 1.32f, 2.07f}, {0, 180, 0}, 0},
    {"trailerd", {-3.00f, 1.37f, 2.07f}, {0, 180, 0}, 1},
    {"trailere", {-2.94f, 1.34f, 2.07f}, {0, 180, 0}, 0},
    {"tracksregion", {586.17f, 200.48f, 564.31f}, {0, 270, 0}, 3},
    {"mountainpassburiedcabin", {3.89f, 1.26f, 0.82f}, {0, 270, 0}, 1},
    {"mountainpassburiedcabin", {-0.52f, 5.17f, 1.711f}, {0, 180, 0}, 1},
    {"mountainpassburiedcabin", {-0.54f, 5.13f, 1.78f}, {0, 0, 0}, 1},
    {"miltontrailerb", {3.92f, 1.50f, 2.07f}, {0, 180, 0}, 3},
    {"huntinglodgea", {7.27f, 1.18f, -1.58f}, {0, 90, 0}, 0},
    {"huntinglodgea", {-1.17f, 1.51f, -5.01f}, {0, 0, 0}, 3},
    {"damtrailerb", {4.01f, 1.49f, 2.07f}, {0, 180, 0}, 0},
    {"crashmountainregion", {889.93f, 162.08f, 346.07f}, {0, 180, 0}, 3},
    {"coastalregion", {757.9f, 25.51f, 646.78f}, {0, 50, 0}, 0},
    {"cannerytrailera", {-3.02f, 1.42f, 2.79f}, {0, 180, 0}, 3},
    {"bunkerc", {1.09f, 1.73f, 3.54f}, {0, 0, 0}, 3},
    {"bunkerc", {-14.72f, 0.33f, 12.93f}, {0, 0, 0}, 3},
    {"bunkerb", {1.13f, 1.67f, 3.54f}, {0, 0, 0}, 3},
    {"bunkerb", {2.94f, 1.54f, 7.68f}, {0, 90, 0}, 3},
    {"bunkerb", {-3.19f, 1.61f, 7.66f}, {0, 270, 0}, 3},
    {"bunkera", {5.93f, 1.61f, 12.63f}, {0, 180, 0}, 3},
    {"bunkera", {1.18f, 1.65f, 1.62f}, {0, 0, 0}, 3},
    {"blackrocktrailerb", {3.9726f, 1.4155f, 2.0799f}, {0, 180, 0}, 1},
    {"airfieldtrailerb", {4.01f, 1.49f, 2.07f}, {0, 180, 0}, 0},
};

// The switches show which way they're flipped (the mod turned the clicked one upside down).
void show_switches() {
    for (const Switch& s : g_switches) {
        Il2CppObject* part = target(s.part);
        if (!alive(part)) continue;
        Il2CppObject* tf = tf_of_go(part);
        Vector3 sc = get_vec(api.tf_scale, tf);
        const float y = std::fabs(sc.y) * (g_on ? -1.0f : 1.0f);
        if (y != sc.y) set_vec(api.tf_set_scale, tf, {sc.x, y, sc.z});
    }
}

// GameManager.InstantiatePlayerObject (the mod's prefix): a scene came in. Its switches: the game's own are swapped for
// the mod's working ones, plus the mod's where the game has none.
void scene_in() {
    for (Switch& s : g_switches) drop(s.part);
    g_switches.clear();
    g_scene = game::active_scene();
    if (!active() || !enabled_here(g_scene)) return;
    g_on = g_scene_on[g_scene];
    const std::string low = lower(g_scene);
    int custom = 0;
    for (const Custom& c : kCustom)
        if (low == c.scene)
            if (Il2CppObject* go = make_switch(c.pos, c.euler, c.variant)) register_switch(go), ++custom;
    int found = 0;
    void* fa[] = {api.transform_type};
    Il2CppObject* all = il2::invoke(api.find_objects, nullptr, fa);
    constexpr const char* kNames[] = {"houselightswitch", "lightswitcha", "lightswitchblack", "switch_a_black",
                                      "switch_a_white",  "switch_a_purple", "switch_b_white"};
    std::vector<Il2CppObject*> originals;
    for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
        Il2CppObject* tf = il2::array_object_at(all, i);
        const std::string nm = lower(name_of(tf));
        if (nm.find("mod_houselightswitch") != std::string::npos) continue;  // (ours)
        for (const char* k : kNames)
            if (nm.find(k) != std::string::npos) {
                originals.push_back(tf);
                break;
            }
    }
    for (Il2CppObject* tf : originals) {
        Il2CppObject* go = go_of(tf);
        bool on = false;
        if (!go || !il2::invoke_value(api.go_active_self, go, on) || !on) continue;
        const std::string nm = lower(name_of(tf));
        const int variant = nm.find("houselightswitch") != std::string::npos || nm.find("lightswitcha") != std::string::npos ? 1 : 2;
        const Vector3 pos = get_vec(api.tf_position, tf), euler = get_vec(api.tf_euler, tf);
        set_active(go, false);
        if (Il2CppObject* made = make_switch(pos, euler, variant)) register_switch(made), ++found;
    }
    show_switches();
    if (found || custom)
        logx::info("houselights: %s: %d light switch(es) made working, %d added; lights %s", g_scene.c_str(), found, custom,
                   g_on ? "on" : "off");
}

// --- the lights ---------------------------------------------------------------------------------------------------

Color to_white(Color c) {  // the mod's "colorless": its HSV saturation x 0.15
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
    if (mx <= 0) return c;
    const float s = (mx - mn) / mx, s2 = s * 0.15f;
    // keep hue and value: each channel's distance below the max scaled with the saturation
    auto ch = [&](float v) { return mx - (mx - v) * (s > 0 ? s2 / s : 0); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

Il2CppObject* lights_of(const Electro& e, Il2CppObject* obj, size_t& n) {
    n = 0;
    if (e.simple) {
        Il2CppObject* arr = il2::read_field<Il2CppObject*>(obj, api.ls_lights);
        n = il2::array_length(arr);
        return arr;
    }
    Il2CppObject* list = il2::read_field<Il2CppObject*>(obj, api.me_lights);
    if (!list) return nullptr;
    n = static_cast<size_t>(std::max(0, il2::read_field<int32_t>(list, kListSize)));
    Il2CppObject* arr = il2::read_field<Il2CppObject*>(list, kListItems);
    n = std::min(n, il2::array_length(arr));
    return arr;
}

void add_electro(Il2CppObject* obj, bool simple) {
    if (!obj) return;
    for (const Electro& e : g_electros)
        if (target(e.h) == obj) return;
    Electro e;
    e.simple = simple;
    e.h = hold(obj);
    size_t n = 0;
    Il2CppObject* arr = lights_of(e, obj, n);
    for (size_t i = 0; i < n; ++i) {
        Il2CppObject* l = il2::array_object_at(arr, i);
        float r = 0;
        Color c{1, 1, 1, 1};
        if (l) il2::invoke_value(api.light_range, l, r), il2::invoke_value(api.light_color, l, c);
        e.ranges.push_back(r);
        e.colors.push_back(c);
    }
    const std::string nm = name_of(go_of(obj));
    for (const char* k : {"Alarm", "Headlight", "Taillight", "Television", "Computer", "Machine", "ControlBox", "Interiorlight"})
        if (nm.find(k) != std::string::npos) e.skip = true;
    g_electros.push_back(std::move(e));
}

void call(const MethodInfo* m, Il2CppObject* o) {
    if (m) il2::invoke(m, o);
}
void call_b(const MethodInfo* m, Il2CppObject* o, bool v) {
    if (!m) return;
    void* a[] = {&v};
    il2::invoke(m, o, a);
}

// HouseLights.UpdateElectroLights, after TLD's own update of them each frame.
void update_lights(Il2CppObject* mngr) {
    if (g_electros.empty()) return;
    const game::PlayerState me = game::read_player();
    if (!me.valid) return;
    bool aurora = false;
    il2::invoke_value(api.aurora_active, mngr, aurora);
    const float normalized = api.aurora_normalized >= 0 ? il2::read_field<float>(mngr, api.aurora_normalized) : 0;
    const float dt = 1.0f / 60.0f;
    for (auto it = g_electros.begin(); it != g_electros.end();) {
        Il2CppObject* o = target(it->h);
        if (!alive(o)) {
            drop(it->h);
            it = g_electros.erase(it);
            continue;
        }
        Electro& e = *it++;
        const Vector3 p = get_vec(api.tf_position, il2::invoke(api.component_tf, o));
        const float dx = p.x - me.position.x, dy = p.y - me.position.y, dz = p.z - me.position.z;
        const bool too_far = std::sqrt(dx * dx + dy * dy + dz * dz) > static_cast<float>(g_lv.cull);
        auto all_off = [&]() {
            if (e.simple) {
                if (api.ls_intensity >= 0) *reinterpret_cast<float*>(reinterpret_cast<char*>(o) + api.ls_intensity) = 0;
                call_b(api.ls_light, o, true);
                call_b(api.ls_emissive, o, true);
                call(api.ls_audio, o);
            } else {
                float one = 1, zero = 0;
                void* a[] = {&one, &zero};
                il2::invoke(api.me_intensity, o, a);
                call_b(api.me_light, o, true);
                call_b(api.me_emissive, o, true);
                call(api.me_audio, o);
            }
        };
        if (too_far && !aurora) {
            all_off();
            continue;
        }
        if (e.applied != g_lv.generation) {  // ranges, colours, shadows: once per setting
            e.applied = g_lv.generation;
            size_t n = 0;
            Il2CppObject* arr = lights_of(e, o, n);
            for (size_t i = 0; i < n && i < e.ranges.size(); ++i) {
                Il2CppObject* l = il2::array_object_at(arr, i);
                if (!l) continue;
                float r = std::min(e.ranges[i] * g_lv.range, 20.0f);
                void* ra[] = {&r};
                il2::invoke(api.light_set_range, l, ra);
                Color c = g_lv.white ? to_white(e.colors[i]) : e.colors[i];
                void* ca[] = {&c};
                il2::invoke(api.light_set_color, l, ca);
                if (g_lv.shadows && api.light_set_shadows) {
                    int sh = kShadowsSoft;
                    void* sa[] = {&sh};
                    il2::invoke(api.light_set_shadows, l, sa);
                }
            }
        }
        if (!e.simple && api.me_flicker >= 0)
            *reinterpret_cast<bool*>(reinterpret_cast<char*>(o) + api.me_flicker) = !g_lv.no_flicker;
        if (g_on && !aurora) {
            if (e.skip) continue;
            if (e.simple) {
                if (api.ls_intensity >= 0) *reinterpret_cast<float*>(reinterpret_cast<char*>(o) + api.ls_intensity) = g_lv.intensity;
                call_b(api.ls_light, o, false);
                call_b(api.ls_emissive, o, false);
                call(g_lv.audio ? api.ls_audio : api.ls_stop, o);
            } else {
                float one = 1, v = g_lv.intensity;
                void* a[] = {&one, &v};
                il2::invoke(api.me_intensity, o, a);
                call_b(api.me_light, o, false);
                call_b(api.me_emissive, o, false);
                call(g_lv.audio ? api.me_audio : api.me_stop, o);
            }
        } else if (!aurora) {
            all_off();
        } else if (e.simple) {
            float d = dt;
            void* a[] = {&d};
            il2::invoke(api.ls_update_intensity, o, a);
        } else {
            float d = dt, v = normalized;
            void* a[] = {&d, &v};
            il2::invoke(api.me_intensity, o, a);
        }
    }
}

// --- co-op ----------------------------------------------------------------------------------------------------------

void say(bool reliable) {
    if (!netrt::status().connected || g_scene.empty()) return;
    net::Writer w(net::Msg::HouseLights, 128);
    w.put<uint8_t>(g_on ? 1 : 0);
    w.put_blob(g_scene, 64);
    netrt::send_packet(w, reliable);
    g_last_said = static_cast<double>(GetTickCount64());
}

void set_on(bool on, const char* why) {
    if (on == g_on) return;
    g_on = on;
    if (!g_scene.empty()) g_scene_on[g_scene] = on;
    show_switches();
    logx::info("houselights: lights %s in %s (%s)", on ? "on" : "off", g_scene.c_str(), why);
}

// --- hooks ----------------------------------------------------------------------------------------------------------

using VoidFn = void (*)(Il2CppObject*, const MethodInfo*);
using StaticObjFn = void (*)(Il2CppObject*, const MethodInfo*);
using HudFn = void (*)(Il2CppObject*, Il2CppObject*, const MethodInfo*);
using BoolFn = bool (*)(Il2CppObject*, const MethodInfo*);
using DarkFn = bool (*)(Il2CppObject*, int32_t, const MethodInfo*);
VoidFn g_player_object = nullptr, g_me_init = nullptr, g_aurora_update = nullptr;
StaticObjFn g_register_simple = nullptr;
HudFn g_hud_text = nullptr;
BoolFn g_interact = nullptr;
DarkFn g_too_dark = nullptr;

void player_object_detour(Il2CppObject* self, const MethodInfo* m) {
    if (api.ok) scene_in();  // (the mod's prefix)
    g_player_object(self, m);
}

void me_init_detour(Il2CppObject* self, const MethodInfo* m) {
    g_me_init(self, m);
    if (!active() || !self || !enabled_here(game::active_scene())) return;
    Il2CppObject* go = go_of(self);
    Il2CppObject* radios = components(api.go_parents, go, api.toggle_type);
    Il2CppObject* screens = components(api.go_children, go, api.screen_type);
    if (il2::array_length(radios) == 0 && il2::array_length(screens) == 0) add_electro(self, false);
    if (api.me_flicker >= 0) *reinterpret_cast<bool*>(reinterpret_cast<char*>(self) + api.me_flicker) = !g_lv.no_flicker;
}

void register_simple_detour(Il2CppObject* light, const MethodInfo* m) {
    g_register_simple(light, m);
    if (active() && enabled_here(game::active_scene())) add_electro(light, true);
}

void aurora_update_detour(Il2CppObject* self, const MethodInfo* m) {
    g_aurora_update(self, m);
    if (active() && enabled_here(g_scene)) update_lights(self);
}

Il2CppObject* switch_under_crosshairs(Il2CppObject* pm) {
    if (!pm || !api.under_crosshairs) return nullptr;
    float reach = g_lv.reach;
    void* a[] = {&reach};
    Il2CppObject* go = il2::invoke(api.under_crosshairs, pm, a);
    return go && name_of(go) == kSwitchName ? go : nullptr;
}

void hud_text_detour(Il2CppObject* self, Il2CppObject* hud, const MethodInfo* m) {
    g_hud_text(self, hud, m);
    if (!active() || !hud || g_switches.empty()) return;
    Il2CppObject* go = switch_under_crosshairs(self);
    if (!go) return;
    int state = kHoverCanInteract;
    void* a[] = {il2::il2cpp_string_new(g_on ? "Wyłącz światło" : "Włącz światło"), go, &state};
    il2::invoke(api.set_hover, hud, a);
}

bool interact_detour(Il2CppObject* self, const MethodInfo* m) {
    bool r = g_interact(self, m);
    if (!active() || g_switches.empty()) return r;
    Il2CppObject* go = switch_under_crosshairs(self);
    if (!go) return r;
    set_on(!g_on, "a switch flipped here");
    say(true);
    if (api.play_sound) {
        void* a[] = {il2::il2cpp_string_new("Stop_RadioAurora"), go_of(self)};
        il2::invoke(api.play_sound, nullptr, a);
    }
    return true;
}

bool too_dark_detour(Il2CppObject* self, int32_t action, const MethodInfo* m) {
    bool r = g_too_dark(self, action, m);
    if (!r || !active() || !g_on) return r;
    bool indoor = false;
    il2::invoke_value(api.weather_indoor, self, indoor);
    return indoor ? false : r;
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* game_object = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* mesh_renderer = il2::find_class(kCore, "UnityEngine", "MeshRenderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* light = il2::find_class(kCore, "UnityEngine", "Light");
    Il2CppClass* collider = il2::find_class(kPhysics, "UnityEngine", "Collider");
    Il2CppClass* box = il2::find_class(kPhysics, "UnityEngine", "BoxCollider");
    Il2CppClass* bundle_c = il2::find_class("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* audio = il2::find_class(kGame, "", "GameAudioManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* hud = il2::find_class(kGame, "", "Panel_HUD");
    Il2CppClass* weather = il2::find_class(kGame, "", "Weather");
    Il2CppClass* aurora = il2::find_class(kGame, "", "AuroraManager");
    Il2CppClass* me = il2::find_class(kGame, "TLD.ModularElectrolizer", "AuroraModularElectrolizer");
    Il2CppClass* ls = il2::find_class(kGame, "", "AuroraLightingSimple");
    Il2CppClass* toggle = il2::find_class(kGame, "", "AuroraActivatedToggle");
    Il2CppClass* screen = il2::find_class(kGame, "", "AuroraScreenDisplay");
    if (!object || !component || !game_object || !transform || !renderer || !mesh_renderer || !material || !shader ||
        !light || !collider || !box || !bundle_c || !gm || !pm || !hud || !weather || !aurora || !me || !ls)
        return false;
    api.material_class = material;
    api.transform_type = il2::type_object(transform);
    api.go_type = il2::type_object(game_object);
    api.mesh_renderer_type = il2::type_object(mesh_renderer);
    api.collider_type = il2::type_object(collider);
    api.box_type = il2::type_object(box);
    api.toggle_type = toggle ? il2::type_object(toggle) : nullptr;
    api.screen_type = screen ? il2::type_object(screen) : nullptr;
    for (Il2CppObject* t : {api.transform_type, api.go_type, api.mesh_renderer_type, api.collider_type, api.box_type,
                            api.toggle_type, api.screen_type})
        if (t) il2::il2cpp_gchandle_new(t, false);
    api.find_objects = il2::find_method_params(object, "FindObjectsOfType", {"System.Type"});
    api.object_name = il2::find_method(object, "get_name", 0);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.component_tf = il2::find_method(component, "get_transform", 0);
    api.go_tf = il2::find_method(game_object, "get_transform", 0);
    api.go_set_active = il2::find_method(game_object, "SetActive", 1);
    api.go_active_self = il2::find_method(game_object, "get_activeSelf", 0);
    api.go_set_layer = il2::find_method(game_object, "set_layer", 1);
    api.go_component = il2::find_method_params(game_object, "GetComponent", {"System.Type"});
    api.go_add = il2::find_method_params(game_object, "AddComponent", {"System.Type"});
    api.go_children = il2::find_method_params(game_object, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    api.go_parents = il2::find_method_params(game_object, "GetComponentsInParent", {"System.Type", "System.Boolean"});
    api.tf_find = il2::find_method_params(transform, "Find", {"System.String"});
    api.tf_position = il2::find_method(transform, "get_position", 0);
    api.tf_set_position = il2::find_method(transform, "set_position", 1);
    api.tf_euler = il2::find_method(transform, "get_eulerAngles", 0);
    api.tf_set_euler = il2::find_method(transform, "set_eulerAngles", 1);
    api.tf_scale = il2::find_method(transform, "get_localScale", 0);
    api.tf_set_scale = il2::find_method(transform, "set_localScale", 1);
    api.box_set_size = il2::find_method(box, "set_size", 1);
    api.renderer_material = il2::find_method(renderer, "get_material", 0);
    api.renderer_set_material = il2::find_method(renderer, "set_material", 1);
    api.material_ctor = il2::find_method_params(material, ".ctor", {"UnityEngine.Shader"});
    api.material_main = il2::find_method(material, "get_mainTexture", 0);
    api.material_set_main = il2::find_method(material, "set_mainTexture", 1);
    api.shader_find = il2::find_method_params(shader, "Find", {"System.String"});
    api.load_bundle = il2::find_method_params(bundle_c, "LoadFromFile", {"System.String"});
    api.load_asset = il2::find_method_params(bundle_c, "LoadAsset", {"System.String", "System.Type"});
    api.light_range = il2::find_method(light, "get_range", 0);
    api.light_set_range = il2::find_method(light, "set_range", 1);
    api.light_color = il2::find_method(light, "get_color", 0);
    api.light_set_color = il2::find_method(light, "set_color", 1);
    api.light_set_shadows = il2::find_method(light, "set_shadows", 1);
    api.outdoors_scene = il2::find_method_params(gm, "IsOutDoorsScene", {"System.String"});
    if (audio) api.play_sound = il2::find_method_params(audio, "PlaySound", {"System.String", "UnityEngine.GameObject"});
    api.under_crosshairs = il2::find_method_params(pm, "GetInteractiveObjectUnderCrosshairs", {"System.Single"});
    api.set_hover = il2::find_method(hud, "SetHoverText", 3);
    api.weather_indoor = il2::find_method(weather, "IsIndoorScene", 0);
    api.aurora_active = il2::find_method(aurora, "AuroraIsActive", 0);
    api.aurora_normalized = il2::field_offset(aurora, "m_NormalizedActive");
    api.me_lights = il2::field_offset(me, "m_LocalLights");
    api.me_flicker = il2::field_offset(me, "m_HasFlickerSet");
    api.me_intensity = il2::find_method_params(me, "UpdateIntensity", {"System.Single", "System.Single"});
    api.me_light = il2::find_method(me, "UpdateLight", 1);
    api.me_emissive = il2::find_method(me, "UpdateEmissiveObjects", 1);
    api.me_audio = il2::find_method(me, "UpdateAudio", 0);
    api.me_stop = il2::find_method(me, "StopAudio", 0);
    api.ls_lights = il2::field_offset(ls, "m_LocalLights");
    api.ls_intensity = il2::field_offset(ls, "m_CurIntensity");
    api.ls_update_intensity = il2::find_method(ls, "UpdateIntensity", 1);
    api.ls_light = il2::find_method(ls, "UpdateLight", 1);
    api.ls_emissive = il2::find_method(ls, "UpdateEmissiveObjects", 1);
    api.ls_audio = il2::find_method(ls, "UpdateAudio", 0);
    api.ls_stop = il2::find_method(ls, "StopAudio", 0);
    const bool core = api.find_objects && api.object_name && api.instantiate && api.component_go && api.go_tf &&
                      api.tf_find && api.load_bundle && api.load_asset && api.light_set_range && api.light_set_color &&
                      api.outdoors_scene && api.under_crosshairs && api.set_hover && api.me_lights >= 0 &&
                      api.me_intensity && api.me_light && api.ls_lights >= 0 && api.aurora_active;
    if (!core) return false;
    auto hook = [](const char* label, const MethodInfo* method, auto detour, auto* original) {
        return method && hooks::install_method(label, method, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    bool ok = hook("GameManager.InstantiatePlayerObject", il2::find_method(gm, "InstantiatePlayerObject", 0),
                   &player_object_detour, &g_player_object);
    ok &= hook("AuroraModularElectrolizer.Initialize", il2::find_method(me, "Initialize", 0), &me_init_detour, &g_me_init);
    ok &= hook("AuroraManager.RegisterAuroraLightSimple", il2::find_method(aurora, "RegisterAuroraLightSimple", 1),
               &register_simple_detour, &g_register_simple);
    ok &= hook("AuroraManager.Update", il2::find_method(aurora, "Update", 0), &aurora_update_detour, &g_aurora_update);
    ok &= hook("PlayerManager.UpdateHUDText", il2::find_method(pm, "UpdateHUDText", 1), &hud_text_detour, &g_hud_text);
    ok &= hook("PlayerManager.InteractiveObjectsProcessInteraction", il2::find_method(pm, "InteractiveObjectsProcessInteraction", 0),
               &interact_detour, &g_interact);
    hook("Weather.IsTooDarkForAction", il2::find_method(weather, "IsTooDarkForAction", 1), &too_dark_detour, &g_too_dark);
    return ok;
}

}  // namespace

void init() {
    api.ok = resolve();
    logx::info("houselights: House Lights %s", api.ok ? "ready" : "unavailable (TLD's calls missing)");
}

void set_settings(const config::Settings& s) {
    coop_features::set_own(net::kFeatureHouseLights, s.house_lights);
    g_lv.intensity = s.hl_intensity;
    g_lv.range = s.hl_range;
    g_lv.white = s.hl_white;
    g_lv.no_flicker = s.hl_no_flicker;
    g_lv.cull = s.hl_cull;
    g_lv.shadows = s.hl_shadows;
    g_lv.audio = s.hl_audio;
    g_lv.reach = s.hl_reach;
    g_lv.outside = s.hl_outside;
    ++g_lv.generation;
}

void on_frame(const std::string& scene, bool in_game) {
    if (!api.ok) return;
    // Another player's switch: theirs and ours are the same lights (the host passes it on).
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::HouseLights, in);
    const auto role = netrt::role();
    const bool host = role && *role == net::Role::Host;
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        const bool on = r.get<uint8_t>() != 0;
        const std::string sc = r.get_blob(64);
        if (!r.ok() || sc.empty()) continue;
        if (host) {
            net::Writer w(net::Msg::HouseLights, 128);
            w.put<uint8_t>(on ? 1 : 0);
            w.put_blob(sc, 64);
            netrt::send_packet_except(p.from, w, true);
        }
        g_scene_on[sc] = on;
        if (sc == g_scene && active()) set_on(on, "another player's switch");
    }
    if (!in_game || scene != g_scene || !active()) return;
    // Lights on in our scene: said now and then, so a player who comes in has them on too.
    if (g_on && static_cast<double>(GetTickCount64()) - g_last_said >= kStateEveryMs) say(false);
}

}  // namespace house_lights
