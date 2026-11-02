// TLDCoop_DLC: Seamless Interiors, milestone 2: co-op. Included by seamless_interiors.cpp (inside its anonymous
// namespace) after seamless_persist.inc. Not part of SI (a single-player mod): see si_port_plan.md §7.
//
// A room stands in the region scene, so "same scene" is no longer "same place": each player's State carries the
// building they're inside (place()). The host's game is the authority on the rooms:
// - it builds a room when any player comes near (pick_building), and tells the friends which rooms it has (Rooms);
// - a friend builds only those, rolls the template with the host's seed (loot_seed_save), never fills a room from its
//   own files and never writes room files (ensure_dir, tick_hydrate); its own player.inside record still puts its body
//   back in a room after a load;
// - a ready room's items and fires get ids made from the building and their spot in the room (coop_rekey: the game's
//   own ids of a room's objects differ from game to game), containers, objects and water sources a building-scoped
//   key (room_key), so the world modules share them like the region's;
// - when a room comes up the host sends its world lists again (netrt::resync): what is in it, as the host has it.

// --- who we are --------------------------------------------------------------------------------------------------------

Coop coop() {
    const auto r = netrt::role();
    if (!r) return Coop::Solo;
    return *r == net::Role::Host ? Coop::Host : Coop::Friend;
}

// The save whose seed rolls a room's loot: ours, or (a friend) the host's world, so a fresh room comes up the same.
std::string loot_seed_save() {
    if (coop() != Coop::Friend) return game::save_id();
    const auto w = netrt::remote_world();
    return w && !w->world_id.empty() ? w->world_id : std::string();
}

// --- which rooms each game has (Msg::Rooms) ----------------------------------------------------------------------------

struct HostRooms {
    bool have = false;
    std::string region;
    std::unordered_set<std::string> ids;
} g_host_rooms;                                                                 // friend: the host's ready rooms
std::array<std::unordered_set<std::string>, net::kMaxPlayers> g_friend_rooms;  // host: each friend's ready rooms
std::array<uint32_t, net::kMaxPlayers> g_friend_link{};  // host: the link those came on (a new link: forgotten, K5)
// Host: rooms whose items go to a friend as full records (kItemAnswer) once the lists go again (K6): a hash says an
// item is there, not where it lies or what state it's in.
struct RoomPuts {
    uint8_t player = 0;  // 0: every friend
    std::vector<std::string> ids;
    double at = 0;
};
std::vector<RoomPuts> g_room_puts, g_room_puts_due;
uint32_t g_rooms_gen = 0;
std::string g_rooms_said;  // what we last told (region and ids)
double g_rooms_said_at = 0;
struct PendingResync {
    uint8_t player = 0;  // 0: every friend
    double at = 0;
};
std::vector<PendingResync> g_resyncs;  // host: lists to send again once the world modules have taken the room in

bool host_has(const Instance& in) {
    if (!g_host_rooms.have || g_host_rooms.region != g_region || !g_host_rooms.ids.count(in.cfg->id)) return false;
    // (K2) and the host is in this region now (its Rooms may be from before it left)
    const auto st = netrt::remote_state(0);
    return !(st && st->valid && st->scene != g_region);
}

std::vector<std::string> ready_ids() {
    std::vector<std::string> out;
    for (const Instance& in : g_instances)
        if (in.stage == Stage::Ready && master_of(in)) out.push_back(in.cfg->id);
    std::sort(out.begin(), out.end());
    return out;
}

std::string joined(const std::vector<std::string>& ids) {
    std::string out;
    for (const std::string& id : ids) out += (out.empty() ? "" : ", ") + id;
    return out.empty() ? "none" : out;
}

// (the world modules scan every 0.5-2 s: the lists go once they have the room's contents)
constexpr double kResyncDelayMs = 2500;

