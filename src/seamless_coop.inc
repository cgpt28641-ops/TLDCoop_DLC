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
struct PlaceFull {
    uint8_t player = 0;
    std::vector<std::string> ids;
    double at = 0;
};
std::vector<PlaceFull> g_place_full;  // A5, host: whole rooms' furniture to send to a friend whose room came up

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
                g_place_full.push_back({p.from, fresh, now + kResyncDelayMs});  // A5: and its furniture as we have it
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
    if (!is_zero(c.door_spot)) return c.door_spot;  // B4: a found building's door
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


// --- A5: furniture and decorations in rooms (Msg::RoomPlace) -----------------------------------------------------------
// A room's placeables are kept out of TLD's PlaceableManager (so world_objects never sees them): each game looks at its
// ready rooms' placeables once a second and tells the other games what changed, by building and the placeable's stable
// id (stabilize_guids): its room-local transform, switched on or off, or gone from the room (taken into a pack). The
// host's game is the authority: a friend's change goes to the host, which applies it (its room files keep it with the
// next save) and passes it on; the host's go to every friend; a friend whose room comes up gets the host's whole room.
// A change for a room not built here waits for it (as K1). Nothing is ever destroyed: one gone is switched off (what
// it holds stays with it); one a game doesn't have is made from TLD's record of it (or, if it's elsewhere in the
// scene with that id, moved), never twice.
struct PlaceSnap {
    std::string sig;
    net::RoomPlaceMsg msg;
};
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> g_place_seen;  // building -> guid -> sig
std::unordered_map<std::string, uintptr_t> g_place_master;  // building -> the room (master handle) those were seen in
std::string g_place_region;
double g_place_next_scan = 0;
std::unordered_map<std::string, std::unordered_map<std::string, net::RoomPlaceMsg>> g_place_waiting;  // "region|id" -> guid
int g_place_logged = 0;

std::string place_sig(bool gone, bool active, V3 p, Quat q, V3 s);
constexpr const char* kJunkGuid = "#junk";
// What a scan would see of it now (so a change we applied isn't told back).
std::string actual_sig(Instance& in, Il2CppObject* p) {
    Il2CppObject* t = tf_of(p);
    return place_sig(false, active_self(go_of(p)), to_local(in, position(t)), mul(conj(in.master_rot), rotation(t)), local_scale(t));
}
std::string place_sig(bool gone, bool active, V3 p, Quat q, V3 s) {
    if (gone) return "gone";
    char b[200];
    std::snprintf(b, sizeof(b), "%d|%.2f,%.2f,%.2f|%.3f,%.3f,%.3f,%.3f|%.2f,%.2f,%.2f", active ? 1 : 0, p.x, p.y, p.z, q.x, q.y,
                  q.z, q.w, s.x, s.y, s.z);
    return b;
}

