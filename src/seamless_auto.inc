// TLDCoop_DLC: Seamless Interiors, B4: buildings without an SI config, found at runtime (plan §4, last paragraph).
// Included by seamless_interiors.cpp (inside its anonymous namespace), after seamless_coop.inc.
//
// SI's configs cover 126 buildings in 12 regions. In any region, once per load and before any room is built, every door
// of the region's own scenes (LoadScene.m_LoadScenesList) that leads to an interior with no configured building near it
// gives a generated config, the way SI's own SIConfigTool configs are made: the room parked on its shell's spot, in and
// out through the game's own m_ExitPointName points, the usual lighting objects and CONTAINER_InaccessibleGear
// destroyed, painted window backdrops kept. Its id is made from the interior scene and its door's guid (the same in
// every game: the host's and the friends' rooms match), so its files, its old-save import and the co-op messages go
// the same way as a configured building's.
//
// Never: caves, mines, the dam, transition zones and caves, prepper caches and bunkers, train cars, story and survival
// interiors (a deny list by scene name); walk-in doors (caves); a door switched off or for the challenge modes only;
// a shell bigger than a building. When its room is built (before it ever wakes): an interior bigger than a building, one
// far off its building's spot, one that would stand in the terrain or another room 80 m up, or one with a door
// anywhere but back to this region (a sub-interior, another region) is refused - the door stays the game's.
//
// coop.ini [interiors] auto_buildings=0 turns discovery off (solo and host; a friend always looks, but only ever builds
// the rooms the host has).

constexpr int kMaxAutoPerRegion = 96;
constexpr float kAutoShellMaxSide = 40.0f, kAutoShellMaxHeight = 30.0f;  // a building's shell, not a town block
constexpr float kAutoRoomMaxSide = 45.0f, kAutoRoomMaxHeight = 25.0f;    // the interior parked above it
constexpr float kAutoRoomMaxShift = 30.0f;  // the interior's middle from its shell's spot (authored round the origin)
constexpr float kAutoCoveredReach = 40.0f;  // a configured building of the same scene this near: that door is its

bool g_auto_buildings = true;  // coop.ini [interiors] auto_buildings
bool g_discovered = false;     // this region's doors were looked at
std::deque<std::string> g_auto_text;  // the found configs' strings (kept where they are, as the configs)
std::unordered_map<std::string, std::string> g_auto_refused;  // id -> why its room was refused (this session)
std::unordered_set<std::string> g_auto_logged;                // ids told in the log already (once per session)
ptrdiff_t g_door_instanced = -1, g_door_challenge_only = -1, g_door_on_contact = -1;  // LoadScene fields
FieldInfo* g_door_list = nullptr;                                                      // LoadScene.m_LoadScenesList

const char* keep_text(const std::string& s) {
    g_auto_text.push_back(s);
    return g_auto_text.back().c_str();
}

void resolve_auto() {
    Il2CppClass* ls = il2::find_class(kGame, "", "LoadScene");
    if (!ls) return;
    g_door_list = il2::il2cpp_class_get_field_from_name(ls, "m_LoadScenesList");
    g_door_instanced = il2::field_offset_quiet(ls, "m_SceneCanBeInstanced");
    g_door_challenge_only = il2::field_offset_quiet(ls, "m_DisableInChallengeMode");
    g_door_on_contact = il2::field_offset_quiet(ls, "m_TransitionOnContact");
    if (!g_door_list) logx::warn("interiors: LoadScene.m_LoadScenesList not found; no buildings are found beyond SI's configs");
}

// Why an interior scene is never a found building ("" none).
std::string auto_denied(const std::string& to) {
    if (to.empty()) return "no scene";
    const std::string base = interior_base(to);
    if (base == g_region) return "the region itself";
    if (leads_to_region(to)) return "a region or transition zone";
    if (base.find('_') != std::string::npos) return "a scene variant";  // _Light, _VFX, _TEST...
    // caves, mines (not a miner's house), the dam, transitions, prepper caches and bunkers, train cars, story/survival
    static const char* const kDeny[] = {"Cave",   "Transition", "Tunnel",  "Prison",    "Prepper",  "Cache",
                                        "Bunker", "Empty",      "Train",   "Boxcar",    "Caboose",  "Locomotive",
                                        "Story",  "Episode",    "Tale",    "Hunted",    "Darkwalker", "Survival",
                                        "Concentrator", "MainMenu", "Boot", "Elevator"};
    for (const char* d : kDeny)
        if (base.find(d) != std::string::npos) return std::string("denied (") + d + ")";
    for (size_t at = base.find("Mine"); at != std::string::npos; at = base.find("Mine", at + 1))
        if (at + 4 >= base.size() || base[at + 4] != 'r') return "denied (Mine)";
    if (base == "Dam" || base.rfind("DamCave", 0) == 0 || base.rfind("DamRiver", 0) == 0) return "denied (the dam)";
    return {};
}

