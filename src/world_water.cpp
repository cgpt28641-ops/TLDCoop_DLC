#include "world_water.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core.h"
#include "game_state.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net_runtime.h"
#include "seamless_interiors.h"

namespace world_water {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr double kCheckMs = 500;  // water sources are compared with what all games last had this often
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;
constexpr ptrdiff_t kCachedPtr = 0x10;  // UnityEngine.Object.m_CachedPtr: null once the native object is gone

struct Vector3 {
    float x, y, z;
};

struct Api {
    FieldInfo* sources = nullptr;                    // static List<WaterSource> WaterSourceManager.m_WaterSources
    const MethodInfo* serialize = nullptr;           // WaterSource.Serialize()
    const MethodInfo* deserialize = nullptr;         // WaterSource.Deserialize(string)
    const MethodInfo* component_go = nullptr;        // Component.get_gameObject()
    const MethodInfo* component_transform = nullptr; // Component.get_transform()
    const MethodInfo* get_position = nullptr;        // Transform.get_position()
    const MethodInfo* guid_of = nullptr;             // static ObjectGuid.GetGuidFromGameObject(GameObject, bool)
} api;
bool g_ready = false;

double now_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) {
    return il2::read_field<T>(o, off);
}
bool alive(Il2CppObject* o) { return o && get<void*>(o, kCachedPtr) != nullptr; }
std::string text(Il2CppObject* s) { return s ? il2::to_utf8(reinterpret_cast<Il2CppString*>(s)) : std::string(); }
std::string short_id(const std::string& id) { return id.size() > 8 ? id.substr(0, 8) : id; }

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

std::vector<Il2CppObject*> all_sources() {
    std::vector<Il2CppObject*> out;
    Il2CppObject* list = nullptr;
    il2::il2cpp_field_static_get_value(api.sources, &list);
    if (!list) return out;
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    const int32_t count = get<int32_t>(list, kListSize);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i)
        if (Il2CppObject* s = il2::array_object_at(items, static_cast<size_t>(i));
            alive(s) && !seamless_interiors::sync_skips(s))  // TLDCoop_DLC: not a Seamless Interiors room still building
            out.push_back(s);
    return out;
}

// TLD's persistent id for it, or "@x,y,z" (its position to the centimetre) when it has none.
std::string id_of(Il2CppObject* source) {
    // TLDCoop_DLC: a Seamless Interiors room's: by its building and spot (the game's ids of a room's differ per game)
    if (std::string key = seamless_interiors::room_key(source); !key.empty()) return key;
    Il2CppObject* go = il2::invoke(api.component_go, source);
    if (!go) return std::string();
    bool warn = false;
    void* args[] = {go, &warn};
    std::string id = text(il2::invoke(api.guid_of, nullptr, args));
    if (!id.empty()) return id;
    Vector3 p{};
    if (!il2::invoke_value(api.get_position, il2::invoke(api.component_transform, source), p)) return std::string();
    char buf[64];
    snprintf(buf, sizeof(buf), "@%.2f,%.2f,%.2f", std::round(p.x * 100) / 100, std::round(p.y * 100) / 100,
             std::round(p.z * 100) / 100);
    return buf;
}

std::string save_of(Il2CppObject* source) { return text(il2::invoke(api.serialize, source)); }

// --- state ---------------------------------------------------------------------------------------------------------

std::string g_scene;                                  // the scene the state below is about ("" = none)
std::unordered_map<std::string, std::string> g_sig;   // id -> save text all games have
netrt::Newest g_newest;                               // id -> each player's newest change applied
uint32_t g_seq = 0;
double g_last_check = 0;
bool g_connected = false;
netrt::Together g_together;                           // host: friends in our scene (our list went to each as they came)
// Host: friends' changes in scenes we aren't in (loaded when we get there). Client: the host's list for a scene we
// aren't in yet.
std::unordered_map<std::string, std::unordered_map<std::string, net::WaterPutMsg>> g_store;
std::optional<net::WaterListMsg> g_list;

std::unordered_map<std::string, Il2CppObject*> by_id() {
    std::unordered_map<std::string, Il2CppObject*> out;
    for (Il2CppObject* s : all_sources())
        if (std::string id = id_of(s); !id.empty()) out.emplace(std::move(id), s);
    return out;
}