void tick_coop_rooms() {
    const Coop role = coop();
    const double now = now_ms();
    static std::vector<net::Packet> packets;
    packets.clear();
    netrt::drain_packets(net::Msg::Rooms, packets);  // (always: unread they would pile up)
    if (!g_engaged) {  // the feature off here: nothing of rooms
        g_host_rooms = {};
        g_resyncs.clear();
        g_room_puts.clear();
        for (auto& r : g_friend_rooms) r.clear();
        return;
    }
    if (role == Coop::Host) {  // (K5) a friend's rooms belong to their link: another link (or none) starts over
        std::array<uint32_t, net::kMaxPlayers> links{};
        for (const net::PlayerInfo& pi : netrt::status().players)
            if (pi.linked && pi.id > 0 && pi.id < net::kMaxPlayers) links[pi.id] = pi.link;
        for (uint8_t id = 1; id < net::kMaxPlayers; ++id)
            if (links[id] != g_friend_link[id]) {
                g_friend_rooms[id].clear();
                g_friend_link[id] = links[id];
            }
    }
    for (const net::Packet& p : packets) {
        net::Reader r = net::packet_body(p);
        net::RoomsMsg m;
        if (!net::read_rooms(r, m)) continue;
        if (role == Coop::Friend) {
            if (p.from != 0) continue;
            std::unordered_set<std::string> ids(m.ids.begin(), m.ids.end());
            if (!g_host_rooms.have || g_host_rooms.region != m.region || g_host_rooms.ids != ids)
                logx::info("interiors: the host's game has %zu room(s) ready in %s: %s", m.ids.size(), m.region.c_str(),
                           joined(m.ids).c_str());
            g_host_rooms.have = true;
            g_host_rooms.region = m.region;
            g_host_rooms.ids = std::move(ids);
        } else if (role == Coop::Host && p.from > 0 && p.from < net::kMaxPlayers) {
            std::unordered_set<std::string> ids;
            if (m.region == g_region) ids.insert(m.ids.begin(), m.ids.end());
            if (p.link != g_friend_link[p.from]) {
                g_friend_rooms[p.from].clear();
                g_friend_link[p.from] = p.link;
            }
            auto& known = g_friend_rooms[p.from];
            std::vector<std::string> fresh;
            for (const std::string& id : ids)
                if (!known.count(id)) fresh.push_back(id);
            known = std::move(ids);
            if (!fresh.empty()) {
                std::sort(fresh.begin(), fresh.end());
                logx::info("interiors: player %u's game has a room up now (%s); our lists of %s go to them again", p.from,
                           joined(fresh).c_str(), g_region.c_str());
                g_resyncs.push_back({p.from, now + kResyncDelayMs});
                g_room_puts.push_back({p.from, fresh, now + kResyncDelayMs});
            }
        }
    }
    if (role == Coop::Solo) {
        g_host_rooms = {};
        g_resyncs.clear();
        g_room_puts.clear();
        for (auto& r : g_friend_rooms) r.clear();
        g_friend_link.fill(0);
        return;
    }
    if (role == Coop::Host) {
        for (auto it = g_resyncs.begin(); it != g_resyncs.end();) {
            if (now < it->at) {
                ++it;
                continue;
            }
            netrt::resync(it->player);
            it = g_resyncs.erase(it);
        }
        for (auto it = g_room_puts.begin(); it != g_room_puts.end();) {
            if (now < it->at) {
                ++it;
                continue;
            }
            g_room_puts_due.push_back(std::move(*it));
            it = g_room_puts.erase(it);
        }
    }
    if (g_region.empty()) return;  // (loading: nothing to say yet)
    net::RoomsMsg m;
    m.region = g_region;
    m.ids = ready_ids();
    const std::string said = m.region + ": " + joined(m.ids);
    const bool changed = said != g_rooms_said;
    if (!changed && now - g_rooms_said_at < 3000) return;
    if (changed) logx::info("interiors: rooms ready in our game, told the %s: %s", role == Coop::Host ? "friends" : "host",
                            said.c_str());
    g_rooms_said = said;
    g_rooms_said_at = now;
    net::Writer w(net::Msg::Rooms, net::kMaxMessage);
    net::write_rooms(w, m);
    if (w.ok()) netrt::send_packet(w, changed);
}

