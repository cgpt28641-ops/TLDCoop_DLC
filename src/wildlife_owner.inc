// TLDCoop_DLC (v44): wildlife ownership, [wildlife] ownership=1 (the host's coop.ini; friends follow it through
// Features). Included by wildlife_sync.cpp inside its anonymous namespace: it builds on the copies (Puppet), capture's
// records and the fight stand-ins above.
//
// Each animal is run by exactly one game, its owner: a real TLD animal there, a copy (a puppet) everywhere else,
// drawn ahead of its last record (net::wild::Predictor). Owners send their animals 10 times a second (WildFrame); a
// friend's go to the host, which passes them on (every friend is linked to the host only).
//
// The host is the arbiter: its registry knows every shared animal (id, scene, owner, last record, TLD's own record of
// it from the last handover), picks each one's owner (net::wild::decide: distance, who looks at it, link, who it is
// after; hysteresis; never in a fight or for the dead) and hands it over:
//   host -> old owner   Release (id, new owner, epoch)
//   old owner -> host   Take: its full state (pose, health, bleeding, wounds, mode, target, animator, and TLD's own
//                       BaseAi.Serialize(): waypoints, flee point...); its real animal is put away (hidden, kept for
//                       later) and a copy takes its place at the same pose and animation time
//   host -> new owner   Take: one of its own hidden animals of that kind (or one spawned) becomes it, at that pose,
//                       BaseAi.Deserialize(), health, bleeding, mode, target, animator states at their times; its copy
//                       goes in the same frame
// An owner gone from the scene (loading, another scene, the link down) is replaced at once from the registry. Nobody
// left in a scene: its animals stay in the registry, frozen, until someone comes back.
// Ids: the game that first shares an animal numbers it in its own range ((player + 1) << 24); the id stays with the
// animal from owner to owner.
// Spawning: one game per scene (the host says which: the one there longest) lets TLD spawn and shares what it has;
// the others hide their own animals (kept to become animals handed to them).

constexpr double kOwnArbiterMs = 250;
constexpr double kOwnControllersMs = 2000;
constexpr double kOwnPendingMs = 3000;
constexpr double kOwnSilentMs = 3000;            // host: an owner in the scene that stopped sending an animal
constexpr double kOwnFrozenMs = 30 * 60 * 1000.0;  // host: frozen animals forgotten after this long
constexpr double kOwnBlockMs = 60000;            // host: a game that couldn't take an animal isn't asked again for this long
constexpr double kOwnPuppetStaleMs = 2500;
constexpr double kOwnWrongMs = 2000;

bool g_own_config = true;  // host: coop.ini [wildlife] ownership (default 1)
bool g_own_was = false;
std::string g_own_scene;
bool g_pool_released = false;  // this visit: our hidden animals were put back to be shared (we spawn here, nothing known)
uint32_t g_own_counter = 0;
double g_own_last_capture = 0;
uint32_t g_own_frame = 0;

struct Owned {
    uintptr_t ai = 0;     // gchandle of the BaseAi
    uint16_t epoch = 0;
    bool spawned = false;  // made for it (TLD's Start resets its health a frame later: written again)
    bool reapplied = true;
    bool kill_pending = false;  // a dead animal handed to us, made for it: killed once TLD has started it
    net::AnimalRec state;  // what it was given
    double since_ms = 0;
};
std::map<uint32_t, Owned> g_owned;
// A friend's animal that died here while the host was in the scene: the host runs its carcass; ours stays (dying) until
// the host's carcass takes its place here (carcass_made).
struct Dying {
    uintptr_t ai = 0;
    double since_ms = 0;
};
std::unordered_map<uint32_t, Dying> g_dying;
constexpr double kDyingKeepMs = 20000;  // ...or this long at most (small game becomes an item on the host instead)
// This visit's animals that were already ours when we came (TLD's save, restored over the first seconds): where we
// spawn but the registry already knows the scene, they're hidden like everyone else's, not shared twice.
std::unordered_set<uintptr_t> g_initial;
double g_own_arrived_ms = 0;
constexpr double kInitialMs = 10000;

// Who runs which scene's spawning (Controllers), and whether the registry knows nothing there (fresh).
struct SceneControl {
    uint8_t player = net::kNoPlayer;
    bool fresh = false;
};
std::unordered_map<std::string, SceneControl> g_controllers;

uint8_t self_id() { return netrt::status().self_id; }
bool hosting_now() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

bool own_mode() {
    const auto role = netrt::role();
    if (!role) return false;
    if (*role == net::Role::Host) return g_own_config;
    return coop_features::on(net::kFeatureWildOwnership);
}

std::string who_name(uint8_t player) {
    if (player == net::kNoPlayer) return "nobody";
    if (player == 0) return "the host";
    return "player " + std::to_string(player);
}

// Everyone in the game now, as the arbiter and the owners see them.
struct Present {
    uint8_t id = 0;
    std::string scene;
    Vector3 pos{};
    float yaw = 0;
    bool inside = false;  // in a Seamless Interiors room
    double rtt_ms = 0;
};
std::vector<Present> present_players(const std::string& my_scene, bool my_in_game) {
    std::vector<Present> out;
    const game::PlayerState me = game::read_player();
    if (my_in_game && me.valid) {
        Present p;
        p.id = self_id();
        p.scene = my_scene;
        p.pos = {me.position.x, me.position.y, me.position.z};
        p.yaw = me.yaw;
        p.inside = seamless_interiors::player_inside();
        out.push_back(p);
    }
    const net::Status st = netrt::status();
    for (const net::StateMsg& s : netrt::remote_states()) {
        if (!s.valid || s.scene.empty()) continue;
        Present p;
        p.id = s.player;
        p.scene = s.scene;
        p.pos = {s.x, s.y, s.z};
        p.yaw = s.yaw;
        p.inside = !s.place.empty();
        for (const net::PlayerInfo& i : st.players)
            if (i.id == s.player && i.rtt_ms > 0) p.rtt_ms = i.rtt_ms;
        out.push_back(p);
    }
    return out;
}

bool player_in_scene(const std::vector<Present>& players, uint8_t id, const std::string& scene) {
    for (const Present& p : players)
        if (p.id == id && p.scene == scene) return true;
    return false;
}

// --- host: the registry and the arbiter ---------------------------------------------------------------------------

struct Entry {
    uint32_t id = 0;
    std::string scene;
    uint8_t owner = net::kNoPlayer;
    uint16_t epoch = 0;
    net::AnimalRec last;
    double last_ms = 0;
    std::string text;  // TLD's own record of it (BaseAi.Serialize) from its last handover
    bool frozen = false;
    double frozen_ms = 0;
    uint8_t pending_to = net::kNoPlayer;
    uint16_t pending_epoch = 0;
    double pending_ms = 0;
    net::wild::Track track;
    std::array<double, net::kMaxPlayers> blocked_until{};
    double wrong_ms = 0;
};
std::unordered_map<uint32_t, Entry> g_registry;
net::wild::RateLimit g_handovers{4, 6};
double g_last_arbiter = 0, g_last_controllers = 0;
std::array<std::pair<std::string, double>, net::kMaxPlayers> g_arrival{};  // host: where each player is, since when
std::string g_controllers_sent;

