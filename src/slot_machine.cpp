#include "slot_machine.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
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

#include "automat_items.h"
#include "coop_notices.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/emotes.h"
#include "net/protocol.h"
#include "net/uniques.h"
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
constexpr const char* kBoxName = "MOD_TLDCoopAutomatBox";  // the stake's container (a child, not interactive)
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
    const MethodInfo *go_ctor = nullptr, *go_tf = nullptr, *go_add = nullptr, *go_set_layer = nullptr, *go_set_active = nullptr;
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

enum Op : uint8_t {
    OpList = 1, OpWant = 2, OpPlace = 3, OpRemove = 4, OpSpinWant = 5, OpSpin = 6, OpCondition = 7, OpBusy = 8,
    OpOpenWant = 9, OpOpenOk = 10, OpContents = 11  // build 2: the stake
};
// Why the host said no (OpBusy)
enum Busy : uint8_t { BusySpinning = 0, BusyBroken = 1, BusyUsed = 2, BusyLocked = 3, BusyEmpty = 4 };
const char* busy_text(uint8_t why) {
    switch (why) {
        case BusyBroken: return "Automat jest zepsuty — trzeba go naprawić płytką elektroniczną";
        case BusyUsed: return "Ktoś inny używa tego automatu";
        case BusyLocked: return "Najpierw odbierz wygraną z automatu";
        case BusyEmpty: return "Włóż stawkę do automatu";
        default: return "Automat jest zajęty";
    }
}
constexpr uint8_t kNobody = 0xFF;
constexpr size_t kSceneMax = 64;
constexpr double kBusyMs = 3600;  // a spin, as the host counts it (the reels take 3.2 s)