// The building a door belongs to: the outermost STR*/STRSPAWN* ancestor that is a building's size (not a group of
// them), else the door's root if that is.
Il2CppObject* auto_shell_of(Il2CppObject* door_tf_obj) {
    std::vector<Il2CppObject*> chain;  // door's parent ... root
    Il2CppObject* t = parent_of(door_tf_obj);
    for (int depth = 0; t && depth < 16; ++depth, t = parent_of(t)) chain.push_back(t);
    auto fits = [&](Il2CppObject* cand) {
        const Box b = mesh_box_of(go_of(cand));
        if (!b.valid()) return false;
        const V3 s = b.max - b.min;
        return s.x <= kAutoShellMaxSide && s.z <= kAutoShellMaxSide && s.y <= kAutoShellMaxHeight;
    };
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)  // outermost first
        if (name_of(*it).rfind("STR", 0) == 0 && fits(*it)) return go_of(*it);
    Il2CppObject* root = chain.empty() ? door_tf_obj : chain.back();
    bool str_seen = false;
    for (Il2CppObject* c : chain) str_seen = str_seen || name_of(c).rfind("STR", 0) == 0;
    if (!str_seen && fits(root)) return go_of(root);
    return nullptr;
}

void forget_auto_region() {
    for (auto& [id, h] : g_auto_shells) drop(h);
    g_auto_shells.clear();
    g_discovered = false;
}

// SI's ObjectsToDestroy as its generated configs have them, for an interior nobody looked at.
int auto_names(const std::string& scene) {
    const int first = kNameCount + static_cast<int>(g_auto_names.size());
    for (const char* n : {"FX_LightShaft_B", "FX_LightShaft_C", "FX_LightShaft_E", "WindowLight", "WindowLightGroup",
                          "InteriorLightingManager_Prefab", "CONTAINER_InaccessibleGear", "Daylight", "Daytime", "Midday",
                          "Day Lights"})
        g_auto_names.emplace_back(n);
    g_auto_names.push_back(scene + "_Lighting_Prefab");
    return first;
}

struct AutoDoor {
    Il2CppObject* door = nullptr;
    Il2CppObject* shell = nullptr;  // GameObject
    std::string scene, guid;
    V3 pos;
};

