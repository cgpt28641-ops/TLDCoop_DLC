#include "seamless_interiors.h"
#include "world_items.h"

// TLDCoop_DLC: Seamless Interiors (karsontr13), ported from its C# source function by function; the SI function each
// part comes from is named in its comment ("SI: Environment.cs PrepareMasterInterior"). See seamless_interiors.h for
// what milestone 1a covers. Every step logs one "interiors:" line; every missing game API is logged once at start.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "coop_features.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"
#include "paths.h"

namespace seamless_interiors {
namespace {

// --- the building list (tools/seamless/gen_configs.py) -----------------------------------------------------------------

struct V3 {
    float x = 0, y = 0, z = 0;
};
struct DoorSpawn {
    const char* door;
    V3 door_pos, entry, exit;
};
struct SubLink {
    const char* target;
    V3 parent_door, child_spawn, parent_spawn;
};
struct Config {
    const char* id;      // SI: InteriorConfig.ResolvedInstanceId
    const char* region;  // ExteriorSceneName
    const char* scene;   // InteriorSceneBaseName (+ _SANDBOX, _DLC01)
    const char* shell;   // ExteriorShellPrefabName ("" for a sub-interior)
    float y_offset;
    V3 scale;            // ScaleAdjustment (only for the hand-typed points; a parked room loads at scale 1)
    V3 fallback;         // FallbackPosition
    bool force_exact;    // ForceExactPosition
    V3 rotation;         // RotationOffset (euler degrees)
    V3 entry, exit;      // EntrySpawnPosition / ExitSpawnPosition (zero: none)
    int doors_first, doors_count;
    int links_first, links_count;
    int destroy_first, destroy_count;  // ObjectsToDestroy (in kNames)
    int disable_first, disable_count;  // ObjectsToDisable, the painted window backdrops left out
    float bounds_radius;
};
#include "seamless_configs.inc"
constexpr int kConfigCount = static_cast<int>(sizeof(kConfigs) / sizeof(kConfigs[0]));

// --- constants from SI -------------------------------------------------------------------------------------------------

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr const char* kAI = "UnityEngine.AIModule.dll";
constexpr const char* kAddressables = "Unity.Addressables.dll";
constexpr const char* kWwise = "AK.Wwise.Unity.API.dll";
constexpr const char* kWwiseTypes = "AK.Wwise.Unity.API.WwiseTypes.dll";
constexpr const char* kWwiseBehaviours = "AK.Wwise.Unity.MonoBehaviour.dll";
constexpr const char* kParticles = "UnityEngine.ParticleSystemModule.dll";

constexpr ptrdiff_t kCachedPtr = 0x10;                     // UnityEngine.Object.m_CachedPtr: null once destroyed
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;   // List<T>._items, _size
constexpr size_t kRaycastHitSize = 44;                     // RaycastHit unboxed: point, normal, face, distance, uv, collider
constexpr ptrdiff_t kHitPoint = 0x00, kHitDistance = 0x1c, kHitCollider = 0x28;

const V3 kParkOffset{0, 80, 0};             // SI: InteriorConfig.ParkOffset, the default for every building
const V3 kTriggerPadding{1, 3, 1};          // SI: Weather.cs TRIGGER_PADDING / Instances.cs TRIGGER_EXPANSION
constexpr float kParkedSideAllowance = 1.5f, kParkedHeightAllowance = 3.0f;  // SI: Instances.cs PARKED_ROOM_*
constexpr float kShellMatchMax = 10.0f;     // SI: Environment.cs SHELL_MATCH_MAX_DISTANCE
constexpr float kBuildDistance = 80.0f;     // the port: a building's room is built when the player comes this near
constexpr double kAwakeWaitMs = 3000;       // SI: Run, "give container Awake time to fire"
constexpr double kPreRollWaitMs = 500;      // SI: Run, after HandleInitialPlaceables
constexpr double kSceneLoadTimeoutMs = 45000;  // SI: the scene load lock heals itself after 45 s
constexpr double kPortalSuppressMs = 2000;  // SI: PORTAL_SUPPRESS_WINDOW
constexpr double kFallGuardMs = 2000;       // the port: no fall damage this long after one of our teleports
constexpr float kDoorFadeOut = 0.25f, kDoorFadeHold = 0.25f, kDoorFadeIn = 0.25f;  // SI: PortalPatches DOOR_FADE_*
constexpr float kEntryFacingReach = 3.0f;   // SI: ENTRY_FACING_DOOR_REACH
constexpr int kDefaultRaycastLayers = -5;   // Physics.DefaultRaycastLayers (~IgnoreRaycastLayer)
constexpr int kQueryIgnore = 1, kQueryCollide = 2;  // QueryTriggerInteraction
constexpr int kLightDirectional = 1, kLightPoint = 2;  // LightType
constexpr int kOcclusionMild = 1;           // SI: s_InsideOcclusionLevel = 1 (AudioOcclusionLevel.MildOcclusion)
constexpr uint32_t kIndoorWindEvent = 3922582288u, kOutdoorWindEvent = 2660556499u;  // SI: RoomSound.cs
constexpr int kOutdoorsFadeOutMs = 250, kOutdoorsFadeInMs = 250, kIndoorWindFadeOutMs = 1000;

const char* const kLightShafts[] = {"FX_LightShaft_B", "FX_LightShaft_E"};  // SI: s_LightShaftObjects
const char* const kLightContainers[] = {"WindowLight", "WindowLightGroup"};  // SI: s_DarkModeLightContainers
// SI: s_DarkModeProtectedFromDestroy: kept and switched by lighting mode; the outdoor mode (ours) keeps them off.
const char* const kDarkOnly[] = {"InteriorLightingManager_Prefab", "DLM_AFHangar_LGT_Prefab", "DarkLightingManager_Prefab",
                                 "Daytime", "Daylight", "Midday", "AmbientLight", "Nighttime", "NightTime"};
const char* const kShellGroups[] = {"Mine_Buildings"};  // SI: s_ShellGroups (the parent of several buildings)

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// --- small maths (Unity's conventions) ---------------------------------------------------------------------------------

V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
bool is_zero(V3 a) { return a.x == 0 && a.y == 0 && a.z == 0; }
float dist(V3 a, V3 b) {
    const V3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}
float dist_xz(V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
Quat mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quat conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
V3 rotate(Quat q, V3 v) {
    const Quat p{v.x, v.y, v.z, 0};
    const Quat r = mul(mul(q, p), conj(q));
    return {r.x, r.y, r.z};
}
Quat axis_angle(float deg, V3 axis) {
    const float h = deg * 3.14159265f / 360.0f;
    return {axis.x * std::sin(h), axis.y * std::sin(h), axis.z * std::sin(h), std::cos(h)};
}
// Quaternion.Euler: z, then x, then y.
Quat euler(V3 deg) {
    return mul(axis_angle(deg.y, {0, 1, 0}), mul(axis_angle(deg.x, {1, 0, 0}), axis_angle(deg.z, {0, 0, 1})));
}
Quat look_yaw(V3 dir) {  // Quaternion.LookRotation(dir flat, up)
    const float yaw = std::atan2(dir.x, dir.z) * 180.0f / 3.14159265f;
    return axis_angle(yaw, {0, 1, 0});
}

struct Box {  // world AABB
    V3 min{1e30f, 1e30f, 1e30f}, max{-1e30f, -1e30f, -1e30f};
    bool valid() const { return min.x <= max.x; }
    void add(V3 p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    bool contains(V3 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
};
struct UBounds {  // UnityEngine.Bounds
    V3 center, extents;
};

// --- the game ----------------------------------------------------------------------------------------------------------

struct Api {
    bool ok = false;
    // types
    Il2CppClass* go_class = nullptr;
    Il2CppObject* t_transform = nullptr, *t_renderer = nullptr, *t_mesh_renderer = nullptr, *t_light = nullptr,
                  *t_camera = nullptr, *t_listener = nullptr, *t_probe = nullptr, *t_box = nullptr, *t_collider = nullptr,
                  *t_obstacle = nullptr, *t_mesh_filter = nullptr, *t_particles = nullptr, *t_char_controller = nullptr,
                  *t_gear = nullptr, *t_placeable = nullptr, *t_container = nullptr, *t_rso = nullptr,
                  *t_load_scene = nullptr, *t_indoor = nullptr, *t_object_guid = nullptr, *t_grabber = nullptr,
                  *t_ilm = nullptr, *t_dlm = nullptr, *t_gimble = nullptr, *t_fire = nullptr, *t_stove = nullptr,
                  *t_campfire = nullptr, *t_audio_zone = nullptr, *t_ak_state = nullptr, *t_wind = nullptr,
                  *t_snow_planes = nullptr, *t_post_layer = nullptr;
    // UnityEngine.Object / GameObject / Component / Transform
    const MethodInfo* name = nullptr, *set_name = nullptr, *destroy = nullptr, *destroy_now = nullptr,
                      *instantiate = nullptr, *find_objects = nullptr, *find_object = nullptr, *instance_id = nullptr;
    const MethodInfo* go_ctor = nullptr, *go_set_active = nullptr, *go_active_self = nullptr, *go_active_tree = nullptr,
                      *go_transform = nullptr, *go_add = nullptr, *go_get = nullptr, *go_children = nullptr,
                      *go_set_layer = nullptr, *go_layer = nullptr, *go_set_static = nullptr, *go_static = nullptr,
                      *go_scene = nullptr;
    const MethodInfo* comp_go = nullptr, *comp_transform = nullptr, *comp_in_parent = nullptr, *comp_in_children = nullptr;
    const MethodInfo* tf_position = nullptr, *tf_set_position = nullptr, *tf_rotation = nullptr, *tf_set_rotation = nullptr,
                      *tf_set_local_position = nullptr, *tf_set_local_scale = nullptr, *tf_parent = nullptr,
                      *tf_set_parent = nullptr, *tf_is_child_of = nullptr, *tf_find = nullptr, *tf_child_count = nullptr,
                      *tf_child = nullptr, *tf_to_world = nullptr, *tf_root = nullptr,
                      *tf_local_scale = nullptr;
    const MethodInfo* behaviour_enabled = nullptr, *behaviour_set_enabled = nullptr;
    // rendering
    const MethodInfo* r_enabled = nullptr, *r_set_enabled = nullptr, *r_bounds = nullptr, *r_lightmap = nullptr,
                      *r_set_lightmap = nullptr, *r_materials = nullptr, *r_set_materials = nullptr, *r_material = nullptr;
    const MethodInfo* mat_keyword_on = nullptr, *mat_disable_keyword = nullptr;
    const MethodInfo* light_type = nullptr, *light_range = nullptr, *light_set_range = nullptr, *light_set_mask = nullptr,
                      *light_set_shadows = nullptr, *light_set_intensity = nullptr;
    const MethodInfo* mesh_filter_mesh = nullptr, *mesh_bounds = nullptr;
    const MethodInfo* lightmaps = nullptr, *set_lightmaps = nullptr, *probes = nullptr, *set_probes = nullptr;
    const MethodInfo* dynamic_gi_update = nullptr;
    const MethodInfo* name_to_layer = nullptr;
    // scenes
    const MethodInfo* scene_by_name = nullptr, *scene_is_loaded = nullptr, *scene_name = nullptr, *scene_roots = nullptr,
                      *scene_root_count = nullptr, *move_to_scene = nullptr, *set_active_scene = nullptr,
                      *load_scene = nullptr, *unload_scene = nullptr;
    bool scene_roots_boxed = false;  // Scene.GetRootGameObjects' compiled code wants a boxed Scene (adjustor thunk)
    const MethodInfo* scene_roots_internal = nullptr, *go_list_ctor = nullptr;  // static GetRootGameObjectsInternal(int, object)
    Il2CppClass* go_list_class = nullptr;                                         // List<GameObject>
    // physics
    const MethodInfo* raycast_all = nullptr, *overlap_sphere = nullptr, *collider_by_id = nullptr;
    const MethodInfo* col_trigger = nullptr, *col_set_trigger = nullptr, *col_enabled = nullptr, *col_set_enabled = nullptr;
    const MethodInfo* box_set_center = nullptr, *box_set_size = nullptr;
    const MethodInfo* obstacle_shape = nullptr, *obstacle_center = nullptr, *obstacle_size = nullptr,
                      *obstacle_carving = nullptr;
    const MethodInfo* cc_radius = nullptr, *cc_set_radius = nullptr, *cc_slope = nullptr, *cc_set_slope = nullptr;
    // random
    const MethodInfo* rnd_state = nullptr, *rnd_set_state = nullptr, *rnd_init = nullptr, *rnd_range = nullptr;
    // TLD
    const MethodInfo* gm_player_manager = nullptr, *gm_player_transform = nullptr, *gm_player_object = nullptr,
                      *gm_main_camera = nullptr, *gm_weather = nullptr, *gm_wind = nullptr, *gm_global = nullptr,
                      *gm_movement = nullptr, *gm_fps_camera = nullptr, *gm_main_menu = nullptr;
    FieldInfo* gm_active_scene = nullptr;
    const MethodInfo* teleport = nullptr, *reset_interaction = nullptr;
    ptrdiff_t pm_indoor = -1, pm_indoors = -1;  // PlayerManager.m_IndoorSpaceTrigger, m_IndoorSpaceTriggers
    ptrdiff_t door_scene = -1, door_exit_point = -1, door_touch_ground = -1, door_guid = -1;  // LoadScene fields
    const MethodInfo* door_mark_explored = nullptr;
    const MethodInfo* fade_out = nullptr, *fade_in = nullptr;
    const MethodInfo* play_sound = nullptr, *gui_click = nullptr, *audio_instance = nullptr, *enter_occlusion = nullptr,
                      *exit_occlusion = nullptr, *update_occlusion = nullptr;
    ptrdiff_t gam_heavy = -1, gam_medium = -1, gam_mild = -1;
    ptrdiff_t zone_occluded = -1, zone_level = -1;  // AudioStateZoneTrigger
    const MethodInfo* ak_get_state = nullptr, *ak_set_state = nullptr, *ak_event_of = nullptr, *ak_action = nullptr,
                      *ak_stop = nullptr;
    uint32_t ak_scene_group = 0, ak_scene_none = 0, ak_interior_group = 0, ak_interior_none = 0, ak_interior_int = 0;
    bool ak_states_ok = false;
    ptrdiff_t ak_state_data = -1;
    const MethodInfo* ak_group_id = nullptr, *ak_value_id = nullptr;  // AK.Wwise.BaseGroupType.get_GroupId, BaseType.get_Id
    ptrdiff_t wind_force_stopped = -1, wind_loop = -1, wind_occluded = -1;
    const MethodInfo* wind_play_loop = nullptr, *wind_force_stop = nullptr;
    ptrdiff_t weather_chill = -1, weather_falling = -1, weather_blowing = -1;
    const MethodInfo* weather_force_outdoor = nullptr, *weather_indoor_scene = nullptr;
    const MethodInfo* lm_level_done = nullptr, *lm_strength_default = nullptr;
    FieldInfo* lm_level_loaded = nullptr;
    ptrdiff_t gp_radius_in = -1, gp_radius_out = -1, gp_near_in = -1, gp_near_out = -1;
    ptrdiff_t mv_slope_in = -1, mv_slope_out = -1;
    const MethodInfo* fps_near_get = nullptr, *fps_near_set = nullptr;
    ptrdiff_t fps_near = -1;
    ptrdiff_t snow_planes_renderer = -1;
    const MethodInfo* snow_planes_enabled = nullptr;
    ptrdiff_t post_fog = -1, fog_enabled = -1;  // PostProcessLayer.fog, Fog.enabled
    ptrdiff_t indoor_dont_count = -1, indoor_outdoor_temp = -1, indoor_delta = -1, indoor_outdoor_light = -1,
              indoor_campfires = -1, indoor_travois = -1, indoor_cant_survey = -1, indoor_valid_safehouse = -1,
              indoor_trigger_id = -1;
    const MethodInfo* indoor_enter = nullptr, *indoor_exit = nullptr;
    const MethodInfo* set_runtime_pdid = nullptr;
    ptrdiff_t rso_list = -1, rso_weights = -1;
    const MethodInfo* rso_quota = nullptr;
    ptrdiff_t gear_rolled = -1, gear_in_inventory = -1;
    const MethodInfo* gear_roll = nullptr, *gear_disable_physics = nullptr, *gear_has_physics = nullptr;
    ptrdiff_t placeable_invalidated = -1;
    ptrdiff_t grabber_camera = -1;
    ptrdiff_t fire_heat = -1;
    const MethodInfo* fire_add = nullptr, *stove_add = nullptr, *campfire_add = nullptr;
    FieldInfo* gear_list = nullptr, *gear_index = nullptr, *container_list = nullptr, *fire_list = nullptr,
             *stove_list = nullptr, *campfire_list = nullptr, *placeables = nullptr, *safehouse_colliders = nullptr;
    const MethodInfo* scene_keys = nullptr;  // SaveGameSlots.GetSceneKeysForCurrentSaveSlot
    int layer_collide_only = -1, layer_trigger = -1, layer_npc = -1;
} api;

Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
uintptr_t hold(Il2CppObject* o) { return o ? il2::il2cpp_gchandle_new(o, false) : 0; }
void drop(uintptr_t& h) {
    if (h) il2::il2cpp_gchandle_free(h);
    h = 0;
}
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
Il2CppObject* live(uintptr_t h) {
    Il2CppObject* o = target(h);
    return alive(o) ? o : nullptr;
}
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, const T& v) {
    if (o && off >= 0) std::memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}
template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) {
    return il2::read_field<T>(o, off);
}
// A managed reference stored into a field: through the GC's write barrier (never put<> for one).
void put_ref(Il2CppObject* o, ptrdiff_t off, void* v) { il2::store_ref(o, off, v); }
void put_slot(Il2CppObject* arr, size_t i, void* v) {  // T[]'s element i (elements from 0x20)
    if (arr && i < il2::array_length(arr)) il2::store_ref(arr, static_cast<ptrdiff_t>(0x20 + i * sizeof(void*)), v);
}
Il2CppString* str(const char* s) { return il2::il2cpp_string_new(s); }
std::string name_of(Il2CppObject* o) {
    Il2CppObject* s = alive(o) ? il2::invoke(api.name, o) : nullptr;
    return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
}
std::string text(Il2CppObject* s) { return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string(); }

Il2CppObject* go_of(Il2CppObject* comp) { return alive(comp) ? il2::invoke(api.comp_go, comp) : nullptr; }
Il2CppObject* tf_of(Il2CppObject* comp) { return alive(comp) ? il2::invoke(api.comp_transform, comp) : nullptr; }
Il2CppObject* go_tf(Il2CppObject* go) { return alive(go) ? il2::invoke(api.go_transform, go) : nullptr; }
bool active_self(Il2CppObject* go) {
    bool a = false;
    return alive(go) && il2::invoke_value(api.go_active_self, go, a) && a;
}
bool active_in_tree(Il2CppObject* go) {
    bool a = false;
    return alive(go) && il2::invoke_value(api.go_active_tree, go, a) && a;
}
void set_active(Il2CppObject* go, bool on) {
    if (!alive(go)) return;
    void* a[] = {&on};
    il2::invoke(api.go_set_active, go, a);
}
void destroy(Il2CppObject* o) {
    if (!alive(o)) return;
    void* a[] = {o};
    il2::invoke(api.destroy, nullptr, a);
}
void destroy_now(Il2CppObject* o) {
    if (!alive(o)) return;
    void* a[] = {o};
    il2::invoke(api.destroy_now, nullptr, a);
}
V3 position(Il2CppObject* tf) {
    V3 p;
    if (alive(tf)) il2::invoke_value(api.tf_position, tf, p);
    return p;
}
void set_position(Il2CppObject* tf, V3 p) {
    if (!alive(tf)) return;
    void* a[] = {&p};
    il2::invoke(api.tf_set_position, tf, a);
}
Quat rotation(Il2CppObject* tf) {
    Quat q;
    if (alive(tf)) il2::invoke_value(api.tf_rotation, tf, q);
    return q;
}
Il2CppObject* parent_of(Il2CppObject* tf) { return alive(tf) ? il2::invoke(api.tf_parent, tf) : nullptr; }
bool child_of(Il2CppObject* tf, Il2CppObject* parent_tf) {
    if (!alive(tf) || !alive(parent_tf)) return false;
    bool r = false;
    void* a[] = {parent_tf};
    return il2::invoke_value(api.tf_is_child_of, tf, r, a) && r;
}
Il2CppObject* find_child(Il2CppObject* tf, const char* path) {
    if (!alive(tf)) return nullptr;
    void* a[] = {str(path)};
    Il2CppObject* t = il2::invoke(api.tf_find, tf, a);
    return alive(t) ? t : nullptr;
}
Il2CppObject* component(Il2CppObject* go, Il2CppObject* type) {
    if (!alive(go) || !type) return nullptr;
    void* a[] = {type};
    Il2CppObject* c = il2::invoke(api.go_get, go, a);
    return alive(c) ? c : nullptr;
}
Il2CppObject* add_component(Il2CppObject* go, Il2CppObject* type) {
    if (!alive(go) || !type) return nullptr;
    void* a[] = {type};
    return il2::invoke(api.go_add, go, a);
}
// GameObject.GetComponentsInChildren(Type, includeInactive): every component of that type under go (go's own too).
template <typename F>
void each_in(Il2CppObject* go, Il2CppObject* type, F&& f) {
    if (!alive(go) || !type || !api.go_children) return;
    bool inactive = true;
    void* a[] = {type, &inactive};
    Il2CppObject* arr = il2::invoke(api.go_children, go, a);
    for (size_t i = 0, n = il2::array_length(arr); i < n; ++i)
        if (Il2CppObject* c = il2::array_object_at(arr, i); alive(c)) f(c);
}
Il2CppObject* in_parent(Il2CppObject* comp, Il2CppObject* type) {
    if (!alive(comp) || !type || !api.comp_in_parent) return nullptr;
    bool inactive = true;
    void* a[] = {type, &inactive};
    Il2CppObject* c = il2::invoke(api.comp_in_parent, comp, a);
    return alive(c) ? c : nullptr;
}
bool has_in_children(Il2CppObject* comp, Il2CppObject* type) {
    if (!alive(comp) || !type || !api.comp_in_children) return false;
    bool inactive = true;
    void* a[] = {type, &inactive};
    return alive(il2::invoke(api.comp_in_children, comp, a));
}
bool behaviour_on(Il2CppObject* b) {
    bool on = false;
    return alive(b) && il2::invoke_value(api.behaviour_enabled, b, on) && on;
}
void set_behaviour(Il2CppObject* b, bool on) {
    if (!alive(b)) return;
    void* a[] = {&on};
    il2::invoke(api.behaviour_set_enabled, b, a);
}
bool renderer_on(Il2CppObject* r) {
    bool on = false;
    return alive(r) && il2::invoke_value(api.r_enabled, r, on) && on;
}
void set_renderer(Il2CppObject* r, bool on) {
    if (!alive(r)) return;
    void* a[] = {&on};
    il2::invoke(api.r_set_enabled, r, a);
}
bool collider_on(Il2CppObject* c) {
    bool on = false;
    return alive(c) && il2::invoke_value(api.col_enabled, c, on) && on;
}
void set_collider(Il2CppObject* c, bool on) {
    if (!alive(c)) return;
    void* a[] = {&on};
    il2::invoke(api.col_set_enabled, c, a);
}
int instance_id(Il2CppObject* o) {
    int32_t id = 0;
    if (alive(o)) il2::invoke_value(api.instance_id, o, id);
    return id;
}
Il2CppObject* static_object(FieldInfo* f) {
    Il2CppObject* o = nullptr;
    if (f) il2::il2cpp_field_static_get_value(f, &o);
    return o;
}
// List<T> of objects.
template <typename F>
void each_listed(Il2CppObject* list, F&& f) {
    Il2CppObject* items = list ? get<Il2CppObject*>(list, kListItems) : nullptr;
    const int32_t n = list ? get<int32_t>(list, kListSize) : 0;
    const size_t len = il2::array_length(items);
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < len; ++i) f(il2::array_object_at(items, static_cast<size_t>(i)), i);
}
int list_count(Il2CppObject* list) { return list ? get<int32_t>(list, kListSize) : 0; }
// List<T>[i], bounds-checked against both the list's size and its backing array (nullptr outside).
Il2CppObject* list_at(Il2CppObject* list, int i) {
    if (!list || i < 0 || i >= list_count(list)) return nullptr;
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    if (static_cast<size_t>(i) >= il2::array_length(items)) return nullptr;
    return il2::array_object_at(items, static_cast<size_t>(i));
}
const MethodInfo* method_up(Il2CppObject* obj, const char* name, int args) {
    for (Il2CppClass* k = obj ? il2::il2cpp_object_get_class(obj) : nullptr; k; k = il2::il2cpp_class_get_parent(k))
        if (const MethodInfo* m = il2::il2cpp_class_get_method_from_name(k, name, args)) return m;
    return nullptr;
}
void list_remove_at(Il2CppObject* list, int i) {
    const MethodInfo* m = method_up(list, "RemoveAt", 1);
    if (!m) return;
    int32_t at = i;
    void* a[] = {&at};
    il2::invoke(m, list, a);
}
void list_add(Il2CppObject* list, Il2CppObject* item) {
    const MethodInfo* m = method_up(list, "Add", 1);
    if (!m) return;
    void* a[] = {item};
    il2::invoke(m, list, a);
}
bool list_contains(Il2CppObject* list, Il2CppObject* item) {
    bool found = false;
    each_listed(list, [&](Il2CppObject* o, int) { found = found || o == item; });
    return found;
}

// --- scenes ------------------------------------------------------------------------------------------------------------

int32_t scene_by_name(const std::string& name) {
    void* a[] = {str(name.c_str())};
    int32_t h = 0;
    il2::invoke_value(api.scene_by_name, nullptr, h, a);
    return h;
}
bool scene_loaded(int32_t h) {
    if (!h) return false;
    bool loaded = false;
    void* a[] = {&h};
    return il2::invoke_value(api.scene_is_loaded, nullptr, loaded, a) && loaded;
}
std::string scene_name(int32_t h) {
    if (!h || !api.scene_name) return {};
    void* a[] = {&h};
    return text(il2::invoke(api.scene_name, nullptr, a));
}
int32_t scene_of(Il2CppObject* go) {
    int32_t h = 0;
    if (alive(go) && api.go_scene) il2::invoke_value(api.go_scene, go, h);
    return h;
}
// A scene's root objects: the static Scene.GetRootGameObjectsInternal(handle, List<GameObject>) (what the instance
// method calls), so nothing depends on how a struct's instance method takes its Scene; the instance method
// GetRootGameObjects() only when that can't be called.
std::vector<Il2CppObject*> scene_roots(int32_t h) {
    std::vector<Il2CppObject*> out;
    if (!h) return out;
    if (api.scene_roots_internal && api.go_list_class && api.go_list_ctor) {
        int32_t count = 0;
        if (api.scene_root_count) {
            void* ca[] = {&h};
            if (il2::invoke_value(api.scene_root_count, nullptr, count, ca) && count <= 0) return out;
        }
        Il2CppObject* list = il2::il2cpp_object_new(api.go_list_class);
        if (list) {
            il2::invoke(api.go_list_ctor, list);
            if (count > 0)  // (as the instance method does first)
                if (const MethodInfo* cap = method_up(list, "set_Capacity", 1)) {
                    void* pa[] = {&count};
                    il2::invoke(cap, list, pa);
                }
            void* a[] = {&h, list};
            il2::invoke(api.scene_roots_internal, nullptr, a);
            each_listed(list, [&](Il2CppObject* go, int) {
                if (go) out.push_back(go);
            });
            return out;
        }
    }
    if (!api.scene_roots) return out;
    alignas(16) unsigned char boxed[0x18] = {};
    std::memcpy(boxed + 0x10, &h, sizeof(h));
    Il2CppObject* arr =
        il2::invoke(api.scene_roots, api.scene_roots_boxed ? static_cast<void*>(boxed) : static_cast<void*>(boxed + 0x10));
    for (size_t i = 0, n = il2::array_length(arr); i < n; ++i)
        if (Il2CppObject* go = il2::array_object_at(arr, i)) out.push_back(go);
    return out;
}
void move_to_scene(Il2CppObject* go, int32_t h) {
    if (!alive(go) || !h) return;
    void* a[] = {go, &h};
    il2::invoke(api.move_to_scene, nullptr, a);
}
void set_active_scene(int32_t h) {
    if (!h) return;
    void* a[] = {&h};
    il2::invoke(api.set_active_scene, nullptr, a);
}
std::string active_region() {
    Il2CppObject* s = static_object(api.gm_active_scene);
    return text(s);
}
// "CampOffice_SANDBOX" -> "CampOffice" (SI: GetInteriorBaseName)
std::string interior_base(const std::string& scene) {
    for (const char* suffix : {"_SANDBOX", "_DLC01"}) {
        const size_t n = std::strlen(suffix);
        if (scene.size() > n && scene.compare(scene.size() - n, n, suffix) == 0) return scene.substr(0, scene.size() - n);
    }
    return scene;
}
// SI: PortalPatches LeadsToARegion
bool leads_to_region(const std::string& scene) {
    const size_t n = std::strlen("Region");
    return !scene.empty() &&
           ((scene.size() >= n && scene.compare(scene.size() - n, n, "Region") == 0) || scene.find("Zone") != std::string::npos);
}

// --- the player --------------------------------------------------------------------------------------------------------

Il2CppObject* player_tf() { return il2::invoke(api.gm_player_transform, nullptr); }
Il2CppObject* player_go() { return il2::invoke(api.gm_player_object, nullptr); }
Il2CppObject* player_manager() { return il2::invoke(api.gm_player_manager, nullptr); }
Il2CppObject* weather() { return il2::invoke(api.gm_weather, nullptr); }
Il2CppObject* wind() { return il2::invoke(api.gm_wind, nullptr); }
Il2CppObject* audio_manager() { return api.audio_instance ? il2::invoke(api.audio_instance, nullptr) : nullptr; }
V3 player_pos() { return position(player_tf()); }

// --- physics -----------------------------------------------------------------------------------------------------------

struct Hit {
    V3 point;
    float distance = 0;
    Il2CppObject* collider = nullptr;
};
std::vector<Hit> raycast_all(V3 origin, V3 dir, float range, int query) {
    std::vector<Hit> out;
    if (!api.raycast_all) return out;
    int32_t mask = kDefaultRaycastLayers, q = query;
    void* a[] = {&origin, &dir, &range, &mask, &q};
    Il2CppObject* arr = il2::invoke(api.raycast_all, nullptr, a);
    const size_t n = il2::array_length(arr);
    const char* base = arr ? reinterpret_cast<const char*>(arr) + 0x20 : nullptr;
    for (size_t i = 0; i < n; ++i) {
        const char* e = base + i * kRaycastHitSize;
        Hit h;
        std::memcpy(&h.point, e + kHitPoint, sizeof(V3));
        std::memcpy(&h.distance, e + kHitDistance, sizeof(float));
        int32_t id = 0;
        std::memcpy(&id, e + kHitCollider, sizeof(id));
        void* ia[] = {&id};
        h.collider = id ? il2::invoke(api.collider_by_id, nullptr, ia) : nullptr;
        if (alive(h.collider)) out.push_back(h);
    }
    return out;
}
bool is_trigger(Il2CppObject* col) {
    bool t = false;
    return il2::invoke_value(api.col_trigger, col, t) && t;
}
bool under_player(Il2CppObject* col) {
    Il2CppObject* p = player_tf();
    Il2CppObject* t = tf_of(col);
    return alive(p) && alive(t) && (t == p || child_of(t, p));
}

// SI: PortalPatches SnapToGround: the nearest non-trigger surface below (not the player), 2.5 m at most, exactly on it.
V3 snap_to_ground(V3 pos) {
    const float max_drop = 2.5f;
    const std::vector<Hit> hits = raycast_all(pos + V3{0, 0.5f, 0}, {0, -1, 0}, max_drop + 0.5f, kQueryIgnore);
    const Hit* best = nullptr;
    for (const Hit& h : hits) {
        if (is_trigger(h.collider) || under_player(h.collider)) continue;
        if (!best || h.distance < best->distance) best = &h;
    }
    return best ? V3{pos.x, best->point.y, pos.z} : pos;
}
// SI: PortalPatches OnTheGround: a vanilla exit point set down on the ground below it (6 m at most).
V3 on_the_ground(V3 point) {
    const std::vector<Hit> hits = raycast_all(point + V3{0, 0.5f, 0}, {0, -1, 0}, 6.0f, kQueryIgnore);
    const Hit* best = nullptr;
    for (const Hit& h : hits) {
        if (is_trigger(h.collider)) continue;
        if (!best || h.distance < best->distance) best = &h;
    }
    return best ? best->point + V3{0, 0.05f, 0} : point;
}

// --- one building ------------------------------------------------------------------------------------------------------

enum class Stage {
    Idle,       // not built (the player hasn't been near)
    Vanilla,    // the door stays the game's own (saved interior, missing scene, failed build)
    Loading,    // its interior scenes are coming in
    PreRoll,    // prepared and placed; a moment for the placeables
    Waking,     // active with its renderers off, for the containers' and placeables' Awake
    Hydrating,  // its saved contents coming back, a step per frame (SI: HydrateRoutine, eager)
    Ready,      // built: the door takes the player in
};

constexpr int kLegacyNone = 0, kLegacyReady = 1, kLegacyHydrating = 2;  // Instance::legacy (seamless_import.inc)

struct Instance {
    const Config* cfg = nullptr;
    Stage stage = Stage::Idle;
    uintptr_t master = 0, master_tf = 0, shell = 0, trigger = 0, space = 0;
    V3 master_pos;
    Quat master_rot;
    V3 trig_center, trig_size;  // InteriorTrigger's box, local to the room
    bool have_trigger = false;
    bool shell_box_cached = false;
    Box shell_box;
    std::vector<uintptr_t> keep_off;    // renderers that stay off when the room is shown (the scene's, collision helpers)
    std::vector<uintptr_t> outsiders;   // loose gear / placeables in the room's volume that aren't part of it, hidden
    std::vector<uintptr_t> colliders_off;  // their colliders we switched off
    struct Op {
        std::string scene;
        uintptr_t op = 0;  // AsyncOperationBase (the handle's m_InternalOp)
        bool done = false, ok = false;
    };
    std::vector<Op> ops;
    uintptr_t cached_lightmaps = 0, cached_probes = 0;
    double stage_since = 0, started = 0;
    bool rolled = false;  // SI: the "<id>Gen_<save>" flag, kept for this visit (RandomSpawnBlockerPatch)
    // milestone 1b: the room's state kept with the save (seamless_persist.inc)
    std::filesystem::path dir;  // our per-save folder, fixed when the room is built
    bool has_data = false;      // any file of this building in it (the door waits for the room instead of the game's)
    bool from_files = false;    // SI: isAlreadyGenerated (loot flag and a gear file): the contents come from the files
    bool hydrated = false;      // SI: ContentHydrated: what CanPersistContent asks
    bool placeable_blocked = false, spawn_blocked = false, gear_blocked = false, container_blocked = false,
         state_blocked = false;  // SI: *SaveBlocked: a file that couldn't be read is never written over
    std::vector<std::string> orphan_spawned, orphan_gear, orphan_containers, orphan_state;  // SI: Orphan*Entries
    std::unordered_set<std::string> spawned_guids;  // SI: SpawnedPlaceableGuids
    std::unordered_set<std::string> curing_applied;              // curing boxes whose contents came back (by guid)
    std::unordered_map<std::string, std::string> curing_kept;    // ... and those that didn't: their record, kept
    // pre-mod saves (seamless_import.inc, SI: LegacyImport.cs)
    int legacy = 0;                      // kLegacy*: none, read and waiting for the room, hydrating from memory
    std::shared_ptr<struct LegacyData> legacy_data;
    std::string import_marker;  // an import's "<id>.imported" not written yet (a write failed): with the next save of the room
    int hydrate_step = 0;
    double hydrate_started = 0;
    std::vector<std::vector<std::string>> gear_rows;
    std::vector<std::string> gear_lines;
    size_t gear_next = 0;
    int gear_restored = 0, gear_failed = 0;
    struct ContainerJob {
        uintptr_t container = 0;
        std::string data, line;
    };
    std::vector<ContainerJob> container_jobs;
    // region items found standing in the room by begin_gear: dealt with only once the room's contents are all back
    // (P8: saved or given up on before that, nothing of the region's was moved into the room or deleted)
    struct RegionItem {
        uintptr_t gi = 0;
        V3 at;
    };
    std::vector<RegionItem> deferred_twins, deferred_taken;
    // the region's items near the room when it began hydrating (instance id -> spot): the only ones purge_leaked may
    // take for leaked copies (P1: what the player drops later is never deleted)
    std::unordered_map<int, V3> hydration_gear;
    std::unordered_map<std::string, std::string> state_fires;  // the building's own fire records, by fire key
    // SI: FrozenTime.cs caches
    std::vector<uintptr_t> closed_fires, closed_pots, closed_lamps, closed_torches, closed_flares, closed_evolve;
    bool closed_was_open = true, closed_dirty = true;
    double closed_next_rescan = 0;
    int closed_logged = 0;
    // milestone 2 (seamless_coop.inc)
    bool coop_friend = false;  // built in a friend's game: no files of ours (the host's game has the room's contents)
};
std::vector<Instance> g_instances;
std::string g_region;               // the region these instances belong to
bool g_feature_on = false;          // coop_features: our own or the host's choice
bool g_engaged = false;             // hooks act (on, or rooms still standing in this region)
bool g_logged_state = false;
Instance* g_building = nullptr;     // the one being built (SI: s_SceneLoadLock, one scene load at a time)
std::string g_loading_base;         // its interior scene while it loads (SI: IsInteriorLoadedAsCloneTemplate)
std::unordered_set<int32_t> g_template_scenes;
std::unordered_map<std::string, uintptr_t> g_pristine;  // SI: s_PristineTemplates, per interior scene
std::unordered_set<std::string> g_missing_scenes;        // interior scene names Addressables doesn't have
bool g_copying_template = false;    // SI: IsCopyingCloneTemplate
std::vector<std::string> g_scene_keys;  // the save slot's scene keys (SI: LegacyImport), read once per region
bool g_scene_keys_read = false, g_scene_keys_failed = false;
std::unordered_map<int, uintptr_t> g_stripped_materials;  // SI: s_StrippedMaterialCache
// Interior scene loads given up on (the region went, the 45 s timeout, a failed build) that are still coming in: the
// scene that arrives must still be kept out of the game (its GameManager destroyed before it wakes, its objects out of
// the save) and is unloaded once it's in. They outlive forget_region.
struct OrphanLoad {
    std::string scene;  // the exact scene name ("CampOffice_SANDBOX")
    uintptr_t op = 0;   // AsyncOperationBase
    double since = 0;
};
std::vector<OrphanLoad> g_orphan_loads;
uintptr_t g_snow_planes = 0;     // SnowPlaneManager (tick_snow_planes)
double g_snow_planes_next = 0;   // ... looked for again from then (FindObjectOfType, at most every 2 s)

// inside state (SI: PlayerLocation.cs)
Instance* g_inside = nullptr;       // SI: PlayerInteriorId
double g_last_portal = -1e18;       // SI: s_LastPortalUseTime
double g_fall_guard_until = 0;
uintptr_t g_entered_space = 0;      // SI: s_EnteredSpace
Instance* g_entered_space_of = nullptr;
int g_occlusion_entered = 0;        // SI: s_InsideOcclusionEntered
bool g_capsule_applied = false, g_body_in_force = false;
int g_doorless_confirm = 0, g_flag_confirm = 0, g_watch_confirm = 0;
double g_next_doorless = 0, g_next_flag = 0, g_next_watch = 0, g_next_sound = 0;
// sound (SI: RoomSound.cs)
uint32_t g_paused_outdoor_wind = 0;
int g_outdoor_wind_pauses = 0, g_wind_loop_failures = 0;
bool g_wind_loop_side_inside = false;
uint32_t g_region_scene_state = 0;
int g_region_state_searches = 0, g_state_corrections = 0;
double g_wind_restore_at = 0;       // SI: RestoreWindAudioAfterLoad (1.5 s after a room is built)
// parked-room weather (SI: ParkedWeather.cs)
bool g_snow_moved = false, g_snow_planes_hidden = false;
uintptr_t g_fog_layer = 0;
bool g_fog_saved = false;
// the door's fade (SI: DeferDoorForFade / DoorThroughBlack)
struct Fade {
    bool busy = false;
    int step = 0;  // 1 going dark, 2 holding, then done
    double until = 0;
    uintptr_t door = 0;
} g_fade;
double g_close_sound_at = 0;  // SI: PlayDoorCloseDelayed

bool in_room(const Instance* in) { return in && in->stage == Stage::Ready; }
Il2CppObject* master_of(const Instance& in) { return live(in.master); }

// The rooms' (and their untouched snapshots') root transforms, looked up once per frame or change instead of a
// handle lookup per room for every object the world modules ask about.
uint32_t g_roots_version = 1, g_roots_built = 0;
std::vector<std::pair<Il2CppObject*, Instance*>> g_room_roots;  // root transform -> its room (nullptr: a snapshot)
void rooms_changed() { ++g_roots_version; }
const std::vector<std::pair<Il2CppObject*, Instance*>>& room_roots() {
    if (g_roots_built == g_roots_version) return g_room_roots;
    g_roots_built = g_roots_version;
    g_room_roots.clear();
    for (Instance& in : g_instances)
        if (Il2CppObject* t = in.master_tf ? target(in.master_tf) : nullptr) g_room_roots.emplace_back(t, &in);
    for (auto& [base, h] : g_pristine)
        if (Il2CppObject* p = live(h))
            if (Il2CppObject* t = go_tf(p)) g_room_roots.emplace_back(t, nullptr);
    return g_room_roots;
}
// Nothing of ours stands in the scene or is coming in: every "is it a room's?" question is a cheap no.
bool no_room_objects();

// The deferred GearManager.Deserialize (SI: PreventGearManagerDuplicationPatch).
struct DeferredGear {
    int overload = 0;  // 1 (string), 2 (string, list), 3 (GearSaveList, list)
    uintptr_t a = 0, b = 0;
};
std::vector<DeferredGear> g_deferred_gear;

// Milestone 1b, in seamless_persist.inc (included further down).
int adopt_strays(Instance& in, const char* why);
int purge_leaked(Instance& in);
int stabilize_guids(Instance& in);
bool decide_loot(Instance& in);
bool tick_hydrate(Instance& in);
void enter_from_record(Instance& in);
bool hold_targets(const Instance& in);
bool hold_active();
void ensure_dir(Instance& in);
Instance* unready_with_data(Il2CppObject* door);
// Old saves, in seamless_import.inc.
bool old_save_gate(Instance& in, std::string& why);
bool compose_import(Instance& in, std::string& why);
void commit_import(Instance& in);
void drop_import(Instance& in);
bool import_complete(Instance& in, std::string& why);
void abandon_import(Instance& in, const std::string& why);

// Milestone 2, in seamless_coop.inc (included after it).
enum class Coop { Solo, Host, Friend };
Coop coop();
bool host_has(const Instance& in);
std::string loot_seed_save();
void coop_room_ready(Instance& in);
Instance* hosted_unready(Il2CppObject* door);
void tick_coop_rooms();

// --- a room's geometry (SI: Instances.cs) ------------------------------------------------------------------------------

V3 to_local(const Instance& in, V3 world) { return rotate(conj(in.master_rot), world - in.master_pos); }
V3 to_world(const Instance& in, V3 local) { return in.master_pos + rotate(in.master_rot, local); }

// SI: IsPositionInVolume: the trigger's box without its padding (negative padding shrinks only across).
bool in_volume(const Instance& in, V3 world, float padding = 0) {
    if (!in.have_trigger) return false;
    const V3 local = to_local(in, world);
    V3 size = in.trig_size - kTriggerPadding;
    size = {std::max(0.1f, size.x), std::max(0.1f, size.y), std::max(0.1f, size.z)};
    V3 half = size * 0.5f;
    if (padding > 0)
        half = half + V3{padding, padding, padding} * 0.5f;
    else if (padding < 0)
        half = half + V3{padding, 0, padding} * 0.5f;
    const V3 d = local - in.trig_center;
    return std::fabs(d.x) <= half.x && std::fabs(d.y) <= half.y && std::fabs(d.z) <= half.z;
}

// SI: TryGetShellMeshBounds: every mesh under the shell, through its transform (works while inactive).
bool shell_mesh_box(Instance& in, Box& out) {
    if (in.shell_box_cached) {
        out = in.shell_box;
        return in.shell_box.valid();
    }
    Il2CppObject* shell = live(in.shell);
    if (!shell) return false;
    in.shell_box_cached = true;
    Box box;
    each_in(shell, api.t_mesh_filter, [&](Il2CppObject* filter) {
        Il2CppObject* mesh = il2::invoke(api.mesh_filter_mesh, filter);
        if (!alive(mesh)) return;
        UBounds b;
        if (!il2::invoke_value(api.mesh_bounds, mesh, b)) return;
        struct Matrix {
            float m[16];
        } mx;
        if (!il2::invoke_value(api.tf_to_world, tf_of(filter), mx)) return;
        const float* m = mx.m;
        for (int k = 0; k < 8; ++k) {
            const V3 c{b.center.x + ((k & 1) ? -b.extents.x : b.extents.x), b.center.y + ((k & 2) ? -b.extents.y : b.extents.y),
                       b.center.z + ((k & 4) ? -b.extents.z : b.extents.z)};
            // Matrix4x4 is column-major: m[0] m00, m[1] m10, m[2] m20, m[4] m01, ... m[12] m03
            box.add({m[0] * c.x + m[4] * c.y + m[8] * c.z + m[12], m[1] * c.x + m[5] * c.y + m[9] * c.z + m[13],
                     m[2] * c.x + m[6] * c.y + m[10] * c.z + m[14]});
        }
    });
    in.shell_box = box;
    out = box;
    return box.valid();
}

// SI: IsPositionInParkedRoom: the shell's box carried up by the park offset, with allowances, or the room's volume.
bool in_parked_room(Instance& in, V3 pos, float padding = 1.0f) {
    Box shell;
    if (shell_mesh_box(in, shell)) {
        const float side = kParkedSideAllowance + padding, up = kParkedHeightAllowance + padding;
        Box b;
        b.min = shell.min + kParkOffset - V3{side, up, side};
        b.max = shell.max + kParkOffset + V3{side, up, side};
        if (b.contains(pos)) return true;
    }
    return in_volume(in, pos, padding);
}

// SI: DistanceFrom: to the building or to its room, whichever is nearer.
float distance_from(const Instance& in, V3 pos) {
    float d = dist(pos, in.cfg->fallback);
    if (master_of(in)) d = std::min(d, dist(pos, in.master_pos));
    return d;
}

// SI: ParkedRoomYaw.cs ParkedInteriorPoint (no yaw turn: no windows yet): a hand-typed point, authored against a room
// standing on its shell (perhaps stretched by ScaleAdjustment), carried into the parked room.
V3 parked_point(const Instance& in, V3 authored) {
    if (!master_of(in)) return authored + kParkOffset;
    const V3 pivot = in.master_pos - kParkOffset;
    V3 from_pivot = authored - pivot;
    const V3 s = in.cfg->scale;
    if ((s.x != 1 || s.y != 1 || s.z != 1) && s.x > 0.01f && s.y > 0.01f && s.z > 0.01f) {
        const Quat as_authored = in.master_rot;
        V3 local = rotate(conj(as_authored), from_pivot);
        local = {local.x / s.x, local.y / s.y, local.z / s.z};
        from_pivot = rotate(as_authored, local);
    }
    return pivot + from_pivot + kParkOffset;
}

Instance* find_instance(const char* id) {
    for (Instance& in : g_instances)
        if (std::strcmp(in.cfg->id, id) == 0) return &in;
    return nullptr;
}

bool is_sub_linked(const Instance& parent, const char* child_id) {
    for (int i = 0; i < parent.cfg->links_count; ++i)
        if (std::strcmp(kLinks[parent.cfg->links_first + i].target, child_id) == 0) return true;
    return false;
}

// --- state: is it on ---------------------------------------------------------------------------------------------------

bool feature_active() { return coop_features::on(net::kFeatureSeamlessInteriors); }

// The hooks act while the feature is on, or while rooms we built still stand in this region (switched off mid-visit:
// the rooms keep working until the region changes; no new ones are built).
bool engaged() { return api.ok && g_engaged; }

// --- room renderers, colliders, outsiders (SI: Environment.cs ShowCloneRenderers, Spawning.cs) --------------------------

// SI: ShowCloneRenderers: every renderer on, except the ones the scene shipped off (and the collision helpers we hid:
// SI's own pass would bring those back here; their being hidden is the point, so they stay off).
void show_room_renderers(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    std::unordered_set<Il2CppObject*> keep;
    for (uintptr_t h : in.keep_off)
        if (Il2CppObject* r = live(h)) keep.insert(r);
    each_in(m, api.t_renderer, [&](Il2CppObject* r) {
        const bool want = !keep.count(r);
        if (renderer_on(r) != want) set_renderer(r, want);
    });
}

// SI: SetObjectVisualState: an outsider's renderers and colliders (only the colliders we switched off come back).
void set_visual(Instance& in, Il2CppObject* go, bool visible) {
    if (!alive(go)) return;
    if (!visible) {
        Il2CppObject* gi = component(go, api.t_gear);
        bool physics = false;
        if (gi && api.gear_has_physics && il2::invoke_value(api.gear_has_physics, gi, physics) && physics)
            il2::invoke(api.gear_disable_physics, gi);
    }
    each_in(go, api.t_renderer, [&](Il2CppObject* r) { set_renderer(r, visible); });
    if (!visible) {
        each_in(go, api.t_collider, [&](Il2CppObject* c) {
            if (collider_on(c)) {
                in.colliders_off.push_back(hold(c));
                set_collider(c, false);
            }
        });
    } else {
        std::vector<uintptr_t> rest;
        Il2CppObject* go_t = go_tf(go);
        for (uintptr_t& h : in.colliders_off) {
            Il2CppObject* c = live(h);
            Il2CppObject* ct = tf_of(c);
            if (c && (ct == go_t || child_of(ct, go_t))) {
                set_collider(c, true);
                drop(h);
            } else if (c) {
                rest.push_back(h);
            } else {
                drop(h);
            }
        }
        in.colliders_off = std::move(rest);
    }
}

bool on_any_shell(Il2CppObject* tf) {
    for (Instance& o : g_instances)
        if (Il2CppObject* s = live(o.shell); s && child_of(tf, go_tf(s))) return true;
    return false;
}
bool in_another_room(const Instance& in, Il2CppObject* tf) {
    for (Instance& o : g_instances)
        if (&o != &in)
            if (Il2CppObject* m = master_of(o); m && child_of(tf, go_tf(m))) return true;
    return false;
}

// SI: SetInteriorItemsVisibleCore's hidden outsiders: loose gear and placeables that stand in the closed room's box but
// aren't part of it are hidden with it and shown again at the door. Since milestone 1b what the player leaves in a room
// is adopted first (AdoptStrayInteriorObjects, hide_room) and saved with the room; this keeps what can't be adopted
// (outside the room's walls but inside its box) out of sight 80 m up.
void set_outsiders_visible(Instance& in, bool visible) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    if (visible) {
        for (uintptr_t& h : in.outsiders) {
            if (Il2CppObject* go = live(h)) set_visual(in, go, true);
            drop(h);
        }
        in.outsiders.clear();
        return;
    }
    Il2CppObject* mt = go_tf(m);
    Il2CppObject* ptf = player_tf();
    std::unordered_set<Il2CppObject*> seen;
    for (uintptr_t h : in.outsiders)
        if (Il2CppObject* go = live(h)) seen.insert(go);
    auto consider = [&](Il2CppObject* comp) {
        Il2CppObject* t = tf_of(comp);
        if (!t) return;
        const V3 p = position(t);
        if (!in_parked_room(in, p, 0)) return;
        if (child_of(t, mt) || (ptf && child_of(t, ptf)) || on_any_shell(t) || in_another_room(in, t)) return;
        if (api.gear_in_inventory >= 0 && il2::class_name_of(comp) == "GearItem" && get<bool>(comp, api.gear_in_inventory))
            return;
        Il2CppObject* go = go_of(comp);
        if (!go || seen.count(go)) return;
        seen.insert(go);
        set_visual(in, go, false);
        in.outsiders.push_back(hold(go));
    };
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (alive(gi)) consider(gi);
    });
    if (api.find_objects && api.t_placeable) {
        void* a[] = {api.t_placeable};
        Il2CppObject* all = il2::invoke(api.find_objects, nullptr, a);
        for (size_t i = 0, n = il2::array_length(all); i < n; ++i)
            if (Il2CppObject* p = il2::array_object_at(all, i); alive(p)) consider(p);
    }
    if (!in.outsiders.empty())
        logx::info("interiors: %s: %zu loose item(s) left in the room are hidden with it", in.cfg->id, in.outsiders.size());
}

