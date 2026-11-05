#include "slot_machine.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "coop_notices.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/emotes.h"
#include "net/protocol.h"
#include "net_runtime.h"
#include "paths.h"

namespace slot_machine {
namespace {

#include "slot_machine_model.inc"

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr ptrdiff_t kArrayData = 0x20;
constexpr int kLayerInteractive = 19;   // TLD's interactive layer (House Lights' switches too)
constexpr int kHoverCanInteract = 3;    // HoverTextState.CanInteract
constexpr int kRGBA32 = 4;              // TextureFormat.RGBA32
constexpr int kDontUnload = 32;         // HideFlags.DontUnloadUnusedAsset
constexpr const char* kName = "MOD_TLDCoopAutomat";
constexpr float kReach = 2.5f;

// The reels' symbols (kAutStrip's numbers)
enum Symbol { Skull = 0, Wolf, Meat, Match, Fire, Gold };
const char* kSymbolName[] = {"czaszka", "wilk", "mięso", "zapałka", "ognisko", "złoty samorodek"};

struct Vector3 { float x, y, z; };
struct Vector2 { float x, y; };
struct Color { float r, g, b, a; };

struct Api {
    bool ok = false;
    Il2CppClass *go_class = nullptr, *mesh_class = nullptr, *material_class = nullptr, *texture_class = nullptr;
    Il2CppClass *vec3_class = nullptr, *vec2_class = nullptr, *int_class = nullptr;
    Il2CppObject *t_mesh_filter = nullptr, *t_mesh_renderer = nullptr, *t_light = nullptr, *t_box = nullptr;
    const MethodInfo *go_ctor = nullptr, *go_tf = nullptr, *go_add = nullptr, *go_set_layer = nullptr;
    const MethodInfo *set_name = nullptr, *object_name = nullptr, *destroy = nullptr, *hide_flags = nullptr;
    const MethodInfo *tf_set_parent = nullptr, *tf_set_local_pos = nullptr, *tf_set_local_euler = nullptr;
    const MethodInfo *tf_set_pos = nullptr, *tf_set_euler = nullptr, *tf_get_pos = nullptr;
    const MethodInfo *mesh_ctor = nullptr, *mesh_vertices = nullptr, *mesh_uv = nullptr, *mesh_triangles = nullptr;
    const MethodInfo *mesh_normals = nullptr, *mesh_bounds = nullptr;
    const MethodInfo *filter_set_mesh = nullptr, *renderer_set_material = nullptr;
    const MethodInfo *material_ctor = nullptr, *material_set_main = nullptr, *shader_find = nullptr;
    const MethodInfo *tex_ctor = nullptr, *tex_get_pixels = nullptr, *tex_set_pixels = nullptr, *tex_apply = nullptr;
    const MethodInfo *tex_filter = nullptr;
    const MethodInfo *light_color = nullptr, *light_range = nullptr, *light_intensity = nullptr, *behaviour_enabled = nullptr;
    const MethodInfo *box_size = nullptr, *box_center = nullptr;
    const MethodInfo *under_crosshairs = nullptr, *set_hover = nullptr;
} api;

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
double now_ms() { return static_cast<double>(GetTickCount64()); }

bool g_test = false;  // coop.ini [automat] test=1
int g_key = 0;        // coop.ini [automat] key (F2): places one; with Shift, removes the nearest

// --- built once: textures, materials, meshes (kept across scenes) ---------------------------------------------------

struct Shared {
    bool tried = false, ok = false;
    uintptr_t mat[3] = {};                 // palette, wolf picture, reel strip
    std::vector<uintptr_t> meshes;         // one per kAutParts entry
} g_shared;

void keep(Il2CppObject* o) {
    if (!o || !api.hide_flags) return;
    int32_t f = kDontUnload;
    void* a[] = {&f};
    il2::invoke(api.hide_flags, o, a);
}

Il2CppObject* new_array(Il2CppClass* element, size_t n) {
    return il2::il2cpp_array_new ? il2::il2cpp_array_new(element, n) : nullptr;
}

// rows top-down, 3 or 4 channels -> a Texture2D (Unity's rows bottom-up)
Il2CppObject* make_texture(const unsigned char* px, int w, int h, int channels, bool point) {
    Il2CppObject* tex = il2::il2cpp_object_new(api.texture_class);
    if (!tex) return nullptr;
    int32_t iw = w, ih = h, fmt = kRGBA32;
    bool mips = false;
    void* ca[] = {&iw, &ih, &fmt, &mips};
    il2::invoke(api.tex_ctor, tex, ca);
    int32_t zero = 0;
    void* ga[] = {&zero};
    Il2CppObject* pixels = il2::invoke(api.tex_get_pixels, tex, ga);
    if (il2::array_length(pixels) != static_cast<size_t>(w) * h) return nullptr;
    uint8_t* dst = reinterpret_cast<uint8_t*>(pixels) + kArrayData;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* s = px + (static_cast<size_t>(y) * w + x) * channels;
            uint8_t* d = dst + (static_cast<size_t>(h - 1 - y) * w + x) * 4;
            d[0] = s[0], d[1] = s[1], d[2] = s[2], d[3] = channels == 4 ? s[3] : 255;
        }
    void* sa[] = {pixels, &zero};
    il2::invoke(api.tex_set_pixels, tex, sa);
    if (point && api.tex_filter) {
        int32_t mode = 0;  // FilterMode.Point
        void* fa[] = {&mode};
        il2::invoke(api.tex_filter, tex, fa);
    }
    bool update_mips = false, unreadable = true;
    void* aa[] = {&update_mips, &unreadable};
    il2::invoke(api.tex_apply, tex, aa);
    keep(tex);
    return tex;
}

Il2CppObject* make_material(Il2CppObject* shader, Il2CppObject* tex) {
    Il2CppObject* mat = il2::il2cpp_object_new(api.material_class);
    void* ca[] = {shader};
    il2::invoke(api.material_ctor, mat, ca);
    if (tex) {
        void* ta[] = {tex};
        il2::invoke(api.material_set_main, mat, ta);
    }
    keep(mat);
    return mat;
}

Il2CppObject* make_mesh(const AutPart& p) {
    Il2CppObject* mesh = il2::il2cpp_object_new(api.mesh_class);
    il2::invoke(api.mesh_ctor, mesh);
    Il2CppObject* verts = new_array(api.vec3_class, p.vn);
    Il2CppObject* uvs = new_array(api.vec2_class, p.vn);
    Il2CppObject* tris = new_array(api.int_class, p.in);
    if (!verts || !uvs || !tris) return nullptr;
    memcpy(reinterpret_cast<char*>(verts) + kArrayData, kAutPos + static_cast<size_t>(p.v0) * 3, sizeof(float) * 3 * p.vn);
    memcpy(reinterpret_cast<char*>(uvs) + kArrayData, kAutUV + static_cast<size_t>(p.v0) * 2, sizeof(float) * 2 * p.vn);
    auto* t = reinterpret_cast<int32_t*>(reinterpret_cast<char*>(tris) + kArrayData);
    for (int i = 0; i < p.in; ++i) t[i] = kAutIdx[p.i0 + i];  // (each part's own, from 0)
    void* va[] = {verts};
    il2::invoke(api.mesh_vertices, mesh, va);
    void* ua[] = {uvs};
    il2::invoke(api.mesh_uv, mesh, ua);
    void* ta[] = {tris};
    il2::invoke(api.mesh_triangles, mesh, ta);
    il2::invoke(api.mesh_normals, mesh);
    il2::invoke(api.mesh_bounds, mesh);
    keep(mesh);
    return mesh;
}

bool build_shared() {
    if (g_shared.tried) return g_shared.ok;
    g_shared.tried = true;
    Il2CppObject* shader = nullptr;
    for (const char* name : {"Shader Forge/TLD_StandardDiffuse", "Standard", "Legacy Shaders/Diffuse"}) {
        void* a[] = {il2::il2cpp_string_new(name)};
        shader = il2::invoke(api.shader_find, nullptr, a);
        if (shader) {
            logx::info("automat: shader '%s'", name);
            break;
        }
    }
    if (!shader) {
        logx::warn("automat: no shader found; the machine can't be shown");
        return false;
    }
    Il2CppObject* tex[3] = {make_texture(kAutPal, kAutPalW, kAutPalH, 3, true),
                            make_texture(kAutWolf, kAutWolfW, kAutWolfH, 3, false),
                            make_texture(kAutReel, kAutReelW, kAutReelH, 3, false)};
    for (int i = 0; i < 3; ++i) {
        if (!tex[i]) {
            logx::warn("automat: texture %d couldn't be made", i);
            return false;
        }
        g_shared.mat[i] = hold(make_material(shader, tex[i]));
    }
    for (const AutPart& p : kAutParts) {
        Il2CppObject* m = make_mesh(p);
        if (!m) {
            logx::warn("automat: the part %s couldn't be made", p.name);
            return false;
        }
        g_shared.meshes.push_back(hold(m));
    }
    g_shared.ok = true;
    logx::info("automat: model ready (%zu parts, 3 textures)", g_shared.meshes.size());
    return true;
}

// --- machines -------------------------------------------------------------------------------------------------------
// Co-op: the host's game keeps every machine of every scene (its file automat_test.txt) and decides each spin; the
// others ask (Msg::Automat) and show what the host says. Alone, our game is its own host.

enum Op : uint8_t { OpList = 1, OpWant = 2, OpPlace = 3, OpRemove = 4, OpSpinWant = 5, OpSpin = 6, OpCondition = 7, OpBusy = 8 };
constexpr size_t kSceneMax = 64;
constexpr double kBusyMs = 3600;  // a spin, as the host counts it (the reels take 3.2 s)

struct Rec {  // the host's record of one machine
    uint32_t id = 0;
    Vector3 pos{};
    float yaw = 0, condition = 1.0f;
    double busy_until = 0;
};
std::map<std::string, std::vector<Rec>> g_store;  // scene -> machines (host / alone)
uint32_t g_next_id = 1;

struct Machine {  // one shown in our scene
    uint32_t id = 0;
    uintptr_t root = 0;
    uintptr_t reel[3] = {}, crank = 0, light = 0;
    Vector3 pos{};
    float yaw = 0;
    float condition = 1.0f;   // 0..1
    bool spinning = false;
    bool jammed = false;
    double spin_start = 0, result_at = 0;
    int stop[3] = {};         // the strip cell each reel stops on
    bool ours = false;        // we pulled the crank: we're told the result
    std::string outcome;
    int light_mode = 0;       // 0 off, 1 spinning (blinks), 2 win (on), 3 jackpot (fast blinks)
    double light_until = 0;
};
std::vector<Machine> g_machines;
std::string g_scene;
double g_spawn_at = 0, g_want_at = 0;
bool g_listed = false;    // a client: the host's list for our scene came
std::mt19937 g_rng{std::random_device{}()};
std::string g_forced;     // "automat wynik ..."

bool connected() { return netrt::status().connected; }
bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}
bool authority() { return !connected() || hosting(); }  // decides (host, or alone)
uint8_t self_id() { return netrt::status().self_id; }