struct Rec {  // the host's record of one machine
    uint32_t id = 0;
    Vector3 pos{};
    float yaw = 0, condition = 1.0f;
    double busy_until = 0;
    bool locked = false;          // winnings inside: no spin until it's emptied
    std::string text;             // what's inside (Container.Serialize)
    uint8_t holder = kNobody;     // who has it open (or is spinning it)
    double held_until = 0;
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
    uintptr_t box = 0;        // its Container (the stake)
    bool locked = false;      // winnings inside
    bool open = false;        // its panel is open here
    double hold_at = 0;       // when we last told the host we still have it open
    int mult = 1;             // the spin's outcome: 0 lost, 1 back, 2, 3, 5
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
// One machine's contents: "0" or "1" (locked), then Container.Serialize's text
std::filesystem::path contents_file(uint32_t id) {
    return paths::instance_dir() / (L"automat_items_" + std::to_wstring(id) + L".txt");
}

void load_contents(Rec& r) {
    std::ifstream in(contents_file(r.id), std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (all.empty()) return;
    r.locked = all[0] == '1';
    r.text = all.substr(1);
}

void save_contents(const Rec& r) {
    std::error_code ec;
    if (r.text.empty() && !r.locked) {
        std::filesystem::remove(contents_file(r.id), ec);
        return;
    }
    std::ofstream out(contents_file(r.id), std::ios::binary | std::ios::trunc);
    out << (r.locked ? '1' : '0') << r.text;
}

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
            load_contents(r);
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

// --- the stake: a container inside the machine (build 2, Karol: "jak do skrzynki") -----------------------------------
// A Container on a child object (not on the interactive layer, so TLD never opens it by itself): our click opens TLD's
// own container panel on it; when the panel closes, what's inside is the stake (one kind, at most 10 units or 3 kg,
// nothing forbidden) and the crank is pulled. The winnings stay inside; no spin until it has been emptied. The host
// keeps every machine's contents (Container.Serialize's text) and who has it open.

constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;
constexpr size_t kItemEntry = 16;  // TLD.Gear.GearItemObject: GearItem, then its name
constexpr int kMaxUnits = 10;
constexpr float kMaxKg = 3.0f;
constexpr double kHoldMs = 60000, kHoldRefreshMs = 20000, kSpinHoldMs = 20000;

struct BoxApi {
    bool ok = false;
    Il2CppObject* t_container = nullptr;
    Il2CppClass* lstring = nullptr;
    const MethodInfo *lstring_ctor = nullptr, *find_all = nullptr, *from_kg = nullptr;
    const MethodInfo *serialize = nullptr, *deserialize = nullptr, *deserialize_add = nullptr, *destroy_all = nullptr;
    const MethodInfo *show = nullptr, *remove_gear = nullptr, *total_weight = nullptr, *destroy_gear = nullptr;
    const MethodInfo *load_panel = nullptr, *panel_enable = nullptr, *player_manager = nullptr, *to_pack = nullptr;
    Il2CppObject* panel_type = nullptr;
    ptrdiff_t panel_container = -1, lstring_id = -1;
    // Container
    ptrdiff_t items = -1, name = -1, empty_pf = -1, searched_pf = -1, capacity = -1, spawn_chance = -1, no_save = -1;
    ptrdiff_t start_inspected = -1, inspected = -1, rolled = -1, not_populated = -1, chance_empty = -1;
    ptrdiff_t min_items = -1, max_items = -1, panel_ref = -1, open_audio = -1, close_audio = -1, search_audio = -1;
    ptrdiff_t filter_name = -1, sort_name = -1, default_filter = -1, selection_mode = -1, is_corpse = -1;
    // GearItem
    ptrdiff_t stack = -1, stack_units = -1, food = -1, food_meat = -1, food_fish = -1;
    std::vector<std::pair<ptrdiff_t, const char*>> deny;  // GearItem components that make it forbidden
} box;
uintptr_t g_template = 0;  // a game container whose panel and sounds ours copies
std::vector<uint32_t> g_closed;  // machines whose panel closed (seen in Panel_Container.Enable, handled next frame)
std::map<uint32_t, std::pair<bool, std::string>> g_contents;  // this scene's machines: locked, contents (as last heard)

template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}

void set_active(Il2CppObject* go, bool on) {
    void* a[] = {&on};
    il2::invoke(api.go_set_active, go, a);
}

Il2CppObject* box_template() {
    Il2CppObject* t = target(g_template);
    if (alive(t)) return t;
    drop(g_template);
    void* a[] = {box.t_container};
    Il2CppObject* all = il2::invoke(box.find_all, nullptr, a);
    for (size_t i = 0; i < il2::array_length(all); ++i) {
        Il2CppObject* c = il2::array_object_at(all, i);
        if (!alive(c) || !il2::read_field<Il2CppObject*>(c, box.panel_ref) || il2::read_field<bool>(c, box.is_corpse) ||
            name_of(c) == kBoxName)
            continue;
        g_template = hold(c);
        logx::info("automat: the stake's container is set up like %s", name_of(c).c_str());
        return c;
    }
    return nullptr;
}

Il2CppObject* new_lstring(const char* id) {
    Il2CppObject* s = il2::il2cpp_object_new(box.lstring);
    if (!s) return nullptr;
    il2::invoke(box.lstring_ctor, s);
    il2::store_ref(s, box.lstring_id, il2::il2cpp_string_new(id));
    return s;
}

// The machine's Container, made inactive, set up, then switched on (its Awake and Start see our settings).
Il2CppObject* make_box(Il2CppObject* root) {
    if (!box.ok) return nullptr;
    Il2CppObject* tmpl = box_template();
    if (!tmpl) {
        logx::warn("automat: no game container to copy the panel from; the stake can't go in");
        return nullptr;
    }
    Il2CppObject* go = new_go(kBoxName);
    if (!go) return nullptr;
    set_active(go, false);
    parent(go, root);
    Il2CppObject* c = add(go, box.t_container);
    if (!c) {
        logx::warn("automat: the stake's container couldn't be added");
        return nullptr;
    }
    for (ptrdiff_t off : {box.panel_ref, box.open_audio, box.close_audio, box.search_audio, box.filter_name, box.sort_name,
                          box.empty_pf, box.searched_pf})
        if (off >= 0) il2::store_ref(c, off, il2::read_field<Il2CppObject*>(tmpl, off));
    put<int32_t>(c, box.default_filter, il2::read_field<int32_t>(tmpl, box.default_filter));
    put<int32_t>(c, box.selection_mode, il2::read_field<int32_t>(tmpl, box.selection_mode));
    if (Il2CppObject* n = new_lstring("Automat")) il2::store_ref(c, box.name, n);
    int64_t cap = 0;
    float kg = kMaxKg;
    void* ka[] = {&kg};
    if (box.from_kg && il2::invoke_value(box.from_kg, nullptr, cap, ka) && cap > 0) put<int64_t>(c, box.capacity, cap);
    put<float>(c, box.spawn_chance, 100.0f);
    put<float>(c, box.chance_empty, 100.0f);
    put<int32_t>(c, box.min_items, 0);
    put<int32_t>(c, box.max_items, 0);
    put<bool>(c, box.no_save, true);  // (not in TLD's save, nor in the co-op's container sync: ours)
    put<bool>(c, box.start_inspected, true);
    put<bool>(c, box.inspected, true);
    put<bool>(c, box.rolled, true);
    put<bool>(c, box.not_populated, false);
    set_active(go, true);
    return c;
}

std::vector<Il2CppObject*> gear_in(Il2CppObject* c) {
    std::vector<Il2CppObject*> out;
    Il2CppObject* list = alive(c) ? il2::read_field<Il2CppObject*>(c, box.items) : nullptr;
    if (!list) return out;
    Il2CppObject* items = il2::read_field<Il2CppObject*>(list, kListItems);
    const int32_t count = il2::read_field<int32_t>(list, kListSize);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* gi = *reinterpret_cast<Il2CppObject* const*>(reinterpret_cast<const char*>(items) + kArrayData +
                                                                   static_cast<size_t>(i) * kItemEntry);
        if (alive(gi)) out.push_back(gi);
    }
    return out;
}