// SI: LeaveAudioZones: an AudioStateZoneTrigger in the room that holds an occlusion lets it go before the room closes.
void leave_audio_zones(Instance& in) {
    Il2CppObject* m = master_of(in);
    Il2CppObject* gam = audio_manager();
    if (!m || !gam || !active_self(m) || api.zone_occluded < 0) return;
    each_in(m, api.t_audio_zone, [&](Il2CppObject* zone) {
        if (!active_in_tree(go_of(zone)) || !get<bool>(zone, api.zone_occluded)) return;
        put<bool>(zone, api.zone_occluded, false);
        int32_t level = get<int32_t>(zone, api.zone_level);
        void* a[] = {&level};
        il2::invoke(api.exit_occlusion, gam, a);
    });
}

// SI: HideInteriorCompletely (+ SetInteriorItemsVisible(false)'s purge and adoption, milestone 1b: what the player left
// in the room is the room's now, saved with it; only what can't be adopted is hidden as an outsider, as in 1a)
void hide_room(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    if (in.hydrated && active_self(m)) {  // (SI's order: adopted first, then the leaked copies purged)
        adopt_strays(in, "the room closed");
        purge_leaked(in);
    }
    set_outsiders_visible(in, false);
    leave_audio_zones(in);
    set_active(m, false);
}