void take_local(const net::WildCtlMsg& t, double now);
void release_local(uint32_t id, uint8_t to, uint16_t epoch, double now);
void host_take(net::WildCtlMsg t, uint8_t from, double now);

void deliver_take(const net::WildCtlMsg& t, double now) {
    if (t.to == 0) {
        take_local(t, now);
        return;
    }
    net::Writer w(net::Msg::WildCtl, net::kMaxMessage);
    net::write_wild_ctl(w, t);
    netrt::send_packet_to(t.to, w, true);
}

net::WildCtlMsg take_from_registry(const Entry& e, uint8_t to, uint16_t epoch) {
    net::WildCtlMsg t;
    t.op = net::WildOp::Take;
    t.id = e.id;
    t.to = to;
    t.epoch = epoch;
    t.scene = e.scene;
    t.state = e.last;
    t.state.epoch = epoch;
    t.text = e.text;
    if (e.last.flags & net::kAnimalDead) t.flags |= net::kTakeDead;  // a body (only ever the host's)
    return t;
}

void transfer(Entry& e, uint8_t to, const char* why, const std::vector<Present>& players, double now) {
    const uint16_t epoch = static_cast<uint16_t>(e.epoch + 1);
    logx::info("wildlife: #%u owner %s -> %s (%s)", e.id, who_name(e.owner).c_str(), who_name(to).c_str(), why);
    const bool owner_here = e.owner != net::kNoPlayer && player_in_scene(players, e.owner, e.scene);
    e.frozen = false;
    if (owner_here) {
        e.pending_to = to;
        e.pending_epoch = epoch;
        e.pending_ms = now;
        if (e.owner == 0) {
            release_local(e.id, to, epoch, now);
        } else {
            net::WildCtlMsg r;
            r.op = net::WildOp::Release;
            r.id = e.id;
            r.to = to;
            r.epoch = epoch;
            net::Writer w(net::Msg::WildCtl, 64);
            net::write_wild_ctl(w, r);
            netrt::send_packet_to(e.owner, w, true);
        }
        return;
    }
    // The owner isn't there to hand it over: from what we know of it.
    e.owner = to;
    e.epoch = epoch;
    e.pending_to = net::kNoPlayer;
    deliver_take(take_from_registry(e, to, epoch), now);
}

void host_take(net::WildCtlMsg t, uint8_t from, double now) {
    auto it = g_registry.find(t.id);
    if (it == g_registry.end()) {
        Entry e;
        e.id = t.id;
        e.scene = t.scene;
        e.owner = from;
        e.last = t.state;
        e.last_ms = now;
        it = g_registry.emplace(t.id, std::move(e)).first;
    }
    Entry& e = it->second;
    if (t.flags & net::kTakeDead) {
        if (e.owner != from) {
            logx::info("wildlife: %s says #%u died, but %s runs it; ignored", who_name(from).c_str(), t.id, who_name(e.owner).c_str());
            return;
        }
        logx::info("wildlife: #%u owner %s -> %s (it died: the host keeps its carcass)", t.id, who_name(from).c_str(),
                   who_name(0).c_str());
        e.owner = 0;
        e.epoch = t.epoch;
        e.pending_to = net::kNoPlayer;
        e.last = t.state;
        e.last_ms = now;
        t.to = 0;
        take_local(t, now);
        return;
    }
    if (t.flags & net::kTakeKept) {
        if (e.pending_to != net::kNoPlayer) {
            logx::info("wildlife: #%u stays with %s (a fight began; handover to %s called off)", t.id, who_name(e.owner).c_str(),
                       who_name(e.pending_to).c_str());
            e.pending_to = net::kNoPlayer;
            e.track.last_switch_ms = now;
        }
        return;
    }
    if (e.pending_to == net::kNoPlayer || t.epoch != e.pending_epoch) {
        logx::info("wildlife: a late handover of #%u from %s (epoch %u) ignored", t.id, who_name(from).c_str(), t.epoch);
        return;
    }
    if (t.flags & net::kTakeNoState) {
        t.state = e.last;
        t.state.epoch = t.epoch;
        t.text = e.text;
        t.scene = e.scene;
    } else {
        if (!t.text.empty()) e.text = t.text;
        e.last = t.state;
        e.last_ms = now;
    }
    t.flags &= static_cast<uint8_t>(~net::kTakeNoState);
    t.to = e.pending_to;
    e.owner = e.pending_to;
    e.epoch = t.epoch;
    e.pending_to = net::kNoPlayer;
    deliver_take(t, now);
}

void host_refuse(uint32_t id, uint16_t epoch, uint8_t from, double now) {
    const auto it = g_registry.find(id);
    if (it == g_registry.end()) return;
    Entry& e = it->second;
    if (e.owner != from || e.epoch != epoch) return;
    logx::info("wildlife: %s couldn't take #%u; someone else will (not them for a minute)", who_name(from).c_str(), id);
    if (from < net::kMaxPlayers) e.blocked_until[from] = now + kOwnBlockMs;
    e.owner = net::kNoPlayer;
}

// A frame of animals from `owner` (its own records): into the registry. Returns the records to pass on.
std::vector<net::AnimalRec> host_ingest(uint8_t owner, const std::string& scene, const std::vector<net::AnimalRec>& recs,
                                        double now) {
    std::vector<net::AnimalRec> pass;
    for (const net::AnimalRec& rec : recs) {
        auto it = g_registry.find(rec.id);
        if (it == g_registry.end()) {
            Entry e;
            e.id = rec.id;
            e.scene = scene;
            e.owner = owner;
            e.epoch = rec.epoch;
            it = g_registry.emplace(rec.id, std::move(e)).first;
            logx::info("wildlife: #%u (%s) shared by %s, its owner", rec.id, kind_name(rec.kind), who_name(owner).c_str());
        }
        Entry& e = it->second;
        if (e.owner != owner) {
            if (e.owner == net::kNoPlayer && e.pending_to == net::kNoPlayer && rec.epoch >= e.epoch) {
                logx::info("wildlife: #%u owner %s -> %s (it runs it, nobody else did)", rec.id, who_name(e.owner).c_str(),
                           who_name(owner).c_str());
                e.owner = owner;
                e.epoch = rec.epoch;
            } else {
                if (now - e.wrong_ms > kOwnWrongMs && owner != 0) {
                    e.wrong_ms = now;
                    logx::info("wildlife: %s still runs #%u, which is %s's: told to let it go", who_name(owner).c_str(),
                               rec.id, who_name(e.owner).c_str());
                    net::WildCtlMsg r;
                    r.op = net::WildOp::Release;
                    r.id = rec.id;
                    r.to = e.owner;
                    r.epoch = e.epoch;
                    net::Writer w(net::Msg::WildCtl, 64);
                    net::write_wild_ctl(w, r);
                    netrt::send_packet_to(owner, w, true);
                }
                continue;
            }
        }
        if (rec.epoch < e.epoch) continue;
        e.last = rec;
        e.last_ms = now;
        e.scene = scene;
        e.frozen = false;
        pass.push_back(rec);
    }
    return pass;
}

