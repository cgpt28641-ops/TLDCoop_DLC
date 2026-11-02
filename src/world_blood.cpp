#include "world_blood.h"

#include <cstring>
#include <string>
#include <vector>

#include "core.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net_runtime.h"
#include "wildlife_sync.h"

namespace world_blood {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr ptrdiff_t kFieldsFrom = 0x20;  // a MonoBehaviour's own fields start here (BloodTrail's state is all in them)

struct Vector3 {
    float x, y, z;
};

struct Api {
    const MethodInfo* player = nullptr;   // static GameManager.GetVpFPSPlayer()
    ptrdiff_t player_trail = -1;          // vp_FPSPlayer.m_BloodTrail
    const MethodInfo* drop = nullptr;     // BloodTrail.LeaveBloodDrop(Vector3)
    const MethodInfo* stain = nullptr;    // BloodTrail.LeaveBloodStain(Vector3, float, float)
    const MethodInfo* cluster = nullptr;  // BloodTrail.LeaveBloodCluster(Vector3, int)
    size_t trail_size = 0;                // BloodTrail's fields end here
    ptrdiff_t trail_ai = -1;              // BloodTrail.m_BaseAi (an animal's trail)
} api;
int g_animal_sent = 0;
bool g_ready = false;
int g_depth = 0;        // inside one of TLD's blood calls (one may make others: only the outer one is told)
bool g_applying = false;  // leaving the other player's blood with our trail: not ours to tell
int g_sent = 0, g_left = 0;

using DropFn = void (*)(Il2CppObject* self, Vector3 pos, const MethodInfo* method);
using StainFn = void (*)(Il2CppObject* self, Vector3 pos, float min, float max, const MethodInfo* method);
using ClusterFn = void (*)(Il2CppObject* self, Vector3 pos, int32_t count, const MethodInfo* method);
DropFn g_drop = nullptr;
StainFn g_stain = nullptr;
ClusterFn g_cluster = nullptr;

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

Il2CppObject* our_trail() {
    Il2CppObject* player = api.player ? il2::invoke(api.player, nullptr) : nullptr;
    return player && api.player_trail >= 0 ? il2::read_field<Il2CppObject*>(player, api.player_trail) : nullptr;
}

// Ours, bleeding (not an animal's), and someone to tell. TLDCoop_DLC: on the host, an animal's blood too (the host's
// game runs the animals: a wounded deer's trail was only on the host's ground, "no blood drops for the others").
bool ours_to_tell(Il2CppObject* self) {
    if (g_applying || g_depth != 1 || !self || !netrt::status().connected || core::settled_scene().empty()) return false;
    if (self == our_trail()) return true;
    Il2CppObject* ai = api.trail_ai >= 0 ? il2::read_field<Il2CppObject*>(self, api.trail_ai) : nullptr;
    if (!ai) return false;
    // TLDCoop_DLC (v45): with wildlife ownership each animal bleeds in the game that runs it (a friend's too): its
    // blood trail is told from there, so every hunter can follow it
    const bool tell = wildlife_sync::ownership() ? wildlife_sync::owned_here(ai) : hosting();
    if (tell && g_animal_sent++ == 0) logx::info("blood: an animal's blood goes to the others too");
    return tell;
}

void send(net::BloodMarkMsg m) {
    m.scene = core::settled_scene();
    net::Writer w(net::Msg::BloodMark);
    net::write_blood_mark(w, m);
    netrt::send_packet(w, true);
    if (g_sent++ == 0) logx::info("blood: our blood goes to the others (first at %.1f, %.1f, %.1f)", m.x, m.y, m.z);
}

void drop_detour(Il2CppObject* self, Vector3 pos, const MethodInfo* method) {
    ++g_depth;
    g_drop(self, pos, method);
    if (ours_to_tell(self)) send({std::string(), net::BloodKind::Drop, pos.x, pos.y, pos.z, 0, 0, 0});
    --g_depth;
}

void stain_detour(Il2CppObject* self, Vector3 pos, float min, float max, const MethodInfo* method) {
    ++g_depth;
    g_stain(self, pos, min, max, method);
    if (ours_to_tell(self)) send({std::string(), net::BloodKind::Stain, pos.x, pos.y, pos.z, min, max, 0});
    --g_depth;
}

void cluster_detour(Il2CppObject* self, Vector3 pos, int32_t count, const MethodInfo* method) {
    ++g_depth;
    g_cluster(self, pos, count, method);
    if (ours_to_tell(self)) send({std::string(), net::BloodKind::Cluster, pos.x, pos.y, pos.z, 0, 0, count});
    --g_depth;
}

// The other player's blood, left by our own trail: its state (where it last dropped, its counters) as it was after.
void leave(const net::BloodMarkMsg& m) {
    Il2CppObject* trail = our_trail();
    if (!trail || api.trail_size <= static_cast<size_t>(kFieldsFrom)) return;
    std::vector<unsigned char> saved(api.trail_size - kFieldsFrom);
    std::memcpy(saved.data(), reinterpret_cast<const char*>(trail) + kFieldsFrom, saved.size());
    Vector3 pos{m.x, m.y, m.z};
    float min = m.a, max = m.b;
    int32_t count = m.count;
    g_applying = true;
    if (m.kind == net::BloodKind::Drop) {
        void* args[] = {&pos};
        il2::invoke(api.drop, trail, args);
    } else if (m.kind == net::BloodKind::Stain) {
        void* args[] = {&pos, &min, &max};
        il2::invoke(api.stain, trail, args);
    } else {
        void* args[] = {&pos, &count};
        il2::invoke(api.cluster, trail, args);
    }
    g_applying = false;
    std::memcpy(reinterpret_cast<char*>(trail) + kFieldsFrom, saved.data(), saved.size());
    if (g_left++ == 0) logx::info("blood: the other player's blood shows here (first at %.1f, %.1f, %.1f)", m.x, m.y, m.z);
}

}  // namespace