// The room's placeables as this game has them now (by id; one on the player or in a pack isn't in the room).
std::unordered_map<std::string, PlaceSnap> scan_places(Instance& in, bool with_spawned) {
    std::unordered_map<std::string, PlaceSnap> out;
    std::vector<Il2CppObject*> all;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* p) { all.push_back(p); });
    const auto best = best_per_guid(all, in);
    for (const auto& [g, p] : best) {
        Il2CppObject* t = tf_of(p);
        if (g == kStableGuidMarker || player_or_inventory(t) || is_carried(in, p)) continue;
        PlaceSnap s;
        s.msg.region = g_region;
        s.msg.building = in.cfg->id;
        s.msg.guid = g;
        const V3 lp = to_local(in, position(t));
        const Quat lr = mul(conj(in.master_rot), rotation(t));
        const V3 sc = local_scale(t);
        const bool on = active_self(go_of(p));
        s.msg.active = on ? 1 : 0;
        s.msg.pos[0] = lp.x, s.msg.pos[1] = lp.y, s.msg.pos[2] = lp.z;
        s.msg.rot[0] = lr.x, s.msg.rot[1] = lr.y, s.msg.rot[2] = lr.z, s.msg.rot[3] = lr.w;
        s.msg.scale[0] = sc.x, s.msg.scale[1] = sc.y, s.msg.scale[2] = sc.z;
        s.sig = place_sig(false, on, lp, lr, sc);
        if (with_spawned && is_spawned_placeable(in, p)) {
            Il2CppObject* data = nullptr;
            if (call(papi.placeable_save, p, nullptr, &data) && data) {
                put<V3>(data, papi.data_position, to_local(in, get<V3>(data, papi.data_position)));
                put<Quat>(data, papi.data_rotation, mul(conj(in.master_rot), get<Quat>(data, papi.data_rotation)));
                void* sa[] = {data};
                s.msg.spawned = call_text(papi.utils_serialize, nullptr, sa);
                if (s.msg.spawned.size() > net::kRoomPlaceSpawnedMax) s.msg.spawned.clear();
            }
        }
        out.emplace(g, std::move(s));
    }
    {  // A11: the room's "junk cleared" goes the same way (a pseudo placeable: active 0 = cleared)
        PlaceSnap s;
        s.msg.region = g_region;
        s.msg.building = in.cfg->id;
        s.msg.guid = kJunkGuid;
        s.msg.active = in.junk_cleared ? 0 : 1;
        s.sig = in.junk_cleared ? "junk:cleared" : "junk:there";
        out.emplace(kJunkGuid, std::move(s));
    }
    return out;
}

void send_place(const net::RoomPlaceMsg& m, int to /* -1 everyone (or the host), else one friend */, int except = -1) {
    net::Writer w(net::Msg::RoomPlace, net::kMaxMessage);
    net::write_room_place(w, m);
    if (!w.ok()) return;
    if (to >= 0) netrt::send_packet_to(static_cast<uint8_t>(to), w, true);
    else if (except >= 0) netrt::send_packet_except(static_cast<uint8_t>(except), w, true);
    else netrt::send_packet(w, true);
}

Il2CppObject* placeable_in_room(Instance& in, const std::string& guid) {
    std::vector<Il2CppObject*> all;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* p) {
        if (placeable_guid(p) == guid) all.push_back(p);
    });
    if (all.empty()) return nullptr;
    const auto best = best_per_guid(all, in);
    auto it = best.find(guid);
    return it == best.end() ? nullptr : it->second;
}

// A placeable of this id anywhere in the scene outside the rooms (switched off ones too), not carried.
Il2CppObject* placeable_elsewhere(const std::string& guid) {
    if (!japi.find_all || !api.t_placeable) return nullptr;
    bool inactive = true;
    void* a[] = {api.t_placeable, &inactive};
    Il2CppObject* all = il2::invoke(japi.find_all, nullptr, a);
    for (size_t i = 0, n = all ? il2::array_length(all) : 0; i < n; ++i) {
        Il2CppObject* p = il2::array_object_at(all, i);
        if (!alive(p) || placeable_guid(p) != guid || is_clone_object(p) || player_or_inventory(tf_of(p))) continue;
        return p;
    }
    return nullptr;
}

// One of a room's placeables that isn't under it any more: where it is (`held`: in the player's hands, being moved),
// or null when it's in a pack or no more.
Il2CppObject* placeable_anywhere(const std::string& guid, bool& held) {
    held = false;
    if (Il2CppObject* p = placeable_elsewhere(guid)) return p;
    if (!japi.find_all || !api.t_placeable || is_carried(guid)) return nullptr;
    bool inactive = false;
    void* a[] = {api.t_placeable, &inactive};
    Il2CppObject* all = il2::invoke(japi.find_all, nullptr, a);
    for (size_t i = 0, n = all ? il2::array_length(all) : 0; i < n; ++i) {
        Il2CppObject* p = il2::array_object_at(all, i);
        if (alive(p) && placeable_guid(p) == guid && player_or_inventory(tf_of(p)) && !in_inventory(tf_of(p))) {
            held = true;
            return p;
        }
    }
    return nullptr;
}