std::filesystem::path save_file() { return paths::instance_dir() / L"automat_test.txt"; }

void load_store() {
    g_store.clear();
    std::ifstream in(save_file());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream s(line);
        std::string scene;
        Rec r;
        if (std::getline(s, scene, '|') && (s >> r.pos.x >> r.pos.y >> r.pos.z >> r.yaw >> r.condition)) {
            if (!(s >> r.id) || !r.id) r.id = g_next_id;
            g_next_id = std::max(g_next_id, r.id + 1);
            g_store[scene].push_back(r);
        }
    }
}

void save_store() {
    std::ofstream out(save_file(), std::ios::trunc);
    for (const auto& [scene, recs] : g_store)
        for (const Rec& r : recs)
            out << scene << '|' << r.pos.x << ' ' << r.pos.y << ' ' << r.pos.z << ' ' << r.yaw << ' ' << r.condition << ' '
                << r.id << '\n';
}

Rec* find_rec(const std::string& scene, uint32_t id) {
    auto it = g_store.find(scene);
    if (it == g_store.end()) return nullptr;
    for (Rec& r : it->second)
        if (r.id == id) return &r;
    return nullptr;
}

Il2CppObject* tf_of(Il2CppObject* go) { return go ? il2::invoke(api.go_tf, go) : nullptr; }