void host_arbitrate(const std::vector<Present>& players, double now) {
    if (now - g_last_arbiter < kOwnArbiterMs) return;
    g_last_arbiter = now;
    // Who has been in which scene since when (the spawning game: the one there longest).
    for (uint8_t id = 0; id < net::kMaxPlayers; ++id) {
        std::string scene;
        for (const Present& p : players)
            if (p.id == id) scene = p.scene;
        if (scene != g_arrival[id].first) g_arrival[id] = {scene, now};
    }
    for (auto it = g_registry.begin(); it != g_registry.end();) {
        Entry& e = it->second;
        if (e.pending_to != net::kNoPlayer) {
            if (now - e.pending_ms > kOwnPendingMs) {
                logx::info("wildlife: no handover of #%u from %s in time: %s takes it from what we know", e.id,
                           who_name(e.owner).c_str(), who_name(e.pending_to).c_str());
                const uint8_t to = e.pending_to;
                e.pending_to = net::kNoPlayer;
                if (player_in_scene(players, to, e.scene)) {
                    e.owner = to;
                    e.epoch = e.pending_epoch;
                    deliver_take(take_from_registry(e, to, e.epoch), now);
                } else {
                    e.owner = net::kNoPlayer;
                }
            }
            ++it;
            continue;
        }
        std::vector<net::wild::Seat> seats;
        bool host_here = false;
        for (const Present& p : players) {
            if (p.scene != e.scene) continue;
            if (p.id == 0) host_here = true;
            if (p.id < net::kMaxPlayers && now < e.blocked_until[p.id]) continue;
            net::wild::Seat s;
            s.player = p.id;
            const float dx = e.last.x - p.pos.x, dy = e.last.y - p.pos.y, dz = e.last.z - p.pos.z;
            s.metres = std::sqrt(dx * dx + dy * dy + dz * dz);
            s.looking = net::wild::looking_at(p.yaw, dx, dz, s.metres);
            s.rtt_ms = p.id == 0 ? 0 : p.rtt_ms;
            s.inside = p.inside;
            seats.push_back(s);
        }
        const bool owner_here = e.owner != net::kNoPlayer && player_in_scene(players, e.owner, e.scene);
        if (seats.empty()) {
            if (!e.frozen) {
                e.frozen = true;
                e.frozen_ms = now;
                logx::info("wildlife: #%u (%s) frozen in '%s' at (%.0f, %.0f, %.0f), hp %.0f: nobody is there", e.id,
                           kind_name(e.last.kind), e.scene.c_str(), e.last.x, e.last.y, e.last.z, e.last.hp);
                e.owner = net::kNoPlayer;
            }
            if (now - e.frozen_ms > kOwnFrozenMs) {
                logx::info("wildlife: #%u forgotten (frozen too long)", e.id);
                it = g_registry.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        if (owner_here && now - e.last_ms > kOwnSilentMs) {
            logx::info("wildlife: #%u (%s) is no longer shared by %s (out of everyone's range, or gone from its game)",
                       e.id, kind_name(e.last.kind), who_name(e.owner).c_str());
            it = g_registry.erase(it);
            continue;
        }
        const bool dead = (e.last.flags & net::kAnimalDead) != 0;
        if (dead && !owner_here) {
            // A body: the host keeps it (its carcass is shared from there); with the host away it waits, frozen.
            if (host_here && g_handovers.take(now)) transfer(e, 0, "its owner left; the host keeps the body", players, now);
            ++it;
            continue;
        }
        net::wild::Facts f;
        f.owner = e.owner;
        f.owner_present = owner_here;
        f.target = e.last.target;
        f.mode = e.last.mode;
        f.dead = dead;
        net::wild::Why why;
        const net::wild::Track before = e.track;
        const uint8_t to = net::wild::decide(e.track, f, seats, now, why);
        if (to != net::kNoPlayer && to != e.owner) {
            if (g_handovers.take(now)) transfer(e, to, net::wild::why_text(why), players, now);
            else e.track = before;  // too many at once: decided again next time
        }
        ++it;
    }
    // Who spawns where.
    std::map<std::string, SceneControl> scenes;
    for (const Present& p : players) {
        auto& c = scenes[p.scene];
        if (c.player == net::kNoPlayer || g_arrival[p.id].second < g_arrival[c.player].second ||
            (g_arrival[p.id].second == g_arrival[c.player].second && p.id < c.player))
            c.player = p.id;
    }
    for (auto& [scene, c] : scenes) {
        c.fresh = true;
        for (const auto& [id, e] : g_registry)
            if (e.scene == scene) {
                c.fresh = false;
                break;
            }
    }
    std::string summary;
    net::WildCtlMsg m;
    m.op = net::WildOp::Controllers;
    for (const auto& [scene, c] : scenes) {
        summary += scene + "=" + std::to_string(c.player) + (c.fresh ? "*" : "") + " ";
        m.scenes.push_back({scene, c.player, static_cast<uint8_t>(c.fresh ? 1 : 0)});
    }
    g_controllers.clear();
    for (const auto& [scene, c] : scenes) g_controllers[scene] = c;
    const bool changed = summary != g_controllers_sent;
    if (changed || now - g_last_controllers >= kOwnControllersMs) {
        net::Writer w(net::Msg::WildCtl, net::kMaxPacket);
        net::write_wild_ctl(w, m);
        netrt::send_packet(w, changed);
        g_last_controllers = now;
        if (changed) {
            for (const auto& [scene, c] : scenes)
                logx::info("wildlife: %s spawns in '%s'%s", who_name(c.player).c_str(), scene.c_str(),
                           c.fresh ? " (nothing shared there yet: its own animals are shared)" : "");
            g_controllers_sent = summary;
        }
    }
}

// --- every game: what we own --------------------------------------------------------------------------------------

bool i_control(const std::string& scene) {
    const auto it = g_controllers.find(scene);
    return it != g_controllers.end() && it->second.player == self_id();
}
bool scene_fresh(const std::string& scene) {
    const auto it = g_controllers.find(scene);
    return it != g_controllers.end() && it->second.fresh;
}

uint8_t target_of(Il2CppObject* ai) {
    if (api.ai_current_target < 0) return net::kNoPlayer;
    Il2CppObject* cur = il2::read_field<Il2CppObject*>(ai, api.ai_current_target);
    if (!cur) return net::kNoPlayer;
    if (api.ai_player_target >= 0 && cur == il2::read_field<Il2CppObject*>(ai, api.ai_player_target)) return self_id();
    if (const auto p = avatar::player_of(cur)) return *p;
    return net::kNoPlayer;
}

// A record of one of our real animals.
net::AnimalRec own_record(Il2CppObject* ai, uint32_t id, uint16_t epoch, const Vector3& pos, const Vector3& euler) {
    net::AnimalRec a;
    a.id = id;
    a.epoch = epoch;
    a.kind = static_cast<uint8_t>(il2::read_field<int32_t>(ai, api.ai_kind));
    a.mode = static_cast<uint8_t>(il2::read_field<int32_t>(ai, api.ai_mode));
    a.flags = a.mode == kAiModeDead ? net::kAnimalDead : 0;
    if (api.ai_wildlife_mode >= 0 && il2::read_field<int32_t>(ai, api.ai_wildlife_mode) == 1) a.flags |= net::kAnimalAurora;
    if (api.ai_bleeding >= 0 && il2::read_field<bool>(ai, api.ai_bleeding)) a.flags |= net::kAnimalBleeding;
    if (api.ai_wounded >= 0 && il2::read_field<bool>(ai, api.ai_wounded)) a.flags |= net::kAnimalWounded;
    if (api.ai_bleed_elapsed >= 0) a.bleed_elapsed = il2::read_field<float>(ai, api.ai_bleed_elapsed);
    if (api.ai_bleed_limit >= 0) a.bleed_limit = il2::read_field<float>(ai, api.ai_bleed_limit);
    if (!std::isfinite(a.bleed_elapsed)) a.bleed_elapsed = 0;
    if (!std::isfinite(a.bleed_limit)) a.bleed_limit = 0;
    a.target = target_of(ai);
    a.hp = il2::read_field<float>(ai, api.ai_hp);
    if (!std::isfinite(a.hp)) a.hp = 0;
    a.x = pos.x, a.y = pos.y, a.z = pos.z;
    a.yaw = euler.y;
    a.pitch = signed_degrees(euler.x);
    a.roll = signed_degrees(euler.z);
    if (Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.ai_animator)) {
        int32_t layers = 0;
        il2::invoke_value(api.layer_count, animator, layers);
        for (int32_t l = 0; l < layers && l < static_cast<int32_t>(net::kMaxAnimalLayers); ++l) {
            StateInfo si{};
            void* args[] = {&l};
            il2::invoke_value(api.state_info, animator, si, args);
            a.layers.push_back({si.full_path, si.normalized_time});
        }
        a.params = read_params(reinterpret_cast<uintptr_t>(ai), animator);
        if (a.params.size() > net::kMaxAnimalParams) a.params.clear();
    }
    return a;
}

// Its animator to exactly these states, times and parameters (no fade: the same frame as the one it replaces).
void set_animator(Il2CppObject* animator, const net::AnimalRec& rec) {
    if (!animator) return;
    for (int32_t l = 0; l < static_cast<int32_t>(rec.layers.size()); ++l) {
        const net::AnimalLayer& layer = rec.layers[static_cast<size_t>(l)];
        if (!layer.state) continue;
        int32_t hash = layer.state, index = l;
        float time = layer.time;
        void* args[] = {&hash, &index, &time};
        il2::invoke(api.play, animator, args);
    }
    const std::vector<Param> schema = read_schema(animator);
    if (rec.params.size() != schema_bytes(schema)) return;
    size_t at = 0;
    for (const Param& param : schema) {
        int32_t hash = param.hash;
        if (param.type == kParamBool) {
            bool b = rec.params[at++] != 0;
            void* args[] = {&hash, &b};
            il2::invoke(api.set_bool, animator, args);
        } else if (param.type == kParamInt) {
            int32_t i = 0;
            memcpy(&i, rec.params.data() + at, 4);
            at += 4;
            void* args[] = {&hash, &i};
            il2::invoke(api.set_int, animator, args);
        } else {
            float f = 0;
            memcpy(&f, rec.params.data() + at, 4);
            at += 4;
            void* args[] = {&hash, &f};
            il2::invoke(api.set_float, animator, args);
        }
    }
}

void send_ctl(const net::WildCtlMsg& m) {
    net::Writer w(net::Msg::WildCtl, net::kMaxMessage);
    net::write_wild_ctl(w, m);
    netrt::send_packet(w, true);  // (a friend: to the host)
}

// The handover's last step on the old owner, or a Take we answer ourselves (the host's own animals).
void send_take(net::WildCtlMsg& t, double now) {
    if (hosting_now()) host_take(t, 0, now);
    else send_ctl(t);
}

void kill_now(Il2CppObject* ai) {
    if (!api.ai_apply_damage || dead_animal(ai)) return;
    float lethal = std::numeric_limits<float>::infinity(), bleed = 0;
    int32_t source = 1;  // DamageSource.Player
    void* args[] = {&lethal, &bleed, &source, il2::il2cpp_string_new("")};
    il2::invoke(il2::virtual_method(ai, api.ai_apply_damage), ai, args);
}

bool aggressive(int32_t mode) {
    return mode == 1 || mode == 6 || mode == 13 || mode == 25 || mode == 8 || mode == 10;
}

// One of our hidden animals of that kind (aurora or not as asked, if there's one), out of the pool.
Il2CppObject* pool_take(int kind, bool aurora) {
    Il2CppObject* fallback = nullptr;
    uintptr_t fallback_key = 0;
    for (const auto& [addr, h] : g_hidden) {
        Il2CppObject* go = target(h);
        Il2CppObject* ai = go && alive(go) ? base_ai_of(go) : nullptr;
        if (!ai || il2::read_field<int32_t>(ai, api.ai_kind) != kind || dead_animal(ai)) continue;
        if (g_fight.active && target(g_fight.ai) == ai) continue;
        const bool is_aurora = api.ai_wildlife_mode >= 0 && il2::read_field<int32_t>(ai, api.ai_wildlife_mode) == 1;
        if (is_aurora == aurora) {
            uintptr_t handle = h;
            g_hidden.erase(addr);
            drop(handle);
            return ai;
        }
        if (!fallback) fallback = ai, fallback_key = addr;
    }
    if (fallback) {
        uintptr_t handle = g_hidden[fallback_key];
        g_hidden.erase(fallback_key);
        drop(handle);
    }
    return fallback;
}

// We run animal t.id from now: one of ours becomes it.
void take_local(const net::WildCtlMsg& t, double now) {
    const net::AnimalRec& s = t.state;
    if (auto o = g_owned.find(t.id); o != g_owned.end()) {
        o->second.epoch = t.epoch;
        logx::info("wildlife: we already run #%u (epoch now %u)", t.id, t.epoch);
        return;
    }
    auto refuse = [&](const char* why) {
        logx::warn("wildlife: can't take #%u (%s): %s", t.id, kind_name(s.kind), why);
        if (hosting_now()) {
            host_refuse(t.id, t.epoch, 0, now);
            return;
        }
        net::WildCtlMsg r;
        r.op = net::WildOp::Refuse;
        r.id = t.id;
        r.epoch = t.epoch;
        send_ctl(r);
    };
    if (t.scene != g_own_scene) return refuse("we aren't in its scene");
    const bool aurora = (s.flags & net::kAnimalAurora) != 0;
    Vector3 pos{s.x, s.y, s.z};
    Il2CppObject* ai = pool_take(s.kind, aurora);
    bool spawned = false;
    if (!ai) {
        ai = spawn_stand_in(s.kind, pos, s.yaw, kAiModeWander, true);
        spawned = ai != nullptr;
    }
    if (!ai) return refuse("no animal of that kind here to take its place, and none could be made");
    Il2CppObject* go = go_of(ai);
    set_active(go, true);
    const bool dead_take = (t.flags & net::kTakeDead) != 0 || (s.flags & net::kAnimalDead) != 0;
    if (!dead_take && !t.text.empty() && api.ai_deserialize) {
        void* args[] = {il2::il2cpp_string_new(t.text.c_str())};
        il2::invoke(api.ai_deserialize, ai, args);
    }
    const float half = s.yaw * 0.5f * 3.14159265f / 180.0f;
    Quaternion rot{0, std::sin(half), 0, std::cos(half)};
    if (api.ai_set_pos_rot) {
        void* args[] = {&pos, &rot};
        il2::invoke(api.ai_set_pos_rot, ai, args);
    } else {
        Vector3 facing{0, s.yaw, 0};
        void* pa[] = {&pos};
        void* ra[] = {&facing};
        il2::invoke(api.set_position, transform_of(ai), pa);
        il2::invoke(api.set_euler, transform_of(ai), ra);
    }
    if (s.hp > 0) write_field<float>(ai, api.ai_hp, s.hp);
    write_field<bool>(ai, api.ai_bleeding, (s.flags & net::kAnimalBleeding) != 0);
    write_field<bool>(ai, api.ai_wounded, (s.flags & net::kAnimalWounded) != 0);
    write_field<float>(ai, api.ai_bleed_elapsed, s.bleed_elapsed);
    write_field<float>(ai, api.ai_bleed_limit, s.bleed_limit);
    const uint8_t me = self_id();
    int32_t mode = s.mode;
    if (dead_take) {
        // It died on its owner: dies here too, where it lay (our carcass is the one every game shares). One made for
        // it is killed once TLD has started it (its Start would undo it).
        if (!spawned) kill_now(ai);
    } else {
        bool has_target = false;
        if (s.target != net::kNoPlayer && s.target == me && api.ai_player_target >= 0 && api.ai_current_target >= 0) {
            il2::store_ref(ai, api.ai_current_target, il2::read_field<Il2CppObject*>(ai, api.ai_player_target));
            has_target = true;
        } else if (s.target != net::kNoPlayer && hosting_now() && api.ai_current_target >= 0) {
            Il2CppObject *a_go = nullptr, *a_tf = nullptr, *a_target = nullptr;
            if (avatar::stand_in(s.target, a_go, a_tf, a_target)) {
                il2::store_ref(ai, api.ai_current_target, a_target);
                has_target = true;
            }
        }
        if (!has_target && api.ai_clear_target) il2::invoke(api.ai_clear_target, ai);
        if (mode == kAiModeDead || mode == kAiModeStruggle || mode == 0 || (aggressive(mode) && !has_target)) mode = kAiModeWander;
        if (il2::read_field<int32_t>(ai, api.ai_mode) != mode) {
            void* args[] = {&mode};
            il2::invoke(api.set_mode, ai, args);
        }
        set_animator(il2::read_field<Il2CppObject*>(ai, api.ai_animator), s);
    }
    // Its copy here goes in the same frame.
    if (auto p = g_puppets.find(t.id); p != g_puppets.end()) {
        remove_puppet(p->second);
        g_puppets.erase(p);
    }
    const auto key = reinterpret_cast<uintptr_t>(ai);
    g_ids[key] = t.id;
    Owned o;
    o.ai = hold(ai);
    o.epoch = t.epoch;
    o.spawned = spawned;
    o.reapplied = !spawned;
    o.kill_pending = spawned && dead_take;
    o.state = s;
    o.since_ms = now;
    g_owned[t.id] = std::move(o);
    logx::info("wildlife: we run #%u now (%s%s, %s, hp %.0f%s%s; %s)", t.id, kind_name(s.kind), aurora ? ", aurora" : "",
               il2::enum_name(il2::find_class(kGame, "", "AiMode"), dead_take ? kAiModeDead : mode).c_str(), s.hp,
               (s.flags & net::kAnimalBleeding) ? ", bleeding" : "", t.text.empty() ? "" : ", TLD's record of it",
               spawned ? "one made for it" : "one of our own took its place");
}

// We stop running animal `id`: our real one is put away, a copy stands in its place, and its state goes to `to`.
void release_local(uint32_t id, uint8_t to, uint16_t epoch, double now) {
    net::WildCtlMsg t;
    t.op = net::WildOp::Take;
    t.id = id;
    t.to = to;
    t.epoch = epoch;
    t.scene = g_own_scene;
    const auto o = g_owned.find(id);
    Il2CppObject* ai = o == g_owned.end() ? nullptr : target(o->second.ai);
    if (!ai || !alive(ai) || !active(go_of(ai))) {
        if (o != g_owned.end()) {
            drop(o->second.ai);
            g_owned.erase(o);
        }
        t.flags = net::kTakeNoState;
        logx::info("wildlife: asked to hand #%u to %s, but it's gone here: from the host's record", id, who_name(to).c_str());
        send_take(t, now);
        return;
    }
    if (il2::read_field<int32_t>(ai, api.ai_mode) == kAiModeStruggle || dead_animal(ai)) {
        t.flags = net::kTakeKept;
        t.epoch = o->second.epoch;
        logx::info("wildlife: keeping #%u (%s): not handed to %s", id, dead_animal(ai) ? "dead" : "in a fight",
                   who_name(to).c_str());
        send_take(t, now);
        return;
    }
    Il2CppObject* tf = transform_of(ai);
    Vector3 pos{}, euler{};
    il2::invoke_value(api.get_position, tf, pos);
    il2::invoke_value(api.get_euler, tf, euler);
    t.state = own_record(ai, id, epoch, pos, euler);
    if (api.ai_serialize)
        if (auto* text = reinterpret_cast<Il2CppString*>(il2::invoke(api.ai_serialize, ai))) t.text = il2::to_utf8(text);
    if (t.text.size() > net::kWildTextMax) t.text.clear();
    // A copy in its place, at its pose and animation time.
    Puppet p;
    if (make_puppet(id, t.state, p, now)) {
        Vector3 e{euler.x, euler.y, euler.z};
        void* pa[] = {&pos};
        void* ra[] = {&e};
        il2::invoke(api.set_position, target(p.transform), pa);
        il2::invoke(api.set_euler, target(p.transform), ra);
        set_animator(target(p.animator), t.state);
        p.latest = t.state;
        p.epoch = static_cast<uint16_t>(epoch - 1);  // the new owner's records (epoch) start its prediction afresh
        p.own = true;
        p.updated_ms = now;
        net::wild::Predictor::Pose pose{pos.x, pos.y, pos.z, euler.y, signed_degrees(euler.x), signed_degrees(euler.z)};
        p.pred.add(pose, now, now);
        g_puppets[id] = std::move(p);
    }
    // Our real one: hidden, kept (it may become another animal handed to us).
    Il2CppObject* go = go_of(ai);
    set_active(go, false);
    const auto addr = reinterpret_cast<uintptr_t>(go);
    if (!g_hidden.count(addr)) g_hidden.emplace(addr, hold(go));
    g_ids.erase(reinterpret_cast<uintptr_t>(ai));
    drop(o->second.ai);
    g_owned.erase(o);
    logx::info("wildlife: #%u (%s, %s, hp %.0f) handed to %s; shown here as a copy from now (TLD's record: %zu bytes)", id,
               kind_name(t.state.kind), il2::enum_name(il2::find_class(kGame, "", "AiMode"), t.state.mode).c_str(),
               t.state.hp, who_name(to).c_str(), t.text.size());
    send_take(t, now);
}

// Every animal of ours, 10 times a second: ours are recorded and sent, others hidden (or, where we spawn, shared).
void own_capture(const std::string& scene, const std::vector<Present>& players, double now) {
    if (now - g_own_last_capture < kCaptureMs) return;
    g_own_last_capture = now;
    const uint8_t me = self_id();
    std::vector<Present> here;
    for (const Present& p : players)
        if (p.scene == scene) here.push_back(p);
    g_friend_spots.clear();
    for (const Present& p : here)
        if (p.id != me && !p.inside) g_friend_spots.push_back(p.pos);
    auto in_range = [&](const Vector3& pos) {
        for (const Present& p : here) {
            const float dx = pos.x - p.pos.x, dy = pos.y - p.pos.y, dz = pos.z - p.pos.z;
            if (dx * dx + dy * dy + dz * dz <= kRangeMetres * kRangeMetres) return true;
        }
        return false;
    };
    const bool control = i_control(scene);
    if (control && scene_fresh(scene) && !g_pool_released) {
        g_pool_released = true;
        int back = 0;
        for (auto& [addr, h] : g_hidden) {
            Il2CppObject* go = target(h);
            Il2CppObject* ai = go && alive(go) ? base_ai_of(go) : nullptr;
            if (ai && !dead_animal(ai)) {
                set_active(go, true);
                ++back;
            }
            drop(h);
        }
        g_hidden.clear();
        logx::info("wildlife: we spawn in '%s' and nothing is shared there yet: our %d animal(s) are shared from now", scene.c_str(), back);
    }
    bool host_here = false;
    for (const Present& p : here) host_here |= p.id == 0;
    const bool host = hosting_now();
    Il2CppObject* fighter = g_fight.active ? target(g_fight.ai) : nullptr;
    std::vector<net::AnimalRec> recs;
    std::unordered_map<uint64_t, std::pair<uint32_t, uint8_t>> owners;
    std::unordered_set<uintptr_t> near_friend, seen;
    if (now - g_emitters_refreshed > 2000) {
        g_emitters_refreshed = now;
        g_ai_emitters.clear();
    }
    int hidden = 0, registered = 0;
    for (Il2CppObject* ai : all_animals()) {
        const auto key = reinterpret_cast<uintptr_t>(ai);
        Il2CppObject* go = go_of(ai);
        if (!active(go) || ai == fighter) continue;
        if (std::find(g_carcasses.begin(), g_carcasses.end(), go) != g_carcasses.end()) continue;
        auto idit = g_ids.find(key);
        if (idit == g_ids.end()) {
            bool dying = false;
            for (const auto& [did, d] : g_dying) dying |= target(d.ai) == ai;
            if (dying || dead_animal(ai)) continue;  // (our own kills from before stay, as without ownership)
            if (now - g_own_arrived_ms < kInitialMs) g_initial.insert(key);
            if (!control || (!g_pool_released && g_initial.count(key))) {
                set_active(go, false);
                const auto addr = reinterpret_cast<uintptr_t>(go);
                if (!g_hidden.count(addr)) g_hidden.emplace(addr, hold(go));
                ++hidden;
                continue;
            }
            Vector3 p{};
            if (!il2::invoke_value(api.get_position, transform_of(ai), p) || !in_range(p)) continue;
            const uint32_t id = (static_cast<uint32_t>(me + 1) << 24) | (++g_own_counter & 0xFFFFFF);
            idit = g_ids.emplace(key, id).first;
            Owned o;
            o.ai = hold(ai);
            o.since_ms = now;
            g_owned[id] = std::move(o);
            ++registered;
        }
        const uint32_t id = idit->second;
        auto o = g_owned.find(id);
        if (o == g_owned.end()) {  // (shouldn't happen: kept in step)
            g_ids.erase(idit);
            continue;
        }
        seen.insert(key);
        Il2CppObject* tf = transform_of(ai);
        Vector3 pos{}, euler{};
        if (!tf || !il2::invoke_value(api.get_position, tf, pos)) continue;
        il2::invoke_value(api.get_euler, tf, euler);
        Owned& own = o->second;
        if (own.kill_pending && now - own.since_ms > 200) {
            own.kill_pending = false;
            kill_now(ai);
        }
        if (!own.reapplied && now - own.since_ms > 200) {
            // Made for it: TLD's Start gave it its own health meanwhile.
            own.reapplied = true;
            if (own.state.hp > 0) write_field<float>(ai, api.ai_hp, own.state.hp);
            write_field<bool>(ai, api.ai_bleeding, (own.state.flags & net::kAnimalBleeding) != 0);
            write_field<float>(ai, api.ai_bleed_elapsed, own.state.bleed_elapsed);
            write_field<float>(ai, api.ai_bleed_limit, own.state.bleed_limit);
        }
        if (dead_animal(ai) && !host && host_here) {
            // It died here with the host in the scene: the host keeps its carcass (shared with everyone from there).
            if (!g_dying.count(id)) {
                net::WildCtlMsg t;
                t.op = net::WildOp::Take;
                t.id = id;
                t.to = 0;
                t.epoch = static_cast<uint16_t>(own.epoch + 1);
                t.flags = net::kTakeDead;
                t.scene = scene;
                t.state = own_record(ai, id, t.epoch, pos, euler);
                send_ctl(t);
                g_dying[id] = Dying{hold(ai), now};
                logx::info("wildlife: our #%u (%s) died: the host keeps its carcass", id, kind_name(t.state.kind));
            }
            g_ids.erase(key);
            seen.erase(key);
            drop(own.ai);
            g_owned.erase(o);
            continue;
        }
        if (!in_range(pos)) continue;
        bool by_friend = false;
        for (const Vector3& f : g_friend_spots) {
            const float dx = pos.x - f.x, dy = pos.y - f.y, dz = pos.z - f.z;
            by_friend |= dx * dx + dy * dy + dz * dz <= kFriendNearMetres * kFriendNearMetres;
        }
        if (by_friend) near_friend.insert(key);
        if (by_friend && api.set_culling)
            if (Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.ai_animator)) {
                int32_t culling = kCullAlwaysAnimate;
                void* cull_args[] = {&culling};
                il2::invoke(api.set_culling, animator, cull_args);
            }
        net::AnimalRec a = own_record(ai, id, own.epoch, pos, euler);
        auto emitters = g_ai_emitters.find(key);
        if (emitters == g_ai_emitters.end()) emitters = g_ai_emitters.emplace(key, wwise_emitters(go)).first;
        for (size_t e = 0; e < emitters->second.size(); ++e) owners[emitters->second[e]] = {a.id, static_cast<uint8_t>(e)};
        recs.push_back(std::move(a));
    }
    // Ours that died, kept until the host's carcass (or item) takes their place.
    for (auto it = g_dying.begin(); it != g_dying.end();) {
        if (now - it->second.since_ms < kDyingKeepMs) {
            ++it;
            continue;
        }
        if (Il2CppObject* ai = target(it->second.ai); ai && alive(ai)) set_active(go_of(ai), false);
        drop(it->second.ai);
        it = g_dying.erase(it);
    }
    // Ours that TLD put away or destroyed.
    for (auto it = g_owned.begin(); it != g_owned.end();) {
        Il2CppObject* ai = target(it->second.ai);
        if (ai && alive(ai) && active(go_of(ai))) {
            ++it;
            continue;
        }
        logx::info("wildlife: our #%u is gone from our game (TLD %s it)", it->first, ai && alive(ai) ? "switched off" : "removed");
        for (auto k = g_ids.begin(); k != g_ids.end();) k = k->second == it->first ? g_ids.erase(k) : std::next(k);
        drop(it->second.ai);
        it = g_owned.erase(it);
    }
    if (hidden) logx::info("wildlife: hid %d of our own animal(s) (another game spawns here; they're kept for handovers)", hidden);
    if (registered) logx::info("wildlife: %d of our animal(s) shared from now (we spawn in '%s')", registered, scene.c_str());
    {
        std::lock_guard lock(g_sound_mutex);
        g_emitter_owner.swap(owners);
        g_sound_host = true;
    }
    for (auto it = g_ai_emitters.begin(); it != g_ai_emitters.end();) it = seen.count(it->first) ? std::next(it) : g_ai_emitters.erase(it);
    for (auto it = g_schemas.begin(); it != g_schemas.end();) it = seen.count(it->first) || g_ids.count(it->first) ? std::next(it) : g_schemas.erase(it);
    g_near_friend.swap(near_friend);

    // The frame, split into datagrams.
    std::vector<net::WildFrameMsg> parts(1);
    size_t used = net::kWildFrameHeader;
    for (net::AnimalRec& a : recs) {
        const size_t size = net::wild_rec_size(a);
        if (used + size > net::kMaxPacket || parts.back().animals.size() == 255) {
            parts.emplace_back();
            used = net::kWildFrameHeader;
        }
        used += size;
        parts.back().animals.push_back(std::move(a));
    }
    ++g_own_frame;
    for (size_t i = 0; i < parts.size(); ++i) {
        net::WildFrameMsg& m = parts[i];
        m.owner = me;
        m.frame = g_own_frame;
        m.t_ms = now;
        m.part = static_cast<uint8_t>(i);
        m.parts = static_cast<uint8_t>(parts.size());
        m.scene = scene;
        if (host) host_ingest(0, scene, m.animals, now);
        if (m.animals.empty() && parts.size() == 1) continue;  // nothing of ours to tell
        net::Writer w(net::Msg::WildFrame);
        net::write_wild_frame(w, m);
        if (w.ok()) netrt::send_packet(w, false);
    }
}

