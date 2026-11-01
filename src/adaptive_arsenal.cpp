#include "adaptive_arsenal.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "arsenal_math.h"
#include "coop_features.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "paths.h"
#include "log.h"
#include "net/protocol.h"
#include "wildlife_hits.h"

namespace adaptive_arsenal {
namespace {

using arsenal::V3;

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr ptrdiff_t kArrayData = 0x20;
constexpr int32_t kAimRevolver = 18;     // PlayerControlMode
constexpr int32_t kSkillRevolver = 8;    // SkillType
constexpr int32_t kTriggersIgnored = 1;  // QueryTriggerInteraction.Ignore
constexpr float kMaxFlight = 1000;       // metres: a bullet that has hit nothing by then is gone
constexpr float kMaxSeconds = 6;
constexpr float kStep = 0.02f;           // seconds per step of flight at most
constexpr float kBackOff = 1.0f;         // TLD's bullet starts this far in front of what ours hit
constexpr float kTrailEvery = 0.05f;     // seconds between the trail's points
constexpr float kTrailLength = 200;      // metres of trail at most
constexpr float kTrailFade = 2;          // seconds
constexpr float kCasingSeconds = 1;

struct Quaternion {
    float x, y, z, w;
};
struct Color {
    float r, g, b, a;
};

struct Api {
    bool ok = false, hud_ok = false;
    // Unity
    const MethodInfo* instantiate = nullptr;         // static Object.Instantiate(Object)
    const MethodInfo* instantiate_under = nullptr;   // static Object.Instantiate(Object, Transform)
    const MethodInfo* destroy = nullptr;             // static Object.Destroy(Object)
    const MethodInfo* object_name = nullptr;
    const MethodInfo* component_go = nullptr, *component_tf = nullptr;
    const MethodInfo* go_ctor = nullptr, *go_active = nullptr, *add_component = nullptr;
    Il2CppClass* go_class = nullptr;
    const MethodInfo* get_position = nullptr, *set_position = nullptr, *get_forward = nullptr, *set_rotation = nullptr;
    const MethodInfo* get_local = nullptr, *set_local = nullptr, *rotate = nullptr, *get_parent = nullptr;
    const MethodInfo* look_rotation = nullptr;       // static Quaternion.LookRotation(Vector3)
    const MethodInfo* delta_time = nullptr;          // static Time.get_deltaTime
    const MethodInfo* raycast = nullptr;             // static Physics.Raycast(Vector3, Vector3, out hit, float, int, qti)
    const MethodInfo* layer_name = nullptr;          // static LayerMask.LayerToName(int)
    const MethodInfo* shader_find = nullptr;         // static Shader.Find(string)
    Il2CppClass* material_class = nullptr;
    const MethodInfo* material_ctor = nullptr;       // Material(Shader)
    Il2CppObject* line_type = nullptr;
    const MethodInfo* set_material = nullptr;
    const MethodInfo* line_start_width = nullptr, *line_end_width = nullptr, *line_world = nullptr;
    const MethodInfo* line_start_color = nullptr, *line_end_color = nullptr, *line_count = nullptr, *line_point = nullptr;
    // TLD
    const MethodInfo* player_manager = nullptr, *in_hands = nullptr, *crouched = nullptr, *walking = nullptr,
                      *control_mode = nullptr;
    const MethodInfo* skill_rifle = nullptr, *rifle_range = nullptr, *skills = nullptr, *get_skill = nullptr,
                      *tier = nullptr;
    const MethodInfo* animation = nullptr, *allowed_to_fire = nullptr, *move_unblocked = nullptr, *input_walk = nullptr;
    ptrdiff_t gear_gun = -1, gun_type = -1, gun_hip = -1, gun_clip = -1, gun_accuracy = -1;
    const MethodInfo* rounds = nullptr;
    ptrdiff_t bullet_range = -1, bullet_damage = -1, bullet_accuracy = -1, bullet_gun = -1;
    ptrdiff_t hud_mobility = -1, popup_sprites = -1;
    const MethodInfo* set_sprite = nullptr, *set_depth = nullptr;
} api;

struct Flight {
    uintptr_t bullet = 0;  // our inactive copy of TLD's bullet, started where this one lands
    V3 pos, vel;
    float travelled = 0, age = 0;
    float damage = 0, range = 0;
    uintptr_t trail = 0;   // its LineRenderer
    std::vector<V3> points;
    float since_point = 0;
};
std::vector<Flight> g_flights;

struct Fading {
    uintptr_t trail = 0;  // LineRenderer
    float t = 0;
};
std::vector<Fading> g_fading;

struct Casing {
    uintptr_t sprite = 0;  // UISprite
    V3 start, ctrl, end;
    float t = 0;
};
std::vector<Casing> g_casings;

std::unordered_map<Il2CppObject*, float> g_started;  // our copies started already (Unity may start them again)
std::mt19937 g_rng{std::random_device{}()};
int32_t g_mask = ~(1 << 2);
uintptr_t g_trail_material = 0;
bool g_material_tried = false;
int g_last_rounds = -1;
Il2CppObject* g_last_gun = nullptr;

Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
uintptr_t hold(Il2CppObject* o) { return o ? il2::il2cpp_gchandle_new(o, false) : 0; }
void let_go(uintptr_t& h) {
    if (h) il2::il2cpp_gchandle_free(h);
    h = 0;
}
bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
bool active() { return coop_features::on(net::kFeatureAdaptiveArsenal); }

std::string name_of(Il2CppObject* o) {
    Il2CppObject* s = o ? il2::invoke(api.object_name, o) : nullptr;
    return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
}
Il2CppObject* go_of(Il2CppObject* c) { return c ? il2::invoke(api.component_go, c) : nullptr; }
Il2CppObject* tf_of(Il2CppObject* c) { return c ? il2::invoke(api.component_tf, c) : nullptr; }
void set_active(Il2CppObject* go, bool on) {
    if (!go) return;
    void* a[] = {&on};
    il2::invoke(api.go_active, go, a);
}
void destroy(Il2CppObject* o) {
    if (!alive(o)) return;
    void* a[] = {o};
    il2::invoke(api.destroy, nullptr, a);
}
V3 vec(Il2CppObject* tf, const MethodInfo* m) {
    V3 v;
    if (tf) il2::invoke_value(m, tf, v);
    return v;
}
void set_vec(Il2CppObject* tf, const MethodInfo* m, V3 v) {
    if (!tf) return;
    void* a[] = {&v};
    il2::invoke(m, tf, a);
}
float delta_time() {
    float dt = 0;
    il2::invoke_value(api.delta_time, nullptr, dt);
    return dt > 0.1f ? 0.1f : dt;
}
template <typename T>
T call(const MethodInfo* m, void* self, void** args = nullptr, T fallback = T{}) {
    T v = fallback;
    if (m) il2::invoke_value(m, self, v, args);
    return v;
}

// --- the shot ----------------------------------------------------------------------------------------------------------

Il2CppObject* trail_material() {
    if (Il2CppObject* m = target(g_trail_material)) return m;
    if (g_material_tried) return nullptr;
    g_material_tried = true;
    // TLDCoop_DLC: off unless asked for (coop.ini [arsenal] trail=1): Unity's shaders found here draw pink in TLD
    // ("Sprites/Default": a big magenta wedge behind every shot)
    if (!GetPrivateProfileIntW(L"arsenal", L"trail", 0, (paths::instance_dir() / L"coop.ini").c_str())) {
        logx::info("arsenal: bullet trails off ([arsenal] trail=1 shows them)");
        return nullptr;
    }
    for (const char* name : {"Sprites/Default", "Legacy Shaders/Particles/Alpha Blended", "Particles/Standard Unlit",
                             "Unlit/Transparent", "Unlit/Color"}) {
        void* a[] = {il2::il2cpp_string_new(name)};
        Il2CppObject* shader = il2::invoke(api.shader_find, nullptr, a);
        if (!shader) continue;
        Il2CppObject* m = il2::il2cpp_object_new(api.material_class);
        void* ca[] = {shader};
        il2::invoke(api.material_ctor, m, ca);
        g_trail_material = hold(m);
        logx::info("arsenal: bullet trails drawn with %s", name);
        return m;
    }
    logx::warn("arsenal: no shader for the bullet trails; bullets fly without one");
    return nullptr;
}

void line_colors(Il2CppObject* line, float alpha) {
    Color a{1, 1, 1, 0}, b{0.7f, 0.7f, 0.7f, 0.7f * alpha};
    void* x[] = {&a};
    il2::invoke(api.line_start_color, line, x);
    void* y[] = {&b};
    il2::invoke(api.line_end_color, line, y);
}

Il2CppObject* make_trail() {
    Il2CppObject* material = trail_material();
    if (!material) return nullptr;
    Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
    void* na[] = {il2::il2cpp_string_new("TLDCoop_BulletTrail")};
    il2::invoke(api.go_ctor, go, na);
    void* ta[] = {api.line_type};
    Il2CppObject* line = il2::invoke(api.add_component, go, ta);
    if (!line) return nullptr;
    void* ma[] = {material};
    il2::invoke(api.set_material, line, ma);
    float w0 = 0.02f, w1 = 0.005f;  // TLDCoop_DLC: a thin streak (0.4 m wide was a wedge)
    void* a0[] = {&w0};
    il2::invoke(api.line_start_width, line, a0);
    void* a1[] = {&w1};
    il2::invoke(api.line_end_width, line, a1);
    bool world = true;
    void* wa[] = {&world};
    il2::invoke(api.line_world, line, wa);
    line_colors(line, 1);
    return line;
}

void draw_trail(Flight& f) {
    Il2CppObject* line = target(f.trail);
    if (!alive(line)) return;
    // the newest points, up to kTrailLength of them
    float len = 0;
    size_t first = 0;
    for (size_t i = f.points.size() - 1; i > 0; --i) {
        len += arsenal::length(f.points[i] - f.points[i - 1]);
        if (len > kTrailLength) {
            first = i;
            break;
        }
    }
    if (first) f.points.erase(f.points.begin(), f.points.begin() + static_cast<ptrdiff_t>(first));
    int32_t n = static_cast<int32_t>(f.points.size());
    void* ca[] = {&n};
    il2::invoke(api.line_count, line, ca);
    for (int32_t i = 0; i < n; ++i) {
        V3 p = f.points[static_cast<size_t>(i)];
        void* pa[] = {&i, &p};
        il2::invoke(api.line_point, line, pa);
    }
}

float gun_accuracy(Il2CppObject* gun, int32_t type) {
    float base = 0;
    if (type == arsenal::kRifle) {
        base = call<float>(api.rifle_range, il2::invoke(api.skill_rifle, nullptr));
    } else {
        Il2CppObject* skills = il2::invoke(api.skills, nullptr);
        int32_t st = kSkillRevolver;
        void* sa[] = {&st};
        Il2CppObject* skill = skills ? il2::invoke(api.get_skill, skills, sa) : nullptr;
        base = arsenal::revolver_range(call<int32_t>(api.tier, skill)) + il2::read_field<float>(gun, api.gun_accuracy);
    }
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    Il2CppObject* anim = il2::invoke(api.animation, nullptr);
    bool no_hip = false;
    int32_t gt = type;
    void* fa[] = {&no_hip, &gt};
    const bool hip = il2::read_field<bool>(gun, api.gun_hip) && anim && !call<bool>(api.allowed_to_fire, anim, fa);
    const bool standing = !call<bool>(api.crouched, pm);
    const bool moving = call<bool>(api.walking, pm);
    return arsenal::accuracy(base, hip, standing, moving);
}

// Where it lands (or ends): TLD's bullet, started `back` metres in front, pointed along `dir`, reaching `reach`.
void land(Flight& f, V3 at, V3 dir, float reach, bool hit) {
    Il2CppObject* bullet = target(f.bullet);
    if (alive(bullet)) {
        Il2CppObject* tf = tf_of(bullet);
        set_vec(tf, api.set_position, at);
        Quaternion q{};
        void* la[] = {&dir};
        if (il2::invoke_value(api.look_rotation, nullptr, q, la)) {
            void* ra[] = {&q};
            il2::invoke(api.set_rotation, tf, ra);
        }
        char* b = reinterpret_cast<char*>(bullet);
        const float range = reach, damage = f.damage * arsenal::damage_share(f.travelled), effective = 1e4f;
        memcpy(b + api.bullet_range, &range, sizeof(float));
        memcpy(b + api.bullet_damage, &damage, sizeof(float));
        memcpy(b + api.bullet_accuracy, &effective, sizeof(float));  // TLD's "effective range": no falloff of its own
        set_active(go_of(bullet), true);
        g_started[bullet] = 0;
        wildlife_hits::start_bullet(bullet);
        static int logged = 0;
        if (logged++ < 20)
            logx::info("arsenal: a bullet %s after %.0f m (%.2f s): %.0f%% of its damage", hit ? "hit" : "ended",
                       f.travelled, f.age, 100 * arsenal::damage_share(f.travelled));
    }
    let_go(f.bullet);
    if (f.trail) g_fading.push_back({f.trail, 0});
    f.trail = 0;
}

// One frame of a bullet's flight. False when it's done.
bool fly(Flight& f, float dt) {
    while (dt > 0) {
        const float h = dt > kStep ? kStep : dt;
        dt -= h;
        const V3 v1 = arsenal::step_velocity(f.vel, h);
        const V3 seg = (f.vel + v1) * (0.5f * h);
        const float len = arsenal::length(seg);
        const V3 dir = arsenal::normalized(seg);
        uint8_t hit[64] = {};
        void* hp = hit;
        float dist = len;
        int32_t mask = g_mask, qti = kTriggersIgnored;
        V3 from = f.pos, d = dir;
        void* ra[] = {&from, &d, hp, &dist, &mask, &qti};
        if (len > 0 && call<bool>(api.raycast, nullptr, ra)) {
            V3 point;
            float along = 0;
            memcpy(&point, hit, sizeof(point));
            memcpy(&along, hit + 28, sizeof(float));  // RaycastHit.m_Distance
            f.travelled += along;
            const float back = f.travelled < kBackOff ? f.travelled : kBackOff;
            f.points.push_back(point);
            draw_trail(f);
            land(f, point - dir * back, dir, back + (f.range > f.travelled ? f.range - f.travelled : 0) + 0.5f, true);
            return false;
        }
        f.pos = f.pos + seg;
        f.vel = v1;
        f.travelled += len;
        f.age += h;
        f.since_point += h;
        if (f.since_point >= kTrailEvery) {
            f.since_point = 0;
            f.points.push_back(f.pos);
            draw_trail(f);
        }
        if (f.travelled > kMaxFlight || f.age > kMaxSeconds) {
            land(f, f.pos, arsenal::normalized(f.vel), 0.05f, false);
            return false;
        }
    }
    return true;
}

// --- the HUD's spent round ---------------------------------------------------------------------------------------------

using UpdateAmmoFn = void (*)(Il2CppObject*, const MethodInfo*);
UpdateAmmoFn g_update_ammo = nullptr;

void update_ammo_detour(Il2CppObject* popup, const MethodInfo* m) {
    g_update_ammo(popup, m);
    if (!api.hud_ok || !active() || !popup) return;
    Il2CppObject* item = il2::invoke(api.in_hands, il2::invoke(api.player_manager, nullptr));
    Il2CppObject* gun = item ? il2::read_field<Il2CppObject*>(item, api.gear_gun) : nullptr;
    const int32_t type = gun ? il2::read_field<int32_t>(gun, api.gun_type) : -1;
    if (!gun || (type != arsenal::kRifle && type != arsenal::kRevolver)) {
        g_last_gun = nullptr;
        g_last_rounds = -1;
        return;
    }
    const int rounds = call<int32_t>(api.rounds, gun);
    if (gun != g_last_gun) {
        g_last_gun = gun;
        g_last_rounds = rounds;
        return;
    }
    Il2CppObject* sprites = il2::read_field<Il2CppObject*>(popup, api.popup_sprites);
    const int n = static_cast<int>(il2::array_length(sprites));
    if (rounds < il2::read_field<int32_t>(gun, api.gun_clip) && rounds < n && rounds < g_last_rounds && n > 1) {
        Il2CppObject* at = il2::array_object_at(sprites, static_cast<size_t>(rounds));
        Il2CppObject* parent = il2::invoke(api.get_parent, tf_of(il2::array_object_at(sprites, 0)));
        Il2CppObject* model = il2::array_object_at(sprites, 1);
        void* ia[] = {model, parent};
        Il2CppObject* copy = (at && parent && model) ? il2::invoke(api.instantiate_under, nullptr, ia) : nullptr;
        if (copy) {
            Casing c;
            c.start = vec(tf_of(at), api.get_local);
            c.ctrl = {c.start.x + 50, c.start.y + 100, c.start.z};
            c.end = {c.start.x + 100, c.start.y - 150, c.start.z};
            set_active(go_of(copy), true);
            set_vec(tf_of(copy), api.set_local, c.start);
            int32_t depth = 30;
            void* da[] = {&depth};
            il2::invoke(api.set_depth, copy, da);
            void* sa[] = {il2::il2cpp_string_new(type == arsenal::kRifle ? "ico_ammo_rifle" : "ico_ammo_revolver")};
            il2::invoke(api.set_sprite, copy, sa);
            c.sprite = hold(copy);
            g_casings.push_back(c);
        }
    }
    g_last_rounds = rounds;
}

void animate_casings(float dt) {
    for (auto it = g_casings.begin(); it != g_casings.end();) {
        Il2CppObject* s = target(it->sprite);
        it->t += dt;
        if (!alive(s) || it->t >= kCasingSeconds) {
            destroy(go_of(s));
            let_go(it->sprite);
            it = g_casings.erase(it);
            continue;
        }
        const float t = it->t / kCasingSeconds, u = 1 - t;
        const V3 p = it->start * (u * u) + it->ctrl * (2 * u * t) + it->end * (t * t);
        Il2CppObject* tf = tf_of(s);
        set_vec(tf, api.set_local, p);
        float x = 0, y = 0, z = 360 * dt;
        void* ra[] = {&x, &y, &z};
        il2::invoke(api.rotate, tf, ra);
        ++it;
    }
}

// --- the revolver aimed on the move ------------------------------------------------------------------------------------

using UpdateFn = void (*)(Il2CppObject*, const MethodInfo*);
UpdateFn g_player_update = nullptr, g_hud_update = nullptr;

void player_update_detour(Il2CppObject* self, const MethodInfo* m) {
    g_player_update(self, m);
    if (!api.ok || !active()) return;
    if (call<int32_t>(api.control_mode, il2::invoke(api.player_manager, nullptr), nullptr, -1) == kAimRevolver &&
        call<bool>(api.move_unblocked, nullptr))
        il2::invoke(api.input_walk, self);
}

void hud_update_detour(Il2CppObject* self, const MethodInfo* m) {
    g_hud_update(self, m);
    if (!api.ok || !active() || !self) return;
    if (Il2CppObject* w = il2::read_field<Il2CppObject*>(self, api.hud_mobility)) set_active(go_of(w), false);
}

// --- setup -------------------------------------------------------------------------------------------------------------

void build_mask() {
    std::string kept, dropped;
    int32_t mask = 0;
    for (int32_t i = 0; i < 32; ++i) {
        void* a[] = {&i};
        Il2CppObject* s = il2::invoke(api.layer_name, nullptr, a);
        const std::string n = s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string();
        if (n.empty()) continue;
        const bool skip = n == "Ignore Raycast" || n == "UI" || n == "Player" || n.find("Trigger") != std::string::npos ||
                          n.find("NoCollide") != std::string::npos || n.find("Weapon") != std::string::npos;
        (skip ? dropped : kept) += " " + std::to_string(i) + ":" + n;
        if (!skip) mask |= 1 << i;
    }
    if (mask) g_mask = mask;
    logx::info("arsenal: bullets hit layers%s; not%s", kept.c_str(), dropped.c_str());
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* quaternion = il2::find_class(kCore, "UnityEngine", "Quaternion");
    Il2CppClass* time = il2::find_class(kCore, "UnityEngine", "Time");
    Il2CppClass* layer = il2::find_class(kCore, "UnityEngine", "LayerMask");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* line = il2::find_class(kCore, "UnityEngine", "LineRenderer");
    Il2CppClass* physics = il2::find_class(kPhysics, "UnityEngine", "Physics");
    api.go_class = il2::find_class(kCore, "UnityEngine", "GameObject");
    api.material_class = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* gun = il2::find_class(kGame, "", "GunItem");
    Il2CppClass* rifle = il2::find_class(kGame, "", "Skill_Rifle");
    Il2CppClass* skills = il2::find_class(kGame, "", "SkillsManager");
    Il2CppClass* skill = il2::find_class(kGame, "", "Skill");
    Il2CppClass* anim = il2::find_class(kGame, "", "PlayerAnimation");
    Il2CppClass* bullet = il2::find_class(kGame, "", "vp_Bullet");
    if (!object || !component || !transform || !quaternion || !time || !layer || !physics || !api.go_class || !gm ||
        !pm || !gear || !gun || !rifle || !skills || !skill || !anim || !bullet)
        return false;
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.instantiate_under = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"});
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.object_name = il2::find_method(object, "get_name", 0);
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.component_tf = il2::find_method(component, "get_transform", 0);
    api.go_ctor = il2::find_method_params(api.go_class, ".ctor", {"System.String"});
    api.go_active = il2::find_method(api.go_class, "SetActive", 1);
    api.add_component = il2::find_method_params(api.go_class, "AddComponent", {"System.Type"});
    api.get_position = il2::find_method(transform, "get_position", 0);
    api.set_position = il2::find_method(transform, "set_position", 1);
    api.get_forward = il2::find_method(transform, "get_forward", 0);
    api.set_rotation = il2::find_method(transform, "set_rotation", 1);
    api.get_local = il2::find_method(transform, "get_localPosition", 0);
    api.set_local = il2::find_method(transform, "set_localPosition", 1);
    api.rotate = il2::find_method_params(transform, "Rotate", {"System.Single", "System.Single", "System.Single"});
    api.get_parent = il2::find_method(transform, "get_parent", 0);
    api.look_rotation = il2::find_method_params(quaternion, "LookRotation", {"UnityEngine.Vector3"});
    api.delta_time = il2::find_method(time, "get_deltaTime", 0);
    api.raycast = il2::find_method_params(physics, "Raycast",
                                          {"UnityEngine.Vector3", "UnityEngine.Vector3", "UnityEngine.RaycastHit&",
                                           "System.Single", "System.Int32", "UnityEngine.QueryTriggerInteraction"});
    api.layer_name = il2::find_method(layer, "LayerToName", 1);
    if (shader) api.shader_find = il2::find_method(shader, "Find", 1);
    if (api.material_class) api.material_ctor = il2::find_method_params(api.material_class, ".ctor", {"UnityEngine.Shader"});
    if (line) {
        api.line_type = il2::type_object(line);
        api.line_start_width = il2::find_method(line, "set_startWidth", 1);
        api.line_end_width = il2::find_method(line, "set_endWidth", 1);
        api.line_world = il2::find_method(line, "set_useWorldSpace", 1);
        api.line_start_color = il2::find_method(line, "set_startColor", 1);
        api.line_end_color = il2::find_method(line, "set_endColor", 1);
        api.line_count = il2::find_method(line, "set_positionCount", 1);
        api.line_point = il2::find_method(line, "SetPosition", 2);
    }
    if (renderer) api.set_material = il2::find_method(renderer, "set_material", 1);

    api.player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    api.in_hands = il2::find_method(pm, "get_m_ItemInHands", 0);
    api.crouched = il2::find_method(pm, "PlayerIsCrouched", 0);
    api.walking = il2::find_method(pm, "PlayerIsWalking", 0);
    api.control_mode = il2::find_method(pm, "GetControlMode", 0);
    api.skill_rifle = il2::find_method(gm, "GetSkillRifle", 0);
    api.rifle_range = il2::find_method(rifle, "GetEffectiveRange", 0);
    api.skills = il2::find_method(gm, "GetSkillsManager", 0);
    api.get_skill = il2::find_method(skills, "GetSkill", 1);
    api.tier = il2::find_method(skill, "GetCurrentTierNumber", 0);
    api.animation = il2::find_method(gm, "GetPlayerAnimationComponent", 0);
    api.allowed_to_fire = il2::find_method(anim, "IsAllowedToFire", 2);
    api.move_unblocked = il2::find_method(gm, "IsMoveInputUnblocked", 0);
    api.gear_gun = il2::field_offset(gear, "m_GunItem");
    api.gun_type = il2::field_offset(gun, "m_GunType");
    api.gun_hip = il2::field_offset(gun, "m_AllowHipFire");
    api.gun_clip = il2::field_offset(gun, "m_ClipSize");
    api.gun_accuracy = il2::field_offset(gun, "m_AccuracyRange");
    api.rounds = il2::find_method(gun, "NumRoundsInClip", 0);
    api.bullet_range = il2::field_offset(bullet, "Range");
    api.bullet_damage = il2::field_offset(bullet, "Damage");
    api.bullet_accuracy = il2::field_offset(bullet, "Accuracy");
    api.bullet_gun = il2::field_offset(bullet, "m_GunType");
    return api.instantiate && api.instantiate_under && api.destroy && api.object_name && api.component_go &&
           api.component_tf && api.go_ctor && api.go_active && api.add_component && api.get_position &&
           api.set_position && api.get_forward && api.set_rotation && api.look_rotation && api.delta_time &&
           api.raycast && api.layer_name && api.player_manager && api.in_hands && api.crouched && api.walking &&
           api.control_mode && api.skill_rifle && api.rifle_range && api.skills && api.get_skill && api.tier &&
           api.animation && api.allowed_to_fire && api.gear_gun >= 0 && api.gun_type >= 0 && api.gun_hip >= 0 &&
           api.gun_clip >= 0 && api.gun_accuracy >= 0 && api.rounds && api.bullet_range >= 0 &&
           api.bullet_damage >= 0 && api.bullet_accuracy >= 0 && api.bullet_gun >= 0;
}

void install_hooks() {
    Il2CppClass* player = il2::find_class(kGame, "", "vp_FPSPlayer");
    Il2CppClass* hud = il2::find_class(kGame, "", "Panel_HUD");
    Il2CppClass* popup = il2::find_class(kGame, "", "EquipItemPopup");
    Il2CppClass* sprite = il2::find_class("Assembly-CSharp.dll", "", "UISprite");
    Il2CppClass* widget = il2::find_class("Assembly-CSharp.dll", "", "UIWidget");
    auto hook = [](const char* name, const MethodInfo* m, auto* detour, auto** original) {
        return m && hooks::install_method(name, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    if (player) {
        api.input_walk = il2::find_method(player, "InputWalk", 0);
        if (!api.input_walk || !api.move_unblocked ||
            !hook("vp_FPSPlayer.Update", il2::find_method(player, "Update", 0), &player_update_detour, &g_player_update))
            logx::warn("arsenal: the revolver can't be aimed on the move (TLD's calls missing)");
    }
    if (hud) {
        api.hud_mobility = il2::field_offset(hud, "m_AimingLimitedMobility");
        if (api.hud_mobility < 0 || !hook("Panel_HUD.Update", il2::find_method(hud, "Update", 0), &hud_update_detour, &g_hud_update))
            logx::warn("arsenal: the HUD's limited-mobility sign stays");
    }
    if (popup && sprite && widget) {
        api.popup_sprites = il2::field_offset(popup, "m_ListAmmoSprites");
        api.set_sprite = il2::find_method(sprite, "set_spriteName", 1);
        api.set_depth = il2::find_method(widget, "set_depth", 1);
        api.hud_ok = api.popup_sprites >= 0 && api.set_sprite && api.set_depth && api.get_local && api.set_local &&
                     api.rotate && api.get_parent &&
                     hook("EquipItemPopup.UpdateAmmoStatus", il2::find_method(popup, "UpdateAmmoStatus", 0),
                          &update_ammo_detour, &g_update_ammo);
    }
    if (!api.hud_ok) logx::warn("arsenal: no spent rounds on the HUD (TLD's calls missing)");
}

}  // namespace

void init() {
    api.ok = resolve();
    if (api.ok) install_hooks();
    logx::info("arsenal: Adaptive Arsenal %s", api.ok ? "ready" : "unavailable (TLD's calls missing)");
}

void set_settings(const config::Settings& s) {
    coop_features::set_own(net::kFeatureAdaptiveArsenal, s.adaptive_arsenal);
    logx::info("arsenal: %s", s.adaptive_arsenal ? "on" : "off");
}

bool take_bullet(Il2CppObject* bullet) {
    if (!api.ok || !bullet) return false;
    if (auto it = g_started.find(bullet); it != g_started.end()) return true;  // ours, started already: not twice
    if (!active()) return false;
    Il2CppObject* item = il2::invoke(api.in_hands, il2::invoke(api.player_manager, nullptr));
    Il2CppObject* gun = item ? il2::read_field<Il2CppObject*>(item, api.gear_gun) : nullptr;
    if (!gun) return false;
    const int32_t type = il2::read_field<int32_t>(gun, api.gun_type);
    if ((type != arsenal::kRifle && type != arsenal::kRevolver) || il2::read_field<int32_t>(bullet, api.bullet_gun) != type)
        return false;

    static bool mask_built = false;
    if (!mask_built) {
        mask_built = true;
        build_mask();
    }
    Il2CppObject* tf = tf_of(bullet);
    const V3 pos = vec(tf, api.get_position);
    V3 dir = vec(tf, api.get_forward);
    const float spread = arsenal::spread_degrees(gun_accuracy(gun, type));
    std::uniform_real_distribution<float> r(-spread, spread);
    dir = arsenal::turned(dir, r(g_rng), r(g_rng));

    // a sleeping copy of TLD's bullet, for where it lands; TLD's own goes
    void* ia[] = {bullet};
    Il2CppObject* copy = il2::invoke(api.instantiate, nullptr, ia);
    if (!copy) return false;
    set_active(go_of(copy), false);
    Flight f;
    f.bullet = hold(copy);
    f.pos = pos;
    f.vel = dir * arsenal::muzzle_velocity(name_of(item).c_str(), type);
    f.damage = il2::read_field<float>(bullet, api.bullet_damage);
    f.range = il2::read_field<float>(bullet, api.bullet_range);
    f.points.push_back(pos);
    if (Il2CppObject* line = make_trail()) f.trail = hold(line);
    g_flights.push_back(std::move(f));
    destroy(go_of(bullet));
    static int logged = 0;
    if (logged++ < 20)
        logx::info("arsenal: %s fired: %.0f m/s, spread up to %.1f degrees", name_of(item).c_str(),
                   arsenal::length(g_flights.back().vel), spread);
    return true;
}

void on_frame(const std::string& scene, bool in_game) {
    (void)scene;
    if (!api.ok) return;
    const float dt = in_game ? delta_time() : 0;
    for (auto it = g_started.begin(); it != g_started.end();) {
        it->second += 1;
        it = it->second > 600 ? g_started.erase(it) : std::next(it);  // ~10 s of frames
    }
    for (auto it = g_flights.begin(); it != g_flights.end();) {
        if (!in_game || !alive(target(it->bullet))) {  // a scene change: it's gone
            let_go(it->bullet);
            destroy(go_of(target(it->trail)));
            let_go(it->trail);
            it = g_flights.erase(it);
            continue;
        }
        it = fly(*it, dt) ? std::next(it) : g_flights.erase(it);
    }
    for (auto it = g_fading.begin(); it != g_fading.end();) {
        Il2CppObject* line = target(it->trail);
        it->t += dt;
        if (!alive(line) || it->t >= kTrailFade) {
            destroy(go_of(line));
            let_go(it->trail);
            it = g_fading.erase(it);
            continue;
        }
        line_colors(line, 1 - it->t / kTrailFade);
        ++it;
    }
    animate_casings(dt);
}

}  // namespace adaptive_arsenal
