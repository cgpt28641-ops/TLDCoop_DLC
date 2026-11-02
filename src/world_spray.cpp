#include "world_spray.h"

#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "core.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net_runtime.h"

namespace world_spray {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr int32_t kSprayPaint = 7;  // DecalProjectorType.SprayPaint
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;

struct Vector3 {
    float x, y, z;
};
struct Color {
    float r, g, b, a;
};

struct Api {
    const MethodInfo* manager = nullptr;     // static GameManager.GetDynamicDecalsManager()
    const MethodInfo* place_down = nullptr;  // DynamicDecalsManager.PlaceDownSprayPaintDecal(DecalProjectorInstance)
    const MethodInfo* remove = nullptr;      // DynamicDecalsManager.RemoveDynamicDecal(DecalProjectorInstance)
    const MethodInfo* create = nullptr;      // DynamicDecalsManager.CreateCustomDecal(name, pos, yaw, normal, scale, type,
                                             //   indoors, projectile)
    ptrdiff_t decals = -1;                   // DynamicDecalsManager.m_DynamicDecalProjectors
    // DecalProjectorInstance
    ptrdiff_t guid = -1, name = -1, type = -1, indoors = -1, pos = -1, normal = -1, scale = -1, yaw = -1, tint = -1,
              projectile = -1, placing = -1;
} api;
bool g_ready = false;
bool g_applying = false;
std::string g_scene;
// What came for scenes we aren't in: scene -> id -> the newest word on it.
std::map<std::string, std::map<std::string, net::SprayMarkMsg>> g_kept;
std::unordered_map<uint8_t, std::string> g_friend_scene;  // host: where each friend was last frame
int g_sent = 0, g_painted = 0;

using PlaceFn = void (*)(Il2CppObject* self, Il2CppObject* decal, const MethodInfo* method);
using RemoveFn = void (*)(Il2CppObject* self, Il2CppObject* decal, const MethodInfo* method);
PlaceFn g_place = nullptr;
RemoveFn g_remove = nullptr;

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) {
    return il2::read_field<T>(o, off);
}
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) std::memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}
std::string text(Il2CppString* s) { return s ? il2::to_utf8(s) : std::string(); }

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

Il2CppObject* manager() { return api.manager ? il2::invoke(api.manager, nullptr) : nullptr; }

bool is_spray(Il2CppObject* d) { return d && get<int32_t>(d, api.type) == kSprayPaint && !get<bool>(d, api.placing); }

net::SprayMarkMsg describe(Il2CppObject* d) {
    net::SprayMarkMsg m;
    m.scene = g_scene;
    m.id = text(get<Il2CppString*>(d, api.guid));
    m.name = text(get<Il2CppString*>(d, api.name));
    const Vector3 p = get<Vector3>(d, api.pos), n = get<Vector3>(d, api.normal), s = get<Vector3>(d, api.scale);
    m.pos[0] = p.x, m.pos[1] = p.y, m.pos[2] = p.z;
    m.normal[0] = n.x, m.normal[1] = n.y, m.normal[2] = n.z;
    m.scale[0] = s.x, m.scale[1] = s.y, m.scale[2] = s.z;
    m.yaw = get<float>(d, api.yaw);
    const Color c = get<Color>(d, api.tint);
    m.colour[0] = c.r, m.colour[1] = c.g, m.colour[2] = c.b, m.colour[3] = c.a;
    m.projectile = get<int32_t>(d, api.projectile);
    m.indoors = get<bool>(d, api.indoors);
    return m;
}

void send(const net::SprayMarkMsg& m, int to = -1) {
    net::Writer w(net::Msg::SprayMark);
    net::write_spray_mark(w, m);
    if (to >= 0)
        netrt::send_packet_to(static_cast<uint8_t>(to), w, true);
    else
        netrt::send_packet(w, true);
}

std::vector<Il2CppObject*> sprays_here() {
    std::vector<Il2CppObject*> out;
    Il2CppObject* list = api.decals >= 0 ? get<Il2CppObject*>(manager(), api.decals) : nullptr;
    if (!list) return out;
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    const int32_t n = get<int32_t>(list, kListSize);
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < il2::array_length(items); ++i)
        if (Il2CppObject* d = il2::array_object_at(items, static_cast<size_t>(i)); is_spray(d)) out.push_back(d);
    return out;
}