void set_local(Il2CppObject* tf, const Vector3* pos, const Vector3* euler) {
    if (pos) {
        void* a[] = {const_cast<Vector3*>(pos)};
        il2::invoke(api.tf_set_local_pos, tf, a);
    }
    if (euler) {
        void* a[] = {const_cast<Vector3*>(euler)};
        il2::invoke(api.tf_set_local_euler, tf, a);
    }
}

Il2CppObject* new_go(const char* name) {
    Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
    void* a[] = {il2::il2cpp_string_new(name)};
    il2::invoke(api.go_ctor, go, a);
    return go;
}

Il2CppObject* add(Il2CppObject* go, Il2CppObject* type) {
    void* a[] = {type};
    return il2::invoke(api.go_add, go, a);
}

void parent(Il2CppObject* child, Il2CppObject* to) {
    bool stays = false;
    void* a[] = {tf_of(to), &stays};
    il2::invoke(api.tf_set_parent, tf_of(child), a);
}

float strip_angle(int cell) {  // the reel's X rotation that puts that cell in the window's middle
    return (0.5f - (static_cast<float>(cell) + 0.5f) / kAutReelCells) * 360.0f;
}

bool spawn(Machine& m) {
    if (!build_shared()) return false;
    Il2CppObject* root = new_go(kName);
    if (!root) return false;
    int32_t layer = kLayerInteractive;
    void* la[] = {&layer};
    il2::invoke(api.go_set_layer, root, la);
    if (Il2CppObject* box = add(root, api.t_box)) {
        Vector3 size{0.42f, 0.66f, 0.38f}, center{0, 0.33f, 0};
        void* sa[] = {&size};
        il2::invoke(api.box_size, box, sa);
        void* ca[] = {&center};
        il2::invoke(api.box_center, box, ca);
    }
    for (size_t i = 0; i < std::size(kAutParts); ++i) {
        const AutPart& p = kAutParts[i];
        Il2CppObject* go = new_go(p.name);
        parent(go, root);
        Vector3 at{p.x, p.y, p.z};
        set_local(tf_of(go), &at, nullptr);
        Il2CppObject* filter = add(go, api.t_mesh_filter);
        Il2CppObject* renderer = add(go, api.t_mesh_renderer);
        void* ma[] = {target(g_shared.meshes[i])};
        il2::invoke(api.filter_set_mesh, filter, ma);
        void* ra[] = {target(g_shared.mat[p.tex])};
        il2::invoke(api.renderer_set_material, renderer, ra);
        const std::string name = p.name;
        if (name == "Reel_1" || name == "Reel_2" || name == "Reel_3") {  // (not Reel_Dividers: it stays put)
            const int r = std::clamp(name.back() - '1', 0, 2);
            m.reel[r] = hold(tf_of(go));
            m.stop[r] = static_cast<int>(g_rng() % kAutReelCells);
            Vector3 e{strip_angle(m.stop[r]), 0, 0};
            set_local(tf_of(go), nullptr, &e);
        } else if (name == "Crank") {
            m.crank = hold(tf_of(go));
        } else if (name == "Bulb") {
            Il2CppObject* lgo = new_go("MOD_TLDCoopAutomatLight");
            parent(lgo, go);
            Vector3 up{0, 0.04f, 0};
            set_local(tf_of(lgo), &up, nullptr);
            if (Il2CppObject* light = add(lgo, api.t_light)) {
                Color c{1.0f, 0.72f, 0.38f, 1};
                float range = 3.0f, intensity = 1.6f;
                void* c1[] = {&c};
                il2::invoke(api.light_color, light, c1);
                if (api.light_range) {
                    void* c2[] = {&range};
                    il2::invoke(api.light_range, light, c2);
                }
                if (api.light_intensity) {
                    void* c3[] = {&intensity};
                    il2::invoke(api.light_intensity, light, c3);
                }
                bool off = false;
                void* c4[] = {&off};
                il2::invoke(api.behaviour_enabled, light, c4);
                m.light = hold(light);
            }
        }
    }
    void* pa[] = {&m.pos};
    il2::invoke(api.tf_set_pos, tf_of(root), pa);
    Vector3 e{0, m.yaw, 0};
    void* ea[] = {&e};
    il2::invoke(api.tf_set_euler, tf_of(root), ea);
    m.root = hold(root);
    return true;
}

void forget(Machine& m) {
    for (uintptr_t* h : {&m.root, &m.reel[0], &m.reel[1], &m.reel[2], &m.crank, &m.light}) drop(*h);
}