std::string serialize(Il2CppObject* c) {
    auto* s = alive(c) ? reinterpret_cast<Il2CppString*>(il2::invoke(box.serialize, c)) : nullptr;
    return s ? il2::to_utf8(s) : std::string();
}

std::string base_name(Il2CppObject* gi) { return std::string(net::gear_base_name(name_of(gi))); }

int units_of(Il2CppObject* gi) {
    Il2CppObject* st = il2::read_field<Il2CppObject*>(gi, box.stack);
    return alive(st) ? std::max(1, il2::read_field<int32_t>(st, box.stack_units)) : 1;
}

// Why it can't be a stake ("" = it can): the spec's forbidden list (HANDOFF 05.11.2026)
std::string why_not(Il2CppObject* gi) {
    const std::string name = base_name(gi);
    if (net::is_unique_gear(name)) return "przedmiot fabularny";
    for (const auto& [off, what] : box.deny)
        if (alive(il2::read_field<Il2CppObject*>(gi, off))) return what;
    if (Il2CppObject* food = il2::read_field<Il2CppObject*>(gi, box.food);
        alive(food) && (il2::read_field<bool>(food, box.food_meat) || il2::read_field<bool>(food, box.food_fish)))
        return "mięso i ryby";
    std::string low = name;
    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (low == "gear_leather" || low == "gear_leatherdried") return "skóry";
    static const std::pair<const char*, const char*> kWords[] = {
        {"hide", "skóry"}, {"pelt", "skóry"}, {"gut", "jelita"}, {"skin", "ubrania ze skór"}, {"bedroll", "śpiwór"},
        {"medkit", "apteczka"}, {"firstaidkit", "apteczka"}, {"hatchet", "narzędzia"}, {"knife", "narzędzia"},
        {"prybar", "narzędzia"}, {"hacksaw", "narzędzia"}, {"hammer", "narzędzia"}, {"whetstone", "narzędzia"},
        {"sewingkit", "narzędzia"}, {"cleaningkit", "narzędzia"}, {"firestriker", "narzędzia"},
        {"magnifyinglens", "narzędzia"}, {"binoculars", "narzędzia"}, {"tldcoop_", "części automatu"}};
    for (const auto& [word, what] : kWords)
        if (low.find(word) != std::string::npos) return what;
    if (name.size() > 3 && name.compare(name.size() - 3, 3, "Key") == 0) return "klucze";
    return {};
}

void apply_contents(Machine& m, bool locked, const std::string& text) {
    m.locked = locked;
    g_contents[m.id] = {locked, text};
    if (m.open) return;  // (ours, open here: what we have is newer)
    Il2CppObject* c = target(m.box);
    if (!alive(c)) return;
    il2::invoke(box.destroy_all, c);
    if (!text.empty()) {
        void* a[] = {il2::il2cpp_string_new(text.c_str()), nullptr};
        il2::invoke(box.deserialize, c, a);
    }
}

void open_panel(uint32_t id);  // below

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
    if (Il2CppObject* collider = add(root, api.t_box)) {
        Vector3 size{0.42f, 0.66f, 0.38f}, center{0, 0.33f, 0};
        void* sa[] = {&size};
        il2::invoke(api.box_size, collider, sa);
        void* ca[] = {&center};
        il2::invoke(api.box_center, collider, ca);
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
    m.box = hold(make_box(root));
    void* pa[] = {&m.pos};
    il2::invoke(api.tf_set_pos, tf_of(root), pa);
    Vector3 e{0, m.yaw, 0};
    void* ea[] = {&e};
    il2::invoke(api.tf_set_euler, tf_of(root), ea);
    m.root = hold(root);
    return true;
}