// --- the game's own indoor space (SI: VanillaView.cs) ------------------------------------------------------------------

Il2CppObject* player_collider() {
    Il2CppObject* go = player_go();
    return go ? component(go, api.t_collider) : nullptr;
}

// SI: DropInactiveIndoorTriggers: interior-scene triggers switched off with a room never sent their exit.
void drop_inactive_indoor_triggers() {
    Il2CppObject* pm = player_manager();
    if (!pm || api.pm_indoors < 0) return;
    Il2CppObject* list = get<Il2CppObject*>(pm, api.pm_indoors);
    for (int i = list_count(list) - 1; i >= 0; --i) {
        Il2CppObject* entry = list_at(list, i);
        if (!alive(entry) || !active_in_tree(go_of(entry))) list_remove_at(list, i);
    }
    Il2CppObject* current = get<Il2CppObject*>(pm, api.pm_indoor);
    if (!alive(current) || !active_in_tree(go_of(current))) put_ref(pm, api.pm_indoor, list_at(list, list_count(list) - 1));
}

// SI: ForgetSpaceOnPlayer
void forget_space_on_player(Il2CppObject* space) {
    Il2CppObject* pm = player_manager();
    if (!pm || api.pm_indoors < 0) return;
    Il2CppObject* list = get<Il2CppObject*>(pm, api.pm_indoors);
    for (int i = list_count(list) - 1; i >= 0; --i) {
        Il2CppObject* entry = list_at(list, i);
        if (!alive(entry) || entry == space) list_remove_at(list, i);
    }
    Il2CppObject* current = get<Il2CppObject*>(pm, api.pm_indoor);
    if (!alive(current) || current == space) put_ref(pm, api.pm_indoor, list_at(list, list_count(list) - 1));
}

// SI: MakeSureSpaceTaken: the game may refuse the trigger (the player's tag); then it's put in by hand.
void make_sure_space_taken(Il2CppObject* space, const char* id) {
    Il2CppObject* pm = player_manager();
    if (!pm || api.pm_indoor < 0) return;
    if (get<Il2CppObject*>(pm, api.pm_indoor) == space) {
        static bool reported = false;
        if (!reported) logx::info("interiors: %s: the player is in the game's own indoor space", id);
        reported = true;
        return;
    }
    static bool warned = false;
    if (!warned) logx::warn("interiors: %s: the game didn't take the indoor space; set by hand", id);
    warned = true;
    Il2CppObject* list = api.pm_indoors >= 0 ? get<Il2CppObject*>(pm, api.pm_indoors) : nullptr;
    if (list && !list_contains(list, space)) list_add(list, space);
    put_ref(pm, api.pm_indoor, space);
}

// SI: SyncVanillaIndoorSpace: the room's IndoorSpaceTrigger follows the inside flag.
void sync_indoor_space() {
    Instance* wanted = g_inside;
    Il2CppObject* entered = live(g_entered_space);
    if (wanted == g_entered_space_of && (!wanted || entered)) return;
    if (g_entered_space_of) {
        drop_inactive_indoor_triggers();
        if (entered)
            if (Il2CppObject* col = player_collider()) {
                void* a[] = {col};
                il2::invoke(api.indoor_exit, entered, a);
            }
        forget_space_on_player(entered);
        drop(g_entered_space);
        g_entered_space_of = nullptr;
    }
    if (!wanted) return;
    Il2CppObject* space = live(wanted->space);
    Il2CppObject* col = player_collider();
    if (!space || !col) return;
    g_entered_space = hold(space);
    g_entered_space_of = wanted;
    void* a[] = {col};
    il2::invoke(api.indoor_enter, space, a);
    make_sure_space_taken(space, wanted->cfg->id);
}

void mark_inside(Instance* in, const char* why) {
    if (g_inside != in)
        logx::info("interiors: the player is inside %s (%s)%s%s", in->cfg->id, why, g_inside ? ", from " : "",
                   g_inside ? g_inside->cfg->id : "");
    g_inside = in;
    sync_indoor_space();
}
void mark_outside(const char* why) {
    if (g_inside) logx::info("interiors: the player is outside again, left %s (%s)", g_inside->cfg->id, why);
    g_inside = nullptr;
    sync_indoor_space();
}

// --- sound (SI: Audio.cs, RoomSound.cs) --------------------------------------------------------------------------------

// SI: ApplyOutdoorWindForInsideState: the wind loop is never left stopped.
void keep_wind_loop() {
    Il2CppObject* w = wind();
    if (!alive(w) || api.wind_force_stopped < 0) return;
    put<bool>(w, api.wind_force_stopped, false);
    if (get<uint32_t>(w, api.wind_loop) != 0 || !api.wind_play_loop) return;
    uint32_t id = 0;
    il2::invoke_value(api.wind_play_loop, w, id);
    put<uint32_t>(w, api.wind_loop, id);
    logx::info("interiors: there was no wind loop; started again (id %u)", id);
}

// SI: ForceRestartWindAudio
void restart_wind(const char* why) {
    Il2CppObject* w = wind();
    if (!alive(w) || api.wind_loop < 0) return;
    const uint32_t before = get<uint32_t>(w, api.wind_loop);
    if (api.wind_force_stop) il2::invoke(api.wind_force_stop, w);
    put<bool>(w, api.wind_force_stopped, false);
    put<uint32_t>(w, api.wind_loop, 0u);
    uint32_t id = 0;
    if (api.wind_play_loop) il2::invoke_value(api.wind_play_loop, w, id);
    put<uint32_t>(w, api.wind_loop, id);
    logx::info("interiors: wind loop restarted (%s): id %u -> %u", why, before, id);
}

uint32_t event_of(uint32_t playing) {
    if (!playing || !api.ak_event_of) return 0;
    uint32_t e = 0;
    void* a[] = {&playing};
    il2::invoke_value(api.ak_event_of, nullptr, e, a);
    return e;
}
void ak_action(int action, uint32_t playing, int fade_ms) {
    if (!api.ak_action || !playing) return;
    int32_t act = action, ms = fade_ms, curve = 4;  // AkCurveInterpolation_Linear
    void* a[] = {&act, &playing, &ms, &curve};
    il2::invoke(api.ak_action, nullptr, a);
}
void ak_stop(uint32_t playing, int fade_ms) {
    if (!api.ak_stop || !playing) return;
    int32_t ms = fade_ms, curve = 4;
    void* a[] = {&playing, &ms, &curve};
    il2::invoke(api.ak_stop, nullptr, a);
}
Il2CppObject* wind_emitter() {  // SI: FindWindEmitter
    Il2CppObject* p = player_tf();
    Il2CppObject* t = find_child(p, "WorldView_SoundEmitter");
    if (!t) t = find_child(p, "WorldView/WorldView_SoundEmitter");
    return t ? go_of(t) : nullptr;
}
uint32_t play_sound(const char* event, Il2CppObject* go) {
    if (!api.play_sound || !alive(go)) return 0;
    uint32_t id = 0;
    void* a[] = {str(event), go};
    il2::invoke_value(api.play_sound, nullptr, id, a);
    return id;
}

// SI: MakeWindLoopFollowThePlayer: in a room the indoor wind loop plays and the outdoor one is paused (not stopped:
// a loop started afresh plays without its main layer for a while); on the way out the outdoor one resumes.
void wind_loop_follows(bool inside) {
    if (inside != g_wind_loop_side_inside) {
        g_wind_loop_side_inside = inside;
        g_wind_loop_failures = 0;
    }
    if (g_wind_loop_failures >= 3 || !api.ak_event_of || api.wind_loop < 0) return;
    Il2CppObject* w = wind();
    if (!alive(w)) return;
    const uint32_t id = get<uint32_t>(w, api.wind_loop);
    const uint32_t playing = event_of(id);
    if (inside) {
        if (playing == kIndoorWindEvent) return;
        Il2CppObject* emitter = wind_emitter();
        const uint32_t indoor = emitter ? play_sound("Play_WindIndoors", emitter) : 0;
        if (!indoor) {
            ++g_wind_loop_failures;
            logx::info("interiors: the indoor wind loop could not be started (%s), try %d",
                       emitter ? "the event did not play" : "no sound object", g_wind_loop_failures);
            return;
        }
        if (playing == kOutdoorWindEvent) {
            if (g_paused_outdoor_wind && g_paused_outdoor_wind != id) ak_stop(g_paused_outdoor_wind, 0);
            ak_action(1, id, kOutdoorsFadeOutMs);  // Pause
            g_paused_outdoor_wind = id;
            g_outdoor_wind_pauses = 1;
        } else if (playing != 0) {
            ak_stop(id, kOutdoorsFadeOutMs);
        }
        put<uint32_t>(w, api.wind_loop, indoor);
        put<bool>(w, api.wind_force_stopped, false);
        logx::info("interiors: inside: indoor wind loop started (id %u), the outdoor one %s (id %u)", indoor,
                   g_paused_outdoor_wind == id && id ? "paused" : "was not there", id);
        return;
    }
    const uint32_t paused = g_paused_outdoor_wind;
    const bool paused_alive = paused && event_of(paused) == kOutdoorWindEvent;
    if (playing == kIndoorWindEvent) {
        g_paused_outdoor_wind = 0;
        if (paused_alive) {
            ak_stop(id, kIndoorWindFadeOutMs);
            for (int i = 0; i < std::max(g_outdoor_wind_pauses, 1); ++i) ak_action(2, paused, kOutdoorsFadeInMs);  // Resume
            put<uint32_t>(w, api.wind_loop, paused);
            put<bool>(w, api.wind_force_stopped, false);
            logx::info("interiors: outside: indoor wind loop stopped (id %u), the outdoor one goes on (id %u)", id, paused);
        } else {
            ++g_wind_loop_failures;
            restart_wind("outdoor loop after leaving");
        }
        return;
    }
    if (!paused) return;
    g_paused_outdoor_wind = 0;
    if (!paused_alive) return;
    if (playing == kOutdoorWindEvent && id != paused) {
        ak_stop(paused, 0);
    } else if (id != paused) {
        for (int i = 0; i < std::max(g_outdoor_wind_pauses, 1); ++i) ak_action(2, paused, kOutdoorsFadeInMs);
        put<uint32_t>(w, api.wind_loop, paused);
        put<bool>(w, api.wind_force_stopped, false);
    }
}

// SI: ApplyInsideOcclusion: the game's own muffling of the outdoors, Mild, while in a room (Enter / Exit paired).
void inside_occlusion(bool inside) {
    Il2CppObject* gam = audio_manager();
    if (!gam) return;
    const int want = inside ? kOcclusionMild : 0;
    if (want == g_occlusion_entered) return;
    int32_t level = 0;
    if (g_occlusion_entered) {
        level = g_occlusion_entered;  // AudioOcclusionLevel
        void* a[] = {&level};
        il2::invoke(api.exit_occlusion, gam, a);
    }
    if (want) {
        level = want;
        void* a[] = {&level};
        il2::invoke(api.enter_occlusion, gam, a);
    }
    g_occlusion_entered = want;
    logx::info("interiors: the game's own muffling of the outdoors is now at level %d (0 none, 1 mild)", want);
}

// SI: SetAudioOcclusion
void set_audio_occlusion(bool occlude) {
    keep_wind_loop();
    wind_loop_follows(occlude);
    g_next_sound = 0;
    inside_occlusion(occlude);
}

// SI: ResetAudioOcclusionCounters: a scene change while inside would leave the game's counter stuck.
void reset_occlusion_counters(const char* why) {
    g_occlusion_entered = 0;
    Il2CppObject* gam = audio_manager();
    if (!gam || api.gam_mild < 0) return;
    const int heavy = get<int32_t>(gam, api.gam_heavy), medium = get<int32_t>(gam, api.gam_medium),
              mild = get<int32_t>(gam, api.gam_mild);
    if (!heavy && !medium && !mild) return;
    put<int32_t>(gam, api.gam_heavy, 0);
    put<int32_t>(gam, api.gam_medium, 0);
    put<int32_t>(gam, api.gam_mild, 0);
    il2::invoke(api.update_occlusion, gam);
    logx::info("interiors: audio occlusion counters reset (%s): heavy %d medium %d mild %d", why, heavy, medium, mild);
}

// SI: FindRegionSceneState: the Scene sound state the region's own AkState sets (not one of a room's).
uint32_t find_region_scene_state(const std::string& region) {
    if (!api.ak_states_ok || !api.find_objects || !api.t_ak_state) return 0;
    void* a[] = {api.t_ak_state};
    Il2CppObject* all = il2::invoke(api.find_objects, nullptr, a);
    uint32_t found = 0;
    int distinct = 0;
    for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
        Il2CppObject* st = il2::array_object_at(all, i);
        Il2CppObject* data = alive(st) ? get<Il2CppObject*>(st, api.ak_state_data) : nullptr;
        if (!data) continue;
        uint32_t group = 0, value = 0;
        il2::invoke_value(api.ak_group_id, data, group);
        il2::invoke_value(api.ak_value_id, data, value);
        if (group != api.ak_scene_group || value == 0 || value == api.ak_scene_none) continue;
        if (is_clone_object(st)) continue;
        if (scene_name(scene_of(go_of(st))).rfind(region, 0) != 0) continue;
        if (value != found) {
            found = value;
            ++distinct;
        }
    }
    if (distinct == 1) {
        logx::info("interiors: %s: found the region's own Scene sound state (%u)", region.c_str(), found);
        return found;
    }
    if (distinct == 0 && g_region_state_searches >= 3) {
        logx::info("interiors: %s: the region has no Scene sound state of its own; None is kept in force", region.c_str());
        return api.ak_scene_none;
    }
    if (distinct > 1 && g_region_state_searches >= 3)
        logx::info("interiors: %s: could not pick the region's own Scene sound state (%d candidates); left alone",
                   region.c_str(), distinct);
    return 0;
}

// SI: KeepRegionSoundStates: the interior scenes' AkState components set the sound engine for an interior when they
// load; the region's values are put back.
void keep_region_sound_states() {
    if (!api.ak_states_ok || !api.ak_get_state || !api.ak_set_state) return;
    if (!g_region_scene_state) {
        if (g_region_state_searches >= 3) return;
        ++g_region_state_searches;
        g_region_scene_state = find_region_scene_state(g_region);
        if (!g_region_scene_state) return;
    }
    uint32_t scene = 0, interior = 0, g1 = api.ak_scene_group, g2 = api.ak_interior_group;
    void* a1[] = {&g1, &scene};
    il2::invoke(api.ak_get_state, nullptr, a1);
    void* a2[] = {&g2, &interior};
    il2::invoke(api.ak_get_state, nullptr, a2);
    const bool scene_wrong = scene != g_region_scene_state, interior_wrong = interior == api.ak_interior_int;
    if (!scene_wrong && !interior_wrong) return;
    if (scene_wrong) {
        uint32_t v = g_region_scene_state;
        void* s[] = {&g1, &v};
        il2::invoke(api.ak_set_state, nullptr, s);
    }
    if (interior_wrong) {
        uint32_t v = api.ak_interior_none;
        void* s[] = {&g2, &v};
        il2::invoke(api.ak_set_state, nullptr, s);
    }
    if (++g_state_corrections <= 5)
        logx::info("interiors: %s: the sound engine was set for an interior (Scene %u, InteriorStates %u); the region's "
                   "own put back (Scene %u), correction %d",
                   g_region.c_str(), scene, interior, g_region_scene_state, g_state_corrections);
}

// SI: PlayDoorTransitionSound (open now; close 0.6 s later, PlayDoorCloseDelayed)
void door_sounds(bool close) {
    Il2CppObject* gam = audio_manager();
    Il2CppObject* gam_go = gam ? go_of(gam) : nullptr;
    Il2CppObject* cam = il2::invoke(api.gm_main_camera, nullptr);
    Il2CppObject* cam_go = cam ? go_of(cam) : nullptr;
    Il2CppObject* pl = player_go();
    if (!close && api.gui_click) il2::invoke(api.gui_click, nullptr);  // wakes the sound pipeline up
    if (gam_go) {
        play_sound(close ? "Play_SndMechDoorWoodClose1" : "Play_SndMechDoorWoodOpen1", gam_go);
        play_sound(close ? "Play_DoorMiscWhooshAClose" : "Play_DoorMiscWhooshAOpen", gam_go);
    }
    if (cam_go) play_sound(close ? "Play_SndMechDoorWoodClose1" : "Play_SndMechDoorWoodOpen1", cam_go);
    if (pl) play_sound(close ? "Play_SndMechDoorWoodClose1" : "Play_SndMechDoorWoodOpen1", pl);
    if (!close) g_close_sound_at = now_ms() + 600;
}

// --- the player's body and the parked room's weather (SI: IndoorMovement.cs, ParkedRoomBody.cs, ParkedWeather.cs) ------

// SI: TickIndoorPlayerCapsule + TickParkedRoomBody: the game's indoor capsule and near clip while in a room.
void tick_capsule() {
    const bool inside = g_inside != nullptr;
    if (!inside && !g_capsule_applied) return;
    Il2CppObject* gp = il2::invoke(api.gm_global, nullptr);
    Il2CppObject* mv = il2::invoke(api.gm_movement, nullptr);
    Il2CppObject* pgo = player_go();
    Il2CppObject* cc = pgo ? component(pgo, api.t_char_controller) : nullptr;
    if (!gp || !mv || !cc || api.gp_radius_in < 0 || api.mv_slope_in < 0) return;
    bool indoor_values = inside;
    if (!inside)
        if (Il2CppObject* w = weather()) il2::invoke_value(api.weather_indoor_scene, w, indoor_values);
    const float radius = get<float>(gp, indoor_values ? api.gp_radius_in : api.gp_radius_out);
    const float slope = get<float>(mv, indoor_values ? api.mv_slope_in : api.mv_slope_out);
    if (radius < 0.05f || slope < 1.0f) return;
    float now_r = 0, now_s = 0;
    il2::invoke_value(api.cc_radius, cc, now_r);
    il2::invoke_value(api.cc_slope, cc, now_s);
    if (std::fabs(now_r - radius) > 1e-4f) {
        float v = radius;
        void* a[] = {&v};
        il2::invoke(api.cc_set_radius, cc, a);
    }
    if (std::fabs(now_s - slope) > 1e-3f) {
        float v = slope;
        void* a[] = {&v};
        il2::invoke(api.cc_set_slope, cc, a);
    }
    if (inside == g_capsule_applied) return;
    g_capsule_applied = inside;
    // near clip: the camera's default (SwapNearPlaneDefault) and its override (TickParkedRoomBody)
    Il2CppObject* fps = il2::invoke(api.gm_fps_camera, nullptr);
    const float near_in = get<float>(gp, api.gp_near_in), near_out = get<float>(gp, api.gp_near_out);
    if (fps && near_in > 0.001f && near_out > 0.001f) {
        const float from = inside ? near_out : near_in, to = inside ? near_in : near_out;
        if (api.fps_near >= 0 && std::fabs(get<float>(fps, api.fps_near) - from) < 1e-4f) put<float>(fps, api.fps_near, to);
        float now_near = 0;
        if (api.fps_near_get && il2::invoke_value(api.fps_near_get, fps, now_near) && std::fabs(now_near - from) < 0.0005f &&
            std::fabs(now_near - to) > 0.0005f) {
            float v = to;
            void* a[] = {&v};
            il2::invoke(api.fps_near_set, fps, a);
        }
    }
    logx::info("interiors: the player's body is at the game's %s size: radius %.2f, slope limit %.0f, near clip %.2f",
               inside ? "indoor" : "outdoor", radius, slope, inside ? near_in : near_out);
}

// SI: TickParkedRoomFog: the camera's fog pass off in a parked room (the fog is drawn for the height it stands at).
void tick_fog() {
    Il2CppObject* layer = live(g_fog_layer);
    if (!g_inside) {
        if (layer) put<bool>(get<Il2CppObject*>(layer, api.post_fog), api.fog_enabled, g_fog_saved);
        drop(g_fog_layer);
        return;
    }
    if (api.post_fog < 0 || api.fog_enabled < 0) return;
    if (!layer) {
        Il2CppObject* cam = il2::invoke(api.gm_main_camera, nullptr);
        layer = cam ? component(go_of(cam), api.t_post_layer) : nullptr;
        Il2CppObject* fog = layer ? get<Il2CppObject*>(layer, api.post_fog) : nullptr;
        if (!fog) return;
        g_fog_layer = hold(layer);
        g_fog_saved = get<bool>(fog, api.fog_enabled);
    }
    Il2CppObject* fog = get<Il2CppObject*>(layer, api.post_fog);
    if (fog && get<bool>(fog, api.fog_enabled)) put<bool>(fog, api.fog_enabled, false);
}

// SI: TickParkedSnowEmitters: the falling and blowing snow follow the player 80 m down, where the weather is.
void tick_snow_emitters() {
    if (!g_inside && !g_snow_moved) return;
    Il2CppObject* w = weather();
    if (!w || api.weather_falling < 0) return;
    Il2CppObject* falling = get<Il2CppObject*>(w, api.weather_falling);
    Il2CppObject* blowing = get<Il2CppObject*>(w, api.weather_blowing);
    Il2CppObject* ptf = player_tf();
    Il2CppObject* cam = il2::invoke(api.gm_main_camera, nullptr);
    if (!g_inside || !alive(cam) || !alive(ptf)) {
        if (!g_snow_moved) return;
        g_snow_moved = false;
        if (Il2CppObject* t = go_tf(falling)) {
            V3 zero;
            void* a[] = {&zero};
            il2::invoke(api.tf_set_local_position, t, a);
        }
        if (Il2CppObject* t = go_tf(blowing)) set_position(t, position(ptf));
        return;
    }
    if (Il2CppObject* t = go_tf(falling)) set_position(t, position(tf_of(cam)) - kParkOffset);
    if (Il2CppObject* t = go_tf(blowing)) set_position(t, position(ptf) - kParkOffset);
    g_snow_moved = true;
}

// SI: TickParkedSnowPlanes: the snow planes (drawn around the camera) are off in a parked room.
void tick_snow_planes() {
    uintptr_t& planes = g_snow_planes;
    if (!g_inside) {
        if (!g_snow_planes_hidden) return;
        g_snow_planes_hidden = false;
        Il2CppObject* p = live(planes);
        Il2CppObject* r = p ? get<Il2CppObject*>(p, api.snow_planes_renderer) : nullptr;
        bool was = true;
        if (p && api.snow_planes_enabled) il2::invoke_value(api.snow_planes_enabled, p, was);
        if (alive(r)) set_renderer(r, was);
        return;
    }
    if (api.snow_planes_renderer < 0) return;
    Il2CppObject* p = live(planes);
    if (!p) {
        drop(planes);
        const double now = now_ms();
        if (now < g_snow_planes_next) return;
        g_snow_planes_next = now + 2000;
        void* a[] = {api.t_snow_planes};
        p = api.t_snow_planes ? il2::invoke(api.find_object, nullptr, a) : nullptr;
        if (!alive(p)) return;
        planes = hold(p);
    }
    Il2CppObject* r = get<Il2CppObject*>(p, api.snow_planes_renderer);
    if (!alive(r)) return;
    if (renderer_on(r)) set_renderer(r, false);
    g_snow_planes_hidden = true;
}

// --- leaving a room (SI: PlayerLocation.cs ExitInteriorState) ----------------------------------------------------------

void exit_room(Instance& in, const char* why, bool player_leaving) {
    const bool leaving = player_leaving || g_inside == &in;
    hide_room(in);
    if (Il2CppObject* s = live(in.shell)) set_active(s, true);
    if (leaving && !live(in.shell))  // a sub-interior: its parent's shell comes back with it
        for (Instance& parent : g_instances)
            if (is_sub_linked(parent, in.cfg->id) && in_room(&parent)) {
                hide_room(parent);
                if (Il2CppObject* s = live(parent.shell)) set_active(s, true);
            }
    if (!leaving) return;
    set_audio_occlusion(false);
    mark_outside(why);
    if (Il2CppObject* w = wind(); alive(w) && api.wind_force_stopped >= 0) put<bool>(w, api.wind_force_stopped, false);
}

// --- building a room (SI: Main.cs Run, Environment.cs) -----------------------------------------------------------------

std::string base_name(const std::string& n) {  // SI: WithoutCopyNumber: "FX_LightShaft_B (2)" -> "FX_LightShaft_B"
    if (n.empty() || n.back() != ')') return n;
    const size_t open = n.rfind(" (");
    if (open == std::string::npos || open == 0 || open + 2 >= n.size() - 1) return n;
    for (size_t i = open + 2; i < n.size() - 1; ++i)
        if (n[i] < '0' || n[i] > '9') return n;
    return n.substr(0, open);
}
bool listed(const char* const* names, size_t count, const std::string& n) {
    for (size_t i = 0; i < count; ++i)
        if (n == names[i]) return true;
    return false;
}
bool in_names(int first, int count, const std::string& n) {
    for (int i = 0; i < count; ++i)
        if (n == kNames[first + i]) return true;
    return false;
}

// SI: FindClosestShell, by a look round the building's spot instead of SI's index of every object name in the scene:
// the colliders within reach, and their parents, are searched for the shell's name (exact or with a suffix such as
// "(Clone)"); the nearest active one not already another room's shell wins. Then SI's fallback: an object whose name
// holds the interior scene's name and "Prefab".
Il2CppObject* find_shell(const Instance& in) {
    const Config& c = *in.cfg;
    if (!*c.shell || !api.overlap_sphere) return nullptr;
    bool group = false;
    for (const char* g : kShellGroups) group = group || std::strcmp(g, c.shell) == 0;
    float limit = std::strcmp(c.shell, "STR_BarnSmallA_Exterior_Prefab") == 0 ? 15.0f : kShellMatchMax;  // SI: s_ShellMatchFarther
    std::unordered_set<Il2CppObject*> used;
    for (const Instance& o : g_instances)
        if (Il2CppObject* s = live(o.shell)) used.insert(s);
    V3 at = c.fallback;
    float radius = group ? c.bounds_radius : 25.0f;
    int32_t mask = -1, query = kQueryCollide;
    void* a[] = {&at, &radius, &mask, &query};
    Il2CppObject* cols = il2::invoke(api.overlap_sphere, nullptr, a);
    std::unordered_set<Il2CppObject*> seen;
    Il2CppObject* best = nullptr;
    float best_d = 1e30f;
    Il2CppObject* fallback_best = nullptr;
    float fallback_d = 1e30f;
    const size_t shell_len = std::strlen(c.shell);
    for (size_t i = 0, n = il2::array_length(cols); i < n; ++i) {
        Il2CppObject* t = tf_of(il2::array_object_at(cols, i));
        for (int depth = 0; t && depth < 12; ++depth, t = parent_of(t)) {
            if (!seen.insert(t).second) break;
            if (is_clone_object(t)) break;
            const std::string nm = name_of(t);
            Il2CppObject* go = go_of(t);
            const float d = group ? 0.0f : dist(position(t), c.fallback);
            if (nm.compare(0, shell_len, c.shell) == 0 && active_in_tree(go) && !used.count(go) && d <= limit && d < best_d) {
                best = go;
                best_d = d;
            }
            if (nm.find(c.scene) != std::string::npos && nm.find("Prefab") != std::string::npos && active_in_tree(go) &&
                !used.count(go) && d <= kShellMatchMax && d < fallback_d) {
                fallback_best = go;
                fallback_d = d;
            }
        }
    }
    return best ? best : fallback_best;
}