// --- every game: the others' animals -------------------------------------------------------------------------------

void puppet_record(const net::AnimalRec& rec, double t_ms, double received_ms, double now) {
    if (g_owned.count(rec.id)) return;  // ours (a late record from before we took it)
    if (g_dying.count(rec.id)) return;                  // ours, dying: the host's carcass takes its place
    auto it = g_puppets.find(rec.id);
    if (it != g_puppets.end() && (it->second.kind != rec.kind ||
                                  it->second.aurora != ((rec.flags & net::kAnimalAurora) != 0) || !alive(target(it->second.go)))) {
        remove_puppet(it->second);
        g_puppets.erase(it);
        it = g_puppets.end();
    }
    if (it == g_puppets.end()) {
        Puppet p;
        if (!make_puppet(rec.id, rec, p, now)) return;
        p.own = true;
        p.epoch = rec.epoch;
        if (g_carcass_ids.count(rec.id)) {
            p.carcass = true;
            free_stuck_arrows(target(p.go));
            set_active(target(p.go), false);
        }
        it = g_puppets.emplace(rec.id, std::move(p)).first;
    }
    Puppet& p = it->second;
    if (rec.epoch < p.epoch) return;  // an older owner's
    if (rec.epoch > p.epoch) {
        p.epoch = rec.epoch;
        p.pred.reset();
    }
    p.updated_ms = now;
    if (p.carcass) return;
    net::wild::Predictor::Pose pose{rec.x, rec.y, rec.z, rec.yaw, rec.pitch, rec.roll};
    p.pred.add(pose, t_ms, received_ms);
    p.latest = rec;
    p.params_dirty = true;
}

