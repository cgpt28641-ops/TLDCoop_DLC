#include "coop_features.h"

#include <array>
#include <cmath>
#include <vector>

#include "coop_server.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"

namespace coop_features {
namespace {

constexpr double kResendMs = 5000;  // the snow drifts: everyone hears it this often

uint32_t g_own = 0;
float g_own_snow = -1;
bool g_host_known = false;
uint32_t g_host_bits = 0;
float g_host_snow = -1;
uint32_t g_sent_bits = 0xffffffff;
double g_last_send = 0;
std::array<uint32_t, net::kMaxPlayers> g_told{};

void host_frame() {
    const net::Status st = netrt::status();
    if (!st.connected) {
        g_told.fill(0);
        g_sent_bits = 0xffffffff;
        return;
    }
    std::vector<uint8_t> fresh;
    for (const net::PlayerInfo& p : st.players)
        if (p.linked && p.id < net::kMaxPlayers && g_told[p.id] != p.link) fresh.push_back(p.id);
    const double now = net::now_ms();
    const bool changed = g_own != g_sent_bits;
    if (!changed && fresh.empty() && now - g_last_send < kResendMs) return;
    net::Writer w(net::Msg::Features, 64);
    net::write_features(w, g_own, g_own_snow);
    if (changed || now - g_last_send >= kResendMs) {
        netrt::send_packet(w, true);
        if (changed)
            logx::info("features: told everyone: ambient lights %s, dynamic trees %s, seamless interiors %s",
                       g_own & net::kFeatureAmbientLights ? "on" : "off", g_own & net::kFeatureDynamicTrees ? "on" : "off",
                       g_own & net::kFeatureSeamlessInteriors ? "on" : "off");
        g_sent_bits = g_own;
        g_last_send = now;
        for (const net::PlayerInfo& p : st.players)
            if (p.linked && p.id < net::kMaxPlayers) g_told[p.id] = p.link;
        return;
    }
    for (uint8_t id : fresh) {
        netrt::send_packet_to(id, w, true);
        for (const net::PlayerInfo& p : st.players)
            if (p.id == id) g_told[id] = p.link;
        logx::info("features: told player %u", id);
    }
}

void client_frame() {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Features, in);
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        uint32_t bits = 0;
        float snow = -1;
        if (!net::read_features(r, bits, snow)) continue;
        if (!g_host_known || bits != g_host_bits)
            logx::info("features: the host has ambient lights %s, dynamic trees %s, seamless interiors %s; so do we",
                       bits & net::kFeatureAmbientLights ? "on" : "off", bits & net::kFeatureDynamicTrees ? "on" : "off",
                       bits & net::kFeatureSeamlessInteriors ? "on" : "off");
        g_host_known = true;
        g_host_bits = bits;
        g_host_snow = snow;
    }
}

// TLDCoop_DLC: on a dedicated server: the first game gives its switches (the server keeps them: they are the world's
// from then on, ours too once it says them back); the world source goes on telling how much snow its trees carry.
double g_server_sent_ms = 0;
void server_frame() {
    if (!coop_server::wants_features() && !coop_server::world_source()) return;
    const double now = net::now_ms();
    if (now - g_server_sent_ms < (coop_server::wants_features() ? 2000 : kResendMs)) return;
    g_server_sent_ms = now;
    net::Writer w(net::Msg::Features, 64);
    net::write_features(w, g_own, g_own_snow);
    netrt::send_packet(w, true);
}

bool client() {
    const auto role = netrt::role();
    return role && *role == net::Role::Client;
}

}  // namespace

void set_own(uint32_t feature, bool on) { g_own = on ? (g_own | feature) : (g_own & ~feature); }
void set_tree_snow(float snow) { g_own_snow = snow; }
bool on(uint32_t feature) {
    if (client()) return g_host_known && (g_host_bits & feature) != 0;
    return (g_own & feature) != 0;
}
bool following_host() { return client() && g_host_known; }
float host_tree_snow() { return following_host() ? g_host_snow : -1; }

void on_frame() {
    const auto role = netrt::role();
    if (role && *role == net::Role::Host) host_frame();
    if (role && *role == net::Role::Client) {
        client_frame();
        server_frame();
    } else {
        g_host_known = false;
        g_host_snow = -1;
    }
}

}  // namespace coop_features
