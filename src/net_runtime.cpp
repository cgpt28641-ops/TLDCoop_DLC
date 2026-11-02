#include "net_runtime.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include "log.h"

namespace netrt {
namespace {

// The session and its thread. Never destroyed at exit: the game exits without unloading us, and tearing the
// socket down under a running thread would crash. stop() (from the co-op menu) shuts them down cleanly instead.
// TLDCoop_DLC: each start has its own Runner (its flag, its event, its session), which its thread holds: a thread that
// doesn't stop in time keeps running on its own (leaked, never reused) instead of sharing the next session's flag.
struct Runner {
    std::atomic<bool> running{true};
    HANDLE done = nullptr;  // signalled when the network thread has left its loop
    net::Session* session = nullptr;
};
Runner* g_runner = nullptr;
net::Session* g_session = nullptr;  // g_runner->session (or null)
net::Config g_config;  // what g_session was started with
bool g_keep_offline = false;     // what keep_offline() last told the session
// TLDCoop_DLC: many modules ask status() every frame; the session's (its lock, its player list built) once per frame,
// and again after a change of ours.
uint64_t g_frame = 0;            // 0: no frames yet (every call asks the session)
uint64_t g_status_frame = ~0ull; // the frame g_status is of
net::Status g_status;
void status_changed() { g_status_frame = ~0ull; }

}  // namespace

bool start(const net::Config& config) {
    if (g_session) return true;
    auto* session = new net::Session([](const std::string& line) { logx::info("net: %s", line.c_str()); });
    if (!session->start(config)) {
        logx::error("net: could not start; co-op disabled this session");
        delete session;
        return false;
    }
    auto* runner = new Runner;
    runner->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    runner->session = session;
    if (!runner->done) {
        logx::error("net: could not start (no event); co-op disabled this session");
        session->stop();
        delete session;
        delete runner;
        return false;
    }
    g_runner = runner;
    g_session = session;
    status_changed();
    g_config = config;
    g_keep_offline = false;
    std::thread([runner] {
        while (runner->running) {
            runner->session->update();
            runner->session->wait(5);
        }
        SetEvent(runner->done);
    }).detach();
    return true;
}

void stop() {
    if (!g_session || !g_runner) return;
    Runner* runner = g_runner;
    g_runner = nullptr;
    g_session = nullptr;  // nobody here touches it again, whatever happens below
    status_changed();
    runner->running = false;
    if (WaitForSingleObject(runner->done, 2000) != WAIT_OBJECT_0) {
        // Its thread still uses the runner and the session: both leaked (the thread stops at its next loop and
        // signals an event nobody waits on; its socket stays open until the game exits).
        logx::warn("net: network thread didn't stop in time; leaking the old session");
        return;
    }
    runner->session->stop();  // says goodbye to the peer
    delete runner->session;
    CloseHandle(runner->done);
    delete runner;
    logx::info("net: stopped");
}

// Settings that need a new session (a new lobby, socket or handshake) when they change; the body doesn't.
bool same_link(const net::Config& a, const net::Config& b) {
    return a.role == b.role && a.transport == b.transport && a.bind_ip == b.bind_ip && a.host_ip == b.host_ip &&
           a.port == b.port && a.code == b.code && a.name == b.name && a.game_version == b.game_version &&
           a.content == b.content && a.content_check == b.content_check &&
           a.ask_to_join == b.ask_to_join;  // TLDCoop_DLC: (the session reads it at start)
}

bool restart(const net::Config& config) {
    if (g_session && same_link(g_config, config)) {
        // Keep the lobby and anyone already in it.
        if (config.body != g_config.body) g_session->set_body(config.body);
        g_config = config;
        logx::info("net: same connection settings; session kept");
        return true;
    }
    stop();
    return start(config);
}

bool active() { return g_session != nullptr; }

void set_body(const std::string& body) {
    g_config.body = body;
    if (g_session) g_session->set_body(body);
}

std::string body() { return g_config.body; }

void set_ready(bool ready) {
    if (g_session) g_session->set_ready(ready);
}

bool admit(uint32_t ticket) {
    status_changed();
    return g_session && g_session->admit(ticket);
}

bool turn_away(uint32_t ticket) {
    status_changed();
    return g_session && g_session->turn_away(ticket);
}

void set_local_state(const net::StateMsg& state) {
    if (g_session) g_session->set_local_state(state);
}

void cut_link(double ms) {
    if (g_session) g_session->cut_link(ms);
}

void set_world(const net::WorldMsg& world) {
    if (g_session) g_session->set_world(world);
}

std::optional<net::WorldMsg> remote_world() {
    return g_session ? g_session->remote_world() : std::nullopt;
}

void send_animals(std::vector<net::AnimalsMsg> parts) {
    if (g_session) g_session->send_animals(std::move(parts));
}

void drain_animals(std::vector<net::AnimalsMsg>& out) {
    if (g_session) g_session->drain_animals(out);
}

void send_animal_sounds(const std::vector<net::AnimalSound>& sounds) {
    if (g_session) g_session->send_animal_sounds(sounds);
}

void drain_animal_sounds(std::vector<net::AnimalSound>& out) {
    if (g_session) g_session->drain_animal_sounds(out);
}

void send_attack(const net::AttackMsg& attack) {
    if (g_session) g_session->send_attack(attack);
}

void drain_attacks(std::vector<net::AttackMsg>& out) {
    if (g_session) g_session->drain_attacks(out);
}

void send_attack_result(const net::AttackResultMsg& result) {
    if (g_session) g_session->send_attack_result(result);
}

void drain_attack_results(std::vector<net::AttackResultMsg>& out) {
    if (g_session) g_session->drain_attack_results(out);
}

void send_packet(const net::Writer& w, bool reliable) {
    if (g_session) g_session->send_packet(w, reliable);
}

void send_packet_to(uint8_t player, const net::Writer& w, bool reliable) {
    if (g_session) g_session->send_packet_to(player, w, reliable);
}

void send_packet_except(uint8_t player, const net::Writer& w, bool reliable) {
    if (g_session) g_session->send_packet_except(player, w, reliable);
}

void keep_offline(bool on) {
    status_changed();
    if (!g_session || on == g_keep_offline) return;
    g_keep_offline = on;
    g_session->keep_offline(on);
}

bool offline() {
    if (!g_session || g_session->status().connected) return false;
    return g_keep_offline || g_config.role == net::Role::Host;
}

// Several modules each take their own message types out of one session inbox (main thread only).
std::vector<net::Packet> g_packets;
// TLDCoop_DLC: the types some module drains (seen in drain_packets). Too many waiting: types nobody drains go first
// (after a while: a module may not have started draining yet), then unreliable ones (oldest first); reliable ones of
// a type someone drains (world changes, acked already) only beyond kPacketsHard, logged.
std::array<bool, 256> g_drained{};
double g_last_trim = 0;
size_t g_trim_logged = 0;
constexpr size_t kPacketsSoft = 2048, kPacketsHard = 32768;
constexpr double kUndrainedKeepMs = 30000;

void trim_packets() {
    const double now = net::now_ms();
    if (now - g_last_trim < 1000) return;
    g_last_trim = now;
    size_t undrained = 0, unreliable = 0, reliable = 0;
    g_packets.erase(std::remove_if(g_packets.begin(), g_packets.end(),
                                   [&](const net::Packet& p) {
                                       const bool drop = !g_drained[static_cast<uint8_t>(p.type)] &&
                                                         now - p.received_ms > kUndrainedKeepMs;
                                       undrained += drop;
                                       return drop;
                                   }),
                    g_packets.end());
    if (g_packets.size() > kPacketsSoft) {
        size_t excess = g_packets.size() - kPacketsSoft;
        g_packets.erase(std::remove_if(g_packets.begin(), g_packets.end(),
                                       [&](const net::Packet& p) {
                                           if (excess == 0 || p.reliable) return false;
                                           --excess;
                                           ++unreliable;
                                           return true;
                                       }),
                        g_packets.end());
    }
    if (g_packets.size() > kPacketsHard) {
        reliable = g_packets.size() - kPacketsHard;
        g_packets.erase(g_packets.begin(), g_packets.begin() + static_cast<ptrdiff_t>(reliable));
    }
    if ((undrained || unreliable || reliable) && g_trim_logged++ < 50)
        logx::warn("net: %zu received message(s) waiting; dropped %zu nobody takes, %zu unreliable, %zu reliable (oldest)",
                   g_packets.size(), undrained, unreliable, reliable);
}

void drain_packets(net::Msg type, std::vector<net::Packet>& out) {
    g_drained[static_cast<uint8_t>(type)] = true;
    if (g_session) g_session->drain_packets(g_packets);
    // (one pass, both kept in order)
    const auto ours = std::stable_partition(g_packets.begin(), g_packets.end(), [&](const net::Packet& p) { return p.type != type; });
    for (auto it = ours; it != g_packets.end(); ++it) out.push_back(std::move(*it));
    g_packets.erase(ours, g_packets.end());
    if (g_packets.size() > kPacketsSoft) trim_packets();
}

std::optional<net::Role> role() {
    return g_session ? std::optional<net::Role>(g_config.role) : std::nullopt;
}

std::optional<net::StateMsg> remote_state() {
    return g_session ? g_session->remote_state() : std::nullopt;
}

std::optional<net::StateMsg> remote_state(uint8_t player) {
    return g_session ? g_session->remote_state(player) : std::nullopt;
}

std::vector<net::StateMsg> remote_states() {
    return g_session ? g_session->remote_states() : std::vector<net::StateMsg>{};
}

void new_frame() { ++g_frame; }

net::Status status() {
    if (!g_session) return net::Status{};
    if (g_frame != 0 && g_status_frame == g_frame) return g_status;
    g_status = g_session->status();
    g_status_frame = g_frame;
    return g_status;
}

void drain_remote_states(std::vector<net::TimedState>& out) {
    if (g_session) g_session->drain_remote_states(out);
}

std::array<uint32_t, net::kMaxPlayers> g_resync{};  // resync(): per friend

std::vector<uint8_t> Together::arrived(const std::string& scene) {
    std::array<uint32_t, net::kMaxPlayers> now{};
    if (g_session && !scene.empty() && g_config.role == net::Role::Host) {
        for (const net::PlayerInfo& p : g_session->status().players) {
            if (!p.linked || !p.caught_up || p.id == 0 || p.id >= net::kMaxPlayers) continue;
            const auto r = g_session->remote_state(p.id);
            if (r && r->valid && r->scene == scene) now[p.id] = p.link;
        }
    }
    std::vector<uint8_t> out;
    for (uint8_t id = 1; id < net::kMaxPlayers; ++id) {
        if (!now[id]) continue;
        if (now[id] != link_[id] || resync_[id] != g_resync[id]) out.push_back(id);
        resync_[id] = g_resync[id];
    }
    link_ = now;
    return out;
}

void resync(uint8_t player) {
    for (uint8_t id = 1; id < net::kMaxPlayers; ++id)
        if (player == 0 || player == id) ++g_resync[id];
}

bool Newest::take(const net::Packet& p, const std::string& key, uint32_t seq) {
    if (p.from >= net::kMaxPlayers) return false;
    auto& seen = seen_[p.from];
    if (link_[p.from] != p.link) {
        seen.clear();
        link_[p.from] = p.link;
    }
    const auto last = seen.find(key);
    if (last != seen.end() && static_cast<int32_t>(seq - last->second) <= 0) return false;
    seen[key] = seq;
    return true;
}

void Newest::clear() {
    for (auto& s : seen_) s.clear();
    link_.fill(0);
}

bool Newest::empty() const {
    for (const auto& s : seen_)
        if (!s.empty()) return false;
    return true;
}

}  // namespace netrt