// The vanilla save slot already holds this building's interior (SI: LegacyImport ProbeLegacySave): the port doesn't
// import it yet, so the building keeps the game's own door (plan §2.5). Keys: the scene's name, an instanced key
// "<scene>_<door guid>" or a door's bare guid.
bool has_vanilla_data(const Instance& in, std::string& key_out) {
    if (!g_scene_keys_read) {
        g_scene_keys_read = true;
        Il2CppObject* list = api.scene_keys ? il2::invoke(api.scene_keys, nullptr) : nullptr;
        g_scene_keys_failed = !api.scene_keys;
        each_listed(list, [&](Il2CppObject* s, int) {
            if (s) g_scene_keys.push_back(text(s));
        });
        logx::info("interiors: the save slot holds %zu scene record(s)%s", g_scene_keys.size(),
                   g_scene_keys_failed ? " (could not be read: every building keeps the game's door)" : "");
    }
    if (g_scene_keys_failed) {
        key_out = "(slot unreadable)";
        return true;
    }
    const std::string scene = in.cfg->scene, prefix = scene + "_";
    std::vector<std::string> guids;
    if (Il2CppObject* shell = live(in.shell))
        each_in(shell, api.t_load_scene, [&](Il2CppObject* door) {
            if (text(get<Il2CppObject*>(door, api.door_scene)) == scene) {
                const std::string g = text(get<Il2CppObject*>(door, api.door_guid));
                if (!g.empty()) guids.push_back(g);
            }
        });
    for (const std::string& k : g_scene_keys) {
        if (k == scene || k.rfind(prefix, 0) == 0 || std::find(guids.begin(), guids.end(), k) != guids.end()) {
            key_out = k;
            return true;
        }
    }
    return false;
}

void set_stage(Instance& in, Stage s) {
    in.stage = s;
    in.stage_since = now_ms();
}

Il2CppObject* new_game_object(const char* name) {
    Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
    void* a[] = {str(name)};
    il2::invoke(api.go_ctor, go, a);
    return alive(go) ? go : nullptr;
}

// SI: LoadInteriorScenes, first half: the three scenes requested together (one building at a time).
bool start_loading(Instance& in) {
    const std::string base = in.cfg->scene;
    in.started = now_ms();
    // SI: another building of this region already loaded the scene: a copy of its untouched snapshot.
    if (Il2CppObject* pristine = live(g_pristine[base])) {
        g_copying_template = true;
        void* a[] = {pristine};
        Il2CppObject* copy = il2::invoke(api.instantiate, nullptr, a);
        g_copying_template = false;
        if (!alive(copy)) {
            logx::warn("interiors: %s: could not copy the template of %s; the door stays the game's", in.cfg->id, base.c_str());
            set_stage(in, Stage::Vanilla);
            return false;
        }
        char nm[160];
        snprintf(nm, sizeof(nm), "Master_%s_Interior", in.cfg->id);
        void* na[] = {str(nm)};
        il2::invoke(api.set_name, copy, na);
        set_active(copy, false);
        in.master = hold(copy);
        in.master_tf = hold(go_tf(copy));
        rooms_changed();
        move_to_scene(copy, scene_by_name(g_region));
        each_in(copy, api.t_renderer, [&](Il2CppObject* r) {
            if (!renderer_on(r)) in.keep_off.push_back(hold(r));
        });
        logx::info("interiors: %s: copied the template of %s (loaded for another building)", in.cfg->id, base.c_str());
        return true;  // straight on to preparing (no scene to wait for)
    }
    // SI: FIX 1: the region's lightmaps and probes, which an interior scene load overwrites.
    if (api.lightmaps) in.cached_lightmaps = hold(il2::invoke(api.lightmaps, nullptr));
    if (api.probes) in.cached_probes = hold(il2::invoke(api.probes, nullptr));
    g_loading_base = base;
    g_template_scenes.clear();
    for (const std::string& name : {base, base + "_SANDBOX", base + "_DLC01"}) {
        if (g_missing_scenes.count(name)) continue;
        Instance::Op op;
        op.scene = name;
        int32_t mode = 1;  // LoadSceneMode.Additive
        bool activate = true;
        int32_t priority = 100, release = 0;  // SceneReleaseMode.ReleaseSceneWhenSceneUnloaded
        void* a[] = {str(name.c_str()), &mode, &activate, &priority, &release};
        Il2CppObject* handle = il2::invoke(api.load_scene, nullptr, a);
        if (handle) {
            const ptrdiff_t off = il2::field_offset_quiet(il2::il2cpp_object_get_class(handle), "m_InternalOp");
            Il2CppObject* internal = off >= 0 ? get<Il2CppObject*>(handle, off) : nullptr;
            op.op = hold(internal);
        }
        if (!op.op) {
            logx::info("interiors: %s: %s could not be requested (not in this game?)", in.cfg->id, name.c_str());
            g_missing_scenes.insert(name);
            continue;
        }
        in.ops.push_back(op);
    }
    if (in.ops.empty()) {
        logx::warn("interiors: %s: none of %s's scenes could be loaded; the door stays the game's", in.cfg->id, base.c_str());
        g_loading_base.clear();
        set_stage(in, Stage::Vanilla);
        return false;
    }
    logx::info("interiors: %s: loading %zu interior scene(s) of %s", in.cfg->id, in.ops.size(), base.c_str());
    set_stage(in, Stage::Loading);
    return false;
}

// Polls the scene loads. True once every one is done (succeeded or failed).
bool loads_done(Instance& in) {
    bool all = true;
    for (Instance::Op& op : in.ops) {
        if (op.done) continue;
        Il2CppObject* o = target(op.op);
        bool done = false;
        int32_t status = 0;
        if (o) {
            if (const MethodInfo* m = method_up(o, "get_IsDone", 0)) il2::invoke_value(m, o, done);
            if (const MethodInfo* m = method_up(o, "get_Status", 0)) il2::invoke_value(m, o, status);
        }
        const int32_t h = scene_by_name(op.scene);
        if (h && scene_loaded(h)) g_template_scenes.insert(h);
        if (done || !o) {
            op.done = true;
            op.ok = status == 1 || scene_loaded(h);  // AsyncOperationStatus.Succeeded
            if (!op.ok) {
                g_missing_scenes.insert(op.scene);
                logx::info("interiors: %s: %s is not in this game (skipped from now on)", in.cfg->id, op.scene.c_str());
            }
            continue;
        }
        all = false;
    }
    return all;
}

// SI: LoadInteriorScenes, second half: lightmaps merged, the scenes' roots put under one Master_<id>_Interior in the
// region scene, the region made the active scene again, a snapshot kept for other buildings of the same scene.
bool merge_scenes(Instance& in) {
    const std::string base = in.cfg->scene;
    // Lightmaps: the outdoor entries [0..n) stay, the interior's are appended (the array the load left, in place).
    Il2CppObject* cached = target(in.cached_lightmaps);
    Il2CppObject* now_maps = api.lightmaps ? il2::invoke(api.lightmaps, nullptr) : nullptr;
    const size_t n_out = il2::array_length(cached), n_now = il2::array_length(now_maps);
    if (cached && now_maps && n_now > n_out) {
        for (size_t i = 0; i < n_out; ++i) put_slot(now_maps, i, il2::array_object_at(cached, i));
        void* a[] = {now_maps};
        il2::invoke(api.set_lightmaps, nullptr, a);
    } else if (cached) {
        void* a[] = {cached};
        il2::invoke(api.set_lightmaps, nullptr, a);
    }
    if (Il2CppObject* probes = target(in.cached_probes)) {
        void* a[] = {probes};
        il2::invoke(api.set_probes, nullptr, a);
    }
    drop(in.cached_lightmaps);
    drop(in.cached_probes);

    char nm[160];
    snprintf(nm, sizeof(nm), "Master_%s_Interior", in.cfg->id);
    Il2CppObject* master = new_game_object(nm);
    if (!master) return false;
    set_active(master, false);
    const int32_t region = scene_by_name(g_region);
    move_to_scene(master, region);
    in.master = hold(master);
    Il2CppObject* mt = go_tf(master);
    in.master_tf = hold(mt);
    rooms_changed();
    int roots = 0;
    int32_t main_scene = 0;
    for (const Instance::Op& op : in.ops) {
        if (!op.ok) continue;
        const int32_t h = scene_by_name(op.scene);
        if (!scene_loaded(h)) continue;
        if (op.scene == base) main_scene = h;
        for (Il2CppObject* root : scene_roots(h)) {
            if (!alive(root) || root == master) continue;
            bool keep_world = false;
            void* pa[] = {mt, &keep_world};
            il2::invoke(api.tf_set_parent, go_tf(root), pa);
            ++roots;
        }
    }
    if (roots == 0) {
        logx::warn("interiors: %s: no objects found in %s's scenes (roots unreadable?); the door stays the game's", in.cfg->id,
                   base.c_str());
        return false;
    }
    // SI: CollectDisabledRenderers: the state the scene shipped, before anything touched it.
    each_in(master, api.t_renderer, [&](Il2CppObject* r) {
        if (!renderer_on(r)) in.keep_off.push_back(hold(r));
    });
    if (region) set_active_scene(region);
    // SI: IsSceneSharedInRegion: an inactive snapshot for the region's other buildings of this scene, kept in the
    // emptied interior scene (it goes when the region does).
    int sharing = 0;
    for (int i = 0; i < kConfigCount; ++i)
        if (g_region == kConfigs[i].region && base == kConfigs[i].scene) ++sharing;
    if (sharing > 1) {
        g_copying_template = true;
        void* a[] = {master};
        Il2CppObject* snap = il2::invoke(api.instantiate, nullptr, a);
        g_copying_template = false;
        if (alive(snap)) {
            const std::string sn = "SI_PristineTemplate_" + base;
            void* na[] = {str(sn.c_str())};
            il2::invoke(api.set_name, snap, na);
            set_active(snap, false);
            if (main_scene) move_to_scene(snap, main_scene);
            g_pristine[base] = hold(snap);
            rooms_changed();
        }
    }
    logx::info("interiors: %s: %d scene root(s) under %s, %.1f s after the request%s", in.cfg->id, roots, nm,
               (now_ms() - in.started) / 1000.0, sharing > 1 ? "; snapshot kept for the other buildings" : "");
    return true;
}

// SI: LightingManagersUse (the component half): does a lighting manager of the room still use what's under t?
bool used_by_lighting(Il2CppObject* t, bool room_has_manager) {
    if (!room_has_manager) return false;
    return has_in_children(t, api.t_light) || has_in_children(t, api.t_particles) || has_in_children(t, api.t_gimble);
}

// SI: StripLightContainer: WindowLight / WindowLightGroup emptied, never destroyed (lighting managers hold them).
void strip_light_container(Il2CppObject* go) {
    Il2CppObject* t = go_tf(go);
    int32_t n = 0;
    il2::invoke_value(api.tf_child_count, t, n);
    for (int32_t i = 0; i < n; ++i) {
        int32_t k = i;
        void* a[] = {&k};
        if (Il2CppObject* child = il2::invoke(api.tf_child, t, a)) set_active(go_of(child), false);
    }
    each_in(go, api.t_light, [&](Il2CppObject* l) {
        int32_t zero = 0;
        float f0 = 0, tiny = 0.01f;
        void* m[] = {&zero};
        il2::invoke(api.light_set_mask, l, m);
        void* s[] = {&zero};
        il2::invoke(api.light_set_shadows, l, s);
        void* i[] = {&f0};
        il2::invoke(api.light_set_intensity, l, i);
        void* r[] = {&tiny};
        il2::invoke(api.light_set_range, l, r);
        set_behaviour(l, false);
    });
}

// SI: PrepareMasterInterior (the outdoor lighting mode): the config's destroy/disable lists, light shafts off,
// directional lights gone, big point lights off, stray cameras and listeners gone, static flags off, collision
// helpers hidden; the dark-mode lighting objects off.
void prepare_room(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    const Config& c = *in.cfg;
    bool room_has_manager = false;
    each_in(m, api.t_ilm, [&](Il2CppObject*) { room_has_manager = true; });
    each_in(m, api.t_dlm, [&](Il2CppObject*) { room_has_manager = true; });
    int shafts = 0, destroyed = 0, kept_for_lighting = 0, disabled = 0, dark_only = 0, emptied = 0;
    std::vector<Il2CppObject*> all;
    each_in(m, api.t_transform, [&](Il2CppObject* t) { all.push_back(t); });
    for (Il2CppObject* t : all) {
        if (!alive(t)) continue;
        Il2CppObject* go = go_of(t);
        const std::string nm = name_of(t);
        if (listed(kLightShafts, std::size(kLightShafts), nm) || listed(kLightShafts, std::size(kLightShafts), base_name(nm))) {
            set_active(go, false);  // never destroyed: the gimbles keep references to them
            ++shafts;
            continue;
        }
        bool destroy_it = in_names(c.destroy_first, c.destroy_count, nm);
        const bool disable_it = in_names(c.disable_first, c.disable_count, nm);
        if ((destroy_it || disable_it) && listed(kDarkOnly, std::size(kDarkOnly), nm)) {
            set_active(go, false);  // dark-mode only: off in the outdoor lighting mode
            ++dark_only;
            continue;
        }
        if (destroy_it && listed(kLightContainers, std::size(kLightContainers), nm)) {
            strip_light_container(go);
            ++emptied;
            destroy_it = false;
        }
        if (destroy_it) {
            if (used_by_lighting(t, room_has_manager)) {
                set_active(go, false);
                ++kept_for_lighting;
            } else {
                destroy(go);
                ++destroyed;
            }
            continue;
        }
        if (disable_it) {
            set_active(go, false);
            ++disabled;
        }
    }
    // Reflection probes: SI scopes them (box projection, importance 0) and keeps them for dark mode; in the outdoor
    // mode they are off (ApplyLightingMode).
    int probes = 0;
    each_in(m, api.t_probe, [&](Il2CppObject* p) {
        set_behaviour(p, false);
        ++probes;
    });
    int suns = 0, big = 0;
    each_in(m, api.t_light, [&](Il2CppObject* l) {
        int32_t type = -1;
        il2::invoke_value(api.light_type, l, type);
        if (type == kLightDirectional) {
            destroy(go_of(l));  // an interior's fake sun is global
            ++suns;
            return;
        }
        float range = 0;
        il2::invoke_value(api.light_range, l, range);
        if (type == kLightPoint && range > 15.0f) {
            if (range > 25.0f) {
                float r = 25.0f;
                void* a[] = {&r};
                il2::invoke(api.light_set_range, l, a);
            }
            set_behaviour(l, false);  // outdoor mode: big point lights spill through the walls
            ++big;
        }
    });
    // Cameras (but a RenderTextureGrabber's: the trader radio draws with it) and audio listeners.
    std::unordered_set<Il2CppObject*> texture_cams;
    if (api.grabber_camera >= 0)
        each_in(m, api.t_grabber, [&](Il2CppObject* g) {
            if (Il2CppObject* cam = get<Il2CppObject*>(g, api.grabber_camera)) texture_cams.insert(cam);
        });
    int cams = 0, listeners = 0;
    each_in(m, api.t_camera, [&](Il2CppObject* cam) {
        if (!texture_cams.count(cam)) {
            destroy(cam);
            ++cams;
        }
    });
    each_in(m, api.t_listener, [&](Il2CppObject* l) {
        destroy(l);
        ++listeners;
    });
    // Static batching off (the room moves), and the collision helpers nobody was meant to see.
    int helpers = 0;
    each_in(m, api.t_mesh_renderer, [&](Il2CppObject* r) {
        Il2CppObject* go = go_of(r);
        bool st = false;
        if (il2::invoke_value(api.go_static, go, st) && st) {
            bool off = false;
            void* a[] = {&off};
            il2::invoke(api.go_set_static, go, a);
        }
        if (!renderer_on(r)) return;
        Il2CppObject* mat = il2::invoke(api.r_material, r);
        int32_t layer = -1;
        il2::invoke_value(api.go_layer, go, layer);
        const bool unauthored = !alive(mat) || name_of(mat).rfind("Default-Material", 0) == 0;
        if (unauthored || (api.layer_collide_only >= 0 && layer == api.layer_collide_only)) {
            set_renderer(r, false);
            in.keep_off.push_back(hold(r));
            ++helpers;
        }
    });
    logx::info("interiors: %s: prepared: %d destroyed, %d kept off for its lighting managers, %d disabled, %d dark-mode "
               "lighting objects off, %d light containers emptied, %d light shafts off, %d sun(s) gone, %d big lights off, "
               "%d probes off, %d camera(s) and %d listener(s) gone, %d collision helper(s) hidden",
               c.id, destroyed, kept_for_lighting, disabled, dark_only, emptied, shafts, suns, big, probes, cams, listeners,
               helpers);
}

// SI: AlignWithExteriorShell (parked, no yaw search: no windows yet).
void align_room(Instance& in) {
    Il2CppObject* mt = live(in.master_tf);
    if (!mt) return;
    const Config& c = *in.cfg;
    Il2CppObject* shell = live(in.shell);
    V3 pos = (c.force_exact || !shell) ? c.fallback : position(go_tf(shell));
    pos.y += c.y_offset;
    pos = pos + kParkOffset;
    const Quat rot = (shell && !c.force_exact) ? mul(rotation(go_tf(shell)), euler(c.rotation)) : euler(c.rotation);
    set_position(mt, pos);
    Quat r = rot;
    void* ra[] = {&r};
    il2::invoke(api.tf_set_rotation, mt, ra);
    V3 one{1, 1, 1};  // a parked room loads at its own size
    void* sa[] = {&one};
    il2::invoke(api.tf_set_local_scale, mt, sa);
    in.master_pos = position(mt);
    in.master_rot = rotation(mt);
    logx::info("interiors: %s: room parked at (%.1f, %.1f, %.1f), %s", c.id, in.master_pos.x, in.master_pos.y, in.master_pos.z,
               shell ? (c.force_exact ? "the config's spot (its shell found)" : "on its shell's spot") : "the config's spot (no shell)");
}

// SI: HandleInitialPlaceables / DisableInteriorContainerSerialization / ApplySafehouseCustomizationFix: the room's
// placeables are marked invalidated while it wakes, and valid again once it's built.
void set_placeables_invalidated(Instance& in, bool invalid) {
    if (api.placeable_invalidated < 0) return;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* p) { put<bool>(p, api.placeable_invalidated, invalid); });
}

uint32_t fnv1a(const std::string& s) {  // SI: StableHash (FNV-1a over UTF-16 code units; ASCII here)
    uint32_t h = 2166136261u;
    for (unsigned char ch : s) {
        h ^= ch;
        h *= 16777619u;
    }
    return h;
}

bool active_up_to(Il2CppObject* t, Il2CppObject* root) {  // SI: IsHierarchyActiveUpTo
    for (int depth = 0; t && t != root && depth < 64; ++depth, t = parent_of(t))
        if (!active_self(go_of(t))) return false;
    return true;
}
bool inside_container(Il2CppObject* t, Il2CppObject* root) {  // SI: IsInsideContainer
    for (Il2CppObject* p = parent_of(t); p && p != root; p = parent_of(p))
        if (component(go_of(p), api.t_container)) return true;
    return false;
}

// SI: PerformInitialLootRoll + RollSingleSpawner + PreRollGearSpawnChance: the game's own random-spawn choice and
// per-item spawn chance, rolled while the room is still closed, seeded from the save and the building, so a building
// rolls the same loot on every load.
void roll_loot(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    const std::string seed_text = loot_seed_save() + "/" + in.cfg->id;  // (a friend: the host's game's)
    alignas(16) int32_t state_before[4] = {};
    bool have_state = false;
    if (api.rnd_state) {
        Il2CppObject* boxed = il2::invoke(api.rnd_state, nullptr);
        if (boxed) {
            std::memcpy(state_before, il2::il2cpp_object_unbox(boxed), sizeof(state_before));
            have_state = true;
        }
    }
    int32_t seed = static_cast<int32_t>(fnv1a(seed_text));
    void* sa[] = {&seed};
    il2::invoke(api.rnd_init, nullptr, sa);
    int spawners = 0, rolled = 0, candidates = 0, quota_total = 0, pre_inactive = 0;
    each_in(m, api.t_rso, [&](Il2CppObject* rso) {
        ++spawners;
        Il2CppObject* list = get<Il2CppObject*>(rso, api.rso_list);
        Il2CppObject* weights = api.rso_weights >= 0 ? get<Il2CppObject*>(rso, api.rso_weights) : nullptr;
        const size_t n = il2::array_length(list);
        std::vector<Il2CppObject*> cands;
        std::vector<int> w;
        for (size_t i = 0; i < n; ++i) {
            Il2CppObject* go = il2::array_object_at(list, i);
            if (!alive(go)) continue;
            if (!active_self(go)) ++pre_inactive;
            set_active(go, false);
            cands.push_back(go);
            int wi = 1;
            if (weights && i < il2::array_length(weights)) {
                int32_t raw = 0;
                std::memcpy(&raw, reinterpret_cast<const char*>(weights) + 0x20 + i * 4, 4);
                if (raw > 0) wi = raw;
            }
            w.push_back(wi);
        }
        if (cands.empty()) return;
        candidates += static_cast<int>(cands.size());
        int32_t quota = 1;
        if (api.rso_quota) il2::invoke_value(api.rso_quota, rso, quota);
        quota = std::clamp<int32_t>(quota, 0, static_cast<int32_t>(cands.size()));
        quota_total += quota;
        for (int picked = 0; picked < quota; ++picked) {
            int total = 0;
            for (int x : w) total += x;
            if (total <= 0) break;
            int32_t lo = 0, hi = total, roll = 0;
            void* ra[] = {&lo, &hi};
            il2::invoke_value(api.rnd_range, nullptr, roll, ra);
            size_t chosen = w.size() - 1;
            for (size_t i = 0; i < w.size(); ++i) {
                roll -= w[i];
                if (roll < 0) {
                    chosen = i;
                    break;
                }
            }
            set_active(cands[chosen], true);
            cands.erase(cands.begin() + static_cast<ptrdiff_t>(chosen));
            w.erase(w.begin() + static_cast<ptrdiff_t>(chosen));
        }
        ++rolled;
    });
    if (have_state && api.rnd_set_state) {
        void* a[] = {state_before};
        il2::invoke(api.rnd_set_state, nullptr, a);
    }
    // The per-item spawn chance (GearItem.RollSpawnChance seeds from the item's position itself).
    int spawn_rolled = 0, spawn_removed = 0, active_after = 0;
    Il2CppObject* mt = go_tf(m);
    each_in(m, api.t_gear, [&](Il2CppObject* gi) {
        Il2CppObject* t = tf_of(gi);
        if (!active_up_to(t, mt)) return;
        if (get<bool>(gi, api.gear_rolled) || inside_container(t, mt)) {
            ++active_after;
            return;
        }
        il2::invoke(api.gear_roll, gi);
        ++spawn_rolled;
        if (!active_self(go_of(gi)))
            ++spawn_removed;
        else
            ++active_after;
    });
    in.rolled = true;
    logx::info("interiors: %s: loot rolled (seed %08x): %d of %d random spawners, %d candidates (%d off before), %d wanted "
               "for this difficulty; spawn chance: %d of %d items removed; %d items stand in the room",
               in.cfg->id, static_cast<uint32_t>(seed), rolled, spawners, candidates, pre_inactive, quota_total, spawn_removed,
               spawn_rolled, active_after);
}

// SI: FreezeGearPhysicsIn: template loot with live physics would fall once the room closes.
void freeze_gear(Instance& in) {
    int frozen = 0;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        bool physics = false;
        if (api.gear_has_physics && il2::invoke_value(api.gear_has_physics, gi, physics) && physics) {
            il2::invoke(api.gear_disable_physics, gi);
            ++frozen;
        }
    });
    if (frozen) logx::info("interiors: %s: physics frozen on %d item(s)", in.cfg->id, frozen);
}

// SI: ComputeLocalInteriorBounds: the room's renderers (not shadow casters, not over 40 m, centre within the
// building's radius), in the room's own frame.
bool room_bounds(Instance& in, V3& center, V3& size) {
    Il2CppObject* m = master_of(in);
    if (!m) return false;
    const float radius = in.cfg->bounds_radius;
    Box box;
    each_in(m, api.t_renderer, [&](Il2CppObject* r) {
        if (name_of(go_of(r)).find("Shadow_Caster") != std::string::npos) return;
        UBounds b;
        if (!il2::invoke_value(api.r_bounds, r, b)) return;
        if (b.extents.x * 2 > 40 || b.extents.y * 2 > 40 || b.extents.z * 2 > 40) return;
        const V3 lc = to_local(in, b.center);
        if (std::fabs(lc.x) > radius || std::fabs(lc.y) > radius || std::fabs(lc.z) > radius) return;
        for (int k = 0; k < 8; ++k)
            box.add(to_local(in, {b.center.x + ((k & 1) ? -b.extents.x : b.extents.x),
                                  b.center.y + ((k & 2) ? -b.extents.y : b.extents.y),
                                  b.center.z + ((k & 4) ? -b.extents.z : b.extents.z)}));
    });
    if (!box.valid()) {
        center = {0, 2, 0};
        size = {25, 18, 25};
        return false;
    }
    center = (box.min + box.max) * 0.5f;
    size = box.max - box.min;
    return true;
}

Il2CppObject* new_child(Il2CppObject* parent_go, const char* name, int layer) {
    Il2CppObject* go = new_game_object(name);
    if (!go) return nullptr;
    bool keep_world = false;
    void* a[] = {go_tf(parent_go), &keep_world};
    il2::invoke(api.tf_set_parent, go_tf(go), a);
    if (layer >= 0) {
        int32_t l = layer;
        void* la[] = {&l};
        il2::invoke(api.go_set_layer, go, la);
    }
    return go;
}
Il2CppObject* add_box(Il2CppObject* go, V3 center, V3 size, bool trigger) {
    Il2CppObject* box = add_component(go, api.t_box);
    if (!alive(box)) return nullptr;
    void* ca[] = {&center};
    il2::invoke(api.box_set_center, box, ca);
    void* sa[] = {&size};
    il2::invoke(api.box_set_size, box, sa);
    if (trigger) {
        bool t = true;
        void* ta[] = {&t};
        il2::invoke(api.col_set_trigger, box, ta);
    }
    return box;
}

