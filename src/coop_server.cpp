#include "coop_server.h"

#include <string>
#include <vector>

#include "log.h"
#include "net_runtime.h"

namespace coop_server {
namespace {

constexpr double kSilentMs = 8000;  // the server says it every 2 s: silent this long, it isn't one (any more)

uint8_t g_flags = 0;
std::string g_scene;  // the scene the server wants our list of (kServerWantsScene)
double g_heard_ms = 0;
uint32_t g_link = 0;  // the host's connection the flags came on

void forget(const char* why) {
    if (g_flags) logx::info("server: %s; no longer on a dedicated server", why);
    g_flags = 0;
    g_scene.clear();
    g_heard_ms = 0;
}

}  // namespace

void on_frame() {
    const auto role = netrt::role();
    if (!role || *role != net::Role::Client) return forget("co-op is off or we host");
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Server, in);
    const net::Status st = netrt::status();
    if (!st.connected) return forget("the link is down");
    uint32_t link = 0;
    for (const net::PlayerInfo& p : st.players)
        if (p.id == 0) link = p.link;
    if (link != g_link) {
        forget("linked to another host");
        g_link = link;
    }
    const double now = net::now_ms();
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        uint8_t flags = 0;
        std::string scene;
        if (!net::read_server(r, flags, &scene) || !(flags & net::kServerIs)) continue;
        if (scene != g_scene && !scene.empty()) logx::info("server: it wants our list of the loose items in %s", scene.c_str());
        g_scene = scene;
        if ((flags | net::kServerWantsScene) != (g_flags | net::kServerWantsScene))
            logx::info("server: we're on a dedicated server%s%s%s",
                       flags & net::kServerWorldSource ? "; our clock and weather are the world's" : "; the world's clock and weather come from it",
                       flags & net::kServerWantsMode ? "; it takes our experience mode and seed" : "",
                       flags & net::kServerWantsFeatures ? "; it takes our feature switches" : "");
        g_flags = flags;
        g_heard_ms = now;
    }
    if (g_flags && now - g_heard_ms > kSilentMs) forget("it went quiet");
}

bool linked() { return (g_flags & net::kServerIs) != 0; }
bool world_source() { return (g_flags & net::kServerWorldSource) != 0; }
bool wants_mode() { return (g_flags & net::kServerWantsMode) != 0; }
bool wants_features() { return (g_flags & net::kServerWantsFeatures) != 0; }
const std::string& wanted_scene() { return g_scene; }

void publish_world(const net::WorldMsg& world) {
    if (!world_source()) return;
    net::Writer w(net::Msg::World);
    net::write_world(w, world);
    if (w.ok()) netrt::send_packet(w, false);
}

}  // namespace coop_server