void despawn(Machine& m) {
    if (Il2CppObject* root = target(m.root); alive(root)) {
        void* a[] = {root};
        il2::invoke(api.destroy, nullptr, a);
    }
    forget(m);
}

void clear_scene() {
    for (Machine& m : g_machines) forget(m);  // (the scene's objects go with it)
    g_machines.clear();
}

Machine* shown(uint32_t id) {
    for (Machine& m : g_machines)
        if (m.id == id) return &m;
    return nullptr;
}

// Our scene's machines as the host has them: new ones made, gone ones removed, moved ones made again.
void show_list(const std::vector<Rec>& recs) {
    for (size_t i = 0; i < g_machines.size();) {
        Machine& m = g_machines[i];
        const auto it = std::find_if(recs.begin(), recs.end(), [&](const Rec& r) { return r.id == m.id; });
        const bool moved = it != recs.end() &&
                           (std::fabs(it->pos.x - m.pos.x) + std::fabs(it->pos.y - m.pos.y) + std::fabs(it->pos.z - m.pos.z) > 0.01f ||
                            std::fabs(it->yaw - m.yaw) > 0.5f);
        if (it == recs.end() || moved) {
            despawn(m);
            g_machines.erase(g_machines.begin() + static_cast<ptrdiff_t>(i));
            continue;
        }
        m.condition = it->condition;
        ++i;
    }
    int made = 0;
    for (const Rec& r : recs) {
        if (shown(r.id)) continue;
        Machine m;
        m.id = r.id, m.pos = r.pos, m.yaw = r.yaw, m.condition = r.condition;
        if (spawn(m)) g_machines.push_back(m), ++made;
    }
    if (made) logx::info("automat: %d machine(s) shown in %s (%zu here)", made, g_scene.c_str(), g_machines.size());
}

Machine* nearest(float max_m) {
    const game::PlayerState me = game::read_player();
    Machine* best = nullptr;
    float best_d = max_m * max_m;
    for (Machine& m : g_machines) {
        const float dx = m.pos.x - me.position.x, dy = m.pos.y - me.position.y, dz = m.pos.z - me.position.z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (d <= best_d) best_d = d, best = &m;
    }
    return best;
}

// --- the network ----------------------------------------------------------------------------------------------------

net::Writer message(Op op, const std::string& scene) {
    net::Writer w(net::Msg::Automat, 512);
    w.put<uint8_t>(op);
    w.put_blob(scene, kSceneMax);
    return w;
}

// Host: a scene's list, to one friend (to) or everyone (to = -1); and to our own scene's view.
void send_list(const std::string& scene, int to = -1) {
    const auto it = g_store.find(scene);
    static const std::vector<Rec> none;
    const std::vector<Rec>& recs = it == g_store.end() ? none : it->second;
    if (scene == g_scene) show_list(recs);
    if (!connected() || !hosting()) return;
    net::Writer w = message(OpList, scene);
    const uint8_t n = static_cast<uint8_t>(std::min<size_t>(recs.size(), 32));
    w.put<uint8_t>(n);
    for (uint8_t i = 0; i < n; ++i) {
        const Rec& r = recs[i];
        w.put<uint32_t>(r.id);
        w.put<float>(r.pos.x), w.put<float>(r.pos.y), w.put<float>(r.pos.z);
        w.put<float>(r.yaw), w.put<float>(r.condition);
    }
    if (to >= 0) netrt::send_packet_to(static_cast<uint8_t>(to), w, true);
    else netrt::send_packet(w, true);
}

// --- the game: the spin and its result ------------------------------------------------------------------------------

constexpr float kWearPerSpin = 0.03f;

float jam_chance(float condition) {  // 2% at full condition, 40% at 10%, more below
    const float c = std::clamp(condition, 0.0f, 1.0f);
    return c >= 0.1f ? 0.02f + (1.0f - c) / 0.9f * 0.38f : 0.4f + (0.1f - c) * 3.0f;
}

int symbol_at(int cell) { return kAutStrip[((cell % kAutReelCells) + kAutReelCells) % kAutReelCells]; }

int find_cell(int symbol) {  // a random cell showing that symbol
    std::vector<int> cells;
    for (int i = 0; i < kAutReelCells; ++i)
        if (kAutStrip[i] == symbol) cells.push_back(i);
    return cells.empty() ? 0 : cells[g_rng() % cells.size()];
}

// The three middle symbols' outcome: 5 jackpot, 3, 2, 1 (back), 0 (lost)
int multiplier(int a, int b, int c) {
    if (b == Skull) return 0;
    if (a == Gold && b == Gold && c == Gold) return 5;
    if (a == b && b == c) return 3;
    if ((a == b && a != Skull) || (b == c && b != Skull) || (a == c && a != Skull)) return 2;
    return 1;
}

std::string outcome_text(const int stop[3], bool jam) {
    if (jam) return "Automat się zaciął! Stawka przepadła";
    const int x = multiplier(symbol_at(stop[0]), symbol_at(stop[1]), symbol_at(stop[2]));
    return x == 5 ? "JACKPOT! ×5" : x == 3 ? "Wygrana ×3!" : x == 2 ? "Wygrana ×2!" : x == 1 ? "Zwrot stawki" : "Przegrana";
}

// Everyone: a spin the host decided, shown on our machine (we're told the result if we pulled the crank).
void show_spin(uint32_t id, const int stop[3], bool jam, float condition, uint8_t spinner) {
    Machine* m = shown(id);
    if (!m) return;
    const double now = now_ms();
    for (int r = 0; r < 3; ++r) m->stop[r] = std::clamp(stop[r], 0, kAutReelCells - 1);
    m->jammed = jam;
    m->condition = condition;
    m->spinning = true;
    m->spin_start = now;
    m->result_at = now + (jam ? 1800 : 3200);
    m->ours = spinner == self_id();
    m->outcome = outcome_text(m->stop, jam);
    m->light_mode = 1;
}

