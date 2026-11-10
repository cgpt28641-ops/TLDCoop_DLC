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
//
// v45, stage 2: the registry goes with the host's save (world_keep, WildOp::Keep records) and animals nobody is near
// stay frozen there (game hours noted); whoever comes near one again gets it as it would be by now: one bleeding out
// died meanwhile (its carcass where it was), one left alone for an hour or more is somewhere else in its spawn region
// (kTakeMoved). An animal TLD puts away in its owner's game is gone from the registry too (Gone). A carcass a friend's
// game kept goes to the host with what was harvested from it (kTakeBody).
// Stage 3 (wildlife_regions.inc): the spawning game counts animals alive in other games and every death anywhere
// (Kill), and the regions' respawn state goes from one spawning game to the next (Regions, kept with the save).

constexpr double kOwnArbiterMs = 250;
constexpr double kOwnControllersMs = 2000;
constexpr double kOwnPendingMs = 3000;
constexpr double kOwnSilentMs = 3000;            // host: an owner in the scene that stopped sending an animal
constexpr double kFrozenForgetHours = 7 * 24;    // host: frozen animals forgotten after this many game hours
constexpr double kMovedAfterHours = 1.0;         // a frozen animal left alone this long is placed anew in its region
constexpr double kRegionsReportMs = 20000;       // the spawning game reports its regions this often
constexpr double kBodyEveryMs = 10000;           // a friend keeping a carcass (the host away) sends it this often
constexpr double kOwnBlockMs = 60000;            // host: a game that couldn't take an animal isn't asked again for this long
constexpr double kOwnPuppetStaleMs = 2500;
constexpr double kOwnWrongMs = 2000;

bool g_own_config = true;  // host: coop.ini [wildlife] ownership (default 1)
bool g_own_record = true;  // [wildlife] handover_record (default 1): TLD's own record of the animal goes with it
                           // (BaseAi.Serialize / Deserialize); 0: only what we read ourselves
bool g_own_was = false;
int g_per_player_cfg = 50;  // v46: [wildlife] per_player (the host's): % more animals for each player beyond the first
int g_per_player = 50;      // ...as the host says (a friend: from Controllers)
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
    // v45
    uint32_t region = 0;     // its spawn region (kept with it from owner to owner)
    bool dead_told = false;  // its death was told (Kill), or it came to us dead
    std::string body;        // a carcass handed to us: what was harvested from it, put on once TLD has made its body
    double body_at = 0;
    double body_sent_ms = 0; // a friend: when the host last got our carcass of it
    bool moved = false;      // kTakeMoved: placed anew in its region once TLD has started it
    int rescued = 0;         // TLDCoop_DLC: times TLD switched it off right after we took it and we put it back
    bool item_made = false;  // TLDCoop_DLC: a dead rabbit/ptarmigan made a carcass item: forgotten when it goes
};
std::map<uint32_t, Owned> g_owned;
// A friend's animal that died here while the host was in the scene: the host runs its carcass; ours stays (dying) until
// the host's carcass takes its place here (carcass_made).
struct Dying {
    uintptr_t ai = 0;
    double since_ms = 0;
    bool small = false;  // TLDCoop_DLC: a rabbit or ptarmigan (the host makes it an item for everyone)
};
std::unordered_map<uint32_t, Dying> g_dying;
constexpr double kDyingKeepMs = 20000;  // ...or this long at most (small game becomes an item on the host instead)
// TLDCoop_DLC: a dead rabbit or ptarmigan of ours goes after its fall (Shatsca 04.11 22:53, "the rabbit doubles after the
// kill": ours lay here 20 s beside the host's carcass item, and TLD lets anyone pick a dead rabbit up)
constexpr double kDyingSmallMs = 1500;
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
    // TLDCoop_DLC: not while our own scene is still loading (the scene name changes before ownership is on there: the
    // host was picked to keep bodies it couldn't take, "can't take #… : we aren't in its scene", 62 times on 04.11)
    if (my_in_game && me.valid && (g_own_scene.empty() || g_own_scene == my_scene)) {
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
    // v45
    double frozen_h = -1;    // game hours when it was frozen (-1: unknown)
    std::string body;        // a carcass: BodyHarvest.Serialize() as its last owner had it
    bool quiet_logged = false;
    double owned_ms = 0;     // when its owner last changed (TLDCoop_DLC: a put-away soon after it is a game that can't keep it)
};
std::unordered_map<uint32_t, Entry> g_registry;
// TLDCoop_DLC: a player whose game put animals away within seconds of being handed them (TLD's own reasons there):
// the spawning game respawned each one, handed it on again, and the host saw a pack of wolves come and go
// (Shatsca: "horda wilków", ~40 wolves in 2 min). Not handed anything for a minute after that.
std::array<double, net::kMaxPlayers> g_keeps_nothing_until{};
std::array<int, net::kMaxPlayers> g_quick_put_aways{};
std::array<double, net::kMaxPlayers> g_quick_put_away_ms{};  // the last one (two within a minute count)
net::wild::RateLimit g_handovers{4, 6};
double g_last_arbiter = 0, g_last_controllers = 0;
std::array<std::pair<std::string, double>, net::kMaxPlayers> g_arrival{};  // host: where each player is, since when
std::string g_controllers_sent;

// v45 (stage 3): spawn regions across games.
std::unordered_map<uint32_t, std::unordered_set<uint32_t>> g_away;  // controller: region key -> ids of its animals we
                                                                    // handed to other games (alive there)