void forget(Machine& m) {
    for (uintptr_t* h : {&m.root, &m.reel[0], &m.reel[1], &m.reel[2], &m.crank, &m.light, &m.box}) drop(*h);
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
        if (!spawn(m)) continue;
        g_machines.push_back(m), ++made;
        if (!r.text.empty() || r.locked) {
            apply_contents(g_machines.back(), r.locked, r.text);  // (the host's own)
        } else if (const auto c = g_contents.find(r.id); c != g_contents.end()) {
            const auto kept = c->second;
            apply_contents(g_machines.back(), kept.first, kept.second);  // (as the host last told us)
        }
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

net::Writer message(Op op, const std::string& scene, size_t max = 512) {
    net::Writer w(net::Msg::Automat, max);
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
    m->mult = jam ? 0 : multiplier(symbol_at(m->stop[0]), symbol_at(m->stop[1]), symbol_at(m->stop[2]));
    m->light_mode = 1;
}

// Host / alone: a "no" for player `who` (told here, or sent)
void refuse(const std::string& scene, uint32_t id, uint8_t who, uint8_t why, const char* what) {
    logx::info("automat: #%u: player %u's %s refused (%s)", id, who, what, busy_text(why));
    if (who == self_id()) {
        coop_notices::tell(busy_text(why));
    } else if (connected()) {
        net::Writer w = message(OpBusy, scene);
        w.put<uint32_t>(id);
        w.put<uint8_t>(why);
        netrt::send_packet_to(who, w, true);
    }
}

// Host / alone: player `who` wants to open machine `id` (refresh: it's still open there)
void decide_open(const std::string& scene, uint32_t id, uint8_t who, bool refresh) {
    Rec* r = find_rec(scene, id);
    if (!r) return;
    const double now = now_ms();
    if (refresh) {
        if (r->holder == who) r->held_until = now + kHoldMs;
        return;
    }
    if (now < r->busy_until) return refuse(scene, id, who, BusySpinning, "opening");
    if (r->holder != kNobody && r->holder != who && now < r->held_until) return refuse(scene, id, who, BusyUsed, "opening");
    r->holder = who;
    r->held_until = now + kHoldMs;
    if (who == self_id()) {
        if (scene == g_scene) open_panel(id);
    } else if (connected()) {
        net::Writer w = message(OpOpenOk, scene);
        w.put<uint32_t>(id);
        netrt::send_packet_to(who, w, true);
    }
}

// Host / alone: machine `id`'s new contents from player `from` (who let go of it)
void decide_contents(const std::string& scene, uint32_t id, bool locked, const std::string& text, uint8_t from) {
    Rec* r = find_rec(scene, id);
    if (!r) return;
    r->locked = locked;
    r->text = text;
    if (r->holder == from) r->holder = kNobody, r->held_until = 0;
    save_contents(*r);
    logx::info("automat: #%u contents from player %u: %zu characters%s", id, from, text.size(), locked ? ", winnings (locked)" : "");
    if (from != self_id() && scene == g_scene)
        if (Machine* m = shown(id)) apply_contents(*m, locked, text);
    if (!connected() || !hosting()) return;
    net::Writer w = message(OpContents, scene, net::kMaxMessage);
    w.put<uint32_t>(id);
    w.put<uint8_t>(locked ? 1 : 0);
    w.put_long_blob(text);
    if (from == self_id()) netrt::send_packet(w, true);
    else netrt::send_packet_except(from, w, true);
}

// Host: a friend's copy of every machine's contents in a scene (after its list)
void send_contents_to(const std::string& scene, uint8_t to) {
    const auto it = g_store.find(scene);
    if (it == g_store.end()) return;
    for (const Rec& r : it->second) {
        if (r.text.empty() && !r.locked) continue;
        net::Writer w = message(OpContents, scene, net::kMaxMessage);
        w.put<uint32_t>(r.id);
        w.put<uint8_t>(r.locked ? 1 : 0);
        w.put_long_blob(r.text);
        netrt::send_packet_to(to, w, true);
    }
}

// Host / alone: a spin of machine `id` in `scene`, asked by player `spinner`
void decide_spin(const std::string& scene, uint32_t id, const std::string& forced, uint8_t spinner) {
    Rec* r = find_rec(scene, id);
    if (!r) return;
    const double now = now_ms();
    auto busy = [&](uint8_t why) { refuse(scene, id, spinner, why, "spin"); };
    if (now < r->busy_until) return busy(BusySpinning);
    if (r->holder != kNobody && r->holder != spinner && now < r->held_until) return busy(BusyUsed);
    if (r->condition <= 0) return busy(BusyBroken);
    if (box.ok && r->locked) return busy(BusyLocked);
    if (box.ok && r->text.empty()) return busy(BusyEmpty);
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
    r->holder = spinner;  // the one who spins has it until the winnings are in (OpContents)
    r->held_until = now + kSpinHoldMs;
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
    std::error_code ec;
    std::filesystem::remove(contents_file(id), ec);
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
                if (host) send_list(scene, p.from), send_contents_to(scene, p.from);
                break;
            case OpOpenWant: {
                const uint32_t id = r.get<uint32_t>();
                const bool refresh = r.get<uint8_t>() != 0;
                if (host && r.ok()) decide_open(scene, id, p.from, refresh);
                break;
            }
            case OpOpenOk: {
                const uint32_t id = r.get<uint32_t>();
                if (!host && r.ok() && scene == g_scene) open_panel(id);
                break;
            }
            case OpContents: {
                const uint32_t id = r.get<uint32_t>();
                const bool locked = r.get<uint8_t>() != 0;
                const std::string text = r.get_long_blob(net::kMaxMessage);
                if (!r.ok()) break;
                if (host) {
                    decide_contents(scene, id, locked, text, p.from);
                } else if (scene == g_scene) {
                    if (Machine* m = shown(id)) apply_contents(*m, locked, text);
                    else g_contents[id] = {locked, text};
                }
                break;
            }
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
                const uint8_t why = r.get<uint8_t>();
                if (r.ok()) coop_notices::tell(busy_text(why));
                break;
            }
            default:
                break;
        }
    }
}