void discover_buildings() {
    if (g_discovered || g_region.empty() || !api.ok) return;
    if (game::seconds_since_level_load() < 0.5f) return;  // (the room record is looked at from 1 s: before it)
    g_discovered = true;
    // only outdoors: a region or a transition zone (in an interior the game loaded itself, its doors are sub-interiors)
    if (!leads_to_region(g_region)) return;
    const Coop role = coop();
    if (role != Coop::Friend && !g_auto_buildings) {
        static bool told = false;
        if (!told) logx::info("interiors: auto: finding buildings beyond SI's configs is off in coop.ini (auto_buildings=0)");
        told = true;
        return;
    }
    if (!g_door_list || api.door_scene < 0) return;
    // (g_instances has room for them all - enter_region's reserve - so no instance moves: pointers to them stay good)
    const double t0 = now_ms();
    std::vector<Il2CppObject*> doors;
    each_listed(static_object(g_door_list), [&](Il2CppObject* d, int) {
        if (alive(d)) doors.push_back(d);
    });
    std::unordered_map<std::string, int> skipped;  // why -> how many doors
    std::vector<AutoDoor> found;
    for (Il2CppObject* d : doors) {
        Il2CppObject* go = go_of(d);
        const std::string sn = scene_name(scene_of(go));
        if (sn != g_region && sn.rfind(g_region + "_", 0) != 0) continue;  // not the region's (a room's, a kept one's)
        if (is_clone_object(go)) continue;
        const std::string to = door_scene(d);
        std::string why = auto_denied(to);
        if (why.empty() && !active_in_tree(go)) why = "switched off";
        if (why.empty() && g_door_challenge_only >= 0 && get<bool>(d, g_door_challenge_only)) why = "not in the challenges";
        if (why.empty() && g_door_on_contact >= 0 && get<bool>(d, g_door_on_contact)) why = "walk-in (a cave's)";
        const V3 dp = position(door_tf(d));
        const bool instanced = g_door_instanced >= 0 && get<bool>(d, g_door_instanced);
        if (why.empty())
            for (const Instance& in : g_instances) {
                const Config& c = *in.cfg;
                if (c.auto_found || interior_base(to) != c.scene) continue;
                // the configured building's own door, its sub-interior's outer door, or a scene the game keeps once
                if (dist(dp, c.fallback) < kAutoCoveredReach || !*c.shell || !instanced) {
                    why = "a configured building's";
                    break;
                }
            }
        AutoDoor a;
        if (why.empty()) {
            a.shell = auto_shell_of(door_tf(d));
            if (!a.shell) why = "no building-sized shell";
        }
        if (!why.empty()) {
            ++skipped[why];
            continue;
        }
        a.door = d;
        a.scene = interior_base(to);
        a.guid = api.door_guid >= 0 ? text(get<Il2CppObject*>(d, api.door_guid)) : std::string();
        if (a.guid.empty()) {  // (deterministic in every game: the door's spot to the centimetre)
            char buf[64];
            snprintf(buf, sizeof(buf), "@%ld,%ld,%ld", std::lround(dp.x * 100), std::lround(dp.y * 100), std::lround(dp.z * 100));
            a.guid = buf;
        }
        a.pos = dp;
        if (instanced) a.guid = "i" + a.guid;  // (marks it, see below)
        found.push_back(std::move(a));
    }
    // One building per (scene, shell) for an instanced scene (each building its own copy), one per scene otherwise
    // (the game keeps one interior, whichever door leads in). The door with the smallest guid names it.
    struct Group {
        std::string scene, key_guid;
        Il2CppObject* shell = nullptr;
        V3 door;
        bool instanced = false;
        int doors = 0;
    };
    std::vector<Group> groups;
    for (const AutoDoor& a : found) {
        const bool inst = a.guid[0] == 'i';
        Group* g = nullptr;
        for (Group& x : groups)
            if (x.scene == a.scene && x.instanced == inst && (!inst || x.shell == a.shell)) g = &x;
        if (!g) {
            groups.push_back({a.scene, a.guid, a.shell, a.pos, inst, 0});
            g = &groups.back();
        }
        ++g->doors;
        if (a.guid < g->key_guid) {
            g->key_guid = a.guid;
            g->door = a.pos;
            g->shell = a.shell;
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        return a.scene != b.scene ? a.scene < b.scene : a.key_guid < b.key_guid;
    });
    int added = 0, refused = 0;
    std::vector<Instance*> fresh;
    for (const Group& g : groups) {
        if (added >= kMaxAutoPerRegion || g_instances.size() >= g_instances.capacity()) {
            ++skipped["over the per-region limit"];
            continue;
        }
        char hex[16];
        snprintf(hex, sizeof(hex), "%08x", fnv1a(g.instanced ? g.key_guid : g.scene + "|" + g_region));
        const std::string id = g.scene.substr(0, 22) + "_a" + hex;
        if (find_instance(id.c_str())) {
            ++skipped["the same id twice"];
            continue;
        }
        const Config* cfg = nullptr;
        for (const Config& c : g_auto_cfgs)
            if (id == c.id && g_region == c.region) cfg = &c;
        Il2CppObject* shell_tf = go_tf(g.shell);
        if (!cfg) {
            Config c{};
            c.id = keep_text(id);
            c.region = keep_text(g_region);
            c.scene = keep_text(g.scene);
            c.shell = keep_text(name_of(g.shell));
            c.scale = {1, 1, 1};
            c.fallback = position(shell_tf);
            c.destroy_first = auto_names(g.scene);
            c.destroy_count = static_cast<int>(g_auto_names.size()) + kNameCount - c.destroy_first;
            c.bounds_radius = 30.0f;
            c.auto_found = true;
            c.door_spot = g.door;
            c.instanced = g.instanced;
            g_auto_cfgs.push_back(c);
            cfg = &g_auto_cfgs.back();
        }
        if (uintptr_t& h = g_auto_shells[id]) drop(h);
        g_auto_shells[id] = hold(g.shell);
        Instance in;
        in.cfg = cfg;
        auto r = g_auto_refused.find(id);
        if (r != g_auto_refused.end()) set_stage(in, Stage::Vanilla);  // refused earlier this session: not loaded again
        g_instances.push_back(std::move(in));
        fresh.push_back(&g_instances.back());
        ++added;
        if (r != g_auto_refused.end()) ++refused;
        if (g_auto_logged.insert(id).second)
            logx::info("interiors: auto: %s: %s in %s, shell '%s' at (%.1f, %.1f, %.1f), %d door(s)%s%s", id.c_str(),
                       g.scene.c_str(), g_region.c_str(), cfg->shell, cfg->fallback.x, cfg->fallback.y, cfg->fallback.z, g.doors,
                       g.instanced ? ", instanced scene" : "",
                       r != g_auto_refused.end() ? (", refused earlier: " + r->second).c_str() : "");
    }
    int back = 0;
    for (Instance* in : fresh)  // A8: a kept room of a found building comes back as a configured one's does
        if (in->stage == Stage::Idle && reattach_kept(*in, g_region)) {
            ensure_dir(*in);
            coop_room_ready(*in);
            ++back;
        }
    if (back) ++g_rooms_gen;
    rooms_changed();
    std::string why_list;
    for (const auto& [why, n] : skipped) why_list += (why_list.empty() ? "" : ", ") + std::to_string(n) + " " + why;
    logx::info("interiors: auto: %s: %d building(s) found beyond SI's configs (%d refused earlier), %zu door(s) looked at%s%s%s "
               "in %.0f ms",
               g_region.c_str(), added, refused, doors.size(), why_list.empty() ? "" : " (skipped: ", why_list.c_str(),
               why_list.empty() ? "" : ")", now_ms() - t0);
}

