// TLDCoop_DLC: the other players leave footprints in the snow (Karol: "nie widać śladów na śniegu innych graczy").
// TLD makes only our own player's trail (FootstepTrailManager.AddPlayerFootstep from the player's feet); the avatars
// had none. Each shown avatar outdoors gets a trail of its own (FootstepTrail(AiPersistent, HumanFootprint), added to
// FootstepTrailManager like an animal's): a print every stride along where it is drawn, left and right in turn, on
// the ground straight under it when that ground is snow (Physics.Raycast down, the collider's tag "Snow").

#include "avatar_tracks.h"

#include <cmath>
#include <map>
#include <string>

#include "avatar.h"
#include "il2cpp_api.h"
#include "log.h"

namespace avatar_tracks {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr int kTrailAiPersistent = 2;  // FootstepTrailType
constexpr int kHumanFootprint = 0;     // SnowImprintType
constexpr float kStride = 0.75f;       // m between prints (one foot each)
constexpr float kSide = 0.12f;         // m from the middle to a foot
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr bool kEnabled = false;

struct V3 {
    float x, y, z;
};
struct RaycastHit {  // UnityEngine.RaycastHit
    V3 point;
    V3 normal;
    uint32_t face;
    float distance;
    float uv[2];
    int32_t collider;
};

struct Api {
    bool ok = false;
    Il2CppClass* trail_class = nullptr;
    FieldInfo* manager_instance = nullptr;  // static FootstepTrailManager.m_Instance
    const MethodInfo* trail_ctor = nullptr, *add_trail = nullptr, *add_step = nullptr;
    const MethodInfo* raycast = nullptr, *collider_by_id = nullptr, *compare_tag = nullptr;
} api;

struct Track {
    uintptr_t trail = 0;      // gchandle of its FootstepTrail
    uintptr_t manager = 0;    // the FootstepTrailManager it was added to (a new scene has another)
    bool have_last = false;
    V3 last{};                // where the last print was put down (the middle, not the foot)
    bool left = false;
};
std::map<uint8_t, Track> g_tracks;
int g_made = 0, g_steps_logged = 0, g_not_snow_logged = 0;

Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
void drop(uintptr_t& h) {
    if (h) il2::il2cpp_gchandle_free(h);
    h = 0;
}

Il2CppObject* manager() {
    Il2CppObject* m = nullptr;
    if (api.manager_instance) il2::il2cpp_field_static_get_value(api.manager_instance, &m);
    return alive(m) ? m : nullptr;
}

Il2CppObject* trail_for(Track& t, Il2CppObject* mgr) {
    Il2CppObject* trail = target(t.trail);
    if (trail && target(t.manager) == mgr) return trail;
    drop(t.trail);
    drop(t.manager);
    trail = il2::il2cpp_object_new(api.trail_class);
    if (!trail) return nullptr;
    int32_t type = kTrailAiPersistent, imprint = kHumanFootprint;
    void* ca[] = {&type, &imprint};
    il2::invoke(api.trail_ctor, trail, ca);
    bool is_player = false;
    void* aa[] = {trail, &is_player};
    il2::invoke(api.add_trail, mgr, aa);
    t.trail = il2::il2cpp_gchandle_new(trail, false);
    t.manager = il2::il2cpp_gchandle_new(mgr, false);
    if (g_made++ < 8) logx::info("tracks: a footprint trail for another player");
    return trail;
}

// The snow under a foot: where, and its slope. False when there's no ground below or it isn't snow.
bool snow_under(const V3& foot, V3& point, V3& normal) {
    V3 origin{foot.x, foot.y + 0.8f, foot.z}, down{0, -1, 0};
    RaycastHit hit{};
    float dist = 2.0f;
    int32_t mask = -5;  // Physics.DefaultRaycastLayers
    int32_t triggers = 1;  // QueryTriggerInteraction.Ignore
    void* ra[] = {&origin, &down, &hit, &dist, &mask, &triggers};
    bool got = false;
    if (!il2::invoke_value(api.raycast, nullptr, got, ra) || !got) return false;
    void* ca[] = {&hit.collider};
    Il2CppObject* col = il2::invoke(api.collider_by_id, nullptr, ca);
    if (!col) return false;
    void* ta[] = {il2::il2cpp_string_new("Snow")};
    bool snow = false;
    if (!il2::invoke_value(api.compare_tag, col, snow, ta) || !snow) {
        if (g_not_snow_logged++ < 3) logx::info("tracks: no print (the ground there isn't tagged snow)");
        return false;
    }
    point = hit.point;
    normal = hit.normal;
    return true;
}

void step(Track& t, Il2CppObject* trail, const V3& at, float yaw) {
    const float rad = yaw * 3.14159265f / 180.0f;
    // Unity: forward (sin yaw, 0, cos yaw); right (cos yaw, 0, -sin yaw)
    const float side = t.left ? -kSide : kSide;
    const V3 foot{at.x + std::cos(rad) * side, at.y, at.z - std::sin(rad) * side};
    V3 point{}, normal{};
    if (!snow_under(foot, point, normal)) return;
    V3 owner = at;
    float y_rot = yaw, deep = 0;
    bool flip = t.left;
    int32_t variant = 0;
    void* sa[] = {&owner, &point, &normal, &y_rot, &flip, &variant, &deep};
    il2::invoke(api.add_step, trail, sa);
    if (g_steps_logged++ < 3) logx::info("tracks: another player's footprint at (%.1f, %.1f, %.1f)", point.x, point.y, point.z);
}

}  // namespace

