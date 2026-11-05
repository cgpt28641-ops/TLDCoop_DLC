#include "coop_activity.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"

namespace coop_activity {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;  // UnityEngine.Object.m_CachedPtr: 0 once the native object is gone

struct Api {
    const MethodInfo* player_manager = nullptr;  // static GameManager.GetPlayerManagerComponent()
    const MethodInfo* control_mode = nullptr;    // PlayerManager.GetControlMode()
    const MethodInfo* find_all = nullptr;        // static Resources.FindObjectsOfTypeAll(Type)
    const MethodInfo* find_one = nullptr;        // static Object.FindObjectOfType(Type)
    const MethodInfo* panel_enabled = nullptr;   // Panel_Base.IsEnabled() (virtual)
    Il2CppObject* t_controller = nullptr;        // typeof(vp_FPSController)
    const MethodInfo* grounded = nullptr;        // vp_FPSController.get_Grounded()
} api;
bool g_ready = false;

// PlayerControlMode values, by name (they move between game versions)
struct Mode {
    const char* name;
    uint8_t activity;
    int64_t value = -1;
};
Mode g_modes[] = {
    {"Climbing", net::ActClimb},     {"StartingFire", net::ActFire}, {"InSeat", net::ActSit},
    {"InSnowShelter", net::ActSit},  {"BigCarry", net::ActCarry},    {"DeployRope", net::ActKneel},
    {"TakeRope", net::ActKneel},     {"InFishingHut", net::ActSit},
};

// TLD's panels that put the body to work while they're open
struct PanelKind {
    const char* name;
    uint8_t activity;
    Il2CppObject* type = nullptr;
    std::vector<uintptr_t> found;  // its instances (prefabs among them never open)
    double looked = -1e18;
};
PanelKind g_panels[] = {
    {"Panel_BodyHarvest", net::ActKneel}, {"Panel_Cooking", net::ActKneel}, {"Panel_FeedFire", net::ActKneel},
    {"Panel_FireStart", net::ActFire},    {"Panel_BreakDown", net::ActChop}, {"Panel_Container", net::ActSearch},
    {"Panel_Crafting", net::ActWork},     {"Panel_Repair", net::ActWork},   {"Panel_IceFishing", net::ActSit},
};

double g_searched = -1e18;  // one search for all panel kinds at a time

bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }

bool panel_open(PanelKind& p, double now) {
    if (!p.type) return false;
    bool any_alive = false;
    for (uintptr_t h : p.found) {
        Il2CppObject* o = il2::il2cpp_gchandle_get_target(h);
        if (!alive(o)) continue;
        any_alive = true;
        bool on = false;
        if (il2::invoke_value(il2::virtual_method(o, api.panel_enabled), o, on) && on) return true;
    }
    // TLD makes a panel the first time it's needed: look again now and then until we have a live one
    if (!any_alive && now - p.looked > 5000 && now - g_searched > 500) {
        p.looked = now;
        g_searched = now;
        for (uintptr_t h : p.found) il2::il2cpp_gchandle_free(h);
        p.found.clear();
        void* a[] = {p.type};
        Il2CppObject* all = il2::invoke(api.find_all, nullptr, a);
        for (size_t i = 0; i < il2::array_length(all) && p.found.size() < 4; ++i)
            if (Il2CppObject* o = il2::array_object_at(all, i); alive(o)) p.found.push_back(il2::il2cpp_gchandle_new(o, false));
    }
    return false;
}

// Our footing: off the ground (TLD's controller), or falling fast (from our height, if the controller can't be read)
std::string g_scene;
uintptr_t g_controller = 0;
double g_last_t = 0, g_air_since = -1;
float g_last_y = 0, g_vy = 0;

bool off_ground(const std::string& scene, float y, double now) {
    const double dt = (now - g_last_t) / 1000.0;
    if (scene != g_scene || dt <= 0 || dt > 1) {
        g_vy = 0;
    } else {
        g_vy += (static_cast<float>((y - g_last_y) / dt) - g_vy) * 0.5f;
    }
    g_last_y = y;
    g_last_t = now;
    if (scene != g_scene) {
        g_scene = scene;
        if (g_controller) il2::il2cpp_gchandle_free(g_controller);
        g_controller = 0;
    }
    Il2CppObject* c = g_controller ? il2::il2cpp_gchandle_get_target(g_controller) : nullptr;
    if (!alive(c) && api.t_controller && api.find_one && api.grounded) {
        if (g_controller) il2::il2cpp_gchandle_free(g_controller);
        g_controller = 0;
        void* a[] = {api.t_controller};
        c = il2::invoke(api.find_one, nullptr, a);
        if (alive(c)) g_controller = il2::il2cpp_gchandle_new(c, false);
    }
    bool grounded = true;
    const bool read = alive(c) && il2::invoke_value(api.grounded, c, grounded);
    const bool air = read ? !grounded : g_vy < -4.0f;
    // a step off a kerb isn't a jump: only after a moment in the air
    if (!air) {
        g_air_since = -1;
        return false;
    }
    if (g_air_since < 0) g_air_since = now;
    return now - g_air_since > 250 || g_vy < -4.0f;
}

}  // namespace

bool init() {
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* mode = il2::find_class(kGame, "", "PlayerControlMode");
    api.player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    api.control_mode = il2::find_method(pm, "GetControlMode", 0);
    api.find_all = il2::find_method_params(il2::find_class(kCore, "UnityEngine", "Resources"), "FindObjectsOfTypeAll", {"System.Type"});
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    api.find_one = il2::find_method_params_quiet(object, "FindObjectOfType", {"System.Type"});
    api.panel_enabled = il2::find_method(il2::find_class(kGame, "", "Panel_Base"), "IsEnabled", 0);
    if (Il2CppClass* controller = il2::find_class(kGame, "", "vp_FPSController")) {
        api.t_controller = il2::type_object(controller);
        api.grounded = il2::find_method_quiet(controller, "get_Grounded", 0);
    }
    int modes = 0, panels = 0;
    if (mode)
        for (Mode& m : g_modes)
            if (il2::enum_value(mode, m.name, m.value)) ++modes;
    for (PanelKind& p : g_panels)
        if (Il2CppClass* k = il2::find_class(kGame, "", p.name)) {
            p.type = il2::type_object(k);
            ++panels;
        }
    g_ready = api.player_manager && api.control_mode && api.find_all && api.panel_enabled;
    logx::info("activity: %s (%d control modes, %d panels, footing %s)", g_ready ? "ready" : "INCOMPLETE", modes, panels,
               api.grounded ? "from the controller" : "from the height");
    return g_ready;
}

uint8_t read_now(const std::string& scene, float y, double now);

uint8_t current(const std::string& scene, float y, double now) {
    static double at = -1e18;
    static uint8_t last = net::ActNone;
    if (now - at < 100 && scene == g_scene) return last;  // ten times a second is plenty
    at = now;
    last = read_now(scene, y, now);
    return last;
}

uint8_t read_now(const std::string& scene, float y, double now) {
    if (!g_ready) return net::ActNone;
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    int32_t mode = -1;
    if (pm && il2::invoke_value(api.control_mode, pm, mode))
        for (const Mode& m : g_modes)
            if (m.value >= 0 && m.value == mode) {
                off_ground(scene, y, now);  // keep the height's history going
                return m.activity;
            }
    if (off_ground(scene, y, now)) return net::ActAir;
    for (PanelKind& p : g_panels)
        if (panel_open(p, now)) return p.activity;
    return net::ActNone;
}

}  // namespace coop_activity