struct RegionStore {
    net::WildCtlMsg msg;  // Regions: scene, hours (host's game hours when reported), states relative to then
    uint8_t reporter = net::kNoPlayer;
};
std::unordered_map<std::string, RegionStore> g_region_store;               // host: per scene
std::unordered_map<std::string, std::vector<net::WildCtlMsg>> g_kills_queued;  // host: deaths for a scene nobody spawns in
std::unordered_map<std::string, net::WildCtlMsg> g_regions_in;  // a controller: the state to put on our regions
std::vector<net::WildCtlMsg> g_kills_in;                        // ...and deaths to count, after it
bool g_regions_applied = false;  // this visit
double g_last_regions_report = 0;

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
    if (e.last.flags & net::kAnimalDead) {
        t.flags |= net::kTakeDead;  // a body
        t.text = e.body;            // v45: what was harvested from it
        if (!e.body.empty()) t.flags |= net::kTakeBody;
    }
    return t;
}

// --- v45, host: deaths and spawn regions across games --------------------------------------------------------------

void kill_local(const net::WildCtlMsg& k);
void regions_local(const net::WildCtlMsg& m);

// A death (or an animal gone) for the game that spawns in its scene; kept until one does.
void tell_controller_kill(const net::WildCtlMsg& k) {
    const auto c = g_controllers.find(k.scene);
    if (c == g_controllers.end() || c->second.player == net::kNoPlayer) {
        auto& q = g_kills_queued[k.scene];
        if (q.size() < 64) q.push_back(k);
        return;
    }
    if (c->second.player == self_id()) {
        kill_local(k);
        return;
    }
    net::Writer w(net::Msg::WildCtl, 128);
    net::write_wild_ctl(w, k);
    netrt::send_packet_to(c->second.player, w, true);
}

net::WildCtlMsg kill_of(const Entry& e, uint8_t flags, uint8_t died_on) {
    net::WildCtlMsg k;
    k.op = net::WildOp::Kill;
    k.id = e.id;
    k.flags = flags;
    k.died_on = died_on;
    k.kind = e.last.kind;
    k.region = e.last.region;
    k.scene = e.scene;
    return k;
}

// The owner's game put it away (TLD's own reasons): gone from the registry, and from the spawning game's count.
void host_gone(uint32_t id, uint8_t from) {
    const auto it = g_registry.find(id);
    if (it == g_registry.end() || it->second.owner != from) return;
    logx::info("wildlife: #%u (%s) is gone from %s's game (TLD put it away): forgotten", id, kind_name(it->second.last.kind),
               who_name(from).c_str());
    if (from < net::kMaxPlayers && from != 0 && it->second.owned_ms > 0 && now_ms() - it->second.owned_ms < 20000) {
        if (now_ms() - g_quick_put_away_ms[from] > 60000) g_quick_put_aways[from] = 0;
        g_quick_put_away_ms[from] = now_ms();
        if (++g_quick_put_aways[from] >= 2) {
            g_quick_put_aways[from] = 0;
            g_keeps_nothing_until[from] = now_ms() + 60000;
            logx::info("wildlife: %s's game puts animals away right after taking them: none handed to it for a minute",
                       who_name(from).c_str());
        }
    }
    tell_controller_kill(kill_of(it->second, net::kKillGone, from));
    g_registry.erase(it);
}

// The spawning game's regions as it reported them: kept for the next game to spawn there (and with the save).
void host_store_regions(net::WildCtlMsg m, uint8_t from) {
    const double h = game_hours();
    if (h < 0 || m.regions.empty()) return;
    m.hours = h;
    RegionStore& st = g_region_store[m.scene];
    const bool first = st.msg.regions.empty();
    st.msg = std::move(m);
    st.reporter = from;
    if (first)
        logx::info("wildlife: %s's spawn regions in '%s' (%zu) kept for whoever spawns there next", who_name(from).c_str(),
                   st.msg.scene.c_str(), st.msg.regions.size());
}

// A new spawning game in `scene`: the regions' state as last known (times brought up to now), and deaths meanwhile.
void host_hand_regions(const std::string& scene, uint8_t to) {
    if (to == net::kNoPlayer) return;
    if (const auto st = g_region_store.find(scene); st != g_region_store.end() && st->second.reporter != to) {
        const double h = game_hours();
        net::WildCtlMsg m = st->second.msg;
        const float since = h >= 0 && m.hours >= 0 ? static_cast<float>(h - m.hours) : 0.0f;
        for (net::WildRegionState& g : m.regions) {
            g.next_h -= since;
            g.trap_reset_h -= since;
            g.cooldown_h -= since;
        }
        m.hours = h;
        logx::info("wildlife: %s spawns in '%s' now: %zu spawn region(s) as %s left them (%.1f h ago)", who_name(to).c_str(),
                   scene.c_str(), m.regions.size(), who_name(st->second.reporter).c_str(), since);
        if (to == self_id()) {
            regions_local(m);
        } else {
            net::Writer w(net::Msg::WildCtl, net::kMaxMessage);
            net::write_wild_ctl(w, m);
            netrt::send_packet_to(to, w, true);
        }
    }
    if (const auto q = g_kills_queued.find(scene); q != g_kills_queued.end()) {
        const std::vector<net::WildCtlMsg> kills = std::move(q->second);
        g_kills_queued.erase(q);
        if (!kills.empty())
            logx::info("wildlife: %zu death(s) in '%s' while nobody spawned there: to %s", kills.size(), scene.c_str(),
                       who_name(to).c_str());
        for (const net::WildCtlMsg& k : kills) tell_controller_kill(k);
    }
}