// --- ids every game shares ---------------------------------------------------------------------------------------------

Instance* instance_of_tf(Il2CppObject* t) {
    const auto& roots = room_roots();
    if (roots.empty()) return nullptr;
    Il2CppObject* root = alive(t) ? il2::invoke(api.tf_root, t) : nullptr;
    if (!root) return nullptr;
    for (const auto& [r, in] : roots)
        if (r == root) return in;
    return nullptr;
}

Il2CppObject* tf_of_any(Il2CppObject* obj) {
    if (!alive(obj)) return nullptr;
    return il2::il2cpp_object_get_class(obj) == api.go_class ? go_tf(obj) : tf_of(obj);
}

std::string local_cm(const Instance& in, Il2CppObject* t) {
    const V3 lp = to_local(in, position(t));
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld,%ld,%ld", std::lround(lp.x * 100), std::lround(lp.y * 100), std::lround(lp.z * 100));
    return buf;
}

bool replace_pdid(Il2CppObject* og, const std::string& id) {
    if (!og || !papi.pdid_replace || !papi.pdid_get) return false;
    if (call_text(papi.pdid_get, og) == id) return false;
    void* a[] = {og, str(id.c_str())};
    return call(papi.pdid_replace, nullptr, a);
}

// The room's items (by SI's building-scoped ids: the template's, or the host's files') and fires (by name and spot)
// get persistent ids built from those, so an ItemPut or a fire record names the same object in every game.
void coop_rekey(Instance& in) {
    if (!papi.pdid_replace || !papi.pdid_get) {
        logx::warn("interiors: %s: PDID calls missing: the room's items and fires keep the game's ids (may not match "
                   "the other games)",
                   in.cfg->id);
        return;
    }
    int items = 0, fires = 0, without = 0;
    std::unordered_set<std::string> used;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        Il2CppObject* go = go_of(gi);
        const std::string g = mod_guid(go);
        Il2CppObject* og = component(go, api.t_object_guid);
        if (g.empty() || !og) {
            ++without;
            return;
        }
        std::string id = md5_guid("coop|gear|" + g);
        for (int n = 1; !used.insert(id).second; ++n) id = md5_guid("coop|gear|" + g + "#" + std::to_string(n));
        if (replace_pdid(og, id)) ++items;
    });
    each_in(master_of(in), api.t_fire, [&](Il2CppObject* f) {
        Il2CppObject* og = in_parent(f, api.t_object_guid);
        if (!og) return;
        Il2CppObject* go = go_of(og);
        if (component(go, api.t_gear)) return;  // a fire on an item: the item's id (above)
        // 1b's fire records (the building's file, the region's save) know the fire by its id as the template has it:
        // kept as our id of it, which fire_key prefers
        if (mod_guid(go).empty())
            if (const std::string old = call_text(papi.pdid_get, og); !old.empty()) set_mod_guid(go, old);
        const std::string key = std::string("coop|fire|") + in.cfg->id + "|" + clean_name(name_of(go)) + "|" + local_cm(in, tf_of(f));
        std::string id = md5_guid(key);
        for (int n = 1; !used.insert(id).second; ++n) id = md5_guid(key + "#" + std::to_string(n));
        if (replace_pdid(og, id)) ++fires;
    });
    logx::info("interiors: %s: co-op ids: %d item(s) and %d fire(s) renamed after the building%s", in.cfg->id, items, fires,
               without ? (" (" + std::to_string(without) + " item(s) without our id keep theirs)").c_str() : "");
}

void coop_room_ready(Instance& in) {
    const Coop role = coop();
    if (role == Coop::Solo) return;
    coop_rekey(in);
    ++g_rooms_gen;
    if (role == Coop::Host) {
        g_resyncs.push_back({0, now_ms() + kResyncDelayMs});
        g_room_puts.push_back({0, {in.cfg->id}, now_ms() + kResyncDelayMs});
    }
    logx::info("interiors: %s: ready in our game (%s); %s", in.cfg->id, role == Coop::Host ? "the host's" : "a friend's",
               role == Coop::Host ? "the friends get our lists again in a moment" : "the host's lists bring what is in it");
}