// SI: SetupWeatherAndParticles (the trigger part: parked rooms have no particle killers) + CreateVanillaIndoorSpace.
void setup_trigger_and_space(Instance& in, V3 center, V3 size) {
    Il2CppObject* m = master_of(in);
    Il2CppObject* killer = new_child(m, "ParticleKiller", api.layer_trigger);
    const V3 padded = size + kTriggerPadding;
    if (killer && add_box(killer, center, padded, true)) {
        in.trig_center = center;
        in.trig_size = padded;
        in.have_trigger = true;
        in.trigger = hold(component(killer, api.t_box));
    }
    Il2CppObject* space_go = new_child(m, "SI_IndoorSpace", -1);
    if (!space_go) return;
    if (Il2CppObject* guid = add_component(space_go, api.t_object_guid); alive(guid) && api.set_runtime_pdid) {
        void* a[] = {str(*in.cfg->scene ? in.cfg->scene : in.cfg->id)};
        il2::invoke(api.set_runtime_pdid, guid, a);
    }
    Il2CppObject* space = add_component(space_go, api.t_indoor);
    if (!alive(space)) {
        logx::warn("interiors: %s: could not add the game's indoor space", in.cfg->id);
        return;
    }
    put<bool>(space, api.indoor_dont_count, false);
    put<bool>(space, api.indoor_outdoor_temp, false);
    put<float>(space, api.indoor_delta, 0.0f);
    put<bool>(space, api.indoor_outdoor_light, false);
    put<bool>(space, api.indoor_campfires, false);
    put<bool>(space, api.indoor_travois, false);
    put<bool>(space, api.indoor_cant_survey, true);
    put<bool>(space, api.indoor_valid_safehouse, false);
    if (api.indoor_trigger_id >= 0) {
        const std::string id = std::string("SI_") + in.cfg->id;
        put_ref(space, api.indoor_trigger_id, str(id.c_str()));
    }
    in.space = hold(space);
}

// SI: SetupSolidPerimeter: four walls on the NPC layer and a carving NavMesh obstacle round the room.
void setup_perimeter(Instance& in, V3 c, V3 s) {
    Il2CppObject* go = new_child(master_of(in), "SolidPerimeter_Blocker", api.layer_npc);
    if (!go) return;
    const float wt = 0.5f;
    const V3 mn = c - s * 0.5f, mx = c + s * 0.5f;
    add_box(go, {c.x, c.y, mx.z + wt / 2}, {s.x, s.y, wt}, false);
    add_box(go, {c.x, c.y, mn.z - wt / 2}, {s.x, s.y, wt}, false);
    add_box(go, {mx.x + wt / 2, c.y, c.z}, {wt, s.y, s.z + wt * 2}, false);
    add_box(go, {mn.x - wt / 2, c.y, c.z}, {wt, s.y, s.z + wt * 2}, false);
    if (Il2CppObject* ob = add_component(go, api.t_obstacle); alive(ob)) {
        int32_t box_shape = 1;  // NavMeshObstacleShape.Box
        void* a[] = {&box_shape};
        il2::invoke(api.obstacle_shape, ob, a);
        V3 center = c, size = s;
        void* ca[] = {&center};
        il2::invoke(api.obstacle_center, ob, ca);
        void* sa[] = {&size};
        il2::invoke(api.obstacle_size, ob, sa);
        bool carve = true;
        void* va[] = {&carve};
        il2::invoke(api.obstacle_carving, ob, va);
    }
}

// SI: StripBakedLightmaps (the outdoor mode): every lightmapped renderer off its lightmap, with a copy of its material
// that has LIGHTMAP_ON off (one copy per source material), so the room is lit by the outdoors.
void strip_lightmaps(Instance& in) {
    int renderers = 0, copies = 0;
    each_in(master_of(in), api.t_renderer, [&](Il2CppObject* r) {
        int32_t index = -1;
        il2::invoke_value(api.r_lightmap, r, index);
        if (index < 0 || index >= 65534) return;
        Il2CppObject* mats = il2::invoke(api.r_materials, r);
        bool swap = false;
        for (size_t i = 0, n = il2::array_length(mats); i < n; ++i) {
            Il2CppObject* mat = il2::array_object_at(mats, i);
            if (!alive(mat)) continue;
            bool on = false;
            void* ka[] = {str("LIGHTMAP_ON")};
            if (!il2::invoke_value(api.mat_keyword_on, mat, on, ka) || !on) continue;
            const int id = instance_id(mat);
            Il2CppObject* copy = live(g_stripped_materials[id]);
            if (!copy) {
                void* ia[] = {mat};
                copy = il2::invoke(api.instantiate, nullptr, ia);
                if (!alive(copy)) continue;
                void* da[] = {str("LIGHTMAP_ON")};
                il2::invoke(api.mat_disable_keyword, copy, da);
                drop(g_stripped_materials[id]);
                g_stripped_materials[id] = hold(copy);
                ++copies;
            }
            put_slot(mats, i, copy);
            swap = true;
        }
        if (swap) {
            void* a[] = {mats};
            il2::invoke(api.r_set_materials, r, a);
        }
        int32_t none = -1;
        void* la[] = {&none};
        il2::invoke(api.r_set_lightmap, r, la);
        ++renderers;
    });
    logx::info("interiors: %s: baked lighting off on %d renderer(s) (%d material copies)", in.cfg->id, renderers, copies);
}

// SI: UpdateGlobalEnvironment: undo what the interior scenes did to the global weather and lighting (the region's
// lightmaps kept through it).
void update_global_environment() {
    Il2CppObject* maps = api.lightmaps ? il2::invoke(api.lightmaps, nullptr) : nullptr;
    Il2CppObject* probes = api.probes ? il2::invoke(api.probes, nullptr) : nullptr;
    uintptr_t hm = hold(maps), hp = hold(probes);
    if (Il2CppObject* w = weather()) il2::invoke(api.weather_force_outdoor, w);
    tick_capsule();
    if (api.lm_level_loaded) {
        bool t = true;
        il2::il2cpp_field_static_set_value(api.lm_level_loaded, &t);
    }
    if (api.lm_level_done) il2::invoke(api.lm_level_done, nullptr);
    if (api.lm_strength_default) il2::invoke(api.lm_strength_default, nullptr);
    if (api.dynamic_gi_update) il2::invoke(api.dynamic_gi_update, nullptr);
    if (Il2CppObject* m = target(hm)) {
        void* a[] = {m};
        il2::invoke(api.set_lightmaps, nullptr, a);
    }
    if (Il2CppObject* p = target(hp)) {
        void* a[] = {p};
        il2::invoke(api.set_probes, nullptr, a);
    }
    drop(hm);
    drop(hp);
}

// SI: ApplySafehouseCustomizationFix: the room's trigger counts as a safehouse space for the game's customizing.
void safehouse_fix(Instance& in) {
    Il2CppObject* list = static_object(api.safehouse_colliders);
    if (!list) {
        static bool warned = false;
        if (!warned) logx::info("interiors: the game's safehouse space list isn't there yet; rooms not added to it");
        warned = true;
    } else {
        if (Il2CppObject* t = live(in.trigger); t && !list_contains(list, t)) list_add(list, t);
        each_in(master_of(in), api.t_indoor, [&](Il2CppObject* trig) {
            if (!get<bool>(trig, api.indoor_valid_safehouse)) return;
            Il2CppObject* col = component(go_of(trig), api.t_collider);
            if (col && !list_contains(list, col)) list_add(list, col);
        });
    }
    set_placeables_invalidated(in, false);
}

// SI: RegisterCloneFiresWithManager: the room's fires, stoves and campfires in the game's fire lists (a fire missing
// there is missing from the next save).
void register_fires(Instance& in) {
    int added = 0;
    auto reg = [&](Il2CppObject* type, FieldInfo* field, const MethodInfo* add) {
        if (!type || !field || !add) return;
        Il2CppObject* list = static_object(field);
        each_in(master_of(in), type, [&](Il2CppObject* f) {
            if (list_contains(list, f)) return;
            void* a[] = {f};
            il2::invoke(add, nullptr, a);
            ++added;
        });
    };
    reg(api.t_fire, api.fire_list, api.fire_add);
    reg(api.t_stove, api.stove_list, api.stove_add);
    reg(api.t_campfire, api.campfire_list, api.campfire_add);
    if (added) logx::info("interiors: %s: %d fire(s) registered with the game", in.cfg->id, added);
}

// The building's scene loads still in flight become orphans (their handles move to g_orphan_loads).
void orphan_ops(Instance& in, const char* why) {
    for (Instance::Op& op : in.ops) {
        if (op.done || !op.op) continue;
        if (Il2CppObject* o = target(op.op)) {
            bool done = false;
            if (const MethodInfo* m = method_up(o, "get_IsDone", 0)) il2::invoke_value(m, o, done);
            (void)done;  // done or not: poll_orphans unloads what came in
            g_orphan_loads.push_back({op.scene, op.op, now_ms()});
            op.op = 0;
            logx::warn("interiors: %s: the load of %s is still coming in (%s); it is kept out of the game and unloaded once in",
                       in.cfg->id, op.scene.c_str(), why);
        } else {
            drop(op.op);
        }
        op.done = true;
        op.ok = false;
    }
}
bool orphan_scene(const std::string& scene_name_now) {
    if (scene_name_now.empty()) return false;
    for (const OrphanLoad& o : g_orphan_loads)
        if (o.scene == scene_name_now) return true;
    return false;
}
bool orphan_loading(const std::string& base) {  // any orphan load of this interior's scenes
    for (const OrphanLoad& o : g_orphan_loads)
        if (interior_base(o.scene) == base) return true;
    return false;
}
// Every frame, rooms or not: an orphan load that is over is unloaded (what came in goes with its scene).
void poll_orphans() {
    if (g_orphan_loads.empty()) return;
    static double next = 0;
    const double now = now_ms();
    if (now < next) return;
    next = now + 250;
    for (auto it = g_orphan_loads.begin(); it != g_orphan_loads.end();) {
        Il2CppObject* o = target(it->op);
        bool done = !o;
        if (o)
            if (const MethodInfo* m = method_up(o, "get_IsDone", 0)) il2::invoke_value(m, o, done);
        const int32_t h = scene_by_name(it->scene);
        const bool loaded = scene_loaded(h);
        if (!done && now - it->since < 300000) {  // (5 minutes: an operation that never ends is let go)
            ++it;
            continue;
        }
        if (loaded && api.unload_scene) {
            void* a[] = {str(it->scene.c_str())};
            il2::invoke(api.unload_scene, nullptr, a);
        }
        logx::info("interiors: the late load of %s is over%s", it->scene.c_str(),
                   loaded ? "; unloaded again (none of it reached the game)" : " (nothing came in)");
        drop(it->op);
        it = g_orphan_loads.erase(it);
    }
}

void fail_build(Instance& in, const char* why) {
    logx::warn("interiors: %s: %s; its door stays the game's", in.cfg->id, why);
    orphan_ops(in, why);
    if (Il2CppObject* m = master_of(in)) destroy(m);
    for (const Instance::Op& op : in.ops)  // what came in mustn't stand in the region at the interior's own spot
        if (op.ok && scene_loaded(scene_by_name(op.scene)) && api.unload_scene) {
            void* a[] = {str(op.scene.c_str())};
            il2::invoke(api.unload_scene, nullptr, a);
            logx::info("interiors: %s: %s unloaded again", in.cfg->id, op.scene.c_str());
        }
    drop(in.master);
    drop(in.master_tf);
    rooms_changed();
    for (auto* list : {&in.deferred_twins, &in.deferred_taken})  // (the region's items stay as they are)
        for (Instance::RegionItem& r : *list) drop(r.gi);
    in.deferred_twins.clear();
    in.deferred_taken.clear();
    drop_import(in);
    set_stage(in, Stage::Vanilla);
    if (g_building == &in) g_building = nullptr;
    g_loading_base.clear();
    g_template_scenes.clear();
}

void replay_deferred_gear();

// One step of the build of the room in progress (SI: Run, as a state machine).
void tick_build() {
    Instance* in = g_building;
    if (!in) return;
    const double now = now_ms();
    switch (in->stage) {
        case Stage::Loading: {
            const bool done = loads_done(*in);
            if (!done && now - in->stage_since < kSceneLoadTimeoutMs) return;
            if (!done) {
                logx::warn("interiors: %s: the scene load took over %.0f s; going on with what came", in->cfg->id,
                           kSceneLoadTimeoutMs / 1000);
                orphan_ops(*in, "the 45 s timeout");
            }
            const bool merged = merge_scenes(*in);
            g_loading_base.clear();
            replay_deferred_gear();
            if (!merged) return fail_build(*in, "its interior scenes could not be merged");
            [[fallthrough]];
        }
        case Stage::Idle: {  // (a copied template arrives here straight from start_loading)
            prepare_room(*in);
            align_room(*in);
            set_placeables_invalidated(*in, true);
            set_stage(*in, Stage::PreRoll);
            return;
        }
        case Stage::PreRoll: {
            if (now - in->stage_since < (hold_targets(*in) ? 0 : kPreRollWaitMs)) return;
            set_placeables_invalidated(*in, true);  // SI: DisableInteriorContainerSerialization (the containers' too)
            stabilize_guids(*in);                   // SI: HandleInitialPlaceables
            if (in->legacy == kLegacyReady) {       // an old save's interior: its files, in memory (seamless_import.inc)
                std::string why;
                if (!compose_import(*in, why))
                    return fail_build(*in, ("its interior in the old save can't be imported: " + why).c_str());
            }
            if (!decide_loot(*in)) roll_loot(*in);  // SI: ProcessSpawnsAndDeduplication, PerformInitialLootRoll
            each_in(master_of(*in), api.t_renderer, [&](Il2CppObject* r) { set_renderer(r, false); });
            set_active(master_of(*in), true);  // SI: invisibly, so the containers' and placeables' Awake run
            freeze_gear(*in);
            set_stage(*in, Stage::Waking);
            return;
        }
        case Stage::Waking: {
            if (now - in->stage_since < (hold_targets(*in) ? 500 : kAwakeWaitMs)) return;
            V3 center, size;
            if (!room_bounds(*in, center, size))
                logx::info("interiors: %s: no renderers to measure; the room's volume is SI's default box", in->cfg->id);
            setup_trigger_and_space(*in, center, size);
            setup_perimeter(*in, center, size);
            strip_lightmaps(*in);
            update_global_environment();
            register_fires(*in);
            safehouse_fix(*in);
            in->hydrate_step = 0;
            set_stage(*in, Stage::Hydrating);
            return;
        }
        case Stage::Hydrating: {
            if (!tick_hydrate(*in)) return;
            if (in->stage == Stage::Vanilla) {  // an old save's import that didn't come back whole (abandon_import)
                g_building = nullptr;
                g_template_scenes.clear();
                return;
            }
            show_room_renderers(*in);
            set_stage(*in, Stage::Ready);
            commit_import(*in);  // an old save's import: its files and marker written now (before the co-op ids)
            coop_room_ready(*in);  // milestone 2: its contents under ids every game shares, and the others told
            if (hold_targets(*in)) {
                enter_from_record(*in);  // SI: Run's early activation: the player saved in this room
            } else {
                // SI: ApplyInitialSyncState: the player isn't in it, so it closes (what stands in its volume with it).
                hide_room(*in);
                if (Il2CppObject* s = live(in->shell)) set_active(s, true);
            }
            g_building = nullptr;
            g_template_scenes.clear();
            g_wind_restore_at = now + 1500;
            g_region_scene_state = 0;  // the new room's AkState components are now in the scene: look again
            g_region_state_searches = 0;
            logx::info("interiors: %s: room ready in %.1f s (box %.1f x %.1f x %.1f m); its door now goes in without a "
                       "loading screen",
                       in->cfg->id, (now - in->started) / 1000.0, in->trig_size.x, in->trig_size.y, in->trig_size.z);
            return;
        }
        default:
            g_building = nullptr;
            return;
    }
}

// Lazy building (the port: SI builds every room behind a black screen at the region's load): the nearest building
// within kBuildDistance of the player whose room isn't built yet, one at a time.
Instance* g_urgent = nullptr;  // a building whose door was used before its room was built (it has saved contents)

void pick_building() {
    if (g_building || !g_feature_on) return;
    static double next = 0;
    const double now = now_ms();
    if (now < next) return;
    next = now + 250;
    // the game's own load first (shorter for the room the player saved in: they wait at its door in the dark)
    if (game::seconds_since_level_load() < (hold_active() ? 1.5f : 5.0f)) return;
    if (game::save_id().empty()) return;  // milestone 1b: the save's folder must be known before any room is filled
    const V3 p = player_pos();
    if (is_zero(p)) return;
    // milestone 2: the host builds a room when any player comes near (the friends' rooms are filled from the host's);
    // a friend builds only the rooms the host has, rolled with the host's seed.
    const Coop role = coop();
    if (role == Coop::Friend && loot_seed_save().empty()) return;  // the host's world id comes with its World
    std::vector<V3> spots{p};
    Instance* wanted_by_friend = nullptr;
    if (role == Coop::Host)
        for (const net::StateMsg& s : netrt::remote_states()) {
            if (!s.valid || s.scene != g_region) continue;
            V3 q{s.x, s.y, s.z};
            if (!s.place.empty()) {  // inside a room of theirs: it stands 80 m above its building
                q = q - kParkOffset;
                if (Instance* fi = find_instance(s.place.c_str()); fi && fi->stage == Stage::Idle) wanted_by_friend = fi;
            }
            spots.push_back(q);
        }
    auto nearest = [&](const Instance& in) {
        float d = 1e30f;
        for (const V3& q : spots) d = std::min(d, dist(q, in.cfg->fallback));
        return d;
    };
    Instance* best = nullptr;
    float best_d = kBuildDistance;
    for (Instance& in : g_instances) {
        if (in.stage != Stage::Idle) continue;
        if (role == Coop::Friend && !host_has(in)) continue;
        if (orphan_loading(in.cfg->scene)) continue;  // a late load of its scene is still coming in (poll_orphans)
        if (hold_targets(in) || &in == g_urgent || &in == wanted_by_friend) {
            best = &in;
            best_d = nearest(in);
            break;
        }
        const float d = nearest(in);
        if (d < best_d) {
            best_d = d;
            best = &in;
        }
    }
    if (!best) return;
    if (best == g_urgent) g_urgent = nullptr;
    best->coop_friend = role == Coop::Friend;
    ensure_dir(*best);
    Instance& in = *best;
    Il2CppObject* shell = find_shell(in);
    in.shell = hold(shell);
    if (*in.cfg->shell && !shell)
        logx::info("interiors: %s: its shell '%s' isn't within reach of its spot; the room stands on the config's spot",
                   in.cfg->id, in.cfg->shell);
    std::string why;
    // The save holds its interior from before the rooms: imported (seamless_import.inc), or the game's door.
    // (a friend: its own save's interiors don't matter; the host's room is the one everybody shares)
    if (role != Coop::Friend && !old_save_gate(in, why)) {
        logx::info("interiors: %s: %s; its door stays the game's%s", in.cfg->id, why.c_str(),
                   in.has_data ? " (our files of it are left alone)" : "");
        set_stage(in, Stage::Vanilla);
        return;
    }
    logx::info("interiors: %s: %s %.0f m from %s; building its room%s", in.cfg->id,
               role == Coop::Host ? "a player is" : "the player is", best_d, shell ? name_of(shell).c_str() : "its spot",
               role == Coop::Friend ? " (the host's: its contents come from the host's game)" : "");
    g_building = &in;
    if (start_loading(in)) set_stage(in, Stage::Idle);  // a copy: prepared on the next tick
    if (in.stage == Stage::Vanilla) g_building = nullptr;
}

// --- the door (SI: PortalPatches.cs) -----------------------------------------------------------------------------------

enum class DoorWay { Vanilla, Enter, Exit, Down, Up };
struct DoorChoice {
    DoorWay way = DoorWay::Vanilla;
    Instance* in = nullptr;      // the room entered or left
    const SubLink* link = nullptr;
    Instance* other = nullptr;   // a sub-link's other room
};

Il2CppObject* door_tf(Il2CppObject* door) { return tf_of(door); }
std::string door_scene(Il2CppObject* door) { return text(get<Il2CppObject*>(door, api.door_scene)); }

// SI: TryHandleSubInteriorLink. 0: no link; 1: out is the way; 2: a link to a room not built: the game's own door.
int sub_link(Il2CppObject* door, DoorChoice& out) {
    Il2CppObject* dt = door_tf(door);
    const V3 dp = position(dt);
    const std::string to = door_scene(door);
    for (Instance& parent : g_instances) {
        if (!in_room(&parent)) continue;
        for (int i = 0; i < parent.cfg->links_count; ++i) {
            const SubLink& l = kLinks[parent.cfg->links_first + i];
            Instance* child = find_instance(l.target);
            if (!child) continue;
            // parent -> child: the door is in the parent room and loads the child's scene, near the link's door
            if (child->cfg->scene == to && child_of(dt, live(parent.master_tf)) && dist_xz(dp, l.parent_door) <= 15) {
                if (!in_room(child)) return 2;  // the child isn't built (SI blocks the door): the game's own door
                out = {DoorWay::Down, child, &l, &parent};
                return 1;
            }
            // child -> parent: the door is in the child room and loads the parent's scene
            if (in_room(child) && parent.cfg->scene == to && child_of(dt, live(child->master_tf))) {
                out = {DoorWay::Up, &parent, &l, child};
                return 1;
            }
        }
    }
    return 0;
}

// Choose what a door does (SI: PortalMagicPatch.Prefix's matching). Only built rooms take doors: a building whose
// room isn't built (or never will be) keeps the game's own transition.
DoorChoice choose(Il2CppObject* door) {
    DoorChoice c;
    if (const int link = sub_link(door, c)) return link == 1 ? c : DoorChoice{};
    Il2CppObject* dt = door_tf(door);
    const std::string to = door_scene(door);
    for (Instance& in : g_instances) {
        if (!in_room(&in)) continue;
        Il2CppObject* shell = live(in.shell);
        const bool shell_door = shell && child_of(dt, go_tf(shell));
        const bool room_door = child_of(dt, live(in.master_tf));
        if (!shell_door && !room_door) continue;
        if (to == in.cfg->scene) return {DoorWay::Enter, &in};
        if (room_door && !leads_to_region(to) && in.cfg->doors_count == 0 && is_zero(in.cfg->exit)) continue;  // a mine shaft
        if (room_door) return {DoorWay::Exit, &in};
        // a shell door to another building's scene (the basement's outer door on the farmhouse): the nearest such room
        Instance* best = nullptr;
        float best_d = 1e30f;
        for (Instance& o : g_instances) {
            if (&o == &in || !in_room(&o) || to != o.cfg->scene) continue;
            const float d = dist(position(dt), o.cfg->fallback);
            if (d < best_d) {
                best_d = d;
                best = &o;
            }
        }
        if (best) return {DoorWay::Enter, best};
        return c;  // the game's own
    }
    // a door that isn't part of a shell: the nearest room within 30 m whose scene it loads, or whose room holds it
    {
        Instance* best = nullptr;
        float best_d = 30.0f;
        bool enter = false;
        const V3 dp = position(dt);
        for (Instance& in : g_instances) {
            if (!in_room(&in)) continue;
            const float d = distance_from(in, dp);
            if (d >= best_d) continue;
            const bool can_enter = to == in.cfg->scene, can_exit = child_of(dt, live(in.master_tf));
            if (!can_enter && !can_exit) continue;
            best_d = d;
            best = &in;
            enter = can_enter;
        }
        if (best) return {enter ? DoorWay::Enter : DoorWay::Exit, best};
    }
    // the region's only room of that scene (the mine concentrator's far doors)
    Instance* only = nullptr;
    int count = 0;
    for (Instance& in : g_instances)
        if (in_room(&in) && to == in.cfg->scene) {
            only = &in;
            ++count;
        }
    if (count == 1) return {DoorWay::Enter, only};
    return c;
}

// SI: FacingIntoRoom: away from the nearest room door within 3 m of the landing spot.
Quat facing_into_room(Instance& in, V3 spawn, Quat as_was) {
    V3 best;
    float best_sqr = kEntryFacingReach * kEntryFacingReach;
    bool found = false;
    each_in(master_of(in), api.t_load_scene, [&](Il2CppObject* door) {
        V3 away = spawn - position(door_tf(door));
        away.y = 0;
        const float sqr = away.x * away.x + away.z * away.z;
        if (sqr < best_sqr && sqr > 0.01f) {
            best_sqr = sqr;
            best = away;
            found = true;
        }
    });
    return found ? look_yaw(best) : as_was;
}

// SI: FindNamedNearest
Il2CppObject* find_named_nearest(Il2CppObject* root_go, const std::string& name, V3 near_pos) {
    if (!alive(root_go) || name.empty()) return nullptr;
    Il2CppObject* best = nullptr;
    float best_d = 1e30f;
    each_in(root_go, api.t_transform, [&](Il2CppObject* t) {
        if (name_of(t) != name) return;
        const float d = dist(position(t), near_pos);
        if (d < best_d) {
            best_d = d;
            best = t;
        }
    });
    return best;
}

// SI: GetDoorEntryPosition
V3 entry_point(Instance& in, Il2CppObject* door) {
    const V3 clicked = position(door_tf(door));
    const Config& c = *in.cfg;
    if (c.doors_count > 0) {
        const DoorSpawn* best = nullptr;
        float best_d = 1e30f;
        for (int i = 0; i < c.doors_count; ++i) {
            const DoorSpawn& d = kDoors[c.doors_first + i];
            const float dd = dist_xz(clicked, d.door_pos);
            if (dd < best_d) {
                best_d = dd;
                best = &d;
            }
        }
        if (best && best_d < 15.0f) return parked_point(in, best->entry);
    }
    if (!is_zero(c.entry)) return parked_point(in, c.entry);
    const std::string point = text(get<Il2CppObject*>(door, api.door_exit_point));
    if (Il2CppObject* t = find_named_nearest(master_of(in), point, clicked)) return position(t);
    Il2CppObject* sp = find_child(door_tf(door), "SpawnPoint");
    return sp ? position(sp) : clicked;
}

// SI: GetDoorExitPosition
V3 exit_point_raw(Instance& in, Il2CppObject* door, bool& vanilla_point) {
    vanilla_point = false;
    const V3 clicked = position(door_tf(door));
    const Config& c = *in.cfg;
    if (c.doors_count > 0) {
        const DoorSpawn* best = nullptr;
        float best_d = 1e30f;
        for (int i = 0; i < c.doors_count; ++i) {
            const DoorSpawn& d = kDoors[c.doors_first + i];
            const float dd = dist_xz(clicked, parked_point(in, d.entry));
            if (dd < best_d) {
                best_d = dd;
                best = &d;
            }
        }
        if (best && best_d < 15.0f) return best->exit;
    }
    if (!is_zero(c.exit)) return c.exit;
    const std::string point = text(get<Il2CppObject*>(door, api.door_exit_point));
    if (Il2CppObject* shell = live(in.shell)) {
        Il2CppObject* search = go_tf(shell);
        for (int level = 0; level < 3 && search; ++level, search = parent_of(search))
            if (Il2CppObject* t = find_named_nearest(go_of(search), point, clicked)) {
                vanilla_point = true;
                return on_the_ground(position(t));
            }
    }
    // entrances outside the shell: the exit point elsewhere in the region, within 30 m of where the door stands outdoors
    if (!point.empty()) {
        const V3 outdoor = clicked - kParkOffset;
        Il2CppObject* best = nullptr;
        float best_d = 30.0f;
        for (const std::string& sn : {g_region, g_region + "_SANDBOX", g_region + "_DLC01"}) {
            const int32_t h = scene_by_name(sn);
            if (!scene_loaded(h)) continue;
            for (Il2CppObject* root : scene_roots(h)) {
                if (!alive(root) || root == master_of(in)) continue;
                if (Il2CppObject* t = find_named_nearest(root, point, outdoor)) {
                    const float d = dist(position(t), outdoor);
                    if (d < best_d) {
                        best_d = d;
                        best = t;
                    }
                }
            }
        }
        if (best) {
            vanilla_point = true;
            return on_the_ground(position(best));
        }
    }
    Il2CppObject* sp = find_child(door_tf(door), "SpawnPoint");
    V3 fb = sp ? position(sp) : clicked;
    fb.y -= c.y_offset;
    return fb - kParkOffset;
}