// Host / alone: a spin of machine `id` in `scene`, asked by player `spinner`
void decide_spin(const std::string& scene, uint32_t id, const std::string& forced, uint8_t spinner) {
    Rec* r = find_rec(scene, id);
    if (!r) return;
    const double now = now_ms();
    auto busy = [&](const char* why) {
        logx::info("automat: #%u: player %u's spin refused (%s)", id, spinner, why);
        if (spinner == self_id()) {
            coop_notices::tell(r->condition <= 0 ? "Automat jest zepsuty — trzeba go naprawić płytką elektroniczną"
                                                 : "Automat jest zajęty");
        } else if (connected()) {
            net::Writer w = message(OpBusy, scene);
            w.put<uint32_t>(id);
            w.put<uint8_t>(r->condition <= 0 ? 1 : 0);
            netrt::send_packet_to(spinner, w, true);
        }
    };
    if (now < r->busy_until) return busy("busy");
    if (r->condition <= 0) return busy("broken");
    const bool jam = forced == "zaciecie" || (forced.empty() && std::uniform_real_distribution<float>(0, 1)(g_rng) < jam_chance(r->condition));
    int stop[3];
    for (int& s : stop) s = static_cast<int>(g_rng() % kAutReelCells);
    auto set = [&](int a, int b, int c) { stop[0] = find_cell(a), stop[1] = find_cell(b), stop[2] = find_cell(c); };
    if (forced == "jackpot") set(Gold, Gold, Gold);
    else if (forced == "trojka") set(Wolf, Wolf, Wolf);
    else if (forced == "para") set(Meat, Meat, Fire);
    else if (forced == "zwrot") set(Wolf, Meat, Fire);
    else if (forced == "czaszka") set(Wolf, Skull, Fire);
    r->condition = std::max(0.0f, r->condition - kWearPerSpin);
    r->busy_until = now + kBusyMs;
    save_store();
    logx::info("automat: #%u spin for player %u: %s | %s | %s%s -> %s; condition %.0f%%", id, spinner,
               kSymbolName[symbol_at(stop[0])], kSymbolName[symbol_at(stop[1])], kSymbolName[symbol_at(stop[2])],
               jam ? " (jammed)" : "", outcome_text(stop, jam).c_str(), r->condition * 100);
    if (scene == g_scene) show_spin(id, stop, jam, r->condition, spinner);
    if (connected() && hosting()) {
        net::Writer w = message(OpSpin, scene);
        w.put<uint32_t>(id);
        for (int s : stop) w.put<uint8_t>(static_cast<uint8_t>(s));
        w.put<uint8_t>(jam ? 1 : 0);
        w.put<float>(r->condition);
        w.put<uint8_t>(spinner);
        netrt::send_packet(w, true);
    }
}

void decide_place(const std::string& scene, const Vector3& pos, float yaw) {
    Rec r;
    r.id = g_next_id++;
    r.pos = pos, r.yaw = yaw;
    g_store[scene].push_back(r);
    save_store();
    logx::info("automat: #%u placed in %s at (%.2f, %.2f, %.2f)", r.id, scene.c_str(), pos.x, pos.y, pos.z);
    send_list(scene);
}

void decide_remove(const std::string& scene, uint32_t id) {
    auto it = g_store.find(scene);
    if (it == g_store.end()) return;
    auto& v = it->second;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Rec& r) { return r.id == id; }), v.end());
    save_store();
    logx::info("automat: #%u removed from %s", id, scene.c_str());
    send_list(scene);
}

void decide_condition(const std::string& scene, uint32_t id, float condition) {
    if (Rec* r = find_rec(scene, id)) {
        r->condition = std::clamp(condition, 0.0f, 1.0f);
        save_store();
        send_list(scene);
    }
}

void receive() {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Automat, in);
    const bool host = hosting();
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        const auto op = static_cast<Op>(r.get<uint8_t>());
        const std::string scene = r.get_blob(kSceneMax);
        if (!r.ok()) continue;
        switch (op) {
            case OpWant:
                if (host) send_list(scene, p.from);
                break;
            case OpPlace: {
                Vector3 pos{r.get<float>(), r.get<float>(), r.get<float>()};
                const float yaw = r.get<float>();
                if (host && r.ok()) decide_place(scene, pos, yaw);
                break;
            }
            case OpRemove: {
                const uint32_t id = r.get<uint32_t>();
                if (host && r.ok()) decide_remove(scene, id);
                break;
            }
            case OpCondition: {
                const uint32_t id = r.get<uint32_t>();
                const float c = r.get<float>();
                if (host && r.ok()) decide_condition(scene, id, c);
                break;
            }
            case OpSpinWant: {
                const uint32_t id = r.get<uint32_t>();
                const std::string forced = r.get_blob(16);
                if (host && r.ok()) decide_spin(scene, id, forced, p.from);
                break;
            }
            case OpList: {
                if (host || scene != g_scene) break;
                const uint8_t n = r.get<uint8_t>();
                std::vector<Rec> recs;
                for (uint8_t i = 0; i < n && r.ok(); ++i) {
                    Rec x;
                    x.id = r.get<uint32_t>();
                    x.pos = Vector3{r.get<float>(), r.get<float>(), r.get<float>()};
                    x.yaw = r.get<float>();
                    x.condition = r.get<float>();
                    recs.push_back(x);
                }
                if (!r.ok()) break;
                g_listed = true;
                show_list(recs);
                break;
            }
            case OpSpin: {
                if (host || scene != g_scene) break;
                const uint32_t id = r.get<uint32_t>();
                int stop[3];
                for (int& s : stop) s = r.get<uint8_t>();
                const bool jam = r.get<uint8_t>() != 0;
                const float c = r.get<float>();
                const uint8_t spinner = r.get<uint8_t>();
                if (r.ok()) show_spin(id, stop, jam, c, spinner);
                break;
            }
            case OpBusy: {
                r.get<uint32_t>();
                const bool broken = r.get<uint8_t>() != 0;
                if (r.ok()) coop_notices::tell(broken ? "Automat jest zepsuty — trzeba go naprawić płytką elektroniczną" : "Automat jest zajęty");
                break;
            }
            default:
                break;
        }
    }
}