void baseline() {
    g_sig.clear();
    for (const auto& [id, s] : by_id()) g_sig[id] = save_of(s);
}

// --- sending -------------------------------------------------------------------------------------------------------

void send_put(const std::string& id, const std::string& save) {
    net::WaterPutMsg m;
    m.scene = g_scene;
    m.seq = ++g_seq;
    m.id = id;
    m.text = save;
    if (m.text.size() > net::kWaterTextMax) return;
    net::Writer w(net::Msg::WaterPut, net::kMaxMessage);
    net::write_water_put(w, m);
    netrt::send_packet(w, true);
}

// Host: what one friend changed goes on to the others, as a change of ours.
void pass_on(uint8_t from, net::WaterPutMsg m) {
    m.seq = ++g_seq;
    net::Writer w(net::Msg::WaterPut, net::kMaxMessage);
    net::write_water_put(w, m);
    netrt::send_packet_except(from, w, true);
}

void send_list(uint8_t to) {
    net::WaterListMsg m;
    m.scene = g_scene;
    for (const auto& [id, s] : by_id()) {
        std::string save = save_of(s);
        if (!save.empty() && save.size() <= net::kWaterTextMax) m.sources.emplace_back(id, std::move(save));
    }
    if (m.sources.empty()) return;
    net::Writer w(net::Msg::WaterList, net::kMaxMessage);
    net::write_water_list(w, m);
    netrt::send_packet_to(to, w, true);
    logx::info("water: a friend is here (%s); sent our %zu water source(s) (player %u)", g_scene.c_str(),
               m.sources.size(), to);
}

void check() {
    for (const auto& [id, s] : by_id()) {
        std::string save = save_of(s);
        if (save.empty()) continue;
        auto known = g_sig.find(id);
        if (known == g_sig.end()) {  // one that showed up since (a scene part streamed in): both games have it as is
            g_sig.emplace(id, std::move(save));
            continue;
        }
        if (known->second == save) continue;
        known->second = save;
        send_put(id, save);
        logx::info("water: water source %s changed here; told the others: %s", short_id(id).c_str(), save.c_str());
    }
}

// --- applying the others' changes ----------------------------------------------------------------------------------

bool load(Il2CppObject* source, const std::string& save) {
    void* args[] = {il2::il2cpp_string_new(save.c_str())};
    il2::invoke(api.deserialize, source, args);
    return true;
}

void apply_put(const net::WaterPutMsg& m, const char* who) {
    const auto here = by_id();
    const auto it = here.find(m.id);
    if (it == here.end()) {
        logx::info("water: %s water source %s isn't here; ignored", who, short_id(m.id).c_str());
        return;
    }
    load(it->second, m.text);
    g_sig[m.id] = save_of(it->second);
    logx::info("water: %s water source %s: %s", who, short_id(m.id).c_str(), m.text.c_str());
}

void apply_list(const net::WaterListMsg& m) {
    const auto here = by_id();
    size_t loaded = 0, missing = 0;
    for (const auto& [id, save] : m.sources) {
        const auto it = here.find(id);
        if (it == here.end()) {
            ++missing;
            continue;
        }
        load(it->second, save);
        ++loaded;
    }
    baseline();
    logx::info("water: loaded the host's %zu water source(s) in %s%s", loaded, g_scene.c_str(),
               missing ? (" (" + std::to_string(missing) + " not here)").c_str() : "");
}

void apply_stored() {
    const auto it = g_store.find(g_scene);
    if (it == g_store.end()) return;
    std::vector<net::WaterPutMsg> changes;
    for (auto& [id, m] : it->second) changes.push_back(std::move(m));
    g_store.erase(it);
    std::sort(changes.begin(), changes.end(),
              [](const auto& a, const auto& b) { return static_cast<int32_t>(a.seq - b.seq) < 0; });
    for (const net::WaterPutMsg& m : changes) apply_put(m, "while we were elsewhere a friend's");
}