// SI: ResolveExitPoint (with its height correction for a hand-raised exit point)
V3 exit_point(Instance& in, Il2CppObject* door) {
    bool vanilla = false;
    V3 p = exit_point_raw(in, door, vanilla);
    Il2CppObject* shell = live(in.shell);
    if (!vanilla && shell && master_of(in)) {
        const float shell_y = position(go_tf(shell)).y;
        const float lift = in.master_pos.y - shell_y;
        if (std::fabs(lift) > 1.0f && std::fabs(p.y - in.master_pos.y) < std::fabs(p.y - shell_y)) p.y -= lift;
    }
    return p;
}

void teleport(V3 pos, Quat rot) {
    Il2CppObject* pm = player_manager();
    if (!pm) return;
    void* a[] = {&pos, &rot};
    il2::invoke(api.teleport, pm, a);
    g_fall_guard_until = now_ms() + kFallGuardMs;
}

// The room comes up (SI: the entry half of PortalMagicPatch, and the sub-link's).
void open_room(Instance& in) {
    if (Il2CppObject* m = master_of(in)) set_active(m, true);
    show_room_renderers(in);
    if (Il2CppObject* s = live(in.shell)) set_active(s, false);
}

void enter(Instance& in, Il2CppObject* door) {
    open_room(in);
    if (!live(in.shell))  // a sub-interior entered from outside: its parent's shell and room go too
        for (Instance& parent : g_instances)
            if (is_sub_linked(parent, in.cfg->id) && in_room(&parent)) {
                if (Il2CppObject* s = live(parent.shell)) set_active(s, false);
                hide_room(parent);
            }
    set_outsiders_visible(in, true);
    set_audio_occlusion(true);
    mark_inside(&in, "through its door");
    Il2CppObject* ptf = player_tf();
    const V3 spawn = snap_to_ground(entry_point(in, door));
    teleport(spawn, facing_into_room(in, spawn, rotation(ptf)));
    if (api.door_mark_explored) il2::invoke(api.door_mark_explored, door);  // the game does this on its own transition
    tick_fog();
    tick_snow_emitters();
    tick_snow_planes();
    logx::info("interiors: entered %s through '%s' -> (%.1f, %.1f, %.1f)", in.cfg->id, name_of(go_of(door)).c_str(), spawn.x,
               spawn.y, spawn.z);
}

void leave(Instance& in, Il2CppObject* door) {
    V3 spawn = exit_point(in, door);
    exit_room(in, "through its door", true);  // first: nothing may meet the room's colliders after the move
    spawn = snap_to_ground(spawn);
    teleport(spawn, rotation(player_tf()));
    tick_fog();
    tick_snow_emitters();
    tick_snow_planes();
    logx::info("interiors: left %s through '%s' -> (%.1f, %.1f, %.1f)", in.cfg->id, name_of(go_of(door)).c_str(), spawn.x,
               spawn.y, spawn.z);
}

void go_down(DoorChoice& c) {  // parent -> child
    Instance& child = *c.in;
    Instance& parent = *c.other;
    open_room(child);
    hide_room(parent);  // the parent first, then the child's items (SI: ORDER MATTERS)
    if (Il2CppObject* s = live(parent.shell)) set_active(s, false);
    set_outsiders_visible(child, true);
    const V3 spawn = snap_to_ground(parked_point(child, c.link->child_spawn));
    teleport(spawn, rotation(player_tf()));
    set_audio_occlusion(true);
    mark_inside(&child, "down the stairs");
    logx::info("interiors: %s -> %s (sub-interior) at (%.1f, %.1f, %.1f)", parent.cfg->id, child.cfg->id, spawn.x, spawn.y,
               spawn.z);
}
void go_up(DoorChoice& c) {  // child -> parent
    Instance& parent = *c.in;
    Instance& child = *c.other;
    open_room(parent);
    hide_room(child);
    if (Il2CppObject* s = live(child.shell)) set_active(s, true);
    set_outsiders_visible(parent, true);
    const V3 spawn = snap_to_ground(parked_point(parent, c.link->parent_spawn));
    teleport(spawn, rotation(player_tf()));
    set_audio_occlusion(true);
    mark_inside(&parent, "up the stairs");
    logx::info("interiors: %s -> %s (back up) at (%.1f, %.1f, %.1f)", child.cfg->id, parent.cfg->id, spawn.x, spawn.y, spawn.z);
}

void carry_out(Il2CppObject* door) {
    DoorChoice c = choose(door);  // again: the world may have changed behind the fade
    g_last_portal = now_ms();
    door_sounds(false);
    switch (c.way) {
        case DoorWay::Enter: enter(*c.in, door); break;
        case DoorWay::Exit: leave(*c.in, door); break;
        case DoorWay::Down: go_down(c); break;
        case DoorWay::Up: go_up(c); break;
        default: logx::info("interiors: the door '%s' no longer leads into a room; nothing done", name_of(go_of(door)).c_str());
    }
}

void fade(bool out, float seconds) {
    const MethodInfo* m = out ? api.fade_out : api.fade_in;
    if (!m) return;
    float t = seconds, delay = 0;
    void* a[] = {&t, &delay, nullptr};
    il2::invoke(m, nullptr, a);
}

// SI: DoorThroughBlack: a quarter second to black, the move, a quarter held, a quarter back.
void tick_fade() {
    if (!g_fade.busy) return;
    const double now = now_ms();
    if (now < g_fade.until) return;
    if (g_fade.step == 1) {
        if (Il2CppObject* door = live(g_fade.door)) carry_out(door);
        g_fade.step = 2;
        g_fade.until = now + kDoorFadeHold * 1000;
        return;
    }
    fade(false, kDoorFadeIn);
    drop(g_fade.door);
    g_fade = {};
}

// --- hooks: the door ---------------------------------------------------------------------------------------------------

// LoadScene.Activate(bool) is the one place every door ends up: PerformInteraction (a click) is Activate(false) then
// an interaction event, Activate() is Activate(false), a hold-to-use door's Update calls Activate itself, and a walk-in
// door's OnTriggerEnter calls it too (read from the 2.58 code). SI patches the three callers; one hook here covers them.
using ActivateFn = void (*)(Il2CppObject* self, bool immediately, const MethodInfo* method);
ActivateFn g_activate = nullptr;
// GameManager.LoadSceneWithLoadingScreen: where a scene change really starts (a door of the game's, a region's edge, a
// co-op join, a respawn): TLD takes the scene's items away just before, which world_items mustn't tell as destroyed.
using LoadScreenFn = void (*)(Il2CppString* scene, const MethodInfo* method);
LoadScreenFn g_load_screen = nullptr;
void load_screen_detour(Il2CppString* scene, const MethodInfo* method) {
    world_items::door_used();
    g_load_screen(scene, method);
}
// A vanilla door to an interior whose scene we are loading right now (for a room, or a late load given up on) or whose
// building's room is being built: the game's own load of that scene would wake its GameManager while ours treats the
// scene as a template (skip_game_manager_awake would destroy the real one). Blocked until it's over (SI: PORTAL-BLOCK).
bool door_scene_busy(Il2CppObject* door, std::string& what) {
    const std::string to = door_scene(door);
    if (to.empty()) return false;
    const std::string base = interior_base(to);
    if (!g_loading_base.empty() && g_loading_base == base) {
        what = g_loading_base + " (loading for a room)";
        return true;
    }
    if (orphan_loading(base)) {
        what = base + " (a late load still coming in)";
        return true;
    }
    if (g_building && base == g_building->cfg->scene && g_building->stage != Stage::Ready &&
        g_building->stage != Stage::Vanilla && g_building->stage != Stage::Idle) {
        what = std::string(g_building->cfg->id) + " (its room is being built)";
        return true;
    }
    return false;
}

void activate_detour(Il2CppObject* self, bool immediately, const MethodInfo* method) {
    if (!self) return g_activate(self, immediately, method);
    std::string busy;
    if (api.ok && door_scene_busy(self, busy)) {  // (also with the feature off: a late load may still be coming in)
        static double told_at = -1e18;
        if (now_ms() - told_at > 2000) logx::info("interiors: [PORTAL-BLOCK] %s isn't ready yet; the door waits", busy.c_str());
        told_at = now_ms();
        put<bool>(self, api.door_touch_ground, false);
        return;
    }
    if (!engaged()) {
        world_items::door_used();  // TLDCoop_DLC: a door of the game's own, which changes the scene
        return g_activate(self, immediately, method);
    }
    if (g_fade.busy) return;  // a door is already being used (SI: s_DoorFadeBusy)
    const DoorChoice c = choose(self);
    if (c.way == DoorWay::Vanilla) {
        if (Instance* wait = hosted_unready(self)) {  // milestone 2: the host has this room; ours is coming
            static int told_coop = 0;
            if (told_coop++ < 20)
                logx::info("interiors: %s: the host's game has this room and ours isn't built yet; the door waits for it",
                           wait->cfg->id);
            if (wait->stage == Stage::Idle) g_urgent = wait;
            return;
        }
        if (Instance* wait = unready_with_data(self)) {  // milestone 1b: its room holds the player's things
            static int told = 0;
            if (told++ < 20)
                logx::info("interiors: %s: its room isn't built yet and has saved contents; the door waits for it", wait->cfg->id);
            if (wait->stage == Stage::Idle) g_urgent = wait;
            return;
        }
        static int logged = 0;
        if (logged++ < 20) logx::info("interiors: '%s' (to %s) is the game's own door", name_of(go_of(self)).c_str(), door_scene(self).c_str());
        world_items::door_used();  // TLDCoop_DLC: the scene changes (a seamless door, or one that waits, doesn't)
        return g_activate(self, immediately, method);
    }
    // In the game a scene load ends a hold-to-use door's wait; with none, Update would use the door again every frame.
    put<bool>(self, api.door_touch_ground, false);
    if (Il2CppObject* pm = player_manager()) il2::invoke(api.reset_interaction, pm);
    g_fade.busy = true;
    g_fade.step = 1;
    g_fade.door = hold(self);
    g_fade.until = now_ms() + kDoorFadeOut * 1000;
    fade(true, kDoorFadeOut);
}

// --- hooks: the game's save kept free of rooms (SI: SpawningPatches.cs, GearLeak.cs, FirePatches.cs, ...) --------------

// Under one of our rooms (or a room's untouched snapshot): every room is a root object of its own, so one look at the
// object's root answers it (Unity keeps one managed object per native one, so the pointers compare).
bool root_is_room(Il2CppObject* t) {
    const auto& roots = room_roots();
    if (roots.empty()) return false;
    Il2CppObject* root = alive(t) ? il2::invoke(api.tf_root, t) : nullptr;
    if (!root) return false;
    for (const auto& [r, in] : roots)
        if (r == root) return true;
    return false;
}
bool no_room_objects() { return room_roots().empty() && g_loading_base.empty() && g_orphan_loads.empty(); }
bool under_any_room(Il2CppObject* comp) { return root_is_room(tf_of(comp)); }

// SI: PreventFakeManagerPatch is skip_game_manager_awake below.

// SI: RandomSpawnBlockerPatch: a spawner in a room already rolled is destroyed before it rolls again.
using VoidFn = void (*)(Il2CppObject* self, const MethodInfo* method);
VoidFn g_rso_start = nullptr;
void rso_start_detour(Il2CppObject* self, const MethodInfo* method) {
    if (engaged() && self)
        for (Instance& in : g_instances)
            if (in.rolled)
                if (Il2CppObject* mt = live(in.master_tf); mt && child_of(tf_of(self), mt)) {
                    destroy(go_of(self));
                    return;
                }
    g_rso_start(self, method);
}

// SI: PreventPlaceableAutoRegisterPatch: the room's placeables stay out of the game's registry (they would come back
// from the region's save as duplicates, or the game would switch the room's furniture off).
using StaticObjFn = void (*)(Il2CppObject* arg, const MethodInfo* method);
StaticObjFn g_placeable_add = nullptr;
bool g_creating_placeable_1b = false;  // mirrors seamless_persist.inc's g_creating_placeable (SI: IsCreatingClonePlaceable)
void placeable_add_detour(Il2CppObject* placeable, const MethodInfo* method) {
    if (engaged() && placeable &&
        (g_copying_template || g_creating_placeable_1b || is_clone_object(placeable))) {
        static int logged = 0;
        if (logged++ < 5) logx::info("interiors: a room's placeable kept out of the game's registry");
        return;
    }
    g_placeable_add(placeable, method);
}

// SI: FixPlaceableManagerSerializeInventoryPatch.Prefix + PruneCloneEntries: dead handles (but carried or removed
// ones) and the rooms' Active/Inactive entries out of PlaceableManager.s_Placeables before the game writes it.
void prune_placeables(const char* where) {
    Il2CppObject* dict = static_object(api.placeables);
    if (!dict) return;
    Il2CppClass* k = il2::il2cpp_object_get_class(dict);
    const ptrdiff_t entries_off = il2::field_offset_quiet(k, "_entries"), count_off = il2::field_offset_quiet(k, "_count");
    const MethodInfo* remove = method_up(dict, "Remove", 1);
    if (entries_off < 0 || count_off < 0 || !remove) {
        static bool warned = false;
        if (!warned) logx::warn("interiors: PlaceableManager.s_Placeables can't be read; the rooms' entries stay in it");
        warned = true;
        return;
    }
    Il2CppObject* entries = get<Il2CppObject*>(dict, entries_off);
    const int32_t count = get<int32_t>(dict, count_off);
    // Entry<string, PlaceableInfo>: hashCode, next, key, value {Placeable m_Handle, PlacementState m_State} = 32 bytes
    constexpr size_t kStride = 32;
    const size_t len = il2::array_length(entries);
    std::vector<Il2CppObject*> keys;
    int dead = 0, rooms = 0;
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < len; ++i) {
        const char* e = reinterpret_cast<const char*>(entries) + 0x20 + i * kStride;
        int32_t next = 0;
        std::memcpy(&next, e + 4, 4);
        Il2CppObject* key = nullptr, *handle = nullptr;
        int32_t state = 0;
        std::memcpy(&key, e + 8, 8);
        std::memcpy(&handle, e + 16, 8);
        std::memcpy(&state, e + 24, 4);
        if (next < -1 || !key) continue;  // a free entry
        if (il2::class_name_of(key) != "System.String") {
            static bool warned = false;
            if (!warned) logx::warn("interiors: PlaceableManager.s_Placeables isn't laid out as expected; left alone");
            warned = true;
            return;
        }
        if (state == 2 || state == 1) continue;  // OnPlayer, Removed
        if (handle && !alive(handle)) {
            keys.push_back(key);
            ++dead;
            continue;
        }
        if (handle && (state == 0 || state == 3) && is_clone_object(handle)) {  // Active, Inactive
            keys.push_back(key);
            ++rooms;
        }
    }
    for (Il2CppObject* key : keys) {
        void* a[] = {key};
        il2::invoke(remove, dict, a);
    }
    if (dead || rooms)
        logx::info("interiors: %d dead and %d room placeable entr(ies) taken off the game's placement list (%s)", dead, rooms,
                   where);
}
using StaticRetFn = Il2CppObject* (*)(const MethodInfo* method);
StaticRetFn g_serialize_inventory = nullptr, g_serialize_placements = nullptr, g_gear_serialize = nullptr,
            g_container_serialize = nullptr, g_fire_serialize = nullptr;
Il2CppObject* serialize_inventory_detour(const MethodInfo* method) {
    if (engaged()) prune_placeables("before the save");
    return g_serialize_inventory(method);
}
Il2CppObject* serialize_placements_detour(const MethodInfo* method) {
    if (engaged()) prune_placeables("before the save");
    return g_serialize_placements(method);
}

// SI: KeepCloneGearOutOfGameSave / PutCloneGearBack / PruneDeadGearManagerEntries, and the same for containers (the
// port's improvement over SI's lost-and-found clean-up): the rooms' items and containers are taken out of the game's
// lists while it writes the region's save, and put back straight after.
// The taking out is undone by a guard's destructor, so a managed exception thrown through the game's Serialize (IL2CPP
// throws them as C++ exceptions, which run destructors on the way) can't leave the rooms' items out of the game's
// lists for good. The list is read again on the way back (the game may have replaced it meanwhile).
struct TakenOut {
    FieldInfo* field = nullptr;
    bool gear = false;
    std::vector<uintptr_t> items;
    TakenOut(FieldInfo* f, bool g) : field(f), gear(g) {}
    TakenOut(const TakenOut&) = delete;
    TakenOut& operator=(const TakenOut&) = delete;
    ~TakenOut() { put_back(); }
    void take_out() {
        Il2CppObject* list = static_object(field);
        if (!list) return;
        int dead = 0;
        for (int i = list_count(list) - 1; i >= 0; --i) {
            Il2CppObject* o = list_at(list, i);
            if (!alive(o)) {
                if (gear) {  // SI: PruneDeadGearManagerEntries
                    list_remove_at(list, i);
                    ++dead;
                }
                continue;
            }
            if (gear && api.gear_in_inventory >= 0 && get<bool>(o, api.gear_in_inventory)) continue;
            if (!is_clone_object(o)) continue;  // (an interior scene loading for a room counts too)
            items.push_back(hold(o));
            list_remove_at(list, i);
        }
        if (dead) logx::info("interiors: %d dead entr(ies) taken off GearManager's list before the save", dead);
    }
    bool back = false;
    void put_back() {
        if (back) return;
        back = true;
        Il2CppObject* list = static_object(field);
        int lost = 0;
        for (uintptr_t& h : items) {
            Il2CppObject* o = target(h);
            if (o && list)
                list_add(list, o);
            else if (o)
                ++lost;
            drop(h);
        }
        items.clear();
        if (lost) logx::warn("interiors: %d room object(s) couldn't go back on the game's list (the list is gone)", lost);
        if (gear && api.gear_index) {
            int32_t zero = 0;
            il2::il2cpp_field_static_set_value(api.gear_index, &zero);
        }
    }
};
Il2CppObject* gear_serialize_detour(const MethodInfo* method) {
    if (!engaged()) return g_gear_serialize(method);
    TakenOut t(api.gear_list, true);
    t.take_out();
    const size_t n = t.items.size();
    Il2CppObject* r = g_gear_serialize(method);
    t.put_back();
    if (n) logx::info("interiors: %zu room item(s) kept out of the game's save", n);
    return r;
}
Il2CppObject* container_serialize_detour(const MethodInfo* method) {
    if (!engaged()) return g_container_serialize(method);
    TakenOut t(api.container_list, false);
    t.take_out();
    const size_t n = t.items.size();
    Il2CppObject* r = g_container_serialize(method);
    t.put_back();
    if (n) logx::info("interiors: %zu room container(s) kept out of the game's save", n);
    return r;
}

// SI: PrepareFiresForSave: a fire that never woke has no HeatSource and would make the fires' save throw.
Il2CppObject* fire_serialize_detour(const MethodInfo* method) {
    if (engaged() && api.fire_heat >= 0) {
        Il2CppObject* list = static_object(api.fire_list);
        int woken = 0, dropped = 0;
        static Il2CppObject* heat_type = nullptr;
        if (!heat_type)
            if (Il2CppClass* hs = il2::find_class(kGame, "", "HeatSource")) heat_type = il2::type_object(hs);
        for (int i = list_count(list) - 1; i >= 0; --i) {
            Il2CppObject* f = list_at(list, i);
            if (alive(f) && !alive(get<Il2CppObject*>(f, api.fire_heat))) {
                Il2CppObject* hs = component(go_of(f), heat_type);
                if (hs) {
                    put_ref(f, api.fire_heat, hs);
                    ++woken;
                }
            }
            if (!alive(f) || !alive(get<Il2CppObject*>(f, api.fire_heat))) {
                list_remove_at(list, i);
                ++dropped;
            }
        }
        if (woken || dropped)
            logx::info("interiors: before the save: %d fire(s) that never woke prepared, %d invalid taken off the list", woken,
                       dropped);
    }
    return g_fire_serialize(method);
}

// SI: PreventGearManagerDuplicationPatch: GearManager.Deserialize begins by destroying all active gear; while an
// interior scene loads that would wipe its items. Kept and run once the load is over.
using GearDes1 = void (*)(Il2CppObject* text, const MethodInfo* method);
using GearDes2 = void (*)(Il2CppObject* a, Il2CppObject* b, const MethodInfo* method);
GearDes1 g_gear_des1 = nullptr;
GearDes2 g_gear_des2 = nullptr, g_gear_des3 = nullptr;
const MethodInfo* g_gear_des_mi[3] = {};
int g_gear_des_depth = 0;
bool deferring_gear() {
    return engaged() && !g_loading_base.empty() && g_gear_des_depth == 0 && active_region() == g_region;
}
void defer_gear(int overload, Il2CppObject* a, Il2CppObject* b) {
    g_deferred_gear.push_back({overload, hold(a), hold(b)});
    logx::warn("interiors: GearManager.Deserialize (%d) held back while %s loads; it runs once the load is over", overload,
               g_loading_base.c_str());
}
void freeze_restored_gear();  // seamless_persist.inc
// SI: FreezeRestoredGearPhysics, after the game's own restore of a supported region's items.
void after_gear_load() {
    if (g_gear_des_depth != 0 || !engaged()) return;
    const std::string region = active_region();
    for (int i = 0; i < kConfigCount; ++i)
        if (region == kConfigs[i].region) {
            freeze_restored_gear();
            return;
        }
}
void gear_des1_detour(Il2CppObject* t, const MethodInfo* method) {
    if (deferring_gear()) return defer_gear(1, t, nullptr);
    ++g_gear_des_depth;
    g_gear_des1(t, method);
    --g_gear_des_depth;
    after_gear_load();
}
void gear_des2_detour(Il2CppObject* a, Il2CppObject* b, const MethodInfo* method) {
    if (deferring_gear()) return defer_gear(2, a, b);
    ++g_gear_des_depth;
    g_gear_des2(a, b, method);
    --g_gear_des_depth;
    after_gear_load();
}
void gear_des3_detour(Il2CppObject* a, Il2CppObject* b, const MethodInfo* method) {
    if (deferring_gear()) return defer_gear(3, a, b);
    ++g_gear_des_depth;
    g_gear_des3(a, b, method);
    --g_gear_des_depth;
    after_gear_load();
}
void replay_deferred_gear() {
    if (g_deferred_gear.empty()) return;
    std::vector<DeferredGear> pending;
    pending.swap(g_deferred_gear);
    for (DeferredGear& d : pending) {
        Il2CppObject* a = target(d.a), *b = target(d.b);
        ++g_gear_des_depth;
        if (d.overload == 1 && g_gear_des1) g_gear_des1(a, g_gear_des_mi[0]);
        if (d.overload == 2 && g_gear_des2) g_gear_des2(a, b, g_gear_des_mi[1]);
        if (d.overload == 3 && g_gear_des3) g_gear_des3(a, b, g_gear_des_mi[2]);
        --g_gear_des_depth;
        drop(d.a);
        drop(d.b);
        logx::info("interiors: the held-back GearManager.Deserialize (%d) ran", d.overload);
    }
}
// SI drops them when the region changes; here they run (the region's own items lost would be worse than late).
void flush_deferred_gear() {
    if (g_deferred_gear.empty()) return;
    logx::warn("interiors: the region changed; %zu held-back GearManager.Deserialize call(s) run now", g_deferred_gear.size());
    g_loading_base.clear();
    replay_deferred_gear();
}

// SI: SafeSavePositionPatch: a save made in a parked room holds the door point outside, not a spot 80 m in the air.
// (The load starts at that door; our own player.inside record then puts the player back in the room, milestone 1b.)
Instance* parked_room_holding(V3 pos) {
    if (g_inside && in_room(g_inside) && in_parked_room(*g_inside, pos)) return g_inside;
    for (Instance& in : g_instances)
        if (in_room(&in) && in_parked_room(in, pos)) return &in;
    return nullptr;
}
bool nearest_region_exit(Instance& in, V3 near_pos, V3& safe) {
    Il2CppObject* best = nullptr;
    float best_d = 1e30f;
    each_in(master_of(in), api.t_load_scene, [&](Il2CppObject* door) {
        if (!leads_to_region(door_scene(door))) return;
        const float d = dist(position(door_tf(door)), near_pos);
        if (d < best_d) {
            best_d = d;
            best = door;
        }
    });
    if (!best) return false;
    V3 p = exit_point(in, best);
    if (is_zero(p) || parked_room_holding(p)) return false;
    p = snap_to_ground(p);
    if (p.y > in.master_pos.y - kParkOffset.y * 0.5f) return false;
    safe = p;
    return true;
}
// The door point outside a room (or its parent's, for a sub-interior) that the game's save holds instead of the room.
bool safe_door_point(Instance& room, V3 near_pos, V3& safe) {
    if (nearest_region_exit(room, near_pos, safe)) return true;
    for (Instance& parent : g_instances)
        if (&parent != &room && is_sub_linked(parent, room.cfg->id) && in_room(&parent) &&
            nearest_region_exit(parent, near_pos, safe))
            return true;
    return false;
}
using PmSerializeFn = Il2CppObject* (*)(Il2CppObject* self, const MethodInfo* method);
PmSerializeFn g_pm_serialize = nullptr;
Il2CppObject* pm_serialize_detour(Il2CppObject* self, const MethodInfo* method) {
    if (!engaged()) return g_pm_serialize(self, method);
    Il2CppObject* ptf = player_tf();
    const V3 here = position(ptf);
    Instance* room = ptf ? parked_room_holding(here) : nullptr;
    V3 safe;
    // The player goes back where they stood however the game's Serialize ends (a managed exception included).
    struct MovedBack {
        uintptr_t tf = 0;
        V3 at;
        ~MovedBack() {
            if (Il2CppObject* t = live(tf)) set_position(t, at);
            drop(tf);
        }
    } back;
    if (room) {
        const bool found = safe_door_point(*room, here, safe);
        if (found) {
            set_position(ptf, safe);
            back.tf = hold(ptf);
            back.at = here;
            logx::info("interiors: %s: the game's save holds the door point (%.1f, %.1f, %.1f), not the room", room->cfg->id,
                       safe.x, safe.y, safe.z);
        } else {
            logx::warn("interiors: %s: no door point found; the save keeps the room's position", room->cfg->id);
        }
    }
    return g_pm_serialize(self, method);
}

// Milestone 1b: the rooms' own save files, the player's place, closed-room time, adoption (SI: SaveData.cs,
// Hydration.cs, FrozenTime.cs, ...).
#include "seamless_persist.inc"
#include "seamless_import.inc"
#include "seamless_coop.inc"