// Asks for, or does, a change: alone or as the host it's done here; a friend asks the host.
void ask_spin(Machine& m) {
    if (m.spinning) return coop_notices::tell("Automat jest zajęty");
    if (m.condition <= 0) return coop_notices::tell("Automat jest zepsuty — trzeba go naprawić płytką elektroniczną");
    const std::string forced = g_forced;
    g_forced.clear();
    if (authority()) return decide_spin(g_scene, m.id, forced, self_id());
    net::Writer w = message(OpSpinWant, g_scene);
    w.put<uint32_t>(m.id);
    w.put_blob(forced, 16);
    netrt::send_packet(w, true);
}

// The angle of reel r at a time: fast, then easing into its stop (value and speed continuous)
float reel_angle(const Machine& m, int r, double now) {
    const double stop_at = m.spin_start + (m.jammed ? 900.0 + 250.0 * r : 1600.0 + 700.0 * r);
    const float target = strip_angle(m.stop[r]) - 360.0f * 4;  // whole turns before it
    const float x = static_cast<float>((stop_at - now) / 1000.0);
    if (x <= 0) return strip_angle(m.stop[r]);
    const float speed = 900.0f;  // degrees a second
    const float f = x >= 0.3f ? speed * (x - 0.15f) : speed / 0.6f * x * x;
    return target - f;
}

void set_light(Machine& m, bool on) {
    Il2CppObject* l = target(m.light);
    if (!alive(l)) return;
    void* a[] = {&on};
    il2::invoke(api.behaviour_enabled, l, a);
}

void animate(Machine& m, double now) {
    if (m.spinning) {
        for (int r = 0; r < 3; ++r) {
            Il2CppObject* tf = target(m.reel[r]);
            if (!alive(tf)) continue;
            Vector3 e{reel_angle(m, r, now), 0, 0};
            set_local(tf, nullptr, &e);
        }
        if (Il2CppObject* tf = target(m.crank); alive(tf)) {
            const float t = static_cast<float>(std::clamp((now - m.spin_start) / 700.0, 0.0, 1.0));
            Vector3 e{-360.0f * t, 0, 0};
            set_local(tf, nullptr, &e);
        }
        if (now >= m.result_at) {
            m.spinning = false;
            if (m.ours) coop_notices::tell(m.outcome);  // only the one who played is told
            const bool jackpot = m.outcome.rfind("JACKPOT", 0) == 0, win = m.outcome.rfind("Wygrana", 0) == 0;
            m.light_mode = jackpot ? 3 : win ? 2 : 0;
            m.light_until = now + (jackpot ? 4000 : 3000);
        }
    }
    bool on = false;
    switch (m.light_mode) {
        case 1: on = static_cast<int>(now / 160) % 2 == 0; break;
        case 2: on = true; break;
        case 3: on = static_cast<int>(now / 90) % 2 == 0; break;
        default:  // worn: a flicker now and then
            on = m.condition < 0.25f && static_cast<int>(now / 70) % 97 == 0;
    }
    if (m.light_mode >= 2 && now > m.light_until) m.light_mode = 0;
    set_light(m, on);
}

// --- hooks: hover and use ---------------------------------------------------------------------------------------------

using HudFn = void (*)(Il2CppObject*, Il2CppObject*, const MethodInfo*);
using InteractFn = bool (*)(Il2CppObject*, const MethodInfo*);
HudFn g_hud = nullptr;
InteractFn g_interact = nullptr;

Machine* machine_under_crosshairs(Il2CppObject* pm) {
    if (!pm || g_machines.empty()) return nullptr;
    float reach = kReach;
    void* a[] = {&reach};
    Il2CppObject* go = il2::invoke(api.under_crosshairs, pm, a);
    if (!go || name_of(go) != kName) return nullptr;
    for (Machine& m : g_machines)
        if (target(m.root) == go) return &m;
    return nullptr;
}

void hud_detour(Il2CppObject* self, Il2CppObject* hud, const MethodInfo* method) {
    g_hud(self, hud, method);
    if (!g_test || !hud) return;
    Machine* m = machine_under_crosshairs(self);
    if (!m) return;
    char text[96];
    snprintf(text, sizeof(text), m->spinning ? "Automat — kręci się…, stan %d%%" : "Automat — zagraj, stan %d%%",
             static_cast<int>(std::lround(m->condition * 100)));
    int state = kHoverCanInteract;
    void* a[] = {il2::il2cpp_string_new(text), target(m->root), &state};
    il2::invoke(api.set_hover, hud, a);
}

bool interact_detour(Il2CppObject* self, const MethodInfo* method) {
    bool r = g_interact(self, method);
    if (!g_test) return r;
    Machine* m = machine_under_crosshairs(self);
    if (!m) return r;
    ask_spin(*m);
    return true;
}