void receive() {
    std::vector<net::Packet> packets;
    netrt::drain_packets(net::Msg::WaterPut, packets);
    netrt::drain_packets(net::Msg::WaterList, packets);
    for (const net::Packet& p : packets) {
        net::Reader r = net::packet_body(p);
        if (p.type == net::Msg::WaterPut) {
            net::WaterPutMsg m;
            if (!net::read_water_put(r, m)) continue;
            if (!g_newest.take(p, m.id, m.seq)) continue;  // a late older one
            if (hosting()) pass_on(p.from, m);
            if (m.scene != g_scene) {
                if (hosting()) {
                    logx::info("water: a friend's water source %s in %s changed; kept for when we get there",
                               short_id(m.id).c_str(), m.scene.c_str());
                    g_store[m.scene][m.id] = std::move(m);
                }
                continue;  // client: the host's list comes when we get there
            }
            apply_put(m, "the other player's");
        } else if (p.type == net::Msg::WaterList && !hosting()) {
            net::WaterListMsg m;
            if (!net::read_water_list(r, m)) continue;
            if (m.scene == g_scene)
                apply_list(m);
            else
                g_list = std::move(m);
        }
    }
}

}  // namespace

bool init() {
    Il2CppClass* manager = il2::find_class(kGame, "", "WaterSourceManager");
    Il2CppClass* source = il2::find_class(kGame, "", "WaterSource");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    api.sources = manager ? il2::il2cpp_class_get_field_from_name(manager, "m_WaterSources") : nullptr;
    api.serialize = il2::find_method(source, "Serialize", 0);
    api.deserialize = il2::find_method_params(source, "Deserialize", {"System.String"});
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.component_transform = il2::find_method(component, "get_transform", 0);
    api.get_position = il2::find_method(transform, "get_position", 0);
    api.guid_of = il2::find_method_params(il2::find_class(kGame, "", "ObjectGuid"), "GetGuidFromGameObject",
                                          {"UnityEngine.GameObject", "System.Boolean"});
    g_ready = api.sources && api.serialize && api.deserialize && api.component_go && api.component_transform &&
              api.get_position && api.guid_of;
    logx::info("water: %s", g_ready ? "ready (toilets and other water sources are shared)"
                                    : "UNAVAILABLE (TLD API missing; each player's water sources are their own)");
    return g_ready;
}

void on_frame(bool in_game) {
    if (!g_ready) return;
    if (!in_game && game::active_scene() == "MainMenu") g_store.clear();
    const bool connected = netrt::status().connected;
    if (!connected && !netrt::offline()) {
        if (g_connected) {
            g_scene.clear();
            g_sig.clear();
            g_newest.clear();
            g_list.reset();
            g_together.clear();
        }
        g_connected = false;
        return;
    }
    if (!connected) g_newest.clear();
    g_connected = true;
    const std::string& settled_scene = in_game ? core::settled_scene() : std::string();
    if (settled_scene != g_scene) {
        g_scene = settled_scene;
        g_sig.clear();
        g_together.clear();
        if (!g_scene.empty()) {
            baseline();
            if (hosting())
                apply_stored();
            else if (g_list && g_list->scene == g_scene)
                apply_list(*g_list);
            g_list.reset();
        }
    }
    receive();
    if (g_scene.empty()) return;
    if (const double now = now_ms(); now - g_last_check >= kCheckMs) {
        g_last_check = now;
        check();
    }
    if (!hosting()) return;
    for (uint8_t id : g_together.arrived(g_scene)) send_list(id);
}

void save_kept(std::vector<world_keep::Record>& out) {
    for (const auto& [scene, sources] : g_store) {
        for (const auto& [id, m] : sources) {
            net::Writer w(net::Msg::WaterPut, net::kMaxMessage);
            net::write_water_put(w, m);
            if (w.ok()) out.push_back(world_keep::record(world_keep::Tag::WaterChange, w));
        }
    }
}

bool load_kept(const world_keep::Record& r) {
    const net::Packet p = world_keep::packet(r);
    net::Reader body = net::packet_body(p);
    net::WaterPutMsg m;
    if (p.type != net::Msg::WaterPut || !net::read_water_put(body, m)) return false;
    g_store[m.scene][m.id] = std::move(m);
    return true;
}

}  // namespace world_water