void own_puppets_frame(double now) {
    for (auto it = g_puppets.begin(); it != g_puppets.end();) {
        Puppet& p = it->second;
        if (now - p.updated_ms > kOwnPuppetStaleMs || !alive(target(p.go))) {
            logx::info("wildlife: the %s #%u is gone (no word of it)", kind_name(p.kind), it->first);
            remove_puppet(p);
            it = g_puppets.erase(it);
            continue;
        }
        net::wild::Predictor::Pose s;
        if (!p.carcass && p.pred.sample(now, s)) {
            Vector3 pos{s.x, s.y, s.z}, euler{s.pitch, s.yaw, s.roll};
            void* pos_args[] = {&pos};
            void* rot_args[] = {&euler};
            il2::invoke(api.set_position, target(p.transform), pos_args);
            il2::invoke(api.set_euler, target(p.transform), rot_args);
        }
        apply_params(p, it->first);
        apply_states(p, now);
        ++it;
    }
}

void play_sounds(const std::vector<net::AnimalSound>& sounds);

// Everything that comes in: frames, control, sounds.
void own_receive(const std::string& scene, bool in_game, double now) {
    const bool host = hosting_now();
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::WildFrame, in);
    for (const net::Packet& pk : in) {
        net::Reader r = net::packet_body(pk);
        net::WildFrameMsg m;
        if (!net::read_wild_frame(r, m)) continue;
        if (host) {
            m.owner = pk.from;  // (each friend speaks for itself)
            m.animals = host_ingest(pk.from, m.scene, m.animals, now);
            if (!m.animals.empty()) {
                net::Writer w(net::Msg::WildFrame);
                net::write_wild_frame(w, m);
                if (w.ok()) netrt::send_packet_except(pk.from, w, false);
            }
        }
        if (!in_game || m.scene != scene) continue;
        for (const net::AnimalRec& rec : m.animals) puppet_record(rec, m.t_ms, pk.received_ms, now);
    }
    in.clear();
    netrt::drain_packets(net::Msg::WildCtl, in);
    for (const net::Packet& pk : in) {
        net::Reader r = net::packet_body(pk);
        net::WildCtlMsg m;
        if (!net::read_wild_ctl(r, m)) continue;
        switch (m.op) {
            case net::WildOp::Controllers:
                if (host) break;
                g_controllers.clear();
                for (const net::WildSceneControl& c : m.scenes) g_controllers[c.scene] = {c.player, c.fresh != 0};
                break;
            case net::WildOp::Release:
                if (!host) release_local(m.id, m.to, m.epoch, now);
                break;
            case net::WildOp::Take:
                if (host) host_take(m, pk.from, now);
                else if (m.to == self_id()) take_local(m, now);
                break;
            case net::WildOp::Refuse:
                if (host) host_refuse(m.id, m.epoch, pk.from, now);
                break;
        }
    }
    in.clear();
    netrt::drain_packets(net::Msg::WildSounds, in);
    std::vector<net::AnimalSound> sounds;
    for (const net::Packet& pk : in) {
        net::Reader r = net::packet_body(pk);
        std::vector<net::AnimalSound> part;
        if (!net::read_sounds(r, part)) continue;
        if (host && !part.empty()) {
            net::Writer w(net::Msg::WildSounds);
            net::write_sounds(w, part);
            if (w.ok()) netrt::send_packet_except(pk.from, w, false);
        }
        sounds.insert(sounds.end(), part.begin(), part.end());
    }
    if (in_game) play_sounds(sounds);
}