bool resolve() {
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* mesh = il2::find_class(kCore, "UnityEngine", "Mesh");
    Il2CppClass* filter = il2::find_class(kCore, "UnityEngine", "MeshFilter");
    Il2CppClass* mrenderer = il2::find_class(kCore, "UnityEngine", "MeshRenderer");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* texture = il2::find_class(kCore, "UnityEngine", "Texture");
    Il2CppClass* tex2d = il2::find_class(kCore, "UnityEngine", "Texture2D");
    Il2CppClass* light = il2::find_class(kCore, "UnityEngine", "Light");
    Il2CppClass* behaviour = il2::find_class(kCore, "UnityEngine", "Behaviour");
    Il2CppClass* box = il2::find_class(kPhysics, "UnityEngine", "BoxCollider");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* hud = il2::find_class(kGame, "", "Panel_HUD");
    if (!object || !go || !tf || !mesh || !filter || !mrenderer || !renderer || !material || !shader || !tex2d || !light ||
        !box || !pm || !hud)
        return false;
    api.go_class = go;
    api.mesh_class = mesh;
    api.material_class = material;
    api.texture_class = tex2d;
    api.vec3_class = il2::find_class(kCore, "UnityEngine", "Vector3");
    api.vec2_class = il2::find_class(kCore, "UnityEngine", "Vector2");
    api.int_class = il2::find_class("mscorlib.dll", "System", "Int32");
    api.t_mesh_filter = il2::type_object(filter);
    api.t_mesh_renderer = il2::type_object(mrenderer);
    api.t_light = il2::type_object(light);
    api.t_box = il2::type_object(box);
    api.go_ctor = il2::find_method_params(go, ".ctor", {"System.String"});
    api.go_tf = il2::find_method(go, "get_transform", 0);
    api.go_add = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_set_layer = il2::find_method(go, "set_layer", 1);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.object_name = il2::find_method(object, "get_name", 0);
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    api.tf_set_parent = il2::find_method_params(tf, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    api.tf_set_local_pos = il2::find_method(tf, "set_localPosition", 1);
    api.tf_set_local_euler = il2::find_method(tf, "set_localEulerAngles", 1);
    api.tf_set_pos = il2::find_method(tf, "set_position", 1);
    api.tf_set_euler = il2::find_method(tf, "set_eulerAngles", 1);
    api.tf_get_pos = il2::find_method(tf, "get_position", 0);
    api.mesh_ctor = il2::find_method(mesh, ".ctor", 0);
    api.mesh_vertices = il2::find_method_params(mesh, "set_vertices", {"UnityEngine.Vector3[]"});
    api.mesh_uv = il2::find_method_params(mesh, "set_uv", {"UnityEngine.Vector2[]"});
    api.mesh_triangles = il2::find_method_params(mesh, "set_triangles", {"System.Int32[]"});
    api.mesh_normals = il2::find_method(mesh, "RecalculateNormals", 0);
    api.mesh_bounds = il2::find_method(mesh, "RecalculateBounds", 0);
    api.filter_set_mesh = il2::find_method(filter, "set_sharedMesh", 1);
    api.renderer_set_material = il2::find_method(renderer, "set_sharedMaterial", 1);
    api.material_ctor = il2::find_method_params(material, ".ctor", {"UnityEngine.Shader"});
    api.material_set_main = il2::find_method(material, "set_mainTexture", 1);
    api.shader_find = il2::find_method_params(shader, "Find", {"System.String"});
    api.tex_ctor = il2::find_method_params(tex2d, ".ctor", {"System.Int32", "System.Int32", "UnityEngine.TextureFormat", "System.Boolean"});
    api.tex_get_pixels = il2::find_method_params(tex2d, "GetPixels32", {"System.Int32"});
    api.tex_set_pixels = il2::find_method_params(tex2d, "SetPixels32", {"UnityEngine.Color32[]", "System.Int32"});
    api.tex_apply = il2::find_method_params(tex2d, "Apply", {"System.Boolean", "System.Boolean"});
    api.tex_filter = texture ? il2::find_method_quiet(texture, "set_filterMode", 1) : nullptr;
    api.light_color = il2::find_method(light, "set_color", 1);
    api.light_range = il2::find_method(light, "set_range", 1);
    api.light_intensity = il2::find_method(light, "set_intensity", 1);
    api.behaviour_enabled = il2::find_method(behaviour, "set_enabled", 1);
    api.box_size = il2::find_method(box, "set_size", 1);
    api.box_center = il2::find_method(box, "set_center", 1);
    api.under_crosshairs = il2::find_method_params(pm, "GetInteractiveObjectUnderCrosshairs", {"System.Single"});
    api.set_hover = il2::find_method(hud, "SetHoverText", 3);
    const bool all = api.vec3_class && api.vec2_class && api.int_class && il2::il2cpp_array_new && api.go_ctor && api.go_tf &&
                     api.go_add && api.tf_set_parent && api.tf_set_local_pos && api.tf_set_local_euler && api.tf_set_pos &&
                     api.mesh_ctor && api.mesh_vertices && api.mesh_uv && api.mesh_triangles && api.mesh_normals &&
                     api.filter_set_mesh && api.renderer_set_material && api.material_ctor && api.material_set_main &&
                     api.shader_find && api.tex_ctor && api.tex_get_pixels && api.tex_set_pixels && api.tex_apply &&
                     api.light_color && api.behaviour_enabled && api.under_crosshairs && api.set_hover;
    if (!all) {
        logx::warn("automat: some of the game's calls are missing (array_new %s)", il2::il2cpp_array_new ? "yes" : "NO");
        return false;
    }
    auto hook = [](const char* label, const MethodInfo* method, auto detour, auto* original) {
        return method && hooks::install_method(label, method, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    bool ok = hook("PlayerManager.UpdateHUDText (automat)", il2::find_method(pm, "UpdateHUDText", 1), &hud_detour, &g_hud);
    ok &= hook("PlayerManager.InteractiveObjectsProcessInteraction (automat)",
               il2::find_method(pm, "InteractiveObjectsProcessInteraction", 0), &interact_detour, &g_interact);
    return ok;
}

}  // namespace

void init() {
    const auto ini = paths::instance_dir() / L"coop.ini";
    g_test = GetPrivateProfileIntW(L"automat", L"test", 0, ini.c_str()) != 0;
    wchar_t key[16];
    GetPrivateProfileStringW(L"automat", L"key", L"F2", key, 16, ini.c_str());
    std::string k;
    for (const wchar_t* c = key; *c; ++c) k += static_cast<char>(*c);
    g_key = net::virtual_key(k);
    if (!g_test) {
        logx::info("automat: off (coop.ini [automat] test=0)");
        return;
    }
    api.ok = resolve();
    load_store();
    size_t n = 0;
    for (const auto& [scene, recs] : g_store) n += recs.size();
    logx::info("automat: TEST %s; %zu machine(s) in our file", api.ok ? "on: command.txt \"automat postaw\" places one" : "unavailable (TLD's calls missing)", n);
}

void on_frame(const std::string& scene, bool in_game) {
    if (!g_test || !api.ok) {  // (a friend's messages: dropped, never left waiting)
        static std::vector<net::Packet> stale;
        stale.clear();
        netrt::drain_packets(net::Msg::Automat, stale);
        return;
    }
    const double now = now_ms();
    static int last_authority = -1;  // alone / host / friend changed: our scene's machines from the right place
    const int auth = authority() ? 1 : 0;
    if (auth != last_authority) {
        last_authority = auth;
        for (Machine& m : g_machines) despawn(m);  // (still in the scene: removed, then made again from the new list)
        g_machines.clear();
        g_scene.clear();
    }
    if (scene != g_scene) {
        clear_scene();
        g_scene = scene;
        g_spawn_at = now + 1500;
        g_listed = false;
    }
    receive();
    if (!in_game) return;
    if (g_spawn_at > 0 && now >= g_spawn_at) {
        g_spawn_at = 0;
        if (authority()) {
            send_list(g_scene);  // (alone or host: ours; shown here)
        } else {
            net::Writer w = message(OpWant, g_scene);
            netrt::send_packet(w, true);
            g_want_at = now;
        }
    }
    if (!authority() && !g_listed && g_want_at > 0 && now - g_want_at > 5000) {  // asked again (the host loading)
        net::Writer w = message(OpWant, g_scene);
        netrt::send_packet(w, true);
        g_want_at = now;
    }
    for (Machine& m : g_machines) animate(m, now);
    // TLDCoop_DLC: the key (no command.txt needed): places one in front of us; with Shift, removes the nearest
    static bool was_down = false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool down = g_key && pid == GetCurrentProcessId() && (GetAsyncKeyState(g_key) & 0x8000) != 0;
    if (down && !was_down) command((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? "usun" : "postaw");
    was_down = down;
}

void command(const std::string& args) {
    std::istringstream in(args);
    std::string what;
    in >> what;
    if (!g_test) {
        logx::warn("dev: automat: off (coop.ini [automat] test=1 turns it on)");
        coop_notices::tell("Automat: włącz test=1 w coop.ini ([automat])");
        return;
    }
    if (!api.ok) {
        logx::warn("dev: automat: unavailable (see the log at start)");
        return;
    }
    const game::PlayerState me = game::read_player();
    if (what == "postaw" || what.empty()) {
        const float yaw = me.yaw * 3.14159265f / 180.0f;
        const Vector3 pos{me.position.x + std::sin(yaw) * 1.3f, me.position.y, me.position.z + std::cos(yaw) * 1.3f};
        const float myaw = me.yaw + 180.0f;
        if (authority()) {
            decide_place(g_scene, pos, myaw);
        } else {
            net::Writer w = message(OpPlace, g_scene);
            w.put<float>(pos.x), w.put<float>(pos.y), w.put<float>(pos.z), w.put<float>(myaw);
            netrt::send_packet(w, true);
        }
        coop_notices::tell("Automat postawiony");
    } else if (what == "usun") {
        Machine* m = nearest(4.0f);
        if (!m) return coop_notices::tell("Automat: żadnego w pobliżu");
        const uint32_t id = m->id;
        if (authority()) {
            decide_remove(g_scene, id);
        } else {
            net::Writer w = message(OpRemove, g_scene);
            w.put<uint32_t>(id);
            netrt::send_packet(w, true);
        }
        coop_notices::tell("Automat usunięty");
    } else if (what == "stan") {
        int pct = 100;
        in >> pct;
        Machine* m = nearest(4.0f);
        if (!m) return coop_notices::tell("Automat: żadnego w pobliżu");
        const float c = std::clamp(pct, 0, 100) / 100.0f;
        if (authority()) {
            decide_condition(g_scene, m->id, c);
        } else {
            net::Writer w = message(OpCondition, g_scene);
            w.put<uint32_t>(m->id);
            w.put<float>(c);
            netrt::send_packet(w, true);
        }
        logx::info("dev: automat #%u condition %d%%", m->id, pct);
    } else if (what == "wynik") {
        in >> g_forced;
        logx::info("dev: automat: the next spin: %s", g_forced.c_str());
    } else if (what == "lista") {
        for (const Machine& m : g_machines)
            logx::info("dev: automat #%u at (%.2f, %.2f, %.2f), condition %.0f%%%s", m.id, m.pos.x, m.pos.y, m.pos.z,
                       m.condition * 100, m.spinning ? ", spinning" : "");
    } else {
        logx::warn("dev: automat: postaw | usun | stan N | wynik jackpot|trojka|para|zwrot|czaszka|zaciecie | lista");
    }
}

}  // namespace slot_machine