bool init() {
    // TLDCoop_DLC: off for now. With these trails in the game the snow round the player rendered black at dusk while
    // the trees stayed lit (Karol's screenshots 22:54, the first evening with them); not shown to be the cause, but the
    // only change to TLD's snow imprints that evening.
    if (!kEnabled) {
        logx::info("tracks: off (the others leave no footprints)");
        return false;
    }
    Il2CppClass* trail = il2::find_class(kGame, "", "FootstepTrail");
    Il2CppClass* mgr = il2::find_class(kGame, "", "FootstepTrailManager");
    Il2CppClass* physics = il2::find_class(kPhysics, "UnityEngine", "Physics");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    if (!trail || !mgr || !physics || !component) {
        logx::warn("tracks: TLD's footprint classes missing; no footprints for the others");
        return false;
    }
    api.trail_class = trail;
    api.manager_instance = il2::il2cpp_class_get_field_from_name(mgr, "m_Instance");
    api.trail_ctor = il2::find_method_params(trail, ".ctor", {"FootstepTrailType", "SnowImprintType"});
    api.add_trail = il2::find_method(mgr, "AddFootstepTrail", 2);
    api.add_step = il2::find_method(trail, "AddFootstep", 7);
    api.raycast = il2::find_method_params(physics, "Raycast",
                                          {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit&",
                                           "System.Single", "System.Int32", "UnityEngine.QueryTriggerInteraction"});
    api.collider_by_id = il2::find_method(physics, "GetColliderByInstanceID", 1);
    api.compare_tag = il2::find_method_params(component, "CompareTag", {"System.String"});
    api.ok = api.manager_instance && api.trail_ctor && api.add_trail && api.add_step && api.raycast &&
             api.collider_by_id && api.compare_tag;
    logx::info("tracks: %s", api.ok ? "ready (the others leave footprints)" : "unavailable (TLD's calls missing)");
    return api.ok;
}

void update(bool in_game) {
    if (!api.ok) return;
    Il2CppObject* mgr = in_game ? manager() : nullptr;
    if (!mgr) {
        for (auto& [id, t] : g_tracks) t.have_last = false;
        return;
    }
    for (const avatar::Seen& s : avatar::seen()) {
        Track& t = g_tracks[s.player];
        if (!s.here || s.at_door || !s.place.empty() || s.dead) {
            t.have_last = false;
            continue;
        }
        const V3 at{s.x, s.y, s.z};
        if (!t.have_last) {
            t.last = at;
            t.have_last = true;
            continue;
        }
        const float dx = at.x - t.last.x, dz = at.z - t.last.z, dy = at.y - t.last.y;
        const float d2 = dx * dx + dz * dz;
        if (d2 > 25.0f || std::fabs(dy) > 3.0f) {  // a jump (teleport, handover): start again from here
            t.last = at;
            continue;
        }
        if (d2 < kStride * kStride) continue;
        Il2CppObject* trail = trail_for(t, mgr);
        if (!trail) continue;
        t.left = !t.left;
        step(t, trail, at, s.yaw);
        t.last = at;
    }
}

}  // namespace avatar_tracks