void own_send_sounds(std::vector<net::AnimalSound>& sounds) {
    constexpr size_t kPerPacket = (net::kMaxPacket - 8) / net::kAnimalSoundSize;
    for (size_t at = 0; at < sounds.size(); at += kPerPacket) {
        const std::vector<net::AnimalSound> part(sounds.begin() + static_cast<ptrdiff_t>(at),
                                                 sounds.begin() + static_cast<ptrdiff_t>(std::min(sounds.size(), at + kPerPacket)));
        net::Writer w(net::Msg::WildSounds);
        net::write_sounds(w, part);
        if (w.ok()) netrt::send_packet(w, false);
    }
}

// Leaving the scene (or ownership): copies go, our hidden animals come back if the scene stays (co-op off).
void own_reset(const char* why, bool restore_hidden) {
    if (g_own_scene.empty() && g_puppets.empty() && g_owned.empty() && g_hidden.empty()) return;
    const size_t puppets = g_puppets.size(), owned = g_owned.size();
    for (auto& [id, p] : g_puppets) remove_puppet(p);
    g_puppets.clear();
    for (auto& [id, o] : g_owned) drop(o.ai);
    g_owned.clear();
    for (auto& [id, d] : g_dying) drop(d.ai);
    g_dying.clear();
    g_initial.clear();
    g_ids.clear();
    int restored = 0;
    for (auto& [addr, h] : g_hidden) {
        Il2CppObject* go = target(h);
        if (restore_hidden && go && alive(go)) {
            Il2CppObject* ai = base_ai_of(go);
            if (!ai || !dead_animal(ai)) {
                set_active(go, true);
                ++restored;
            }
        }
        drop(h);
    }
    g_hidden.clear();
    g_pool_released = false;
    {
        std::lock_guard lock(g_sound_mutex);
        g_emitter_owner.clear();
    }
    g_friend_spots.clear();
    g_near_friend.clear();
    logx::info("wildlife: ownership: left '%s' (%s): %zu copies gone, %zu of ours no longer shared%s", g_own_scene.c_str(),
               why, puppets, owned, restore_hidden ? (", " + std::to_string(restored) + " hidden ones back").c_str() : "");
    g_own_scene.clear();
}