// --- doors -------------------------------------------------------------------------------------------------------------

// A friend's door whose room the host has but ours isn't built yet: it waits for ours.
Instance* hosted_unready(Il2CppObject* door) {
    if (coop() != Coop::Friend) return nullptr;
    const std::string to = door_scene(door);
    const V3 dp = position(door_tf(door));
    for (Instance& in : g_instances) {
        if (in.stage == Stage::Ready || in.stage == Stage::Vanilla || to != in.cfg->scene) continue;
        if (dist(dp, in.cfg->fallback) > 40.0f) continue;
        if (host_has(in)) return &in;
    }
    return nullptr;
}

// --- a co-op teleport (a join, a respawn) ------------------------------------------------------------------------------

V3 door_of_unbuilt(const Config& c) {
    if (!is_zero(c.exit)) return c.exit;
    if (c.doors_count > 0) return kDoors[c.doors_first].exit;
    return c.fallback;
}
V3 door_of_unbuilt(const Instance& in) { return door_of_unbuilt(*in.cfg); }

Instance* instance_of_tf(Il2CppObject* t);
// (K9) a spot in the room an unbuilt building's room will stand on: its box once it has one (the build's Waking),
// else the parked spot within the building's own radius.
bool in_unbuilt_room(const Instance& in, V3 p) {
    if (in.stage == Stage::Ready || in.stage == Stage::Vanilla) return false;
    if (in.have_trigger && master_of(in)) return in_volume(in, p, 0.5f);
    const V3 parked = in.cfg->fallback + kParkOffset;
    const float up = p.y - parked.y;
    return up > -6.0f && up < 14.0f && dist_xz(p, parked) < std::max(6.0f, in.cfg->bounds_radius + 1.0f);
}

// The co-op ids (coop_rekey) of the items in a room's saved file, read once per region visit (K1: a friend's take of
// one of them, while the room isn't built here, waits for it).
std::unordered_map<const Instance*, std::unordered_set<std::string>> g_file_item_ids;
const std::unordered_set<std::string>& file_item_ids(Instance& in) {
    auto it = g_file_item_ids.find(&in);
    if (it != g_file_item_ids.end()) return it->second;
    std::unordered_set<std::string>& ids = g_file_item_ids[&in];
    ensure_dir(in);
    if (in.dir.empty()) return ids;
    const Records r = read_records(building_file(in.dir, in, "gear"), "gear", 14);
    for (const auto& row : r.rows) {
        const std::string g = unesc(row[1]);
        if (g.empty()) continue;
        ids.insert(md5_guid("coop|gear|" + g));
        for (int n = 1; n <= 3; ++n) ids.insert(md5_guid("coop|gear|" + g + "#" + std::to_string(n)));
    }
    return ids;
}

}  // namespace (reopened below)

// --- public (milestone 2) ----------------------------------------------------------------------------------------------

std::string place() {
    if (!api.ok || !g_engaged || !g_inside || !in_room(g_inside)) return {};
    return g_inside->cfg->id;
}

bool door_position(const std::string& id, float& x, float& y, float& z) {
    const Config* c = nullptr;
    for (int i = 0; i < kConfigCount && !c; ++i)
        if (id == kConfigs[i].id) c = &kConfigs[i];
    for (int hops = 0; c && !*c->shell && hops < 4; ++hops) {  // a sub-interior: the building it's reached from
        const Config* parent = nullptr;
        for (int i = 0; i < kConfigCount && !parent; ++i)
            for (int l = 0; l < kConfigs[i].links_count; ++l)
                if (std::strcmp(kLinks[kConfigs[i].links_first + l].target, c->id) == 0) parent = &kConfigs[i];
        if (!parent) break;
        c = parent;
    }
    if (!c) return false;
    const V3 door = door_of_unbuilt(*c);
    if (is_zero(door)) return false;
    x = door.x;
    y = door.y;
    z = door.z;
    return true;
}