// --- hooks: inside (SI: WeatherPatches.cs, WindInstanceFix.cs, EvolvePatches.cs, SafehouseFixes.cs) -------------------

// Wind.PlayerShelteredFromWind is folded with two other methods (can't be hooked): it reads m_PlayerOccluded, which
// Wind.UpdateWindOcclusion sets. So: after that, in a room, the player is occluded.
VoidFn g_update_wind_occlusion = nullptr;
void update_wind_occlusion_detour(Il2CppObject* self, const MethodInfo* method) {
    g_update_wind_occlusion(self, method);
    if (engaged() && g_inside && self) put<bool>(self, api.wind_occluded, true);
}
struct Vec3Arg {
    float x, y, z;
};
using OccludedFn = bool (*)(Il2CppObject* self, Vec3Arg* pos, const MethodInfo* method);
OccludedFn g_position_occluded = nullptr;
bool position_occluded_detour(Il2CppObject* self, Vec3Arg* pos, const MethodInfo* method) {
    if (engaged() && g_inside) return true;
    return g_position_occluded(self, pos, method);
}
VoidFn g_calc_temperature = nullptr;
void calc_temperature_detour(Il2CppObject* self, const MethodInfo* method) {
    g_calc_temperature(self, method);
    if (engaged() && g_inside && self) put<float>(self, api.weather_chill, 0.0f);
}
using BoolFn = bool (*)(Il2CppObject* self, const MethodInfo* method);
BoolFn g_indoor_environment = nullptr;
bool indoor_environment_detour(Il2CppObject* self, const MethodInfo* method) {
    if (engaged() && g_inside) return true;
    return g_indoor_environment(self, method);
}
// SI: WindStartFixPatch: the wind's audio is never left force-stopped (the delayed half is g_wind_restore_at).
VoidFn g_wind_start = nullptr;
void wind_start_detour(Il2CppObject* self, const MethodInfo* method) {
    if (engaged() && self && api.wind_force_stopped >= 0) put<bool>(self, api.wind_force_stopped, false);
    g_wind_start(self, method);
    if (engaged() && g_inside) g_wind_restore_at = now_ms() + 1000;
}
// SI: EvolveItemIndoorPatch: curing works in a room.
BoolFn g_evolve_indoor = nullptr;
bool evolve_indoor_detour(Il2CppObject* self, const MethodInfo* method) {
    const bool r = g_evolve_indoor(self, method);
    if (r || !engaged() || !self) return r;
    const V3 p = position(tf_of(self));
    for (Instance& in : g_instances)
        if (in_room(&in) && in_parked_room(in, p, 0)) return true;
    return false;
}
// SI: IsPositionInsideAnyInstance (the player's spot, in an open room)
bool player_in_open_room() {
    if (!engaged() || !g_inside) return false;
    const V3 p = player_pos();
    for (Instance& in : g_instances)
        if (in_room(&in) && active_self(master_of(in)) && distance_from(in, p) <= 80 && in_parked_room(in, p)) return true;
    return false;
}
BoolFn g_in_customizable = nullptr;
bool in_customizable_detour(Il2CppObject* self, const MethodInfo* method) {
    const bool r = g_in_customizable(self, method);
    return r || (engaged() && g_inside != nullptr);
}
using DecorationFn = bool (*)(Il2CppObject* item, const MethodInfo* method);
DecorationFn g_decoration_inside = nullptr;
bool decoration_inside_detour(Il2CppObject* item, const MethodInfo* method) {
    const bool r = g_decoration_inside(item, method);
    return r || player_in_open_room();
}
using OverlapFn = Il2CppObject* (*)(Il2CppObject* self, void* hit, const MethodInfo* method);
OverlapFn g_place_overlap = nullptr;
Il2CppObject* place_overlap_detour(Il2CppObject* self, void* hit, const MethodInfo* method) {
    if (player_in_open_room()) return nullptr;
    return g_place_overlap(self, hit, method);
}
using PenetrationFn = bool (*)(Il2CppObject* self, Il2CppObject* candidate, const MethodInfo* method);
PenetrationFn g_penetration = nullptr;
bool penetration_detour(Il2CppObject* self, Il2CppObject* candidate, const MethodInfo* method) {
    if (player_in_open_room()) return false;
    return g_penetration(self, candidate, method);
}
using OutOfBoundsFn = bool (*)(Il2CppObject* self, void* hit, const MethodInfo* method);
OutOfBoundsFn g_out_of_bounds = nullptr;
bool out_of_bounds_detour(Il2CppObject* self, void* hit, const MethodInfo* method) {
    if (player_in_open_room()) return false;
    return g_out_of_bounds(self, hit, method);
}
// SI: ArrivalFallDamagePatch (there for the mine); the port: no fall damage for 2 s after any of our teleports.
using FallFn = void (*)(Il2CppObject* self, float height, float damage_override, const MethodInfo* method);
FallFn g_fall_damage = nullptr;
void fall_damage_detour(Il2CppObject* self, float height, float damage_override, const MethodInfo* method) {
    if (engaged() && now_ms() < g_fall_guard_until) {
        logx::info("interiors: fall damage after our teleport ignored (%.1f m)", height);
        return;
    }
    g_fall_damage(self, height, damage_override, method);
}

// --- the per-frame watchers (SI: PlayerLocation.cs, InsideFlag.cs, Visibility.cs) --------------------------------------

// SI: TickDoorlessExitDetection: moved out of the room some other way (a console teleport, a mod): closed after 3
// checks a quarter second apart, the player 3 m outside its volume.
void tick_doorless_exit() {
    if (!g_inside) {
        g_doorless_confirm = 0;
        return;
    }
    const double now = now_ms();
    if (now < g_next_doorless) return;
    g_next_doorless = now + 250;
    if (now - g_last_portal <= kPortalSuppressMs || g_building || g_fade.busy) {
        g_doorless_confirm = 0;
        return;
    }
    Instance& in = *g_inside;
    if (!in_room(&in) || !active_in_tree(master_of(in))) {
        g_doorless_confirm = 0;
        return;
    }
    const V3 p = player_pos();
    if (in_volume(in, p, 3.0f) || in_parked_room(in, p)) {
        g_doorless_confirm = 0;
        return;
    }
    if (++g_doorless_confirm < 3) return;
    g_doorless_confirm = 0;
    logx::info("interiors: %s: the player left the room without a door (at %.1f, %.1f, %.1f); closing it", in.cfg->id, p.x,
               p.y, p.z);
    g_last_portal = now;
    exit_room(in, "left without a door", true);
}

// SI: TickPlayerInsideCloneFlagGuard: the inside flag with no room open for 3 s is cleared (unless the player
// stands in a parked room's box: then the room closed round them, the real fault, and the flag is left).
void tick_flag_guard() {
    if (!g_inside) {
        g_flag_confirm = 0;
        return;
    }
    const double now = now_ms();
    if (now < g_next_flag) return;
    g_next_flag = now + 500;
    if (now - g_last_portal <= kPortalSuppressMs || g_fade.busy) {
        g_flag_confirm = 0;
        return;
    }
    for (Instance& in : g_instances)
        if (active_in_tree(master_of(in))) {
            g_flag_confirm = 0;
            return;
        }
    const V3 p = player_pos();
    for (Instance& in : g_instances)
        if (in_room(&in) && in_parked_room(in, p, 2.0f)) {
            static bool warned = false;
            if (!warned) logx::warn("interiors: %s: no room is open but the player stands in its box; the flag is left", in.cfg->id);
            warned = true;
            g_flag_confirm = 0;
            return;
        }
    if (++g_flag_confirm < 6) return;
    g_flag_confirm = 0;
    set_audio_occlusion(false);
    mark_outside("no room open (stuck flag)");
}

// SI: the watchdog's force-close (FORCE_HIDE_CONFIRMATIONS): a room open that the player isn't in is closed.
void tick_watchdog() {
    const double now = now_ms();
    if (now < g_next_watch) return;
    g_next_watch = now + 500;
    if (now - g_last_portal <= kPortalSuppressMs || g_fade.busy || g_building) {
        g_watch_confirm = 0;
        return;
    }
    Instance* stray = nullptr;
    for (Instance& in : g_instances)
        if (in_room(&in) && &in != g_inside && active_self(master_of(in))) stray = &in;
    if (!stray) {
        g_watch_confirm = 0;
        return;
    }
    if (++g_watch_confirm < 6) return;
    g_watch_confirm = 0;
    logx::info("interiors: %s: open with nobody in it; closed", stray->cfg->id);
    exit_room(*stray, "watchdog", false);
}

void tick_sound() {
    const double now = now_ms();
    if (g_close_sound_at > 0 && now >= g_close_sound_at) {
        g_close_sound_at = 0;
        door_sounds(true);
    }
    if (g_wind_restore_at > 0 && now >= g_wind_restore_at) {  // SI: RestoreWindAudioAfterLoad / WindStartFixPatch
        g_wind_restore_at = 0;
        Il2CppObject* w = wind();
        if (alive(w) && api.wind_loop >= 0 && get<uint32_t>(w, api.wind_loop) == 0) restart_wind("after a room was built");
        if (g_inside) keep_wind_loop();
    }
    if (now < g_next_sound) return;
    g_next_sound = now + 1000;
    if (!audio_manager()) return;
    bool any_room = false;
    for (Instance& in : g_instances) any_room = any_room || in_room(&in);
    if (!any_room) return;
    keep_region_sound_states();
    wind_loop_follows(g_inside != nullptr);
}

// --- the region came or went -------------------------------------------------------------------------------------------

void forget_region(const char* why) {
    if (g_inside) {
        g_inside = nullptr;
        g_entered_space_of = nullptr;
        drop(g_entered_space);
    }
    reset_occlusion_counters(why);
    flush_deferred_gear();
    if (g_fade.busy) fade(false, 0);
    drop(g_fade.door);
    g_fade = {};
    g_fall_guard_until = 0;
    if (g_hold.active) release_hold("the region changed");
    g_record_checked = false;
    g_rolled_read = false;
    g_mod_guids.clear();
    g_stable_checked.clear();
    g_carried_wrong_prev.clear();
    g_urgent = nullptr;
    for (Instance& in : g_instances) {
        for (auto* list : {&in.closed_fires, &in.closed_pots, &in.closed_lamps, &in.closed_torches, &in.closed_flares,
                           &in.closed_evolve})
            for (uintptr_t& h : *list) drop(h);
        for (Instance::ContainerJob& j : in.container_jobs) drop(j.container);
        for (auto* list : {&in.deferred_twins, &in.deferred_taken})
            for (Instance::RegionItem& r : *list) drop(r.gi);
        drop(in.master);
        drop(in.master_tf);
        drop(in.shell);
        drop(in.trigger);
        drop(in.space);
        drop(in.cached_lightmaps);
        drop(in.cached_probes);
        for (uintptr_t& h : in.keep_off) drop(h);
        for (uintptr_t& h : in.outsiders) drop(h);
        for (uintptr_t& h : in.colliders_off) drop(h);
        orphan_ops(in, "the region changed");
        for (Instance::Op& op : in.ops) drop(op.op);
    }
    const size_t had = g_instances.size();
    g_instances.clear();
    rooms_changed();
    g_building = nullptr;
    g_loading_base.clear();
    g_template_scenes.clear();
    for (auto& [k, h] : g_pristine) drop(h);
    g_pristine.clear();
    rooms_changed();
    g_scene_keys.clear();
    g_scene_keys_read = g_scene_keys_failed = false;
    g_overlay.clear();  // an import not finished in this region (nothing of it was written)
    g_region_scene_state = 0;
    g_region_state_searches = 0;
    g_state_corrections = 0;
    g_paused_outdoor_wind = 0;
    g_snow_moved = g_snow_planes_hidden = false;
    drop(g_snow_planes);
    g_snow_planes_next = 0;
    for (auto& [id, h] : g_stripped_materials) {  // the rooms' material copies (SI: s_StrippedMaterialCache)
        if (Il2CppObject* mat = live(h)) destroy(mat);
        drop(h);
    }
    g_stripped_materials.clear();
    drop(g_fog_layer);
    g_capsule_applied = false;
    if (had) logx::info("interiors: %s left (%s); its %zu building(s) forgotten", g_region.c_str(), why, had);
    g_region.clear();
}

void enter_region(const std::string& region) {
    g_region = region;
    if (now_ms() - g_region_fires_at > 30000) g_region_fires.clear();  // the fire records kept belong to an earlier load
    for (int i = 0; i < kConfigCount; ++i) {
        if (region != kConfigs[i].region) continue;
        Instance in;
        in.cfg = &kConfigs[i];
        g_instances.push_back(in);
    }
    rooms_changed();
    if (!g_instances.empty())
        logx::info("interiors: %s: %zu building(s) with seamless doors; rooms are built as the player comes within %.0f m",
                   region.c_str(), g_instances.size(), kBuildDistance);
}

bool resolve();
void resolve_layers();

}  // namespace

// --- public ------------------------------------------------------------------------------------------------------------

bool loading_template() { return api.ok && !g_loading_base.empty(); }

bool player_inside() { return api.ok && g_inside != nullptr; }

bool is_clone_object(Il2CppObject* obj) {
    if (!api.ok || no_room_objects() || !alive(obj)) return false;
    Il2CppClass* k = il2::il2cpp_object_get_class(obj);
    const bool is_go = k == api.go_class;
    Il2CppObject* t = is_go ? go_tf(obj) : tf_of(obj);
    if (!t) return false;
    if (root_is_room(t)) return true;
    if (!g_loading_base.empty() || !g_orphan_loads.empty()) {  // SI: IsInteriorLoadedAsCloneTemplate (by the scene's name:
        const int32_t sc = scene_of(is_go ? obj : go_of(obj));  // its handle may not be seen yet), or a late load's scene
        const std::string sn = scene_name(sc);
        if (!g_loading_base.empty() && (g_template_scenes.count(sc) || interior_base(sn) == g_loading_base)) return true;
        if (orphan_scene(sn)) return true;
    }
    return false;
}

bool skip_game_manager_awake(Il2CppObject* gm) {
    if (!api.ok || !alive(gm) || (!engaged() && g_orphan_loads.empty())) return false;
    Il2CppObject* go = go_of(gm);
    if (under_any_room(gm)) {
        destroy(gm);
        logx::info("interiors: a room's GameManager destroyed before it woke");
        return true;
    }
    if (!g_loading_base.empty() && interior_base(scene_name(scene_of(go))) == g_loading_base) {
        destroy(go);
        logx::info("interiors: the GameManager of %s (loading for a room) destroyed before it woke", g_loading_base.c_str());
        return true;
    }
    if (const std::string sn = g_orphan_loads.empty() ? std::string() : scene_name(scene_of(go)); orphan_scene(sn)) {
        destroy(go);
        logx::info("interiors: the GameManager of %s (a late load for a room given up on) destroyed before it woke", sn.c_str());
        return true;
    }
    return false;
}

void set_settings(const config::Settings& s) {
    coop_features::set_own(net::kFeatureSeamlessInteriors, s.seamless_interiors);
    g_import_old = s.seamless_import_old;
    logx::info("interiors: Seamless Interiors %s in coop.ini (old saves' interiors %s)", s.seamless_interiors ? "on" : "off",
               g_import_old ? "imported" : "left to the game's doors");
}

void on_frame(const std::string& scene, bool in_game) {
    if (!api.ok) return;
    poll_orphans();  // (rooms or not: a late load given up on is unloaded once it's in)
    rooms_changed();  // (the roots looked up again this frame)
    if (in_game) resolve_layers();
    g_feature_on = feature_active();
    const std::string region = in_game ? active_region() : std::string();
    {  // the same region loaded again (a respawn, a save of it loaded): its rooms went with the old one
        static float last_clock = -1;
        const float clock = game::seconds_since_level_load();
        bool reloaded = !g_region.empty() && region == g_region && last_clock >= 0 && clock >= 0 && clock < last_clock - 0.5f;
        last_clock = clock;
        for (const Instance& in : g_instances)
            reloaded = reloaded || (in.stage == Stage::Ready && !master_of(in));
        if (reloaded) forget_region("loaded again");
    }
    if (region != g_region) {
        if (!g_region.empty()) forget_region(region.empty() ? "loading or menu" : "another scene");
        if (!region.empty() && g_feature_on) enter_region(region);
    }
    bool any_built = false;
    for (const Instance& in : g_instances) any_built = any_built || in.stage == Stage::Ready || in.stage == Stage::Loading ||
                                                        in.stage == Stage::PreRoll || in.stage == Stage::Waking ||
                                                        in.stage == Stage::Hydrating;
    const bool engaged_now = g_feature_on || any_built;
    if (engaged_now != g_engaged || !g_logged_state) {
        g_logged_state = true;
        logx::info("interiors: %s", engaged_now ? (g_feature_on ? "on" : "off, but this region's rooms keep working until it changes")
                                                : "off (every door is the game's own)");
    }
    g_engaged = engaged_now;
    if (!g_engaged || g_instances.empty()) return;
    (void)scene;
    tick_coop_rooms();  // milestone 2: which rooms each game has
    tick_build();
    check_inside_record();  // milestone 1b: back into the room the save was made in
    tick_hold();
    pick_building();
    tick_fade();
    tick_closed_time();
    tick_carried_guard();
    tick_doorless_exit();
    sync_indoor_space();
    tick_capsule();
    tick_flag_guard();
    tick_watchdog();
    tick_sound();
    tick_fog();
}

void on_late_frame() {
    if (!api.ok || !g_engaged) return;
    tick_snow_emitters();
    tick_snow_planes();
}