bool init() {
    Il2CppClass* trail = il2::find_class(kGame, "", "BloodTrail");
    Il2CppClass* player = il2::find_class(kGame, "", "vp_FPSPlayer");
    api.player = il2::find_method(il2::find_class(kGame, "", "GameManager"), "GetVpFPSPlayer", 0);
    api.player_trail = il2::field_offset(player, "m_BloodTrail");
    api.drop = il2::find_method_params(trail, "LeaveBloodDrop", {"UnityEngine.Vector3"});
    api.stain = il2::find_method_params(trail, "LeaveBloodStain", {"UnityEngine.Vector3", "System.Single", "System.Single"});
    api.cluster = il2::find_method_params(trail, "LeaveBloodCluster", {"UnityEngine.Vector3", "System.Int32"});
    api.trail_ai = il2::field_offset(trail, "m_BaseAi");
    // Its state to keep: every field up to its last one (m_DropCount, an int).
    if (const ptrdiff_t last = il2::field_offset(trail, "m_DropCount"); last > kFieldsFrom)
        api.trail_size = static_cast<size_t>(last) + sizeof(int32_t);
    g_ready = api.player && api.player_trail >= 0 && api.drop && api.stain && api.cluster && api.trail_size > 0 &&
              hooks::install_method("BloodTrail.LeaveBloodDrop", api.drop, reinterpret_cast<void*>(&drop_detour),
                                    reinterpret_cast<void**>(&g_drop)) &&
              hooks::install_method("BloodTrail.LeaveBloodStain", api.stain, reinterpret_cast<void*>(&stain_detour),
                                    reinterpret_cast<void**>(&g_stain)) &&
              hooks::install_method("BloodTrail.LeaveBloodCluster", api.cluster, reinterpret_cast<void*>(&cluster_detour),
                                    reinterpret_cast<void**>(&g_cluster));
    logx::info("blood: %s", g_ready ? "ready (a bleeding player's blood is seen by everyone)"
                                    : "UNAVAILABLE (TLD API missing; each player sees only their own blood)");
    return g_ready;
}

void on_frame(bool in_game) {
    if (!g_ready) return;
    std::vector<net::Packet> packets;
    netrt::drain_packets(net::Msg::BloodMark, packets);
    const std::string& scene = core::settled_scene();
    for (const net::Packet& p : packets) {
        net::Reader r = net::packet_body(p);
        net::BloodMarkMsg m;
        if (!net::read_blood_mark(r, m)) continue;
        if (hosting()) {  // on to the other friends
            net::Writer w(net::Msg::BloodMark);
            net::write_blood_mark(w, m);
            netrt::send_packet_except(p.from, w, true);
        }
        if (in_game && !scene.empty() && m.scene == scene) leave(m);
    }
}

}  // namespace world_blood