std::string place_scene(const std::string& id) {
    for (int i = 0; i < kConfigCount; ++i)
        if (id == kConfigs[i].id) return kConfigs[i].scene;
    return {};
}

bool sync_skips(Il2CppObject* obj) {
    if (!is_clone_object(obj)) return false;
    const Instance* in = instance_of_tf(tf_of_any(obj));
    return !(in && in_room(in) && in->hydrated);
}

std::string room_key(Il2CppObject* comp) {
    if (!api.ok || g_instances.empty() || no_room_objects()) return {};
    Il2CppObject* t = tf_of_any(comp);
    const Instance* in = instance_of_tf(t);
    if (!in || !in_room(in)) return {};
    Il2CppObject* go = il2::il2cpp_object_get_class(comp) == api.go_class ? comp : go_of(comp);
    // (K6) part of a piece of furniture with an id: by that id and its spot on it, so the key stays when the furniture
    // was moved (in the host's room, not in a friend's template)
    if (Il2CppObject* pl = api.t_placeable ? in_parent(comp, api.t_placeable) : nullptr; pl && papi.placeable_guid >= 0)
        if (const std::string g = placeable_guid(pl); !g.empty()) {
            Il2CppObject* pt = tf_of(pl);
            const V3 on = rotate(conj(rotation(pt)), position(t) - position(pt));
            char buf[64];
            snprintf(buf, sizeof(buf), "%ld,%ld,%ld", std::lround(on.x * 100), std::lround(on.y * 100), std::lround(on.z * 100));
            return std::string("si:") + in->cfg->id + "|g:" + g + "|" + clean_name(name_of(go)) + "|" + buf;
        }
    return std::string("si:") + in->cfg->id + "|" + clean_name(name_of(go)) + "|" + local_cm(*in, t);
}

uint32_t rooms_generation() { return g_rooms_gen; }

bool active_in_closed_room(Il2CppObject* go) {
    if (!api.ok || g_instances.empty() || no_room_objects() || !alive(go)) return false;
    Il2CppObject* t = go_tf(go);
    const Instance* in = instance_of_tf(t);
    if (!in || !in_room(in) || !in->hydrated) return false;
    Il2CppObject* mt = live(in->master_tf);
    return mt && active_up_to(t, mt);
}

bool settle_in_room(Il2CppObject* gear) {
    if (!api.ok || g_instances.empty() || !alive(gear)) return false;
    Il2CppObject* t = tf_of(gear);
    const V3 p = position(t);
    for (Instance& in : g_instances) {
        if (!in_room(&in) || !in.hydrated || !master_of(in) || !in_parked_room(in, p, 0)) continue;
        if (in.gear_blocked) return false;  // (its items aren't written: it stays the region's)
        Il2CppObject* mt = live(in.master_tf);
        if (!mt || child_of(t, mt)) return true;
        if (active_self(master_of(in))) return false;  // open here: it's adopted when the room closes (hide_room)
        bool keep_world = true;
        void* pa[] = {mt, &keep_world};
        il2::invoke(api.tf_set_parent, t, pa);
        freeze_physics(gear);
        logx::info("interiors: %s: %s, put there by another player, kept in the room (closed in our game)", in.cfg->id,
                   name_of(go_of(gear)).c_str());
        return true;
    }
    return false;
}

bool unbuilt_room_at(float x, float y, float z) {
    if (!api.ok || !g_engaged || g_instances.empty()) return false;
    const V3 p{x, y, z};
    for (const Instance& in : g_instances)
        if (in_unbuilt_room(in, p)) return true;
    return false;
}

bool unbuilt_room_has_item(const std::string& pdid) {
    if (!api.ok || !g_engaged || g_instances.empty() || pdid.empty() || coop() != Coop::Host) return false;
    for (Instance& in : g_instances) {
        if (in.stage == Stage::Ready || in.stage == Stage::Vanilla) continue;
        if (file_item_ids(in).count(pdid)) return true;
    }
    return false;
}

