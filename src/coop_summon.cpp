#include "coop_summon.h"

#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

#include "coop_notices.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"

namespace coop_summon {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr uint8_t kEveryone = 0xFF;

struct V3 {
    float x, y, z;
};
struct Quat {
    float x, y, z, w;
};

struct Api {
    const MethodInfo* player_tf = nullptr;       // static GameManager.GetPlayerTransform()
    const MethodInfo* player_manager = nullptr;  // static GameManager.GetPlayerManagerComponent()
    const MethodInfo* teleport = nullptr;        // PlayerManager.TeleportPlayer(Vector3, Quaternion)
    const MethodInfo* position = nullptr;        // Transform.get_position
    const MethodInfo* rotation = nullptr;        // Transform.get_rotation
    const MethodInfo* console_log = nullptr;     // static uConsole.Log(string)
} api;

std::string g_scene;

using RunFn = Il2CppObject* (*)(Il2CppString* line, const MethodInfo* method);
RunFn g_run = nullptr;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void say(const std::string& text) {
    logx::info("summon: %s", text.c_str());
    if (api.console_log) {
        void* a[] = {il2::il2cpp_string_new(text.c_str())};
        il2::invoke(api.console_log, nullptr, a);
    }
}

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

// `teleportplayer NAME|all` typed into the host's console.
void command(const std::vector<std::string>& args) {
    if (!hosting()) {
        say("teleportplayer: only the host can call players to them");
        return;
    }
    if (args.size() < 2) {
        say("teleportplayer NAME (part of the name is enough) or teleportplayer all");
        return;
    }
    Il2CppObject* tf = api.player_tf ? il2::invoke(api.player_tf, nullptr) : nullptr;
    V3 pos{};
    Quat rot{0, 0, 0, 1};
    if (!tf || !il2::invoke_value(api.position, tf, pos) || !il2::invoke_value(api.rotation, tf, rot) || g_scene.empty()) {
        say("teleportplayer: not in the game yet");
        return;
    }
    const float fx = 2 * (rot.x * rot.z + rot.w * rot.y), fz = 1 - 2 * (rot.x * rot.x + rot.y * rot.y);
    const float yaw = std::atan2(fx, fz) * 57.29578f;
    const std::string want = lower(args[1]);
    const bool all = want == "all";
    const net::Status st = netrt::status();
    std::vector<std::pair<uint8_t, std::string>> targets;
    for (const net::PlayerInfo& p : st.players)
        if (p.id != 0 && p.linked && (all || lower(p.name).find(want) != std::string::npos)) targets.emplace_back(p.id, p.name);
    if (targets.empty()) {
        std::string names;
        for (const net::PlayerInfo& p : st.players)
            if (p.id != 0) names += (names.empty() ? "" : ", ") + p.name;
        say("teleportplayer: nobody called '" + args[1] + "' is linked" + (names.empty() ? "" : " (players: " + names + ")"));
        return;
    }
    if (!all && targets.size() > 1) {
        say("teleportplayer: '" + args[1] + "' fits more than one player; type more of the name");
        return;
    }
    // Their scene: only the same one (another region would need a scene load).
    const std::vector<net::StateMsg> states = netrt::remote_states();
    for (const auto& [id, name] : targets) {
        std::string their_scene;
        for (const net::StateMsg& s : states)
            if (s.player == id) their_scene = s.scene;
        if (!their_scene.empty() && their_scene != g_scene) {
            say("teleportplayer: " + name + " is in " + their_scene + ", we're in " + g_scene + "; only within the same region");
            continue;
        }
        net::Writer w(net::Msg::Summon, 128);
        w.put_blob(g_scene, 64);
        w.put<float>(pos.x);
        w.put<float>(pos.y);
        w.put<float>(pos.z);
        w.put<float>(yaw);
        netrt::send_packet_to(id, w, true);
        char text[256];
        snprintf(text, sizeof(text), "teleportplayer: %s called to us (%.1f, %.1f, %.1f)", name.c_str(), pos.x, pos.y, pos.z);
        say(text);
    }
}

Il2CppObject* run_detour(Il2CppString* line, const MethodInfo* method) {
    if (line) {
        std::istringstream in(il2::to_utf8(line));
        std::vector<std::string> args;
        for (std::string a; in >> a;) args.push_back(a);
        if (!args.empty()) {
            const std::string cmd = lower(args[0]);
            if (cmd == "teleportplayer" || cmd == "tpplayer") {
                command(args);
                return nullptr;
            }
        }
    }
    return g_run(line, method);
}

// A friend: the host calls us to them.
void receive() {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Summon, in);
    for (const net::Packet& p : in) {
        if (hosting()) continue;  // (only the host calls)
        net::Reader r = net::packet_body(p);
        const std::string scene = r.get_blob(64);
        V3 pos{r.get<float>(), r.get<float>(), r.get<float>()};
        const float yaw = r.get<float>();
        if (!r.ok()) continue;
        if (scene != g_scene) {
            logx::info("summon: the host called us to %s, but we're in %s", scene.c_str(), g_scene.c_str());
            coop_notices::tell("The host called you, but you're in another region");
            continue;
        }
        Il2CppObject* pm = api.player_manager ? il2::invoke(api.player_manager, nullptr) : nullptr;
        if (!pm || !api.teleport) continue;
        const float half = yaw * 0.5f / 57.29578f;
        Quat rot{0, std::sin(half), 0, std::cos(half)};
        void* a[] = {&pos, &rot};
        il2::invoke(api.teleport, pm, a);
        logx::info("summon: the host called us to them: (%.1f, %.1f, %.1f) in %s", pos.x, pos.y, pos.z, scene.c_str());
        coop_notices::tell("The host called you to them");
    }
}

}  // namespace

bool init() {
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* console = il2::find_class(kGame, "", "uConsole");
    api.player_tf = il2::find_method(gm, "GetPlayerTransform", 0);
    api.player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    api.teleport = il2::find_method_params(pm, "TeleportPlayer", {"UnityEngine.Vector3", "UnityEngine.Quaternion"});
    api.position = il2::find_method(tf, "get_position", 0);
    api.rotation = il2::find_method(tf, "get_rotation", 0);
    api.console_log = il2::find_method_params(console, "Log", {"System.String"});
    const MethodInfo* run = il2::find_method_params(console, "RunCommand", {"System.String"});
    if (!run || !api.teleport || !api.player_tf || !api.position || !api.rotation) {
        logx::warn("summon: TLD's console or teleport not found; no teleportplayer");
        return false;
    }
    if (!hooks::install_method("uConsole.RunCommand", run, reinterpret_cast<void*>(&run_detour),
                               reinterpret_cast<void**>(&g_run)))
        return false;
    logx::info("summon: ready (the host's F1 console: teleportplayer NAME | all)");
    return true;
}

void on_frame(const std::string& scene, bool in_game) {
    g_scene = in_game ? scene : std::string();
    if (in_game) receive();
}

}  // namespace coop_summon