Il2CppObject* spray_by_id(const std::string& id) {
    for (Il2CppObject* d : sprays_here())
        if (text(get<Il2CppString*>(d, api.guid)) == id) return d;
    return nullptr;
}

bool sharing() { return g_ready && !g_applying && !g_scene.empty() && netrt::status().connected; }

void place_detour(Il2CppObject* self, Il2CppObject* decal, const MethodInfo* method) {
    g_place(self, decal, method);
    if (!sharing() || !decal || get<int32_t>(decal, api.type) != kSprayPaint) return;
    const net::SprayMarkMsg m = describe(decal);
    if (m.id.empty()) return;
    send(m);
    ++g_sent;
    logx::info("spray: our marking %s (%s) at (%.1f, %.1f, %.1f) goes to the others", m.id.substr(0, 8).c_str(),
               m.name.c_str(), m.pos[0], m.pos[1], m.pos[2]);
}

void remove_detour(Il2CppObject* self, Il2CppObject* decal, const MethodInfo* method) {
    const bool tell = sharing() && is_spray(decal);
    const std::string id = tell ? text(get<Il2CppString*>(decal, api.guid)) : std::string();
    g_remove(self, decal, method);
    if (id.empty()) return;
    net::SprayMarkMsg m;
    m.scene = g_scene;
    m.id = id;
    m.gone = true;
    send(m);
    logx::info("spray: our marking %s is gone; the others wipe it too", id.substr(0, 8).c_str());
}

// The other player's word on a marking in our scene: painted as TLD paints one, under their id, or wiped.
void apply(const net::SprayMarkMsg& m) {
    Il2CppObject* mgr = manager();
    if (!mgr) return;
    Il2CppObject* have = spray_by_id(m.id);
    g_applying = true;
    if (m.gone) {
        if (have) {
            void* args[] = {have};
            il2::invoke(api.remove, mgr, args);
            logx::info("spray: the other player's marking %s is wiped here too", m.id.substr(0, 8).c_str());
        }
    } else if (!have) {
        Vector3 pos{m.pos[0], m.pos[1], m.pos[2]}, normal{m.normal[0], m.normal[1], m.normal[2]},
            scale{m.scale[0], m.scale[1], m.scale[2]};
        float yaw = m.yaw;
        int32_t type = kSprayPaint, projectile = m.projectile;
        bool indoors = m.indoors;
        void* args[] = {il2::il2cpp_string_new(m.name.c_str()), &pos, &yaw, &normal, &scale, &type, &indoors, &projectile};
        Il2CppObject* d = il2::invoke(api.create, mgr, args);
        if (d) {
            il2::store_ref(d, api.guid, il2::il2cpp_string_new(m.id.c_str()));  // (a managed reference: write barrier)
            put<Color>(d, api.tint, Color{m.colour[0], m.colour[1], m.colour[2], m.colour[3]});
            void* place_args[] = {d};
            il2::invoke(api.place_down, mgr, place_args);
            if (g_painted++ == 0 || g_painted % 10 == 0)
                logx::info("spray: the other player's marking %s (%s) is painted here (%d so far)",
                           m.id.substr(0, 8).c_str(), m.name.c_str(), g_painted);
        } else {
            logx::warn("spray: the other player's marking %s (%s) couldn't be painted here", m.id.substr(0, 8).c_str(),
                       m.name.c_str());
        }
    }
    g_applying = false;
}

void keep(const net::SprayMarkMsg& m) { g_kept[m.scene][m.id] = m; }

// Host: a friend who has just come into our scene gets every marking we have here.
void welcome_friends() {
    const net::Status st = netrt::status();
    for (const net::PlayerInfo& p : st.players) {
        const auto s = netrt::remote_state(p.id);
        const std::string there = s && s->valid ? s->scene : std::string();
        std::string& before = g_friend_scene[p.id];
        if (there != before && there == g_scene && !g_scene.empty()) {
            int n = 0;
            for (Il2CppObject* d : sprays_here()) {
                const net::SprayMarkMsg m = describe(d);
                if (m.id.empty()) continue;
                send(m, p.id);
                ++n;
            }
            if (n) logx::info("spray: player %u came into %s; sent our %d marking(s) here", p.id, g_scene.c_str(), n);
        }
        before = there;
    }
}

}  // namespace