void own_frame(const std::string& scene, bool in_game, double now) {
    const bool host = hosting_now();
    const std::vector<Present> players = present_players(scene, in_game);
    if (host) host_arbitrate(players, now);
    if (!in_game) {
        own_reset("not in the game", false);
        own_receive(scene, false, now);
        if (!host) handle_attacks(now, false);
        g_mirroring = false;
        return;
    }
    if (g_own_scene != scene) {
        own_reset("another scene", false);
        g_own_scene = scene;
        g_own_last_capture = 0;
        g_own_arrived_ms = now;
        logx::info("wildlife: ownership on in '%s' (we %s)", scene.c_str(),
                   host ? "are the arbiter" : "follow the host's choices");
    }
    own_receive(scene, true, now);
    own_capture(scene, players, now);
    own_puppets_frame(now);
    if (!host) {
        // For the carcasses (wildlife_carcass): the host's are followed while it's here; kept while it's away.
        const bool host_here = player_in_scene(players, 0, scene);
        if (g_mirroring && !host_here) g_took_over_scene = scene;
        if (host_here) g_took_over_scene.clear();
        g_mirroring = host_here;
        handle_attacks(now, true);
    }
}

std::string describe_ownership() {
    std::string out;
    char line[200];
    snprintf(line, sizeof(line), "ownership %s; we run %zu animal(s), show %zu copies, keep %zu hidden; registry %zu",
             own_mode() ? "on" : "off", g_owned.size(), g_puppets.size(), g_hidden.size(), g_registry.size());
    out = line;
    for (const auto& [id, o] : g_owned) {
        snprintf(line, sizeof(line), "\n  ours #%u epoch %u", id, o.epoch);
        out += line;
    }
    for (const auto& [id, e] : g_registry) {
        snprintf(line, sizeof(line), "\n  #%u %s in '%s' owner %s epoch %u%s%s at (%.0f, %.0f, %.0f) hp %.0f mode %u target %s", id,
                 kind_name(e.last.kind), e.scene.c_str(), who_name(e.owner).c_str(), e.epoch, e.frozen ? " FROZEN" : "",
                 e.pending_to != net::kNoPlayer ? (" -> " + who_name(e.pending_to)).c_str() : "", e.last.x, e.last.y,
                 e.last.z, e.last.hp, e.last.mode, who_name(e.last.target).c_str());
        out += line;
    }
    for (const auto& [scene, c] : g_controllers) {
        snprintf(line, sizeof(line), "\n  '%s': %s spawns%s", scene.c_str(), who_name(c.player).c_str(), c.fresh ? " (fresh)" : "");
        out += line;
    }
    return out;
}