void place_at(Instance& in, Il2CppObject* p, const net::RoomPlaceMsg& m) {
    Il2CppObject* t = tf_of(p);
    set_position(t, to_world(in, V3{m.pos[0], m.pos[1], m.pos[2]}));
    Quat r = mul(in.master_rot, Quat{m.rot[0], m.rot[1], m.rot[2], m.rot[3]});
    void* ra[] = {&r};
    il2::invoke(api.tf_set_rotation, t, ra);
    V3 sc{m.scale[0], m.scale[1], m.scale[2]};
    void* sa[] = {&sc};
    il2::invoke(api.tf_set_local_scale, t, sa);
}

// Applies another game's word on one of a room's placeables. False: the room isn't here to take it (it waits).
bool apply_place(const net::RoomPlaceMsg& m, uint8_t from) {
    Instance* in = find_instance(m.building.c_str());
    if (!in || m.region != g_region || !in_room(in) || !in->hydrated || !master_of(*in)) return false;
    const char* who = from == 0 ? "the host" : "a friend";
    auto& seen = g_place_seen[in->cfg->id];
    if (m.guid == kJunkGuid) {  // A11: its junk cleared (or, a load of an older save, not)
        const bool cleared = m.active == 0;
        if (cleared != in->junk_cleared) {
            const int n = cleared ? clear_junk_in(*in) : restore_junk_in(*in);
            logx::info("interiors: %s: %s %s the room's junk (%d object(s))", in->cfg->id, who, cleared ? "cleared" : "brought back", n);
            if (g_junk_owner == in)
                if (Il2CppObject* jm = junk_manager()) put<bool>(jm, japi.cleared, cleared);
        }
        seen[m.guid] = cleared ? "junk:cleared" : "junk:there";
        return true;
    }
    Il2CppObject* p = placeable_in_room(*in, m.guid);
    if (p && (player_or_inventory(tf_of(p)) || is_carried(*in, p))) {  // ours, in our hands or pack: not theirs to move
        if (g_place_logged++ < 60) logx::info("interiors: %s: %s's word on '%s' isn't applied: we carry it", in->cfg->id, who, m.guid.c_str());
        return true;
    }
    if (m.gone) {
        if (p && active_self(go_of(p))) {
            set_active(go_of(p), false);  // (switched off, never destroyed: whatever it holds stays with it)
            if (g_place_logged++ < 60)
                logx::info("interiors: %s: %s took '%s' (%s) from the room", in->cfg->id, who, name_of(go_of(p)).c_str(), m.guid.c_str());
        }
        if (p) seen[m.guid] = actual_sig(*in, p);
        return true;
    }
    if (!p) {
        if (Il2CppObject* other = placeable_elsewhere(m.guid)) {  // put down in the region (not adopted yet): moved there
            place_at(*in, other, m);
            set_active(go_of(other), m.active != 0);
            if (g_place_logged++ < 60)
                logx::info("interiors: %s: %s put '%s' down in the room (ours stood elsewhere in the scene: moved)", in->cfg->id, who,
                           m.guid.c_str());
            return true;
        }
        if (m.spawned.empty() || !papi.t_placeable_data || !papi.placeable_create) {
            if (g_place_logged++ < 60)
                logx::warn("interiors: %s: %s's word on '%s', which we don't have and can't make; left", in->cfg->id, who, m.guid.c_str());
            return true;
        }
        Il2CppObject* data = deserialize_object(m.spawned, papi.t_placeable_data);
        Il2CppObject* placed = nullptr;
        if (data) {
            put<V3>(data, papi.data_position, to_world(*in, get<V3>(data, papi.data_position)));
            put<Quat>(data, papi.data_rotation, mul(in->master_rot, get<Quat>(data, papi.data_rotation)));
            void* a[] = {str(m.guid.c_str()), data};
            g_creating_placeable_1b = true;
            call(papi.placeable_create, nullptr, a, &placed);
            g_creating_placeable_1b = false;
        }
        if (!alive(placed)) {
            logx::warn("interiors: %s: %s put '%s' down in the room; TLD couldn't make it here", in->cfg->id, who, m.guid.c_str());
            return true;
        }
        Il2CppObject* t = tf_of(placed);
        if (Il2CppObject* mt = live(in->master_tf); mt && !child_of(t, mt)) {
            bool keep_world = true;
            void* a[] = {mt, &keep_world};
            il2::invoke(api.tf_set_parent, t, a);
        }
        put<bool>(placed, api.placeable_invalidated, true);
        void* ra[] = {placed};
        call(papi.placeable_remove, nullptr, ra);
        in->spawned_guids.insert(m.guid);
        p = placed;
        if (g_place_logged++ < 60) logx::info("interiors: %s: %s put '%s' (%s) down in the room; made here", in->cfg->id, who,
                                              name_of(go_of(p)).c_str(), m.guid.c_str());
    }
    place_at(*in, p, m);
    set_active(go_of(p), m.active != 0);
    seen[m.guid] = actual_sig(*in, p);
    return true;
}