bool init() {
    Il2CppClass* mgr = il2::find_class(kGame, "", "DynamicDecalsManager");
    Il2CppClass* decal = il2::find_class(kGame, "", "DecalProjectorInstance");
    api.manager = il2::find_method(il2::find_class(kGame, "", "GameManager"), "GetDynamicDecalsManager", 0);
    api.place_down = il2::find_method(mgr, "PlaceDownSprayPaintDecal", 1);
    api.remove = il2::find_method_params(mgr, "RemoveDynamicDecal", {"DecalProjectorInstance"});
    api.create = il2::find_method(mgr, "CreateCustomDecal", 8);
    api.decals = il2::field_offset(mgr, "m_DynamicDecalProjectors");
    api.guid = il2::field_offset(decal, "m_Guid");
    api.name = il2::field_offset(decal, "m_DecalName");
    api.type = il2::field_offset(decal, "m_DecalProjectorType");
    api.indoors = il2::field_offset(decal, "m_Indoors");
    api.pos = il2::field_offset(decal, "m_Pos");
    api.normal = il2::field_offset(decal, "m_Normal");
    api.scale = il2::field_offset(decal, "m_Scale");
    api.yaw = il2::field_offset(decal, "m_Yaw");
    api.tint = il2::field_offset(decal, "m_ColorTint");
    api.projectile = il2::field_offset(decal, "m_ProjectileType");
    api.placing = il2::field_offset(decal, "m_IsPlacing");
    g_ready = api.manager && api.place_down && api.remove && api.create && api.decals >= 0 && api.guid >= 0 &&
              api.name >= 0 && api.type >= 0 && api.pos >= 0 && api.normal >= 0 && api.scale >= 0 && api.yaw >= 0 &&
              api.tint >= 0 && api.projectile >= 0 && api.placing >= 0 && api.indoors >= 0 &&
              hooks::install_method("DynamicDecalsManager.PlaceDownSprayPaintDecal", api.place_down,
                                    reinterpret_cast<void*>(&place_detour), reinterpret_cast<void**>(&g_place)) &&
              hooks::install_method("DynamicDecalsManager.RemoveDynamicDecal", api.remove,
                                    reinterpret_cast<void*>(&remove_detour), reinterpret_cast<void**>(&g_remove));
    logx::info("spray: %s", g_ready ? "ready (spray paint markings, and so the map's, are everyone's)"
                                    : "UNAVAILABLE (TLD API missing; each player's markings are their own)");
    return g_ready;
}

void on_frame(bool in_game) {
    if (!g_ready) return;
    const std::string scene = in_game ? core::settled_scene() : std::string();
    if (scene != g_scene) {
        g_scene = scene;
        g_friend_scene.clear();
        if (const auto it = g_kept.find(g_scene); it != g_kept.end() && !g_scene.empty()) {
            for (const auto& [id, m] : it->second) apply(m);
            logx::info("spray: %zu marking change(s) kept for %s put in", it->second.size(), g_scene.c_str());
            g_kept.erase(it);
        }
    }
    std::vector<net::Packet> packets;
    netrt::drain_packets(net::Msg::SprayMark, packets);
    for (const net::Packet& p : packets) {
        net::Reader r = net::packet_body(p);
        net::SprayMarkMsg m;
        if (!net::read_spray_mark(r, m)) continue;
        if (hosting()) {
            net::Writer w(net::Msg::SprayMark);
            net::write_spray_mark(w, m);
            netrt::send_packet_except(p.from, w, true);
        }
        if (!g_scene.empty() && m.scene == g_scene)
            apply(m);
        else
            keep(m);
    }
    if (hosting() && !g_scene.empty() && netrt::status().connected) welcome_friends();
    if (!netrt::active()) g_kept.clear();
}

}  // namespace world_spray