// Asks for, or does, a change: alone or as the host it's done here; a friend asks the host.
void ask_spin(Machine& m) {
    if (m.spinning) return coop_notices::tell(busy_text(BusySpinning));
    if (m.condition <= 0) return coop_notices::tell(busy_text(BusyBroken));
    const std::string forced = g_forced;
    g_forced.clear();
    if (authority()) return decide_spin(g_scene, m.id, forced, self_id());
    net::Writer w = message(OpSpinWant, g_scene);
    w.put<uint32_t>(m.id);
    w.put_blob(forced, 16);
    netrt::send_packet(w, true);
}

void ask_open(Machine& m, bool refresh) {
    if (!refresh && m.spinning) return coop_notices::tell(busy_text(BusySpinning));
    m.hold_at = now_ms();
    if (authority()) return decide_open(g_scene, m.id, self_id(), refresh);
    net::Writer w = message(OpOpenWant, g_scene);
    w.put<uint32_t>(m.id);
    w.put<uint8_t>(refresh ? 1 : 0);
    netrt::send_packet(w, true);
}

// Ours, now: the contents to the host (and so to everyone), letting go of the machine
void send_contents(Machine& m) {
    const std::string text = serialize(target(m.box));
    g_contents[m.id] = {m.locked, text};
    if (authority()) return decide_contents(g_scene, m.id, m.locked, text, self_id());
    net::Writer w = message(OpContents, g_scene, net::kMaxMessage);
    w.put<uint32_t>(m.id);
    w.put<uint8_t>(m.locked ? 1 : 0);
    w.put_long_blob(text);
    netrt::send_packet(w, true);
}

// The host said yes: TLD's container panel on the machine's stake (as TLD does: the panel loaded, then shown)
void open_panel(uint32_t id) {
    Machine* m = shown(id);
    if (!m) return;
    Il2CppObject* c = target(m->box);
    if (!alive(c)) return coop_notices::tell("Automat: nie da się go otworzyć (zobacz log)");
    if (box.load_panel && box.panel_type) {
        void* la[] = {box.panel_type};
        il2::invoke(box.load_panel, nullptr, la);
    }
    m->open = true;
    m->hold_at = now_ms();
    void* a[] = {nullptr};
    il2::invoke(box.show, c, a);
    logx::info("automat: #%u opened (%s)", id, m->locked ? "winnings inside" : "the stake goes in");
}