// A frozen animal handed out again: as it would be by now (stage 2, "in the background").
void age_frozen(Entry& e, net::WildCtlMsg& t) {
    const double now_h = game_hours();
    const double from_h = e.frozen_h;
    e.frozen_h = -1;
    if (from_h < 0 || now_h < 0 || now_h <= from_h || (e.last.flags & net::kAnimalDead)) return;
    const double h = now_h - from_h;
    if ((e.last.flags & net::kAnimalBleeding) && e.last.bleed_limit > 0) {
        e.last.bleed_elapsed += static_cast<float>(h * 60.0);
        if (e.last.bleed_elapsed >= e.last.bleed_limit) {
            logx::info("wildlife: #%u (%s) bled out while nobody was near (%.1f h): its carcass lies where it was", e.id,
                       kind_name(e.last.kind), h);
            e.last.flags = static_cast<uint8_t>((e.last.flags | net::kAnimalDead) & ~net::kAnimalBleeding);
            e.last.mode = kAiModeDead;
            e.last.hp = 0;
            e.last.target = net::kNoPlayer;
            e.text.clear();
            e.body.clear();
            tell_controller_kill(kill_of(e, 0, net::kNoPlayer));
            const uint8_t to = t.to;
            const uint16_t epoch = t.epoch;
            t = take_from_registry(e, to, epoch);
            return;
        }
        t.state.bleed_elapsed = e.last.bleed_elapsed;
    }
    if (h >= kMovedAfterHours) {
        t.flags |= net::kTakeMoved;
        t.hours = h;
        t.state.target = net::kNoPlayer;
        t.text.clear();  // (its old plans: where it fled to, what it stalked)
        logx::info("wildlife: #%u (%s) was left alone %.1f h: it has moved on in its spawn region", e.id, kind_name(e.last.kind), h);
    }
}