namespace {

template <typename Fn>
bool hook(const char* label, const MethodInfo* m, Fn detour, Fn* original) {
    if (!m) {
        logx::warn("interiors: %s not found; that part of Seamless Interiors is off", label);
        return false;
    }
    return hooks::install_method(label, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

// TLDCoop_DLC: Unity's layer names can only be asked once the engine runs (LayerMask.NameToLayer during the mod's
// start-up crashed the game: 0xC0000005 in UnityPlayer.dll, and the whole mod was switched off).
void resolve_layers() {
    static bool done = false;
    if (done || !api.name_to_layer) return;
    done = true;
    auto layer = [](const char* n) {
        int32_t l = -1;
        void* a[] = {str(n)};
        il2::invoke_value(api.name_to_layer, nullptr, l, a);
        return l;
    };
    api.layer_collide_only = layer("CharacterControllerCollideOnly");
    api.layer_trigger = layer("TriggerIgnoreRaycast");
    api.layer_npc = layer("NPC");
    logx::info("interiors: layers %d / %d / %d", api.layer_collide_only, api.layer_trigger, api.layer_npc);
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* comp = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* behaviour = il2::find_class(kCore, "UnityEngine", "Behaviour");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* light = il2::find_class(kCore, "UnityEngine", "Light");
    Il2CppClass* mesh_filter = il2::find_class(kCore, "UnityEngine", "MeshFilter");
    Il2CppClass* mesh = il2::find_class(kCore, "UnityEngine", "Mesh");
    Il2CppClass* lightmap_settings = il2::find_class(kCore, "UnityEngine", "LightmapSettings");
    Il2CppClass* layer_mask = il2::find_class(kCore, "UnityEngine", "LayerMask");
    Il2CppClass* random = il2::find_class(kCore, "UnityEngine", "Random");
    Il2CppClass* scene = il2::find_class(kCore, "UnityEngine.SceneManagement", "Scene");
    Il2CppClass* scene_manager = il2::find_class(kCore, "UnityEngine.SceneManagement", "SceneManager");
    Il2CppClass* physics = il2::find_class(kPhysics, "UnityEngine", "Physics");
    Il2CppClass* collider = il2::find_class(kPhysics, "UnityEngine", "Collider");
    Il2CppClass* box = il2::find_class(kPhysics, "UnityEngine", "BoxCollider");
    Il2CppClass* cc = il2::find_class(kPhysics, "UnityEngine", "CharacterController");
    Il2CppClass* obstacle = il2::find_class(kAI, "UnityEngine.AI", "NavMeshObstacle");
    Il2CppClass* addressables = il2::find_class(kAddressables, "UnityEngine.AddressableAssets", "Addressables");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* load_scene = il2::find_class(kGame, "", "LoadScene");
    if (!object || !go || !comp || !tf || !behaviour || !renderer || !scene || !scene_manager || !physics || !collider ||
        !addressables || !gm || !pm || !load_scene) {
        logx::warn("interiors: some of the game's core classes are missing; Seamless Interiors is unavailable");
        return false;
    }
    auto type = [](Il2CppClass* k) { return k ? il2::type_object(k) : nullptr; };
    auto game_type = [&](const char* ns, const char* name) { return type(il2::find_class(kGame, ns, name)); };
    api.go_class = go;
    api.t_transform = type(tf);
    api.t_renderer = type(renderer);
    api.t_mesh_renderer = type(il2::find_class(kCore, "UnityEngine", "MeshRenderer"));
    api.t_light = type(light);
    api.t_camera = type(il2::find_class(kCore, "UnityEngine", "Camera"));
    api.t_listener = type(il2::find_class("UnityEngine.AudioModule.dll", "UnityEngine", "AudioListener"));
    api.t_probe = type(il2::find_class(kCore, "UnityEngine", "ReflectionProbe"));
    api.t_box = type(box);
    api.t_collider = type(collider);
    api.t_obstacle = type(obstacle);
    api.t_mesh_filter = type(mesh_filter);
    api.t_particles = type(il2::find_class(kParticles, "UnityEngine", "ParticleSystem"));
    api.t_char_controller = type(cc);
    api.t_gear = game_type("", "GearItem");
    api.t_placeable = game_type("TLD.Placement", "Placeable");
    api.t_container = game_type("", "Container");
    api.t_rso = game_type("", "RandomSpawnObject");
    api.t_load_scene = type(load_scene);
    api.t_indoor = game_type("", "IndoorSpaceTrigger");
    api.t_object_guid = game_type("", "ObjectGuid");
    api.t_grabber = game_type("", "RenderTextureGrabber");
    api.t_ilm = game_type("", "InteriorLightingManager");
    api.t_dlm = game_type("", "DarkLightingManager");
    api.t_gimble = game_type("", "LightShaftGimble");
    api.t_fire = game_type("", "Fire");
    api.t_stove = game_type("", "WoodStove");
    api.t_campfire = game_type("", "Campfire");
    api.t_audio_zone = game_type("", "AudioStateZoneTrigger");
    api.t_wind = game_type("", "Wind");
    api.t_snow_planes = game_type("", "SnowPlaneManager");
    api.t_post_layer = game_type("UnityEngine.Rendering.PostProcessing", "PostProcessLayer");
    api.t_ak_state = type(il2::find_class(kWwiseBehaviours, "", "AkState"));

    api.name = il2::find_method(object, "get_name", 0);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.destroy_now = il2::find_method_params(object, "DestroyImmediate", {"UnityEngine.Object"});
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.find_objects = il2::find_method_params(object, "FindObjectsOfType", {"System.Type"});
    api.find_object = il2::find_method_params(object, "FindObjectOfType", {"System.Type"});
    api.instance_id = il2::find_method(object, "GetInstanceID", 0);
    api.go_ctor = il2::find_method_params(go, ".ctor", {"System.String"});
    api.go_set_active = il2::find_method(go, "SetActive", 1);
    api.go_active_self = il2::find_method(go, "get_activeSelf", 0);
    api.go_active_tree = il2::find_method(go, "get_activeInHierarchy", 0);
    api.go_transform = il2::find_method(go, "get_transform", 0);
    api.go_add = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_get = il2::find_method_params(go, "GetComponent", {"System.Type"});
    api.go_children = il2::find_method_params(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    api.go_set_layer = il2::find_method(go, "set_layer", 1);
    api.go_layer = il2::find_method(go, "get_layer", 0);
    api.go_set_static = il2::find_method(go, "set_isStatic", 1);
    api.go_static = il2::find_method(go, "get_isStatic", 0);
    api.go_scene = il2::find_method(go, "get_scene", 0);
    api.comp_go = il2::find_method(comp, "get_gameObject", 0);
    api.comp_transform = il2::find_method(comp, "get_transform", 0);
    api.comp_in_parent = il2::find_method_params(comp, "GetComponentInParent", {"System.Type", "System.Boolean"});
    api.comp_in_children = il2::find_method_params(comp, "GetComponentInChildren", {"System.Type", "System.Boolean"});
    api.tf_position = il2::find_method(tf, "get_position", 0);
    api.tf_set_position = il2::find_method(tf, "set_position", 1);
    api.tf_rotation = il2::find_method(tf, "get_rotation", 0);
    api.tf_set_rotation = il2::find_method(tf, "set_rotation", 1);
    api.tf_set_local_position = il2::find_method(tf, "set_localPosition", 1);
    api.tf_set_local_scale = il2::find_method(tf, "set_localScale", 1);
    api.tf_local_scale = il2::find_method(tf, "get_localScale", 0);
    api.tf_parent = il2::find_method(tf, "get_parent", 0);
    api.tf_set_parent = il2::find_method_params(tf, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    api.tf_is_child_of = il2::find_method(tf, "IsChildOf", 1);
    api.tf_find = il2::find_method(tf, "Find", 1);
    api.tf_child_count = il2::find_method(tf, "get_childCount", 0);
    api.tf_child = il2::find_method(tf, "GetChild", 1);
    api.tf_to_world = il2::find_method(tf, "get_localToWorldMatrix", 0);
    api.tf_root = il2::find_method(tf, "get_root", 0);
    api.behaviour_enabled = il2::find_method(behaviour, "get_enabled", 0);
    api.behaviour_set_enabled = il2::find_method(behaviour, "set_enabled", 1);
    api.r_enabled = il2::find_method(renderer, "get_enabled", 0);
    api.r_set_enabled = il2::find_method(renderer, "set_enabled", 1);
    api.r_bounds = il2::find_method(renderer, "get_bounds", 0);
    api.r_lightmap = il2::find_method(renderer, "get_lightmapIndex", 0);
    api.r_set_lightmap = il2::find_method(renderer, "set_lightmapIndex", 1);
    api.r_materials = il2::find_method(renderer, "get_sharedMaterials", 0);
    api.r_set_materials = il2::find_method(renderer, "set_sharedMaterials", 1);
    api.r_material = il2::find_method(renderer, "get_sharedMaterial", 0);
    if (material) {
        api.mat_keyword_on = il2::find_method_params(material, "IsKeywordEnabled", {"System.String"});
        api.mat_disable_keyword = il2::find_method_params(material, "DisableKeyword", {"System.String"});
    }
    if (light) {
        api.light_type = il2::find_method(light, "get_type", 0);
        api.light_range = il2::find_method(light, "get_range", 0);
        api.light_set_range = il2::find_method(light, "set_range", 1);
        api.light_set_mask = il2::find_method(light, "set_cullingMask", 1);
        api.light_set_shadows = il2::find_method(light, "set_shadows", 1);
        api.light_set_intensity = il2::find_method(light, "set_intensity", 1);
    }
    if (mesh_filter) api.mesh_filter_mesh = il2::find_method(mesh_filter, "get_sharedMesh", 0);
    if (mesh) api.mesh_bounds = il2::find_method(mesh, "get_bounds", 0);
    if (lightmap_settings) {
        api.lightmaps = il2::find_method(lightmap_settings, "get_lightmaps", 0);
        api.set_lightmaps = il2::find_method(lightmap_settings, "set_lightmaps", 1);
        api.probes = il2::find_method(lightmap_settings, "get_lightProbes", 0);
        api.set_probes = il2::find_method(lightmap_settings, "set_lightProbes", 1);
    }
    if (Il2CppClass* gi = il2::find_class(kCore, "UnityEngine", "DynamicGI"))
        api.dynamic_gi_update = il2::find_method_quiet(gi, "UpdateEnvironment", 0);
    if (!api.dynamic_gi_update) logx::info("interiors: DynamicGI.UpdateEnvironment isn't in this build (skipped)");
    if (layer_mask) api.name_to_layer = il2::find_method(layer_mask, "NameToLayer", 1);
    // (the layers' numbers are asked for in game: calling into Unity at start-up crashed it, see resolve_layers)
    if (random) {
        api.rnd_state = il2::find_method(random, "get_state", 0);
        api.rnd_set_state = il2::find_method(random, "set_state", 1);
        api.rnd_init = il2::find_method(random, "InitState", 1);
        api.rnd_range = il2::find_method_params(random, "Range", {"System.Int32", "System.Int32"});
    }
    api.scene_by_name = il2::find_method_params(scene_manager, "GetSceneByName", {"System.String"});
    api.scene_is_loaded = il2::find_method(scene, "GetIsLoadedInternal", 1);
    api.scene_name = il2::find_method(scene, "GetNameInternal", 1);
    api.scene_roots = il2::find_method(scene, "GetRootGameObjects", 0);
    api.scene_roots_internal = il2::find_method_quiet(scene, "GetRootGameObjectsInternal", 2);
    api.scene_root_count = il2::find_method_quiet(scene, "GetRootCountInternal", 1);
    if (const MethodInfo* with_list = il2::find_method_params_quiet(scene, "GetRootGameObjects",
                                                                    {"System.Collections.Generic.List<UnityEngine.GameObject>"});
        with_list && il2::il2cpp_class_from_type)
        if (const Il2CppType* t = il2::il2cpp_method_get_param(with_list, 0))
            if ((api.go_list_class = il2::il2cpp_class_from_type(t)))
                api.go_list_ctor = il2::find_method_quiet(api.go_list_class, ".ctor", 0);
    logx::info("interiors: scene roots read %s", api.scene_roots_internal && api.go_list_ctor
                                                     ? "with Scene.GetRootGameObjectsInternal"
                                                     : "with Scene.GetRootGameObjects() (the static call isn't there)");
    api.move_to_scene = il2::find_method(scene_manager, "MoveGameObjectToScene", 2);
    api.set_active_scene = il2::find_method(scene_manager, "SetActiveScene", 1);
    api.unload_scene = il2::find_method_params(scene_manager, "UnloadSceneAsync", {"System.String"});
    if (api.scene_roots) {
        // A value type's instance method: its compiled code takes the struct's address, unless the runtime gives us
        // an adjustor thunk (add rcx, 0x10; jmp ...), which wants the boxed object.
        const unsigned char* code = static_cast<const unsigned char*>(il2::method_pointer(api.scene_roots));
        api.scene_roots_boxed = code && code[0] == 0x48 && code[1] == 0x83 && code[2] == 0xC1 && code[3] == 0x10;
    }
    api.load_scene = il2::find_method_params(addressables, "LoadSceneAsync",
                                             {"System.Object", "UnityEngine.SceneManagement.LoadSceneMode", "System.Boolean",
                                              "System.Int32",
                                              "UnityEngine.ResourceManagement.ResourceProviders.SceneReleaseMode"});
    api.raycast_all = il2::find_method_params(physics, "RaycastAll",
                                              {"UnityEngine.Vector3", "UnityEngine.Vector3", "System.Single", "System.Int32",
                                               "UnityEngine.QueryTriggerInteraction"});
    api.overlap_sphere = il2::find_method_params(physics, "OverlapSphere",
                                                 {"UnityEngine.Vector3", "System.Single", "System.Int32",
                                                  "UnityEngine.QueryTriggerInteraction"});
    api.collider_by_id = il2::find_method(physics, "GetColliderByInstanceID", 1);
    api.col_trigger = il2::find_method(collider, "get_isTrigger", 0);
    api.col_set_trigger = il2::find_method(collider, "set_isTrigger", 1);
    api.col_enabled = il2::find_method(collider, "get_enabled", 0);
    api.col_set_enabled = il2::find_method(collider, "set_enabled", 1);
    if (box) {
        api.box_set_center = il2::find_method(box, "set_center", 1);
        api.box_set_size = il2::find_method(box, "set_size", 1);
    }
    if (obstacle) {
        api.obstacle_shape = il2::find_method(obstacle, "set_shape", 1);
        api.obstacle_center = il2::find_method(obstacle, "set_center", 1);
        api.obstacle_size = il2::find_method(obstacle, "set_size", 1);
        api.obstacle_carving = il2::find_method(obstacle, "set_carving", 1);
    }
    if (cc) {
        api.cc_radius = il2::find_method(cc, "get_radius", 0);
        api.cc_set_radius = il2::find_method(cc, "set_radius", 1);
        api.cc_slope = il2::find_method(cc, "get_slopeLimit", 0);
        api.cc_set_slope = il2::find_method(cc, "set_slopeLimit", 1);
    }
    // TLD
    api.gm_player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    api.gm_player_transform = il2::find_method(gm, "GetPlayerTransform", 0);
    api.gm_player_object = il2::find_method(gm, "GetPlayerObject", 0);
    api.gm_main_camera = il2::find_method(gm, "GetMainCamera", 0);
    api.gm_weather = il2::find_method(gm, "GetWeatherComponent", 0);
    api.gm_wind = il2::find_method(gm, "GetWindComponent", 0);
    api.gm_global = il2::find_method(gm, "GetGlobalParameters", 0);
    api.gm_movement = il2::find_method(gm, "GetPlayerMovementComponent", 0);
    api.gm_fps_camera = il2::find_method(gm, "GetVpFPSCamera", 0);
    api.gm_active_scene = il2::il2cpp_class_get_field_from_name(gm, "m_ActiveScene");
    api.teleport = il2::find_method(pm, "TeleportPlayer", 2);
    api.reset_interaction = il2::find_method(pm, "ResetInteraction", 0);
    api.pm_indoor = il2::field_offset(pm, "m_IndoorSpaceTrigger");
    api.pm_indoors = il2::field_offset(pm, "m_IndoorSpaceTriggers");
    api.door_scene = il2::field_offset(load_scene, "m_SceneToLoad");
    api.door_exit_point = il2::field_offset(load_scene, "m_ExitPointName");
    api.door_touch_ground = il2::field_offset(load_scene, "m_ActivateWhenPlayerTouchGround");
    api.door_guid = il2::field_offset(load_scene, "m_GUID");
    api.door_mark_explored = il2::find_method(load_scene, "MarkExplored", 0);
    if (Il2CppClass* fade = il2::find_class(kGame, "", "CameraFade")) {
        api.fade_out = il2::find_method(fade, "FadeOut", 3);
        api.fade_in = il2::find_method(fade, "FadeIn", 3);
    }
    if (Il2CppClass* gam = il2::find_class(kGame, "", "GameAudioManager")) {
        api.play_sound = il2::find_method_params(gam, "PlaySound", {"System.String", "UnityEngine.GameObject"});
        api.gui_click = il2::find_method(gam, "PlayGUIButtonClick", 0);
        api.audio_instance = il2::find_method(gam, "get_Instance", 0);
        api.enter_occlusion = il2::find_method(gam, "EnterOcclusionTrigger", 1);
        api.exit_occlusion = il2::find_method(gam, "ExitOcclusionTrigger", 1);
        api.update_occlusion = il2::find_method(gam, "UpdateAudioOcclusion", 0);
        api.gam_heavy = il2::field_offset(gam, "m_InsideHeavyOcclusionTriggerCount");
        api.gam_medium = il2::field_offset(gam, "m_InsideMediumOcclusionTriggerCount");
        api.gam_mild = il2::field_offset(gam, "m_InsideMildOcclusionTriggerCount");
    }
    if (Il2CppClass* zone = il2::find_class(kGame, "", "AudioStateZoneTrigger")) {
        api.zone_occluded = il2::field_offset(zone, "m_Occluded");
        api.zone_level = il2::field_offset(zone, "m_OcclusionLevel");
    }
    if (Il2CppClass* ak = il2::find_class(kWwise, "", "AkSoundEngine")) {
        api.ak_get_state = il2::find_method_params(ak, "GetState", {"System.UInt32", "System.UInt32&"});
        api.ak_set_state = il2::find_method_params(ak, "SetState", {"System.UInt32", "System.UInt32"});
        api.ak_event_of = il2::find_method(ak, "GetEventIDFromPlayingID", 1);
        api.ak_action = il2::find_method_params(ak, "ExecuteActionOnPlayingID",
                                                {"AkActionOnEventType", "System.UInt32", "System.Int32", "AkCurveInterpolation"});
        api.ak_stop = il2::find_method_params(ak, "StopPlayingID", {"System.UInt32", "System.Int32", "AkCurveInterpolation"});
    }
    {
        Il2CppClass* states = il2::find_class(kGame, "AK", "STATES");
        Il2CppClass* sc = states ? il2::find_nested_class(states, "SCENE") : nullptr;
        Il2CppClass* sc_state = sc ? il2::find_nested_class(sc, "STATE") : nullptr;
        Il2CppClass* ins = states ? il2::find_nested_class(states, "INTERIORSTATES") : nullptr;
        Il2CppClass* ins_state = ins ? il2::find_nested_class(ins, "STATE") : nullptr;
        auto lit = [](Il2CppClass* k, const char* name, uint32_t& out) {
            int64_t v = 0;
            FieldInfo* f = k ? il2::il2cpp_class_get_field_from_name(k, name) : nullptr;
            if (!f || !il2::literal_value(f, v)) return false;
            out = static_cast<uint32_t>(v);
            return true;
        };
        api.ak_states_ok = lit(sc, "GROUP", api.ak_scene_group) && lit(sc_state, "NONE", api.ak_scene_none) &&
                           lit(ins, "GROUP", api.ak_interior_group) && lit(ins_state, "NONE", api.ak_interior_none) &&
                           lit(ins_state, "INTERIORSTATE_INT", api.ak_interior_int);
        if (Il2CppClass* st = il2::find_class(kWwiseBehaviours, "", "AkState")) api.ak_state_data = il2::field_offset(st, "data");
        if (Il2CppClass* group = il2::find_class(kWwiseTypes, "AK.Wwise", "BaseGroupType"))
            api.ak_group_id = il2::find_method(group, "get_GroupId", 0);
        if (Il2CppClass* bt = il2::find_class(kWwiseTypes, "AK.Wwise", "BaseType")) api.ak_value_id = il2::find_method(bt, "get_Id", 0);
        api.ak_states_ok = api.ak_states_ok && api.ak_state_data >= 0 && api.ak_group_id && api.ak_value_id;
        if (!api.ak_states_ok) logx::info("interiors: the sound states (AK.STATES) can't be read; the region's are not re-asserted");
    }
    if (Il2CppClass* w = il2::find_class(kGame, "", "Wind")) {
        api.wind_force_stopped = il2::field_offset(w, "m_WindAudioForceStopped");
        api.wind_loop = il2::field_offset(w, "m_WindLoopAudioInstance");
        api.wind_occluded = il2::field_offset(w, "m_PlayerOccluded");
        api.wind_play_loop = il2::find_method(w, "PlayProceduralWindAudio", 0);
        api.wind_force_stop = il2::find_method(w, "ForceStopAudioLoop", 0);
    }
    if (Il2CppClass* w = il2::find_class(kGame, "", "Weather")) {
        api.weather_chill = il2::field_offset(w, "m_CurrentWindChill");
        api.weather_falling = il2::field_offset(w, "m_FallingSnow");
        api.weather_blowing = il2::field_offset(w, "m_BlowingSnow");
        api.weather_force_outdoor = il2::find_method(w, "ForceOutdoorEnvironment", 0);
        api.weather_indoor_scene = il2::find_method(w, "IsIndoorScene", 0);
    }
    if (Il2CppClass* lm = il2::find_class(kGame, "", "LightingManager")) {
        api.lm_level_done = il2::find_method(lm, "OnLevelLoadComplete", 0);
        api.lm_strength_default = il2::find_method(lm, "SetLightingStrengthDefault", 0);
        api.lm_level_loaded = il2::il2cpp_class_get_field_from_name(lm, "m_LevelLoadComplete");
    }
    if (Il2CppClass* gp = il2::find_class(kGame, "", "GlobalParameters")) {
        api.gp_radius_in = il2::field_offset(gp, "m_PlayerRadiusIndoors");
        api.gp_radius_out = il2::field_offset(gp, "m_PlayerRadiusOutdoors");
        api.gp_near_in = il2::field_offset(gp, "m_NearClipPlaneIndoors");
        api.gp_near_out = il2::field_offset(gp, "m_NearClipPlaneOutdoors");
    }
    if (Il2CppClass* mv = il2::find_class(kGame, "", "PlayerMovement")) {
        api.mv_slope_in = il2::field_offset(mv, "m_MaxSlopeIndoors");
        api.mv_slope_out = il2::field_offset(mv, "m_MaxSlopeOutdoors");
    }
    if (Il2CppClass* fps = il2::find_class(kGame, "", "vp_FPSCamera")) {
        api.fps_near = il2::field_offset(fps, "m_NearClipPlane");
        api.fps_near_get = il2::find_method(fps, "GetNearPlaneOverride", 0);
        api.fps_near_set = il2::find_method(fps, "SetNearPlaneOverride", 1);
    }
    if (Il2CppClass* sp = il2::find_class(kGame, "", "SnowPlaneManager")) {
        api.snow_planes_renderer = il2::field_offset(sp, "m_MeshRenderer");
        api.snow_planes_enabled = il2::find_method(sp, "get_IsEnabled", 0);
    }
    if (Il2CppClass* layer_k = il2::find_class(kGame, "UnityEngine.Rendering.PostProcessing", "PostProcessLayer")) {
        api.post_fog = il2::field_offset(layer_k, "fog");
        if (Il2CppClass* fog = il2::find_class(kGame, "UnityEngine.Rendering.PostProcessing", "Fog"))
            api.fog_enabled = il2::field_offset(fog, "enabled");
    }
    if (Il2CppClass* indoor = il2::find_class(kGame, "", "IndoorSpaceTrigger")) {
        api.indoor_dont_count = il2::field_offset(indoor, "m_DontCountAsInterior");
        api.indoor_outdoor_temp = il2::field_offset(indoor, "m_UseOutdoorTemperature");
        api.indoor_delta = il2::field_offset(indoor, "m_TemperatureDeltaCelsius");
        api.indoor_outdoor_light = il2::field_offset(indoor, "m_UseOutdoorLighting");
        api.indoor_campfires = il2::field_offset(indoor, "m_AllowCampfires");
        api.indoor_travois = il2::field_offset(indoor, "m_AllowDropTravois");
        api.indoor_cant_survey = il2::field_offset(indoor, "m_CantSurvey");
        api.indoor_valid_safehouse = il2::field_offset(indoor, "m_ValidSafehouse");
        api.indoor_trigger_id = il2::field_offset(indoor, "m_TriggerID");
        api.indoor_enter = il2::find_method(indoor, "OnTriggerEnter", 1);
        api.indoor_exit = il2::find_method(indoor, "OnTriggerExit", 1);
    }
    if (Il2CppClass* pdid = il2::find_class("TLD.PDID.dll", "TLD.PDID", "PdidObjectBase"))
        api.set_runtime_pdid = il2::find_method(pdid, "SetRuntimeCachedPdid", 1);
    if (Il2CppClass* rso = il2::find_class(kGame, "", "RandomSpawnObject")) {
        api.rso_list = il2::field_offset(rso, "m_ObjectList");
        api.rso_weights = il2::field_offset_quiet(rso, "m_Weights");
        api.rso_quota = il2::find_method(rso, "GetNumObjectsToEnableCurrentXPMode", 0);
    }
    if (Il2CppClass* gear = il2::find_class(kGame, "", "GearItem")) {
        api.gear_rolled = il2::field_offset(gear, "m_RolledSpawnChance");
        api.gear_in_inventory = il2::field_offset(gear, "m_InPlayerInventory");
        api.gear_roll = il2::find_method(gear, "RollSpawnChance", 0);
        api.gear_disable_physics = il2::find_method(gear, "DisablePhysics", 0);
        api.gear_has_physics = il2::find_method(gear, "HasPhysicsEnabled", 0);
    }
    if (Il2CppClass* p = il2::find_class(kGame, "TLD.Placement", "Placeable"))
        api.placeable_invalidated = il2::field_offset(p, "m_Invalidated");
    if (Il2CppClass* g = il2::find_class(kGame, "", "RenderTextureGrabber")) api.grabber_camera = il2::field_offset(g, "m_Camera");
    if (Il2CppClass* f = il2::find_class(kGame, "", "Fire")) api.fire_heat = il2::field_offset(f, "m_HeatSource");
    if (Il2CppClass* fm = il2::find_class(kGame, "", "FireManager")) {
        api.fire_add = il2::find_method(fm, "AddFire", 1);
        api.stove_add = il2::find_method(fm, "AddWoodStove", 1);
        api.campfire_add = il2::find_method(fm, "AddCampfire", 1);
        api.fire_list = il2::il2cpp_class_get_field_from_name(fm, "m_Fires");
        api.stove_list = il2::il2cpp_class_get_field_from_name(fm, "m_WoodStoves");
        api.campfire_list = il2::il2cpp_class_get_field_from_name(fm, "m_Campfires");
    }
    if (Il2CppClass* gmgr = il2::find_class(kGame, "", "GearManager")) {
        api.gear_list = il2::il2cpp_class_get_field_from_name(gmgr, "m_Gear");
        api.gear_index = il2::il2cpp_class_get_field_from_name(gmgr, "m_CurrentIndex");
    }
    if (Il2CppClass* cm = il2::find_class(kGame, "", "ContainerManager"))
        api.container_list = il2::il2cpp_class_get_field_from_name(cm, "m_Containers");
    if (Il2CppClass* plm = il2::find_class(kGame, "TLD.Placement", "PlaceableManager"))
        api.placeables = il2::il2cpp_class_get_field_from_name(plm, "s_Placeables");
    if (Il2CppClass* sh = il2::find_class(kGame, "", "SafehouseManager"))
        api.safehouse_colliders = il2::il2cpp_class_get_field_from_name(sh, "s_SafehouseIndoorSpaceTriggerColliders");
    if (Il2CppClass* slots = il2::find_class(kGame, "", "SaveGameSlots"))
        api.scene_keys = il2::find_method(slots, "GetSceneKeysForCurrentSaveSlot", 0);

    struct Need {
        const char* what;
        bool ok;
    };
    const Need needs[] = {
        {"UnityEngine object calls", api.name && api.set_name && api.destroy && api.instantiate && api.go_ctor &&
                                         api.go_set_active && api.go_active_self && api.go_active_tree && api.go_transform &&
                                         api.go_add && api.go_get && api.go_children && api.comp_go && api.comp_transform},
        {"Transform calls", api.tf_position && api.tf_set_position && api.tf_rotation && api.tf_set_rotation && api.tf_root &&
                                api.tf_parent && api.tf_set_parent && api.tf_is_child_of && api.tf_find &&
                                api.tf_set_local_scale && api.tf_set_local_position},
        {"renderer calls", api.r_enabled && api.r_set_enabled && api.r_bounds && api.r_lightmap && api.r_set_lightmap &&
                               api.r_materials && api.r_set_materials && api.r_material && api.mat_keyword_on &&
                               api.mat_disable_keyword && api.behaviour_set_enabled && api.behaviour_enabled},
        {"scene calls", api.scene_by_name && api.scene_is_loaded && (api.scene_roots || api.scene_roots_internal) && api.move_to_scene &&
                            api.set_active_scene && api.load_scene && api.lightmaps && api.set_lightmaps},
        {"physics calls", api.raycast_all && api.overlap_sphere && api.collider_by_id && api.col_trigger &&
                              api.col_set_trigger && api.col_enabled && api.col_set_enabled && api.box_set_center &&
                              api.box_set_size},
        {"the player", api.gm_player_manager && api.gm_player_transform && api.gm_player_object && api.teleport &&
                           api.gm_active_scene},
        {"the door's fields", api.door_scene >= 0 && api.door_exit_point >= 0 && api.door_touch_ground >= 0},
        {"the loot roll", api.rnd_init && api.rnd_range && api.rso_list >= 0 && api.gear_roll && api.gear_rolled >= 0},
        {"the indoor space", api.t_indoor && api.indoor_enter && api.indoor_exit && api.pm_indoor >= 0},
        {"the save's lists", api.gear_list && api.container_list},
    };
    bool ok = true;
    for (const Need& n : needs)
        if (!n.ok) {
            logx::warn("interiors: %s are missing in this build; Seamless Interiors is unavailable", n.what);
            ok = false;
        }
    // --- hooks: installed always, inert unless engaged ---
    // (the door's first, even when the rest is missing: world_items::door_used, which keeps a door's scene change from
    // being told as items destroyed, is called from it for every door)
    hook("LoadScene.Activate", il2::find_method_params(load_scene, "Activate", {"System.Boolean"}), &activate_detour,
         &g_activate);
    hook("GameManager.LoadSceneWithLoadingScreen", il2::find_method_params(gm, "LoadSceneWithLoadingScreen", {"System.String"}),
         &load_screen_detour, &g_load_screen);
    if (!ok) return false;
    if (Il2CppClass* rso = il2::find_class(kGame, "", "RandomSpawnObject"))
        hook("RandomSpawnObject.Start", il2::find_method(rso, "Start", 0), &rso_start_detour, &g_rso_start);
    if (Il2CppClass* plm = il2::find_class(kGame, "TLD.Placement", "PlaceableManager")) {
        hook("PlaceableManager.Add", il2::find_method(plm, "Add", 1), &placeable_add_detour, &g_placeable_add);
        hook("PlaceableManager.SerializeInventory", il2::find_method(plm, "SerializeInventory", 0), &serialize_inventory_detour,
             &g_serialize_inventory);
        hook("PlaceableManager.SerializeScenePlacements", il2::find_method(plm, "SerializeScenePlacements", 0),
             &serialize_placements_detour, &g_serialize_placements);
    }
    if (Il2CppClass* gmgr = il2::find_class(kGame, "", "GearManager")) {
        hook("GearManager.Serialize", il2::find_method(gmgr, "Serialize", 0), &gear_serialize_detour, &g_gear_serialize);
        g_gear_des_mi[0] = il2::find_method_params(gmgr, "Deserialize", {"System.String"});
        g_gear_des_mi[1] = il2::find_method_params(gmgr, "Deserialize",
                                                   {"System.String", "System.Collections.Generic.List<GearItem>"});
        g_gear_des_mi[2] = il2::find_method_params(gmgr, "Deserialize",
                                                   {"GearSaveList", "System.Collections.Generic.List<GearItem>"});
        hook("GearManager.Deserialize(string)", g_gear_des_mi[0], &gear_des1_detour, &g_gear_des1);
        hook("GearManager.Deserialize(string, list)", g_gear_des_mi[1], &gear_des2_detour, &g_gear_des2);
        hook("GearManager.Deserialize(GearSaveList, list)", g_gear_des_mi[2], &gear_des3_detour, &g_gear_des3);
    }
    if (Il2CppClass* cm = il2::find_class(kGame, "", "ContainerManager"))
        hook("ContainerManager.Serialize", il2::find_method(cm, "Serialize", 0), &container_serialize_detour,
             &g_container_serialize);
    if (Il2CppClass* fm = il2::find_class(kGame, "", "FireManager"))
        hook("FireManager.Serialize", il2::find_method(fm, "Serialize", 0), &fire_serialize_detour, &g_fire_serialize);
    hook("PlayerManager.Serialize", il2::find_method(pm, "Serialize", 0), &pm_serialize_detour, &g_pm_serialize);
    hook("PlayerManager.ObjectToPlaceOverlapsWithObjectsThatBlockPlacement",
         il2::find_method(pm, "ObjectToPlaceOverlapsWithObjectsThatBlockPlacement", 1), &place_overlap_detour,
         &g_place_overlap);
    hook("PlayerManager.HasCollisionPenetration", il2::find_method(pm, "HasCollisionPenetration", 1), &penetration_detour,
         &g_penetration);
    hook("PlayerManager.IsHitPointOutOfBounds", il2::find_method(pm, "IsHitPointOutOfBounds", 1), &out_of_bounds_detour,
         &g_out_of_bounds);
    if (Il2CppClass* w = il2::find_class(kGame, "", "Wind")) {
        hook("Wind.UpdateWindOcclusion", il2::find_method(w, "UpdateWindOcclusion", 0), &update_wind_occlusion_detour,
             &g_update_wind_occlusion);
        hook("Wind.IsPositionOccludedFromWind", il2::find_method(w, "IsPositionOccludedFromWind", 1), &position_occluded_detour,
             &g_position_occluded);
        hook("Wind.Start", il2::find_method(w, "Start", 0), &wind_start_detour, &g_wind_start);
    }
    if (Il2CppClass* w = il2::find_class(kGame, "", "Weather")) {
        hook("Weather.CalculateCurrentTemperature", il2::find_method(w, "CalculateCurrentTemperature", 0),
             &calc_temperature_detour, &g_calc_temperature);
        hook("Weather.IsIndoorEnvironment", il2::find_method(w, "IsIndoorEnvironment", 0), &indoor_environment_detour,
             &g_indoor_environment);
    }
    if (Il2CppClass* ev = il2::find_class(kGame, "", "EvolveItem"))
        hook("EvolveItem.ObjectInIndoorTrigger", il2::find_method(ev, "ObjectInIndoorTrigger", 0), &evolve_indoor_detour,
             &g_evolve_indoor);
    if (Il2CppClass* sh = il2::find_class(kGame, "", "SafehouseManager")) {
        hook("SafehouseManager.InCustomizableSafehouse", il2::find_method(sh, "InCustomizableSafehouse", 0),
             &in_customizable_detour, &g_in_customizable);
        hook("SafehouseManager.IsDecorationItemInsideSafehouse", il2::find_method(sh, "IsDecorationItemInsideSafehouse", 1),
             &decoration_inside_detour, &g_decoration_inside);
    }
    if (Il2CppClass* fd = il2::find_class(kGame, "", "FallDamage"))
        hook("FallDamage.ApplyFallDamage", il2::find_method(fd, "ApplyFallDamage", 2), &fall_damage_detour, &g_fall_damage);
    if (!g_activate) {
        logx::warn("interiors: the door can't be hooked; Seamless Interiors is unavailable");
        return false;
    }
    resolve_persist();
    resolve_import();
    return true;
}

}  // namespace

void init() {
    api.ok = resolve();
    logx::info("interiors: Seamless Interiors %s (%d buildings in %s)", api.ok ? "ready" : "unavailable", kConfigCount,
               "12 regions");
}

}  // namespace seamless_interiors