// Its panel closed here: forbidden items back to the pack, then the stake checked and the crank pulled.
void on_closed(Machine& m) {
    m.open = false;
    Il2CppObject* c = target(m.box);
    if (!alive(c)) return;
    std::vector<Il2CppObject*> gear = gear_in(c);
    // A circuit board put in repairs it (+25% each, as many as it needs; the rest stay in, then go back as forbidden)
    if (m.condition < 1.0f) {
        int used = 0;
        for (Il2CppObject* gi : gear) {
            if (base_name(gi) != automat_items::kBoard || m.condition + 0.25f * used >= 1.0f) continue;
            void* da[] = {gi};
            il2::invoke(box.destroy_gear, c, da);
            ++used;
        }
        if (used) {
            const float now_condition = std::min(1.0f, m.condition + 0.25f * used);
            logx::info("automat: #%u repaired with %d board(s): %.0f%% -> %.0f%%", m.id, used, m.condition * 100, now_condition * 100);
            m.condition = now_condition;
            if (authority()) {
                decide_condition(g_scene, m.id, now_condition);
            } else {
                net::Writer w = message(OpCondition, g_scene);
                w.put<uint32_t>(m.id);
                w.put<float>(now_condition);
                netrt::send_packet(w, true);
            }
            char t[64];
            snprintf(t, sizeof(t), "Automat naprawiony — stan %d%%", static_cast<int>(std::lround(now_condition * 100)));
            coop_notices::tell(t);
            gear = gear_in(c);
        }
    }
    if (m.locked) {  // the winnings: taken (all of them, or not yet)
        if (gear.empty()) m.locked = false;
        logx::info("automat: #%u closed; %s", m.id, gear.empty() ? "emptied, it can spin again" : "winnings still inside");
        return send_contents(m);
    }
    std::string back;  // forbidden: back to the pack
    Il2CppObject* pm = il2::invoke(box.player_manager, nullptr);
    for (Il2CppObject* gi : gear) {
        const std::string why = why_not(gi);
        if (why.empty() || !pm) continue;
        logx::info("automat: #%u: %s can't be a stake (%s); back to the pack", m.id, name_of(gi).c_str(), why.c_str());
        bool track = false, notify = true, to_inventory = true;
        void* pa[] = {gi, &track, &notify};
        il2::invoke(box.to_pack, pm, pa);
        void* ra[] = {gi, &to_inventory};
        il2::invoke(box.remove_gear, c, ra);
        if (back.find(why) == std::string::npos) back += (back.empty() ? "" : ", ") + why;
    }
    if (!back.empty()) coop_notices::tell("Automat tego nie przyjmuje (" + back + ") — oddane do plecaka");
    gear = gear_in(c);
    if (gear.empty()) return send_contents(m);
    std::string kind;
    int units = 0;
    bool mixed = false;
    for (Il2CppObject* gi : gear) {
        const std::string n = base_name(gi);
        if (kind.empty()) kind = n;
        else if (n != kind) mixed = true;
        units += units_of(gi);
    }
    int64_t weight = 0, limit = 0;
    float kg = kMaxKg;
    void* ka[] = {&kg};
    const bool weighed = il2::invoke_value(box.total_weight, c, weight) && box.from_kg &&
                         il2::invoke_value(box.from_kg, nullptr, limit, ka) && limit > 0;
    send_contents(m);  // (what's inside now: the stake, or what stays there)
    if (mixed) return coop_notices::tell("Automat: tylko jeden rodzaj przedmiotu naraz");
    if (units > kMaxUnits || (weighed && weight > limit))
        return coop_notices::tell("Automat: za duża stawka — najwyżej 10 sztuk albo 3 kg");
    logx::info("automat: #%u stake: %d x %s; the crank pulled", m.id, units, kind.c_str());
    ask_spin(m);
}

// We pulled the crank: the result is put in the machine here (the host decided it), then sent.
void apply_result(Machine& m) {
    Il2CppObject* c = target(m.box);
    if (!alive(c)) return;
    if (m.mult <= 0) {
        il2::invoke(box.destroy_all, c);
    } else if (m.mult >= 2) {  // the stake again, as it is (condition, wetness, units): k-1 more times
        const std::string stake = serialize(c);
        for (int i = 1; i < m.mult && !stake.empty(); ++i) {
            void* a[] = {il2::il2cpp_string_new(stake.c_str()), nullptr};
            il2::invoke(box.deserialize_add, c, a);
        }
    }
    m.locked = !gear_in(c).empty();
    logx::info("automat: #%u result x%d put in (%zu item(s) inside)", m.id, m.mult, gear_in(c).size());
    send_contents(m);
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
            if (m.ours) {  // only the one who played is told; and puts the result in
                coop_notices::tell(m.outcome);
                if (box.ok) apply_result(m);
            }
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
    const char* what = m->spinning ? "kręci się…" : !box.ok ? "zagraj" : m->locked ? "odbierz wygraną" : "otwórz";
    snprintf(text, sizeof(text), "Automat — %s, stan %d%%", what, static_cast<int>(std::lround(m->condition * 100)));
    int state = kHoverCanInteract;
    void* a[] = {il2::il2cpp_string_new(text), target(m->root), &state};
    il2::invoke(api.set_hover, hud, a);
}

bool interact_detour(Il2CppObject* self, const MethodInfo* method) {
    bool r = g_interact(self, method);
    if (!g_test) return r;
    Machine* m = machine_under_crosshairs(self);
    if (!m) return r;
    if (box.ok) ask_open(*m, false);
    else ask_spin(*m);  // (no container: build 1's click)
    return true;
}

using PanelEnableFn = void (*)(Il2CppObject*, bool, bool, Il2CppObject*, const MethodInfo*);
PanelEnableFn g_panel_enable = nullptr;
void panel_enable_detour(Il2CppObject* self, bool enable, bool reset_filter, Il2CppObject* on_closed, const MethodInfo* method) {
    if (!enable && g_test && self) {
        Il2CppObject* c = il2::read_field<Il2CppObject*>(self, box.panel_container);
        for (const Machine& m : g_machines)
            if (m.open && c && target(m.box) == c && std::find(g_closed.begin(), g_closed.end(), m.id) == g_closed.end())
                g_closed.push_back(m.id);
    }
    g_panel_enable(self, enable, reset_filter, on_closed, method);
}