void transfer(Entry& e, uint8_t to, const char* why, const std::vector<Present>& players, double now) {
    const uint16_t epoch = static_cast<uint16_t>(e.epoch + 1);
    logx::info("wildlife: #%u owner %s -> %s (%s)", e.id, who_name(e.owner).c_str(), who_name(to).c_str(), why);
    e.owned_ms = now;
    const bool owner_here = e.owner != net::kNoPlayer && player_in_scene(players, e.owner, e.scene);
    const bool was_frozen = e.frozen;
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
    net::WildCtlMsg t = take_from_registry(e, to, epoch);
    if (was_frozen) age_frozen(e, t);
    deliver_take(t, now);
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
        logx::info("wildlife: #%u owner %s -> %s (it died: the host keeps its carcass%s)", t.id, who_name(from).c_str(),
                   who_name(0).c_str(), (t.flags & net::kTakeBody) ? ", as harvested there" : "");
        if (t.flags & net::kTakeBody) e.body = t.text;
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
            // (only new handovers: what they run already stays theirs)
            if (p.id < net::kMaxPlayers && p.id != e.owner && now < g_keeps_nothing_until[p.id]) continue;
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
        // v45: nobody runs it (frozen, or its owner left): only someone near enough takes it; out of everyone's
        // range it stays as it was, frozen
        bool near_one = false;
        for (const net::wild::Seat& st : seats) near_one |= st.metres <= kRangeMetres;
        const bool dead_now = (e.last.flags & net::kAnimalDead) != 0;
        if (seats.empty() || (!owner_here && !near_one && !(dead_now && host_here))) {
            if (!e.frozen) {
                e.frozen = true;
                e.frozen_ms = now;
                e.frozen_h = game_hours();
                logx::info("wildlife: #%u (%s) frozen in '%s' at (%.0f, %.0f, %.0f), hp %.0f: nobody is %s", e.id,
                           kind_name(e.last.kind), e.scene.c_str(), e.last.x, e.last.y, e.last.z, e.last.hp,
                           seats.empty() ? "there" : "near it");
                e.owner = net::kNoPlayer;
            }
            const double h = game_hours();
            if (e.frozen_h >= 0 && h >= 0 && h - e.frozen_h > kFrozenForgetHours) {
                logx::info("wildlife: #%u (%s) forgotten (nobody came near it for %.0f game hours)", e.id,
                           kind_name(e.last.kind), h - e.frozen_h);
                if (!dead_now) tell_controller_kill(kill_of(e, net::kKillGone, net::kNoPlayer));
                it = g_registry.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        if (owner_here && now - e.last_ms > kOwnSilentMs) {
            // v45: out of everyone's range; its owner still runs it (and says so if TLD puts it away: Gone)
            if (!e.quiet_logged)
                logx::info("wildlife: #%u (%s) is out of everyone's range; %s still runs it", e.id, kind_name(e.last.kind),
                           who_name(e.owner).c_str());
            e.quiet_logged = true;
        } else {
            e.quiet_logged = false;
        }
        const bool dead = (e.last.flags & net::kAnimalDead) != 0;
        if (dead && !owner_here) {
            // A body: the host keeps it (its carcass is shared from there); with the host away it waits, frozen.
            // (Shatsca 08.11 17:05: the host couldn't make a bear for a body and was handed it again every 250 ms, 1317
            // times in 6 min: a refusal blocks the host too, as it does anyone else)
            if (host_here && now >= e.blocked_until[0] && g_handovers.take(now)) transfer(e, 0, "its owner left; the host keeps the body", players, now);
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
    m.per_player = static_cast<uint8_t>(g_per_player_cfg);
    for (const auto& [scene, c] : scenes) {
        summary += scene + "=" + std::to_string(c.player) + (c.fresh ? "*" : "") + " ";
        m.scenes.push_back({scene, c.player, static_cast<uint8_t>(c.fresh ? 1 : 0)});
    }
    std::vector<std::pair<std::string, uint8_t>> new_controllers;
    for (const auto& [scene, c] : scenes) {
        const auto old = g_controllers.find(scene);
        if (old == g_controllers.end() || old->second.player != c.player) new_controllers.emplace_back(scene, c.player);
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
    // v45: a new spawning game gets the regions' state (after Controllers: it knows it spawns there by then)
    for (const auto& [scene, player] : new_controllers) host_hand_regions(scene, player);
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
    a.flags |= net::variant_flags(variant_of(ai));  // TLDCoop_DLC (v50): which wolf
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
Il2CppObject* pool_take(int kind, bool aurora, int variant = 0, bool strict = true) {
    // TLDCoop_DLC (v50): the same wolf (timberwolf, starving, ordinary) and aurora or not; else the same wolf; else any
    // of the kind (Shatsca 06.11: a timberwolf handed over became an ordinary wolf)
    Il2CppObject* fallback = nullptr;
    uintptr_t fallback_key = 0;
    int fallback_score = -1;
    for (const auto& [addr, h] : g_hidden) {
        Il2CppObject* go = target(h);
        Il2CppObject* ai = go && alive(go) ? base_ai_of(go) : nullptr;
        if (!ai || il2::read_field<int32_t>(ai, api.ai_kind) != kind || dead_animal(ai)) continue;
        if (g_fight.active && target(g_fight.ai) == ai) continue;
        const bool is_aurora = api.ai_wildlife_mode >= 0 && il2::read_field<int32_t>(ai, api.ai_wildlife_mode) == 1;
        const bool same_wolf = variant_of(ai) == variant;
        if (is_aurora == aurora && same_wolf) {
            uintptr_t handle = h;
            g_hidden.erase(addr);
            drop(handle);
            return ai;
        }
        const int score = (same_wolf ? 2 : 0) + (is_aurora == aurora ? 1 : 0);
        if (score > fallback_score) fallback = ai, fallback_key = addr, fallback_score = score;
    }
    // a stand-in of another wolf would change before their eyes: one is made instead, if it can be
    if (fallback && strict && variant_of(fallback) != variant) return nullptr;
    if (fallback) {
        uintptr_t handle = g_hidden[fallback_key];
        g_hidden.erase(fallback_key);
        drop(handle);
    }
    return fallback;
}

// v45: whether anyone we know of stands within `metres` of `p` in our scene.
bool player_near(const Vector3& p, float metres) {
    auto close = [&](float x, float y, float z) {
        const float dx = p.x - x, dy = p.y - y, dz = p.z - z;
        return dx * dx + dy * dy + dz * dz <= metres * metres;
    };
    const game::PlayerState me = game::read_player();
    if (me.valid && close(me.position.x, me.position.y, me.position.z)) return true;
    for (const net::StateMsg& st : netrt::remote_states())
        if (st.valid && st.scene == g_own_scene && close(st.x, st.y, st.z)) return true;
    return false;
}

// v45 (kTakeMoved): somewhere else in its spawn region, out of everyone's sight (TLD's own spawn spot choice).
bool moved_spot(uint32_t key, Vector3& pos, float& yaw, double now) {
    Il2CppObject* r = region_by_key(key, now);
    if (!r || !api.region_try_pos) return false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        Vector3 p{};
        Quaternion q{0, 0, 0, 1};
        void* args[] = {&p, &q};
        bool ok = false;
        if (!il2::invoke_value(api.region_try_pos, r, ok, args) || !ok || !std::isfinite(p.x)) continue;
        if (player_near(p, 80.0f)) continue;
        pos = p;
        yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z), 1.0f - 2.0f * (q.y * q.y + q.x * q.x)) * 180.0f / 3.14159265f;
        return true;
    }
    return false;
}

std::string body_text(Il2CppObject* ai) {
    Il2CppObject* bh = ai && api.ai_body >= 0 ? il2::read_field<Il2CppObject*>(ai, api.ai_body) : nullptr;
    if (!bh || !api.bh_serialize) return {};
    auto* text = reinterpret_cast<Il2CppString*>(il2::invoke(api.bh_serialize, bh));
    std::string out = text ? il2::to_utf8(text) : std::string();
    return out.size() <= net::kWildTextMax ? out : std::string();
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
    float yaw = s.yaw;
    const bool dead_given = (t.flags & net::kTakeDead) != 0 || (s.flags & net::kAnimalDead) != 0;
    bool moved = false;
    if ((t.flags & net::kTakeMoved) && !dead_given) moved = moved_spot(s.region, pos, yaw, now);
    for (auto& [key, ids] : g_away) ids.erase(t.id);  // v45: (ours to count again, as one of ours here)
    const int variant = net::animal_variant(s.flags);  // TLDCoop_DLC (v50): which wolf
    // TLDCoop_DLC (Shatsca 09.11: "the stag's carcass vanished as Szymon came near"): a dead one handed over is made
    // for it first; one of our own from the spawn region's pool, dead, was removed by TLD 1.6 s later, and the body with it
    Il2CppObject* ai = nullptr;
    bool spawned = false;
    if (dead_given) {
        ai = spawn_stand_in(s.kind, pos, yaw, kAiModeWander, true, variant);
        spawned = ai != nullptr;
    }
    if (!ai) ai = pool_take(s.kind, aurora, variant);
    if (!ai) {
        ai = spawn_stand_in(s.kind, pos, yaw, kAiModeWander, true, variant);
        spawned = ai != nullptr;
    }
    if (!ai) ai = pool_take(s.kind, aurora, variant, false);  // (none could be made: any of the kind after all)
    if (ai && variant_of(ai) != variant)
        logx::warn("wildlife: #%u is a %s; only a %s could take its place here", t.id, variant_name(variant),
                   variant_name(variant_of(ai)));
    if (!ai) return refuse("no animal of that kind here to take its place, and none could be made");
    Il2CppObject* go = go_of(ai);
    set_active(go, true);
    const bool dead_take = dead_given;
    if (!dead_take && !moved && g_own_record && !t.text.empty() && api.ai_deserialize) {
        void* args[] = {il2::il2cpp_string_new(t.text.c_str())};
        il2::invoke(api.ai_deserialize, ai, args);
    }
    const float half = yaw * 0.5f * 3.14159265f / 180.0f;
    Quaternion rot{0, std::sin(half), 0, std::cos(half)};
    if (api.ai_set_pos_rot) {
        void* args[] = {&pos, &rot};
        il2::invoke(api.ai_set_pos_rot, ai, args);
    } else {
        Vector3 facing{0, yaw, 0};
        void* pa[] = {&pos};
        void* ra[] = {&facing};
        il2::invoke(api.set_position, transform_of(ai), pa);
        il2::invoke(api.set_euler, transform_of(ai), ra);
    }
    // TLDCoop_DLC: handed an animal in a scene another game spawns in, our own spawn region (it took it in with TLD's
    // record) counted it as one of its own over its target (the spawning game's is higher: [wildlife] per_player) and
    // switched it off within a second or two (Szymon's log: "we run #... now" then "TLD switched off it"), the spawning
    // game spawned another, and so on: the host's pack of wolves. Out of our region's list (not a death there).
    const std::string own_scene = g_own_scene;
    if (!dead_given && !i_control(own_scene)) detach_from_region(ai);
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
        if (moved && mode != kAiModeWander && s.mode != 4 /* Flee */) mode = kAiModeWander;
        if (il2::read_field<int32_t>(ai, api.ai_mode) != mode) {
            void* args[] = {&mode};
            il2::invoke(api.set_mode, ai, args);
        }
        if (!moved) set_animator(il2::read_field<Il2CppObject*>(ai, api.ai_animator), s);
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
    o.region = s.region ? s.region : region_key(region_of(ai));
    o.dead_told = dead_take;  // (it died elsewhere: told there)
    if (dead_take && (t.flags & net::kTakeBody) && !t.text.empty()) {
        o.body = t.text;
        o.body_at = now + 1500;  // once TLD has made its carcass
    }
    g_owned[t.id] = std::move(o);
    if (moved)
        logx::info("wildlife: #%u was left alone %.1f h: placed anew in its spawn region at (%.0f, %.0f, %.0f)", t.id, t.hours,
                   pos.x, pos.y, pos.z);
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
    t.state.region = o->second.region;
    if (api.ai_serialize && g_own_record)
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
    // Our real one: hidden, kept (it may become another animal handed to us). v45: arrows stuck in it drop where it
    // stands (they'd be gone with it, or turn up in another animal it becomes); out of its spawn region's list (TLD
    // would switch it back on), and the spawning game counts it as alive elsewhere.
    Il2CppObject* go = go_of(ai);
    free_stuck_arrows(go);
    detach_from_region(ai);
    if (i_control(g_own_scene) && t.state.region) g_away[t.state.region].insert(id);
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

// --- v45, every game: spawn regions where we spawn ------------------------------------------------------------------

void kill_local(const net::WildCtlMsg& k) {
    if (g_kills_in.size() < 128) g_kills_in.push_back(k);
}
void regions_local(const net::WildCtlMsg& m) { g_regions_in[m.scene] = m; }

// Where we spawn: the regions' state from the last game that spawned here, the deaths elsewhere counted, what the
// regions should count as alive (population_detour), and our regions' state for the host, now and then.
void own_regions(const std::string& scene, bool control, double now) {
    g_pop_extra.clear();
    if (!control) return;
    const double now_h = game_hours();
    if (now_h < 0) return;
    if (const auto in = g_regions_in.find(scene); in != g_regions_in.end()) {
        int put = 0;
        for (const net::WildRegionState& g : in->second.regions)
            if (Il2CppObject* r = region_by_key(g.key, now)) {
                apply_region(r, g, now_h);
                ++put;
            }
        if (put || now - g_own_arrived_ms > 15000) {  // (the regions may not be up yet just after loading)
            logx::info("wildlife: %d of %zu spawn region(s) here as the last game spawning here left them", put,
                       in->second.regions.size());
            g_regions_in.erase(in);
            g_regions_applied = true;
            g_last_regions_report = now;  // (not reported back before they had a moment)
        }
    }
    if (g_regions_in.count(scene)) return;  // its kills after it
    for (auto it = g_kills_in.begin(); it != g_kills_in.end();) {
        const net::WildCtlMsg& k = *it;
        if (k.scene != scene) {
            it = g_kills_in.erase(it);
            continue;
        }
        for (auto& [key, ids] : g_away) ids.erase(k.id);
        if (!(k.flags & net::kKillGone) && !((k.flags & net::kKillCounted) && k.died_on == self_id())) {
            if (Il2CppObject* r = region_by_key(k.region, now)) {
                count_death(r, now_h);
                logx::info("wildlife: #%u (%s) died in %s: its spawn region here waits to respawn it, as TLD would",
                           k.id, kind_name(k.kind), who_name(k.died_on).c_str());
            } else if (k.region) {
                logx::info("wildlife: #%u died, but its spawn region isn't here (yet?): not counted", k.id);
            }
        }
        it = g_kills_in.erase(it);
    }
    // What each region should count as alive though it isn't active here: its animals in other games, and ours kept
    // switched off for handovers (still on its list).
    for (const auto& [addr, h] : g_hidden) {
        if (g_detached.count(addr)) continue;
        Il2CppObject* go = target(h);
        Il2CppObject* ai = go && alive(go) ? base_ai_of(go) : nullptr;
        if (!ai || dead_animal(ai)) continue;
        if (Il2CppObject* r = region_of(ai)) ++g_pop_extra[reinterpret_cast<uintptr_t>(r)];
    }
    for (const auto& [key, ids] : g_away)
        if (!ids.empty())
            if (Il2CppObject* r = region_by_key(key, now)) g_pop_extra[reinterpret_cast<uintptr_t>(r)] += static_cast<int32_t>(ids.size());
    if (now - g_last_regions_report >= kRegionsReportMs) {
        g_last_regions_report = now;
        net::WildCtlMsg m;
        m.op = net::WildOp::Regions;
        m.scene = scene;
        for (Il2CppObject* r : scene_regions()) {
            if (m.regions.size() >= net::kWildRegionsMax) break;
            if (const uint32_t key = region_key(r)) m.regions.push_back(read_region(r, key, now_h));
        }
        if (m.regions.empty()) return;
        if (hosting_now()) host_store_regions(std::move(m), self_id());
        else send_ctl(m);
    }
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
    add_inside_spot();
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
            // v45: one of ours we hid when we came that TLD switched back on a while later, where we spawn, is TLD's own
            // (re)spawn there (its region was short of animals): shared like any other
            const auto hidden_it = g_hidden.find(reinterpret_cast<uintptr_t>(go));
            const bool respawned = control && hidden_it != g_hidden.end() && now - g_own_arrived_ms > kInitialMs * 3;
            const bool initial_hide = !g_pool_released && g_initial.count(key) && !respawned;
            if (respawned) {
                logx::info("wildlife: TLD switched one of our hidden %s back on (its spawn region was short): shared from now",
                           kind_name(il2::read_field<int32_t>(ai, api.ai_kind)));
                drop(hidden_it->second);
                g_hidden.erase(hidden_it);
                // TLDCoop_DLC: no longer one of those we hid on arrival (Kerel 04.11 20:31-20:35: hidden again at once,
                // switched on again by TLD a second later, 51 times: nothing TLD spawned near him while the host was in a
                // house ever stayed)
                g_initial.erase(key);
            }
            if (!control || initial_hide) {
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
            o.region = region_key(region_of(ai));
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
            if (own.state.hp > 0) wildlife_carcass::unfreeze(ai);  // TLDCoop_DLC: alive: its body isn't frozen
            write_field<bool>(ai, api.ai_bleeding, (own.state.flags & net::kAnimalBleeding) != 0);
            write_field<float>(ai, api.ai_bleed_elapsed, own.state.bleed_elapsed);
            write_field<float>(ai, api.ai_bleed_limit, own.state.bleed_limit);
        }
        const bool is_dead = dead_animal(ai);
        if (is_dead && !own.body.empty() && now >= own.body_at) {
            // v45: a carcass handed to us: as harvested where it was
            if (Il2CppObject* bh = api.ai_body >= 0 ? il2::read_field<Il2CppObject*>(ai, api.ai_body) : nullptr;
                bh && api.bh_deserialize) {
                bool scene_load = false;
                void* args[] = {il2::il2cpp_string_new(own.body.c_str()), &scene_load};
                il2::invoke(api.bh_deserialize, bh, args);
                logx::info("wildlife: #%u's carcass here as it was harvested in its last game (%zu bytes)", id, own.body.size());
            }
            // TLDCoop_DLC: killed moments ago (within 10 min): a fresh body, whatever the one that took its place had
            if (wildlife_carcass::fresh_kill(ai))
                logx::info("wildlife: #%u is a fresh kill: its carcass not frozen, condition 100%%", id);
            own.body.clear();
        }
        if (is_dead && !own.dead_told) {
            // v45: a death here: the game that spawns in this scene counts it (its respawn waits as TLD's would)
            own.dead_told = true;
            net::WildCtlMsg k;
            k.op = net::WildOp::Kill;
            k.id = id;
            k.scene = scene;
            k.region = own.region;
            k.kind = static_cast<uint8_t>(il2::read_field<int32_t>(ai, api.ai_kind));
            k.died_on = me;
            if (control && own.region && !g_detached.count(reinterpret_cast<uintptr_t>(go)) &&
                region_key(region_of(ai)) == own.region)
                k.flags |= net::kKillCounted;  // (TLD's own RemoveFromSpawnRegion did it, here where it spawns)
            logx::info("wildlife: our #%u (%s) died here%s", id, kind_name(k.kind),
                       (k.flags & net::kKillCounted) ? " (its spawn region counted it)" : ": told to the game that spawns here");
            if (host) tell_controller_kill(k);
            else send_ctl(k);
        }
        if (is_dead && !host && !host_here && now - own.body_sent_ms > kBodyEveryMs) {
            // v45: a carcass we keep while the host is away: the host has it as it is here (for when we leave)
            own.body_sent_ms = now;
            net::WildCtlMsg k;
            k.op = net::WildOp::Keep;
            k.id = id;
            k.epoch = own.epoch;
            k.flags = net::kTakeDead;
            k.scene = scene;
            k.state = own_record(ai, id, own.epoch, pos, euler);
            k.state.region = own.region;
            k.text = body_text(ai);
            if (!k.text.empty()) k.flags |= net::kTakeBody;
            send_ctl(k);
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
                t.state.region = own.region;
                t.text = body_text(ai);  // v45: as harvested here
                if (!t.text.empty()) t.flags |= net::kTakeBody;
                send_ctl(t);
                const char* kind = kind_name(t.state.kind);
                g_dying[id] = Dying{hold(ai), now, !strcmp(kind, "rabbit") || !strcmp(kind, "ptarmigan")};
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
                // Animated (a whole animal, no imposter) whether or not our camera sees it: another player does.
                int32_t culling = kCullAlwaysAnimate;
                void* cull_args[] = {&culling};
                il2::invoke(api.set_culling, animator, cull_args);
                static const ptrdiff_t on_screen = il2::field_offset_quiet(il2::find_class(kGame, "", "BaseAi"), "m_ImposterDistanceOnScreen");
                static const ptrdiff_t off_screen = il2::field_offset_quiet(il2::find_class(kGame, "", "BaseAi"), "m_ImposterDistanceOffScreen");
                static const ptrdiff_t cull_mode = il2::field_offset_quiet(il2::find_class(kGame, "", "BaseAi"), "m_ImposterCullingMode");
                write_field<float>(ai, on_screen, 100000.0f);
                write_field<float>(ai, off_screen, 100000.0f);
                write_field<int32_t>(ai, cull_mode, culling);
            }
        net::AnimalRec a = own_record(ai, id, own.epoch, pos, euler);
        a.region = own.region;
        auto emitters = g_ai_emitters.find(key);
        if (emitters == g_ai_emitters.end()) emitters = g_ai_emitters.emplace(key, wwise_emitters(go)).first;
        for (size_t e = 0; e < emitters->second.size(); ++e) owners[emitters->second[e]] = {a.id, static_cast<uint8_t>(e)};
        recs.push_back(std::move(a));
    }
    // Ours that died, kept until the host's carcass (or item) takes their place.
    for (auto it = g_dying.begin(); it != g_dying.end();) {
        if (now - it->second.since_ms < (it->second.small ? kDyingSmallMs : kDyingKeepMs)) {
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
        // TLDCoop_DLC: switched off by TLD within seconds of our taking it (Kerel 04.11 19:58: "we run #21967910 now
        // (... one made for it)", 16 ms later "TLD switched off it"): the spawn region we spawn in counted it over its
        // target. Lost, the host forgot it and the region made another (the vanishing animals, the packs). Back on, out
        // of the region's list, at most twice.
        if (ai && alive(ai) && !it->second.dead_told && it->second.rescued < 2 && now - it->second.since_ms < 15000) {
            ++it->second.rescued;
            detach_from_region(ai);
            set_active(go_of(ai), true);
            logx::info("wildlife: TLD switched off #%u %.1f s after we took it; switched back on and out of its spawn "
                       "region's list (%d)", it->first, (now - it->second.since_ms) / 1000, it->second.rescued);
            ++it;
            continue;
        }
        // TLDCoop_DLC (Shatsca 09.11: "I wounded a stag, followed its blood, then the blood and the stag were gone"):
        // switched off again and again right after we took it, it isn't gone for everyone: it goes back (Refuse), and
        // the host gives it to someone else (likely itself) as it was, wounds and all
        if (ai && alive(ai) && !it->second.dead_told && now - it->second.since_ms < 15000) {
            logx::info("wildlife: TLD keeps switching off #%u we just took; handed back, not forgotten", it->first);
            if (host) {
                host_refuse(it->first, it->second.epoch, me, now);
            } else {
                net::WildCtlMsg r;
                r.op = net::WildOp::Refuse;
                r.id = it->first;
                r.epoch = it->second.epoch;
                send_ctl(r);
            }
            for (auto k = g_ids.begin(); k != g_ids.end();) k = k->second == it->first ? g_ids.erase(k) : std::next(k);
            drop(it->second.ai);
            it = g_owned.erase(it);
            continue;
        }
        logx::info("wildlife: our #%u is gone from our game (TLD %s it)", it->first, ai && alive(ai) ? "switched off" : "removed");
        // TLDCoop_DLC: a dead one that became a carcass item (wildlife_carcass) is gone too: kept as dead, the
        // registry made it again on every visit and it became one more carcass item (Shatsca 05.11 19:45/20:21: four
        // rabbits on one spot, fourteen made again on coming back to RuralRegion).
        if (!it->second.dead_told || it->second.item_made) {  // v45: gone for everyone (the host forgets it)
            if (host) {
                host_gone(it->first, me);
            } else {
                net::WildCtlMsg g;
                g.op = net::WildOp::Gone;
                g.id = it->first;
                g.epoch = it->second.epoch;
                send_ctl(g);
            }
        }
        for (auto k = g_ids.begin(); k != g_ids.end();) k = k->second == it->first ? g_ids.erase(k) : std::next(k);
        drop(it->second.ai);
        it = g_owned.erase(it);
    }
    own_regions(scene, control, now);
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
    if (it != g_puppets.end() && (it->second.kind != rec.kind || it->second.variant != net::animal_variant(rec.flags) ||
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
void stop_dead_sounds();

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
                if (g_per_player != m.per_player)
                    logx::info("wildlife: the host's game has %d%% more animals for each player beyond the first", m.per_player);
                g_per_player = m.per_player;
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
            case net::WildOp::Gone:
                if (host) host_gone(m.id, pk.from);
                break;
            case net::WildOp::Kill:
                if (host) tell_controller_kill(m);  // (a death in a friend's game)
                else kill_local(m);                 // (we spawn there)
                break;
            case net::WildOp::Regions:
                if (host) host_store_regions(std::move(m), pk.from);
                else regions_local(m);
                break;
            case net::WildOp::Keep:
                if (host) {  // a friend's carcass, kept while we're away
                    const auto e = g_registry.find(m.id);
                    if (e != g_registry.end() && e->second.owner == pk.from) {
                        e->second.last = m.state;
                        e->second.last_ms = now;
                        if (m.flags & net::kTakeBody) e->second.body = m.text;
                    }
                }
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
    if (in_game) stop_dead_sounds();  // TLDCoop_DLC
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
    g_away.clear();  // v45
    g_pop_extra.clear();
    g_regions_in.clear();
    g_kills_in.clear();
    g_regions_applied = false;
    g_detached.clear();
    forget_region_keys();
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
    if (g_own_counter == 0) g_own_counter = static_cast<uint32_t>((GetTickCount64() * 2654435761ull) >> 7) & 0x7FFFFF;  // v45
    if (scene == "MainMenu" && (!g_registry.empty() || !g_region_store.empty())) {
        // v45: another save may come: what's kept goes with this one (world_keep reads it back)
        logx::info("wildlife: main menu: the registry (%zu) and the spawn regions kept (%zu scene(s)) are put away",
                   g_registry.size(), g_region_store.size());
        g_registry.clear();
        g_region_store.clear();
        g_kills_queued.clear();
    }
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
        g_region_variant.clear();  // TLDCoop_DLC (v50): the spawn areas are new ones
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
    size_t away = 0;
    for (const auto& [key, ids] : g_away) away += ids.size();
    int32_t counted = 0;
    for (const auto& [r, n] : g_pop_extra) counted += n;
    snprintf(line, sizeof(line), "\n  v45: %zu of ours alive in other games, %d counted by our spawn regions, %zu scene(s) of "
             "regions kept, %zu death(s) waiting for a spawning game, game hours %.1f",
             away, counted, g_region_store.size(), g_kills_queued.size(), game_hours());
    out += line;
    return out;
}

// --- v45: the registry and the spawn regions with the host's save (world_keep) -------------------------------------

void own_save_kept(std::vector<world_keep::Record>& out) {
    if (!hosting_now() || !own_mode()) return;
    const double h = game_hours();
    size_t animals = 0;
    for (const auto& [id, e] : g_registry) {
        net::WildCtlMsg k;
        k.op = net::WildOp::Keep;
        k.id = id;
        k.epoch = e.epoch;
        const bool dead = (e.last.flags & net::kAnimalDead) != 0;
        k.flags = dead ? net::kTakeDead : 0;
        k.text = dead ? e.body : e.text;
        if (dead && !e.body.empty()) k.flags |= net::kTakeBody;
        k.hours = e.frozen && e.frozen_h >= 0 ? e.frozen_h : h;
        k.scene = e.scene;
        k.state = e.last;
        net::Writer w(net::Msg::WildCtl, net::kMaxMessage);
        net::write_wild_ctl(w, k);
        if (!w.ok()) continue;
        out.push_back(world_keep::record(world_keep::Tag::Wildlife, w));
        ++animals;
    }
    for (const auto& [scene, st] : g_region_store) {
        net::Writer w(net::Msg::WildCtl, net::kMaxMessage);
        net::write_wild_ctl(w, st.msg);
        if (w.ok()) out.push_back(world_keep::record(world_keep::Tag::WildRegions, w));
    }
    logx::info("wildlife: saved with our game: %zu animal(s) of the registry, the spawn regions of %zu scene(s)", animals,
               g_region_store.size());
}

bool own_load_kept(const world_keep::Record& r) {
    const net::Packet pk = world_keep::packet(r);
    net::Reader rd = net::packet_body(pk);
    net::WildCtlMsg m;
    if (pk.type != net::Msg::WildCtl || !net::read_wild_ctl(rd, m)) return false;
    if (m.op == net::WildOp::Keep) {
        if (g_registry.count(m.id)) return true;  // (already live)
        Entry e;
        e.id = m.id;
        e.scene = m.scene;
        e.owner = net::kNoPlayer;
        e.epoch = m.epoch;
        e.last = m.state;
        e.last_ms = now_ms();
        e.frozen = true;
        e.frozen_ms = e.last_ms;
        e.frozen_h = m.hours;
        if (m.flags & net::kTakeDead) e.body = (m.flags & net::kTakeBody) ? m.text : std::string();
        else e.text = m.text;
        g_registry.emplace(m.id, std::move(e));
        return true;
    }
    if (m.op == net::WildOp::Regions) {
        RegionStore& st = g_region_store[m.scene];
        st.msg = std::move(m);
        st.reporter = net::kNoPlayer;
        return true;
    }
    return false;
}

// --- v46: more animals with more players -----------------------------------------------------------------------------

// SpawnRegion.CalculateTargetPopulation (its maximum less the trapped and the pending ones), in the game that spawns:
// the maximum grows by [wildlife] per_player % (the host's) for each player in the game beyond the first. A shared
// world with four hunters had the animals of one.
using TargetFn = int32_t (*)(Il2CppObject* self, const MethodInfo* method);
TargetFn g_target_population = nullptr;
int g_target_logged = 0;
int32_t target_detour(Il2CppObject* self, const MethodInfo* method) {
    const int32_t v = g_target_population(self, method);
    const auto role = netrt::role();
    if (v <= 0 || !self || !role) return v;
    const bool spawner = own_mode() ? i_control(game::active_scene()) : *role == net::Role::Host;
    const int per = own_mode() && *role != net::Role::Host ? g_per_player : g_per_player_cfg;
    const size_t players = netrt::status().players.size() + 1;
    if (!spawner || per <= 0 || players < 2) return v;
    const int32_t held = (api.region_trapped >= 0 ? il2::read_field<int32_t>(self, api.region_trapped) : 0) +
                         (api.region_pending >= 0 ? il2::read_field<int32_t>(self, api.region_pending) : 0);
    const int32_t most = v + std::max(held, 0);
    const double factor = 1.0 + per / 100.0 * static_cast<double>(players - 1);
    const int32_t out = std::max(v, static_cast<int32_t>(std::lround(most * factor)) - std::max(held, 0));
    if (out != v && g_target_logged++ < 10)
        logx::info("wildlife: %zu players: a spawn region keeps %d animal(s) instead of %d (+%d%% each beyond the first)",
                   players, out, v, per);
    return out;
}