bool rooms_pending_build(const std::string& scene) {
    if (!api.ok || scene.empty()) return false;
    if (scene != g_region) {  // not entered here yet (the scene loading, or another region): none of its rooms is built
        if (!g_feature_on) return false;
        for (int i = 0; i < kConfigCount; ++i)
            if (scene == kConfigs[i].region) return true;
        return false;
    }
    if (!g_engaged) return false;
    for (const Instance& in : g_instances)
        if (in.stage != Stage::Ready && in.stage != Stage::Vanilla) return true;
    return false;
}

bool host_lacks_room_of(Il2CppObject* obj) {
    if (!api.ok || !g_engaged || coop() != Coop::Friend || no_room_objects()) return false;
    const Instance* in = instance_of_tf(tf_of_any(obj));
    return in && !host_has(*in);
}

std::string room_of(Il2CppObject* obj) {
    if (!api.ok || !g_engaged || no_room_objects()) return {};
    const Instance* in = instance_of_tf(tf_of_any(obj));
    return in && in_room(in) ? in->cfg->id : std::string();
}

std::vector<std::pair<uint8_t, std::vector<std::string>>> take_room_puts() {
    std::vector<std::pair<uint8_t, std::vector<std::string>>> out;
    for (RoomPuts& r : g_room_puts_due) out.emplace_back(r.player, std::move(r.ids));
    g_room_puts_due.clear();
    return out;
}

bool prepare_teleport(const std::string& where, float& x, float& y, float& z, bool& indoors) {
    if (!api.ok || (where.empty() && !g_engaged)) return false;
    Instance* in = where.empty() || !g_engaged ? nullptr : find_instance(where.c_str());
    if (g_engaged && g_inside && g_inside != in) exit_room(*g_inside, "teleported to another player", true);
    if (!in) {
        if (where.empty()) return false;
        // no such room here (yet): at its building's door (K8), else beside it - not 80 m up where the room would stand
        for (int i = 0; i < kConfigCount; ++i)
            if (where == kConfigs[i].id) {
                const V3 door = door_of_unbuilt(kConfigs[i]);
                x = door.x;
                y = door.y;
                z = door.z;
                indoors = false;
                g_fall_guard_until = now_ms() + kFallGuardMs;
                logx::info("interiors: the other player is inside %s, which we don't have; to its door (%.1f, %.1f, %.1f)",
                           where.c_str(), door.x, door.y, door.z);
                return true;
            }
        y -= kParkOffset.y;
        indoors = false;
        logx::info("interiors: the other player is inside %s, which we don't know; to below their spot instead", where.c_str());
        return true;
    }
    if (in_room(in) && master_of(*in)) {
        if (g_inside != in) {
            open_room(*in);
            if (!live(in->shell))
                for (Instance& parent : g_instances)
                    if (is_sub_linked(parent, in->cfg->id) && in_room(&parent))
                        if (Il2CppObject* s = live(parent.shell)) set_active(s, false);
            set_outsiders_visible(*in, true);
            set_audio_occlusion(true);
            mark_inside(in, "beside another player");
        }
        g_last_portal = now_ms();
        g_fall_guard_until = now_ms() + kFallGuardMs;
        indoors = true;
        logx::info("interiors: going to the other player inside %s (built here)", in->cfg->id);
        return true;
    }
    const V3 door = door_of_unbuilt(*in);
    x = door.x;
    y = door.y;
    z = door.z;
    indoors = false;
    g_fall_guard_until = now_ms() + kFallGuardMs;
    if (in->stage == Stage::Idle) g_urgent = in;
    logx::info("interiors: the other player is inside %s, whose room isn't built here yet; to its door (%.1f, %.1f, %.1f), "
               "the room comes up first",
               in->cfg->id, door.x, door.y, door.z);
    return true;
}

namespace {