bool resolve_box() {
    Il2CppClass* container = il2::find_class(kGame, "", "Container");
    Il2CppClass* panel = il2::find_class(kGame, "", "Panel_Container");
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* lstring = il2::find_class(kGame, "", "LocalizedString");
    Il2CppClass* resources = il2::find_class(kCore, "UnityEngine", "Resources");
    Il2CppClass* weight = il2::find_class("TLD.IntBackedUnit.dll", "TLD.IntBackedUnit", "ItemWeight");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* stack = il2::find_class(kGame, "", "StackableItem");
    Il2CppClass* food = il2::find_class(kGame, "", "FoodItem");
    if (!container || !panel || !gear || !lstring || !resources || !gm || !pm || !api.go_set_active) return false;
    box.t_container = il2::type_object(container);
    box.lstring = lstring;
    box.lstring_ctor = il2::find_method(lstring, ".ctor", 0);
    box.lstring_id = il2::field_offset(lstring, "m_LocalizationID");
    box.find_all = il2::find_method_params(resources, "FindObjectsOfTypeAll", {"System.Type"});
    box.from_kg = weight ? il2::find_method_quiet(weight, "FromKilograms", 1) : nullptr;
    box.serialize = il2::find_method(container, "Serialize", 0);
    box.deserialize = il2::find_method(container, "Deserialize", 2);
    box.deserialize_add = il2::find_method(container, "DeserializeAdditive", 2);
    box.destroy_all = il2::find_method(container, "DestroyAllGear", 0);
    box.show = il2::find_method(container, "ShowContainerContents", 1);
    box.remove_gear = il2::find_method_params(container, "RemoveGear", {"GearItem", "System.Boolean"});
    box.total_weight = il2::find_method(container, "GetTotalWeightKG", 0);
    box.destroy_gear = il2::find_method(container, "DestroyGear", 1);
    box.load_panel = il2::find_method_params(il2::find_class(kGame, "", "InterfaceManager"), "LoadPanel", {"System.Type"});
    box.panel_type = il2::type_object(panel);
    box.panel_enable = il2::find_method(panel, "Enable", 3);
    box.panel_container = il2::field_offset(panel, "m_Container");
    box.player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    box.to_pack = il2::find_method(pm, "AddItemToPlayerInventory", 3);
    struct F { ptrdiff_t* at; const char* name; };
    for (const F& f : {F{&box.items, "m_Items"}, F{&box.name, "m_LocalizedDisplayName"}, F{&box.empty_pf, "m_EmptyPostfixOverride"},
                       F{&box.searched_pf, "m_SearchedPostfixOverride"}, F{&box.capacity, "m_Capacity"},
                       F{&box.spawn_chance, "m_SpawnChance"}, F{&box.no_save, "m_DisableSerialization"},
                       F{&box.start_inspected, "m_StartInspected"}, F{&box.inspected, "m_Inspected"},
                       F{&box.rolled, "m_RolledSpawnChance"}, F{&box.not_populated, "m_NotPopulated"},
                       F{&box.chance_empty, "m_ChanceEmpty"}, F{&box.min_items, "m_MinRandomItems"},
                       F{&box.max_items, "m_MaxRandomItems"}, F{&box.panel_ref, "m_ContainerPanel"},
                       F{&box.open_audio, "m_OpenAudio"}, F{&box.close_audio, "m_CloseAudio"},
                       F{&box.search_audio, "m_SearchAudio"}, F{&box.filter_name, "m_ContainerFilterName"},
                       F{&box.sort_name, "m_ContainerSortName"}, F{&box.default_filter, "m_DefaultFilter"},
                       F{&box.selection_mode, "m_StartSelectionMode"}, F{&box.is_corpse, "m_IsCorpse"}})
        *f.at = il2::field_offset_quiet(container, f.name);
    box.stack = il2::field_offset_quiet(gear, "m_StackableItem");
    box.stack_units = stack ? il2::field_offset_quiet(stack, "m_Units") : -1;
    box.food = il2::field_offset_quiet(gear, "m_FoodItem");
    box.food_meat = food ? il2::field_offset_quiet(food, "m_IsMeat") : -1;
    box.food_fish = food ? il2::field_offset_quiet(food, "m_IsFish") : -1;
    static const std::pair<const char*, const char*> kDeny[] = {
        {"m_AmmoItem", "naboje"}, {"m_ArrowItem", "gotowe strzały"}, {"m_BearSpearItem", "broń"},
        {"m_BodyHarvestItem", "zwierzęta"}, {"m_BowItem", "broń"}, {"m_GunItem", "broń"},
        {"m_FlareGunRoundItem", "naboje"}, {"m_FlareItem", "flary"}, {"m_FlashlightItem", "narzędzia"},
        {"m_HandheldShortwaveItem", "narzędzia"}, {"m_KeroseneLampItem", "narzędzia"}, {"m_TorchItem", "narzędzia"},
        {"m_NarrativeCollectibleItem", "przedmiot fabularny"}, {"m_ShowOnMapItem", "mapy i notatki"},
        {"m_ResearchItem", "książki"}, {"m_ToolsItem", "narzędzia"}, {"m_ForceLockItem", "narzędzia"},
        {"m_CanOpeningItem", "narzędzia"}, {"m_Travois", "narzędzia"}, {"m_WildlifeItem", "zwierzęta"},
        {"m_InProgressCraftItem", "niedokończone"}, {"m_IceFishingHoleClearItem", "narzędzia"},
        {"m_SnareItem", "narzędzia"}, {"m_NoiseMakerItem", "broń"}, {"m_Bed", "śpiwór"},
        {"m_BlueprintItemUnlock", "mapy i notatki"}, {"m_Respirator", "narzędzia"}};
    for (const auto& [field, what] : kDeny)
        if (const ptrdiff_t off = il2::field_offset_quiet(gear, field); off >= 0) box.deny.emplace_back(off, what);
    const bool all = box.t_container && box.lstring_ctor && box.lstring_id >= 0 && box.find_all && box.serialize &&
                     box.deserialize && box.deserialize_add && box.destroy_all && box.show && box.remove_gear &&
                     box.total_weight && box.panel_type && box.panel_enable && box.panel_container >= 0 &&
                     box.player_manager && box.to_pack && box.destroy_gear && box.items >= 0 && box.name >= 0 && box.panel_ref >= 0 &&
                     box.no_save >= 0;
    if (!all) return false;
    return hooks::install_method("Panel_Container.Enable (automat)", box.panel_enable,
                                 reinterpret_cast<void*>(&panel_enable_detour), reinterpret_cast<void**>(&g_panel_enable));
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
    Il2CppClass* box_collider = il2::find_class(kPhysics, "UnityEngine", "BoxCollider");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* hud = il2::find_class(kGame, "", "Panel_HUD");
    if (!object || !go || !tf || !mesh || !filter || !mrenderer || !renderer || !material || !shader || !tex2d || !light ||
        !box_collider || !pm || !hud)
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
    api.t_box = il2::type_object(box_collider);
    api.go_ctor = il2::find_method_params(go, ".ctor", {"System.String"});
    api.go_tf = il2::find_method(go, "get_transform", 0);
    api.go_add = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_set_layer = il2::find_method(go, "set_layer", 1);
    api.go_set_active = il2::find_method(go, "SetActive", 1);
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
    api.box_size = il2::find_method(box_collider, "set_size", 1);
    api.box_center = il2::find_method(box_collider, "set_center", 1);
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
    automat_items::init(g_test);  // (always: a save with our items must load; boards from electronics only in test)
    if (!g_test) {
        logx::info("automat: off (coop.ini [automat] test=0)");
        return;
    }
    api.ok = resolve();
    box.ok = api.ok && resolve_box();
    if (api.ok && !box.ok) logx::warn("automat: the stake (container) is unavailable; a click just spins");
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
        g_contents.clear();
        g_closed.clear();
        g_scene = scene;
        g_spawn_at = now + 1500;
        g_listed = false;
    }
    receive();
    automat_items::on_frame(in_game);  // (the recipes)
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
    if (!g_closed.empty()) {  // panels closed last frame
        const std::vector<uint32_t> closed = std::move(g_closed);
        g_closed.clear();
        for (uint32_t id : closed)
            if (Machine* m = shown(id); m && m->open) on_closed(*m);
    }
    for (Machine& m : g_machines)  // still open here: still ours (the host lets go after a minute without word)
        if (m.open && now - m.hold_at > kHoldRefreshMs) ask_open(m, true);
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
    } else if (what == "daj") {  // automat daj plytka|mechanizm|automat [N]
        std::string which;
        int n = 1;
        in >> which >> n;
        if (!automat_items::give(which, n))
            logx::warn("dev: automat daj: plytka | mechanizm | automat [N] (%s)", automat_items::ready() ? "unknown name" : "items unavailable");
    } else if (what == "lista") {
        for (const Machine& m : g_machines)
            logx::info("dev: automat #%u at (%.2f, %.2f, %.2f), condition %.0f%%%s", m.id, m.pos.x, m.pos.y, m.pos.z,
                       m.condition * 100, m.spinning ? ", spinning" : "");
    } else {
        logx::warn("dev: automat: postaw | usun | stan N | wynik jackpot|trojka|para|zwrot|czaszka|zaciecie | daj plytka|mechanizm|automat [N] | lista");
    }
}

}  // namespace slot_machine