void tick_room_places() {
    static std::vector<net::Packet> packets;
    packets.clear();
    netrt::drain_packets(net::Msg::RoomPlace, packets);  // (always: unread they would pile up)
    const Coop role = coop();
    if (role == Coop::Solo || !g_engaged) {
        g_place_seen.clear();
        g_place_master.clear();
        g_place_full.clear();
        if (role == Coop::Solo) g_place_waiting.clear();
        return;
    }
    if (g_place_region != g_region) {  // other rooms now (their placeables went with the old region)
        g_place_seen.clear();
        g_place_master.clear();
        g_place_full.clear();
        g_place_region = g_region;
    }
    for (const net::Packet& pk : packets) {
        net::Reader r = net::packet_body(pk);
        net::RoomPlaceMsg m;
        if (!net::read_room_place(r, m) || m.guid.empty()) continue;
        if (role == Coop::Friend && pk.from != 0) continue;
        if (role == Coop::Host && pk.from > 0) send_place(m, -1, pk.from);  // the other friends hear of it too
        if (!apply_place(m, pk.from)) {
            g_place_waiting[m.region + "|" + m.building][m.guid] = m;  // the newest word on it waits for the room
            if (g_place_logged++ < 60)
                logx::info("interiors: %s's word on '%s' in %s waits for that room", pk.from == 0 ? "the host" : "a friend",
                           m.guid.c_str(), m.building.c_str());
        }
    }
    const double now = now_ms();
    if (now < g_place_next_scan || g_region.empty()) return;
    g_place_next_scan = now + 1000;
    for (Instance& in : g_instances) {
        if (!in_room(&in) || !in.hydrated || !master_of(in)) continue;
        // what waited for this room first
        if (auto w = g_place_waiting.find(g_region + "|" + in.cfg->id); w != g_place_waiting.end()) {
            auto list = std::move(w->second);
            g_place_waiting.erase(w);
            for (auto& [g, m] : list) apply_place(m, role == Coop::Friend ? 0 : 1);
            logx::info("interiors: %s: %zu furniture change(s) that waited for the room applied", in.cfg->id, list.size());
        }
        if (g_place_master[in.cfg->id] != in.master) {  // a room built again (a reload of the region): a fresh first look
            g_place_seen.erase(in.cfg->id);
            g_place_master[in.cfg->id] = in.master;
        }
        auto seen_it = g_place_seen.find(in.cfg->id);
        auto now_places = scan_places(in, false);
        if (seen_it == g_place_seen.end()) {  // first look at it: how it came up (nobody changed anything)
            auto& base = g_place_seen[in.cfg->id];
            for (auto& [g, s] : now_places) base[g] = s.sig;
            continue;
        }
        auto& seen = seen_it->second;
        int sent = 0;
        std::unordered_map<std::string, PlaceSnap> with;  // (TLD's records of the player's own, made once if needed)
        bool with_made = false;
        for (auto& [g, s] : now_places) {
            auto it = seen.find(g);
            if (it != seen.end() && it->second == s.sig) continue;
            if (!with_made) {  // how to make it, for a game that hasn't got it (the player's own furniture only)
                with = scan_places(in, true);
                with_made = true;
            }
            if (auto w = with.find(g); w != with.end()) s.msg.spawned = w->second.msg.spawned;
            seen[g] = s.sig;
            send_place(s.msg, -1);
            ++sent;
        }
        for (auto it = seen.begin(); it != seen.end();) {
            if (now_places.count(it->first)) {
                ++it;
                continue;
            }
            // Not under the room: put down somewhere in the scene (TLD may leave a moved one outside the room until it
            // is adopted), in the player's hands while they move it (not gone), or really gone (in a pack, or no more).
            bool held = false;
            if (Il2CppObject* o = placeable_anywhere(it->first, held)) {
                if (held) {
                    ++it;
                    continue;
                }
                const std::string sig = actual_sig(in, o);
                if (sig != it->second) {
                    Il2CppObject* t = tf_of(o);
                    net::RoomPlaceMsg m;
                    m.region = g_region;
                    m.building = in.cfg->id;
                    m.guid = it->first;
                    const V3 lp = to_local(in, position(t));
                    const Quat lr = mul(conj(in.master_rot), rotation(t));
                    const V3 sc = local_scale(t);
                    m.active = active_self(go_of(o)) ? 1 : 0;
                    m.pos[0] = lp.x, m.pos[1] = lp.y, m.pos[2] = lp.z;
                    m.rot[0] = lr.x, m.rot[1] = lr.y, m.rot[2] = lr.z, m.rot[3] = lr.w;
                    m.scale[0] = sc.x, m.scale[1] = sc.y, m.scale[2] = sc.z;
                    send_place(m, -1);
                    ++sent;
                    it->second = sig;
                }
                ++it;
                continue;
            }
            net::RoomPlaceMsg m;
            m.region = g_region;
            m.building = in.cfg->id;
            m.guid = it->first;
            m.gone = 1;
            send_place(m, -1);
            ++sent;
            it = seen.erase(it);
        }
        if (sent && g_place_logged++ < 60)
            logx::info("interiors: %s: %d furniture change(s) told the %s", in.cfg->id, sent, role == Coop::Host ? "friends" : "host");
    }
    if (role == Coop::Host)
        for (auto it = g_place_full.begin(); it != g_place_full.end();) {
            if (now < it->at) {
                ++it;
                continue;
            }
            for (const std::string& id : it->ids)
                if (Instance* in = find_instance(id.c_str()); in && in_room(in) && in->hydrated && master_of(*in)) {
                    auto all = scan_places(*in, true);
                    for (auto& [g, s] : all) send_place(s.msg, it->player);
                    logx::info("interiors: %s: its %zu placeable(s) as we have them sent to player %u", id.c_str(), all.size(), it->player);
                }
            it = g_place_full.erase(it);
        }
}

}  // namespace (reopened below)

// --- public (milestone 2) ----------------------------------------------------------------------------------------------

std::string place() {
    if (!api.ok || !g_engaged || !g_inside || !in_room(g_inside)) return {};
    return g_inside->cfg->id;
}

bool door_position(const std::string& id, float& x, float& y, float& z) {
    const Config* c = config_by_id(id);  // (B4: a found building's too)
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
    const Config* c = config_by_id(id);
    return c ? std::string(c->scene) : std::string();
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
        for (const Config& c : g_auto_cfgs)  // B4: found there on an earlier visit
            if (scene == c.region) return true;
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
        if (const Config* c = config_by_id(where)) {
            const V3 door = door_of_unbuilt(*c);
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