// At the build, before the room ever wakes (stage Idle, prepared and parked): is this found building's room one that
// can stand 80 m above its shell?
bool auto_room_ok(Instance& in, std::string& why) {
    auto refuse = [&](const std::string& w) {
        why = w;
        g_auto_refused[in.cfg->id] = w;
        return false;
    };
    Il2CppObject* m = master_of(in);
    if (!m) return refuse("no room");
    // every door in it leads back to this region (or within the room): a sub-interior or a way elsewhere stays vanilla
    std::string other;
    each_in(m, api.t_load_scene, [&](Il2CppObject* d) {
        const std::string to = door_scene(d);
        if (to.empty() || interior_base(to) == in.cfg->scene || to == g_region || interior_base(to) == g_region) return;
        if (other.empty()) other = to;
    });
    if (!other.empty()) return refuse("a door in it leads to " + other);
    if (!live(in.shell)) return refuse("its shell is gone");
    // (its pieces as room_bounds takes them: none over 40 m - painted backdrops - nor 60 m across from its origin)
    const Box b = mesh_box_of(m, 40.0f, in.master_pos, 60.0f);
    if (!b.valid()) return refuse("nothing in it to measure");
    const V3 size = b.max - b.min, mid = (b.min + b.max) * 0.5f;
    if (size.x > kAutoRoomMaxSide || size.z > kAutoRoomMaxSide || size.y > kAutoRoomMaxHeight) {
        char buf[96];
        snprintf(buf, sizeof(buf), "too big (%.0f x %.0f x %.0f m)", size.x, size.y, size.z);
        return refuse(buf);
    }
    const V3 shell_at = position(go_tf(live(in.shell)));
    if (dist_xz(mid, shell_at) > kAutoRoomMaxShift) {
        char buf[96];
        snprintf(buf, sizeof(buf), "it stands %.0f m off its building", dist_xz(mid, shell_at));
        return refuse(buf);
    }
    // nothing of the world (terrain, a cliff, another building or room) in or above the parked room's space (the room
    // itself is still switched off: its colliders aren't in the world)
    const V3 probes[] = {mid, {b.min.x, 0, b.min.z}, {b.min.x, 0, b.max.z}, {b.max.x, 0, b.min.z}, {b.max.x, 0, b.max.z}};
    for (V3 p : probes) {
        const V3 from{p.x, b.max.y + 150.0f, p.z};
        for (const Hit& h : raycast_all(from, {0, -1, 0}, 150.0f + size.y + 1.0f, kQueryIgnore)) {
            if (is_trigger(h.collider) || under_player(h.collider)) continue;
            if (h.point.y > b.min.y - 0.5f) {
                char buf[160];
                snprintf(buf, sizeof(buf), "'%s' stands in its parked spot (%.1f m above the floor)",
                         name_of(go_of(h.collider)).c_str(), h.point.y - b.min.y);
                return refuse(buf);
            }
        }
    }
    for (const Instance& o : g_instances) {  // the other rooms of the region already standing
        if (&o == &in || !o.have_trigger || !master_of(o)) continue;
        const V3 c = to_world(o, o.trig_center);
        const float half = std::max(o.trig_size.x, o.trig_size.z) * 0.5f, up = o.trig_size.y * 0.5f;
        if (c.x + half > b.min.x && c.x - half < b.max.x && c.z + half > b.min.z && c.z - half < b.max.z && c.y + up > b.min.y &&
            c.y - up < b.max.y)
            return refuse(std::string("it would overlap ") + o.cfg->id + "'s room");
    }
    logx::info("interiors: auto: %s: room accepted (%.0f x %.0f x %.0f m, %.0f m off its shell's spot)", in.cfg->id, size.x,
               size.y, size.z, dist_xz(mid, shell_at));
    return true;
}
