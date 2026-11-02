// TLDCoop_DLC: Seamless Interiors: saves from before the rooms (SI: Core/SeamlessInteriorsMod.LegacyImport.cs,
// ProbeLegacySave + TranscodeLegacySaveRoutine + ApplyLegacyRandomSpawns + TranscodeDecorationStates). Included by
// seamless_interiors.cpp after seamless_persist.inc, inside its anonymous namespace.
//
// A building the player used before (its interior scene has a record in the game's save slot) gets its room built from
// that record instead of a fresh roll: what lies on the floor, what is in the containers and curing boxes, what was
// broken down, harvested or opened, the fires, the furniture the player moved or put down, and which of the scene's
// random objects the game's roll kept. The record is only READ (the slot is never written); its parts are turned into
// our 1b files in memory (seamless_persist.inc's g_overlay), the room is built from them, and only when every item,
// container with items, placed object and curing box came back are the files written (staged like any other 1b save:
// they reach the disk with the game's next save) together with the marker "<id>.imported". Anything that can't be
// read or doesn't come back: the building keeps the game's door (1a/1b's behaviour) and the log says why.
//
// After the import the game's own record of the interior is still in the slot. Our door never loads that scene again
// (the room has saved contents, so the door waits for it), so nothing comes twice. If the game's record changes later
// (the mod switched off and the interior played the old way), the marker's hash no longer matches: the game's door
// again, our files left alone.
//
// Coordinates: the interior scene's own coordinates are the room's local ones (plan §1.2): positions carry over as
// they are. In co-op only the host imports (the friends' rooms come from the host's game, milestone 2).

// (Instance::legacy's kLegacyNone / kLegacyReady / kLegacyHydrating: seamless_interiors.cpp)
bool g_import_old = true;  // coop.ini [interiors] import_old
bool g_reimport_changed = true;  // coop.ini [interiors] reimport_changed (TLDCoop_DLC)

struct IApi {
    bool ok = false;
    const MethodInfo* slot_from_name = nullptr, *decompress = nullptr;
    ptrdiff_t slot_dict = -1;
    Il2CppObject* t_scene = nullptr, *t_gear_list = nullptr, *t_gear_proxy = nullptr, *t_container_list = nullptr,
                  *t_container_proxy = nullptr, *t_rso_list = nullptr;
    // SceneSaveGameFormat
    ptrdiff_t s_gear = -1, s_containers = -1, s_fires = -1, s_rso = -1, s_water = -1, s_harvest = -1, s_open = -1,
              s_breakdown = -1, s_placements = -1, s_curing = -1, s_junk = -1;
    ptrdiff_t gl_items = -1, gd_prefab = -1, gd_text = -1, gp_pos = -1, gp_rot = -1;     // GearSaveList, GearItemSaveDataProxy
    ptrdiff_t cl_items = -1, cd_pos = -1, cd_prefab = -1, cd_text = -1, cp_items = -1, cp_decor = -1;  // containers
    ptrdiff_t rl_items = -1, rd_name = -1, rd_pos = -1;                                  // RandomSpawnObjectSaveList
    ptrdiff_t pi_guid = -1, pi_data = -1, pi_state = -1, pd_active = -1, pd_scale = -1;  // PlaceableInfoSaveData
} iapi;

struct LegacyPlacement {
    std::string guid;
    int32_t state = 0;  // TLD.Placement.PlacementState: Active 0, Removed 1, OnPlayer 2, Inactive 3, Unregistered 4
    bool has_data = false, active = true;
    V3 pos, scale;
    Quat rot;
    std::string text;  // PlaceableSaveData as the game serializes it
};
struct LegacyCuring {
    std::string text;
    int items = 0;
};
struct LegacyData {
    std::string key, hash;
    std::vector<std::string> gear, containers, state;  // record lines in our 1b formats
    std::unordered_set<std::string> containers_with_items;
    int items = 0, items_skipped = 0, containers_full = 0, container_items = 0, states = 0, states_skipped = 0, fires = 0;
    std::vector<LegacyPlacement> placements;
    std::unordered_map<std::string, LegacyCuring> curing;  // CuringStorageState.m_GUID -> its record
    std::vector<std::pair<std::string, V3>> rso;           // the random objects the game's roll kept
    bool rso_recorded = false;
    // filled when the room is there
    int moved = 0, removed = 0, spawned = 0, curing_boxes = 0, curing_items = 0, rso_kept = 0, rso_off = 0;
    std::vector<std::string> overlay;  // the in-memory files' paths
};

constexpr const char* kStateNames[] = {"Active", "Removed", "OnPlayer", "Inactive", "Unregistered"};

// --- JSON by hand (SI: SplitJsonArray / UnwrapJsonStringElement / FindClosingQuote) -----------------------------------

size_t closing_quote(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); ++i) {
        if (s[i] == '\\') {
            ++i;
            continue;
        }
        if (s[i] == '"') return i;
    }
    return std::string::npos;
}
std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
// "\"...\"" -> its text; anything else as it is.
std::string unwrap_json_string(const std::string& e) {
    if (e.empty() || e[0] != '"') return e;
    const size_t end = closing_quote(e, 1);
    return end == std::string::npos ? e : json_unescape(e.substr(1, end - 1));
}
// The elements of the first array in the text (quote and escape aware). ok=false: brackets or quotes don't close.
std::vector<std::string> split_json_array(const std::string& json, bool& ok) {
    std::vector<std::string> out;
    ok = true;
    size_t i = json.find('[');
    if (i == std::string::npos) return out;
    ++i;
    int depth = 0;
    bool in_string = false;
    size_t start = std::string::npos;
    auto add = [&](size_t a, size_t len) {
        std::string t = trimmed(json.substr(a, len));
        if (!t.empty()) out.push_back(std::move(t));
    };
    for (; i < json.size(); ++i) {
        const char c = json[i];
        if (in_string) {
            if (c == '\\')
                ++i;
            else if (c == '"')
                in_string = false;
            continue;
        }
        if (c == '"') {
            in_string = true;
            if (depth == 0 && start == std::string::npos) start = i;
        } else if (c == '{' || c == '[') {
            if (depth == 0 && start == std::string::npos) start = i;
            ++depth;
        } else if (c == '}' || c == ']') {
            if (depth == 0) {  // the array's own end
                if (start != std::string::npos) add(start, i - start);
                return out;
            }
            --depth;
        } else if (c == ',' && depth == 0) {
            if (start != std::string::npos) add(start, i - start);
            start = std::string::npos;
        } else if (depth == 0 && start == std::string::npos && !std::isspace(static_cast<unsigned char>(c))) {
            start = i;
        }
    }
    ok = false;  // the array never closed
    return out;
}

std::string hash_text(const std::string& s) {  // FNV-1a 64
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// --- the game's save slot, read only (SI: TryLoadSlotString; SaveGameSlots.TryLoadDataFromSlot<T> by hand: the slot's
//     BaseState.m_Dict[key] -> EncryptString.DecompressBytesToString, as the game's own TryGetData does) -------------

bool slot_text(const std::string& key, std::string& out, std::string& why) {
    out.clear();
    Il2CppString* name = nullptr;
    if (papi.save_name) il2::il2cpp_field_static_get_value(papi.save_name, &name);
    if (!name || il2::to_utf8(name).empty()) {
        why = "the save's name isn't known";
        return false;
    }
    void* a[] = {name};
    Il2CppObject* slot = nullptr;
    if (!call(iapi.slot_from_name, nullptr, a, &slot) || !slot) {
        why = "the save slot '" + il2::to_utf8(name) + "' isn't loaded";
        return false;
    }
    Il2CppObject* dict = get<Il2CppObject*>(slot, iapi.slot_dict);
    const MethodInfo* try_get = dict ? method_up(dict, "TryGetValue", 2) : nullptr;
    if (!try_get) {
        why = "the save slot's records can't be read";
        return false;
    }
    Il2CppObject* bytes = nullptr;
    void* ta[] = {str(key.c_str()), &bytes};
    Il2CppObject* found = nullptr;
    if (!call(try_get, dict, ta, &found) || !found || !*static_cast<const bool*>(il2::il2cpp_object_unbox(found)) || !bytes) {
        why = "the slot has no record '" + key + "'";
        return false;
    }
    void* da[] = {bytes};
    out = trimmed(call_text(iapi.decompress, nullptr, da));
    if (!out.empty() && out[0] == '"') out = trimmed(unwrap_json_string(out));  // stored as a JSON string
    // SI: LooksLikeSceneSave
    if (out.empty() || out[0] != '{' || out.find("\"m_") == std::string::npos) {
        why = "the record '" + key + "' couldn't be unpacked as a scene's save (" + std::to_string(il2::array_length(bytes)) +
              " bytes)";
        out.clear();
        return false;
    }
    return true;
}

// --- which record is this building's (SI: ResolveLegacySource / FindLegacyDoorGuid) -----------------------------------

int buildings_sharing_scene(const Instance& in) {  // SI: CountInstancesSharingScene
    int n = 0;
    for (const Instance& o : g_instances)
        if (std::strcmp(o.cfg->scene, in.cfg->scene) == 0) ++n;
    return n;
}

// The ids of the building's doors into its scene: on its shell, and (SI) the nearest such door within 60 m that isn't
// a room's.
std::vector<std::string> door_guids(Instance& in) {
    const std::string scene = in.cfg->scene;
    std::vector<std::string> guids;
    auto add_door = [&](Il2CppObject* door) {
        if (text(get<Il2CppObject*>(door, api.door_scene)) != scene) return;
        const std::string g = text(get<Il2CppObject*>(door, api.door_guid));
        if (!g.empty() && std::find(guids.begin(), guids.end(), g) == guids.end()) guids.push_back(g);
    };
    if (Il2CppObject* shell = live(in.shell)) each_in(shell, api.t_load_scene, add_door);
    if (api.find_objects && api.t_load_scene) {  // SI: the nearest door to this scene within 60 m that isn't a room's
        void* a[] = {api.t_load_scene};
        Il2CppObject* all = il2::invoke(api.find_objects, nullptr, a);
        Il2CppObject* best = nullptr;
        float best_d = 60.0f;
        for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
            Il2CppObject* d = il2::array_object_at(all, i);
            if (!alive(d) || is_clone_object(d) || text(get<Il2CppObject*>(d, api.door_scene)) != scene) continue;
            const float dd = dist(position(tf_of(d)), in.cfg->fallback);
            if (dd <= best_d) {
                best_d = dd;
                best = d;
            }
        }
        if (best) add_door(best);
    }
    return guids;
}

bool slot_listed(const std::string& k) { return std::find(g_scene_keys.begin(), g_scene_keys.end(), k) != g_scene_keys.end(); }

// SI step 1: the record under one of its doors' ids ("<scene>_<guid>", or the bare guid).
bool guid_key(const Instance& in, const std::vector<std::string>& guids, std::string& key) {
    const std::string scene = in.cfg->scene;
    for (const std::string& g : guids) {
        if (slot_listed(scene + "_" + g)) {
            key = scene + "_" + g;
            return true;
        }
        if (slot_listed(g)) {
            key = g;
            return true;
        }
    }
    return false;
}

// The record a building imported earlier (its "<id>.imported" marker's key), "" for none.
std::string marker_key(Instance& in) {
    ensure_dir(in);
    if (in.dir.empty()) return {};
    const fs::path marker = building_file(in.dir, in, "imported");
    if (!exists_preferred(marker)) return {};
    const Records r = read_records(marker, "imported", 2);
    return r.ok && !r.rows.empty() ? unesc(r.rows[0][0]) : std::string();
}

// A record's key in the "<scene>_<guid>" form (a bare guid gets the scene in front) for comparing claims.
std::string claim_form(const Instance& in, const std::string& k) {
    const std::string prefix = std::string(in.cfg->scene) + "_";
    return k == in.cfg->scene || k.rfind(prefix, 0) == 0 ? k : prefix + k;
}

// The building's own record only: its door's instanced key, its door's guid, (A10, SI: FindLastUnclaimedInstancedKey)
// the one instanced key of a shared scene left over once every other building sharing it has claimed its own or is
// known never to have been visited, or (a scene no other building here uses) the scene's name. Never a record another
// building claims (by its door or its import). `never_visited`: a building sharing its scene whose doors are known,
// none of whose records exist, while every record of the scene is another's (it was never entered): a fresh room.
bool resolve_key(Instance& in, std::string& key, std::string& why, bool* never_visited = nullptr) {
    if (never_visited) *never_visited = false;
    const std::string scene = in.cfg->scene;
    const std::vector<std::string> guids = door_guids(in);
    const int sharing = buildings_sharing_scene(in);
    // The other buildings of this scene: what each claims, and how many can't be told.
    std::unordered_map<std::string, std::string> claimed;  // claim form -> building
    int unresolved = 0;
    std::string unresolved_ids;
    if (sharing > 1)
        for (Instance& o : g_instances) {
            if (&o == &in || std::strcmp(o.cfg->scene, in.cfg->scene) != 0) continue;
            std::string k = marker_key(o);
            const std::vector<std::string> og = k.empty() ? door_guids(o) : std::vector<std::string>{};
            if (k.empty() && !guid_key(o, og, k) && og.empty()) {
                ++unresolved;
                unresolved_ids += (unresolved_ids.empty() ? "" : ", ") + std::string(o.cfg->id);
                continue;
            }
            if (!k.empty()) claimed[claim_form(in, k)] = o.cfg->id;
        }
    auto taken_by = [&](const std::string& k) -> std::string {
        const auto it = claimed.find(claim_form(in, k));
        return it == claimed.end() ? std::string() : it->second;
    };
    if (guid_key(in, guids, key)) {
        if (const std::string other = taken_by(key); !other.empty()) {
            why = "its door's record '" + key + "' is claimed by " + other + " too; neither takes it";
            key.clear();
            return false;
        }
        return true;
    }
    if (sharing <= 1) {
        if (slot_listed(scene)) {
            key = scene;
            return true;
        }
        why = "the save holds a record like its interior but none is surely this building's (" + std::to_string(guids.size()) +
              " door id(s) of it)";
        return false;
    }
    // A10: a shared scene. Its records that no other building claims:
    std::vector<std::string> unclaimed;
    for (const std::string& k : g_scene_keys)
        if (k.rfind(scene + "_", 0) == 0 && taken_by(k).empty()) unclaimed.push_back(k);
    const bool plain = slot_listed(scene);  // the scene's own name: whose can't be told with several buildings
    if (guids.empty() && unresolved == 0 && unclaimed.size() == 1 && !plain) {
        key = unclaimed.front();
        logx::info("interiors: %s: its door's id wasn't found; '%s' is the one record of %s left over once the other %d "
                   "building(s) were paired (SI: last unclaimed key)", in.cfg->id, key.c_str(), in.cfg->scene, sharing - 1);
        return true;
    }
    if (!guids.empty() && unclaimed.empty() && !plain && unresolved == 0) {
        if (never_visited) *never_visited = true;
        why = "never entered (its door's records aren't in the save; every record of " + scene + " is another building's)";
        return false;
    }
    std::string left;
    for (const std::string& k : unclaimed) left += (left.empty() ? "'" : ", '") + k + "'";
    why = std::to_string(sharing) + " buildings here use " + scene + "; this one's door id(s) (" + std::to_string(guids.size()) +
          ") match no record; unclaimed: " + (left.empty() ? std::string("none") : left) + (plain ? " (and the plain '" + scene + "')" : "") +
          (unresolved ? "; can't tell for " + unresolved_ids : "") + "; not guessing (ambiguous: the game's door)";
    return false;
}

// --- reading the record (SI: TranscodeGear / TranscodeContainers / TranscodeInteractiveStates / TranscodeFires /
//     the placements and the RSO list), into lines of our formats --------------------------------------------------------

std::string field_text(Il2CppObject* o, ptrdiff_t off) { return o && off >= 0 ? text(get<Il2CppObject*>(o, off)) : std::string(); }

bool read_gear(Instance& in, LegacyData& d, const std::string& blob, std::string& why) {
    if (trimmed(blob).empty()) return true;
    Il2CppObject* list = deserialize_object(blob, iapi.t_gear_list);
    if (!list) {
        why = "its items (GearSaveList, " + std::to_string(blob.size()) + " characters) couldn't be read";
        return false;
    }
    std::unordered_set<std::string> used;
    bool bad = false;
    each_listed(get<Il2CppObject*>(list, iapi.gl_items), [&](Il2CppObject* gd, int i) {
        if (bad) return;
        const std::string prefab = field_text(gd, iapi.gd_prefab), s = field_text(gd, iapi.gd_text);
        if (!gd || prefab.empty() || s.empty()) {  // nothing the game itself could make either
            ++d.items_skipped;
            return;
        }
        Il2CppObject* proxy = deserialize_object(s, iapi.t_gear_proxy);
        if (!proxy) {
            why = "item #" + std::to_string(i) + " ('" + prefab + "') couldn't be read";
            bad = true;
            return;
        }
        const V3 pos = get<V3>(proxy, iapi.gp_pos);
        Quat rot = get<Quat>(proxy, iapi.gp_rot);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)) {
            why = "item #" + std::to_string(i) + " ('" + prefab + "') has no position";
            bad = true;
            return;
        }
        if (rot.x == 0 && rot.y == 0 && rot.z == 0 && rot.w == 0) rot = Quat{};  // SI: a degenerate rotation
        // our id of it, as the template items' (SI: GenerateDeterministicPDIDs), unique in the room
        const std::string base = std::string(in.cfg->id) + "_" + clean_name(prefab) + "_" + f2(pos.x) + "_" + f2(pos.y) + "_" + f2(pos.z);
        std::string id = base;
        for (int n = 1; !used.insert(id).second; ++n) id = base + "#" + std::to_string(n);
        // scale 0 0 0: "leave it" (SI: the vanilla record has none; a place point's own scale stays)
        d.gear.push_back(esc(prefab) + "\t" + esc(id) + "\t" + v3_fields(pos) + "\t" + q_fields(rot) + "\t0\t0\t0\t1\t" + esc(s));
        ++d.items;
    });
    return !bad;
}

bool read_containers(LegacyData& d, const std::string& blob, std::string& why) {
    if (trimmed(blob).empty()) return true;
    Il2CppObject* list = deserialize_object(blob, iapi.t_container_list);
    if (!list) {
        why = "its containers (ContainerSaveList) couldn't be read";
        return false;
    }
    bool bad = false;
    each_listed(get<Il2CppObject*>(list, iapi.cl_items), [&](Il2CppObject* cd, int i) {
        if (bad || !cd) return;
        const std::string data = field_text(cd, iapi.cd_text), prefab = field_text(cd, iapi.cd_prefab);
        if (data.empty()) return;
        Il2CppObject* proxy = deserialize_object(data, iapi.t_container_proxy);
        if (!proxy) {
            why = "container #" + std::to_string(i) + " ('" + prefab + "') couldn't be read";
            bad = true;
            return;
        }
        const int n = list_count(get<Il2CppObject*>(proxy, iapi.cp_items)) + list_count(get<Il2CppObject*>(proxy, iapi.cp_decor));
        // fields as save_containers writes them: key (none that matches: the spot decides), name, spot, the game's text
        const std::string line = esc("legacy###" + std::to_string(i)) + "\t" + esc(clean_name(prefab)) + "\t" +
                                 v3_fields(get<V3>(cd, iapi.cd_pos)) + "\t" + esc(data);
        d.containers.push_back(line);
        if (n > 0) {
            d.containers_with_items.insert(line);
            ++d.containers_full;
            d.container_items += n;
        }
    });
    return !bad;
}

// The scene's aggregate lists (each element one object's own Serialize() text): keyed as our save keys them (by the
// guid inside; SI: CollectLegacyStates).
bool read_states(LegacyData& d, const char* tag, const std::string& blob, std::unordered_set<std::string>& used, std::string& why) {
    if (trimmed(blob).empty()) return true;
    bool ok = true;
    const std::vector<std::string> elements = split_json_array(blob, ok);
    if (!ok) {
        why = std::string("its ") + tag + " states couldn't be read";
        return false;
    }
    for (const std::string& raw : elements) {
        const std::string e = unwrap_json_string(raw);
        const std::string g = guid_in(e);
        if (g.empty()) {  // SI: without a guid the object can't be told: skipped
            ++d.states_skipped;
            continue;
        }
        const std::string key = std::string(tag) + "|g:" + g;
        if (!used.insert(key).second) continue;
        d.state.push_back(esc(key) + "\t" + esc(e));
        ++d.states;
    }
    return true;
}

bool read_placements(LegacyData& d, Il2CppObject* list, std::string& why) {
    bool bad = false;
    each_listed(list, [&](Il2CppObject* info, int i) {
        if (bad || !info) return;
        LegacyPlacement p;
        p.guid = field_text(info, iapi.pi_guid);
        if (p.guid.empty()) return;  // SI: only the guid is needed (a "Removed" record has nothing else)
        p.state = get<int32_t>(info, iapi.pi_state);
        if (Il2CppObject* data = get<Il2CppObject*>(info, iapi.pi_data)) {
            p.has_data = true;
            p.pos = get<V3>(data, papi.data_position);
            p.rot = get<Quat>(data, papi.data_rotation);
            p.scale = iapi.pd_scale >= 0 ? get<V3>(data, iapi.pd_scale) : V3{};
            p.active = iapi.pd_active >= 0 ? get<bool>(data, iapi.pd_active) : true;
            void* a[] = {data};
            p.text = call_text(papi.utils_serialize, nullptr, a);
            if (p.text.empty()) {
                why = "placed object #" + std::to_string(i) + " couldn't be written out";
                bad = true;
                return;
            }
        }
        d.placements.push_back(std::move(p));
    });
    return !bad;
}

bool read_curing(LegacyData& d, Il2CppObject* list, std::string& why) {
    bool bad = false;
    each_listed(list, [&](Il2CppObject* st, int) {
        if (bad || !st) return;
        const std::string g = field_text(st, papi.curing_guid);
        const int items = curing_count(st);
        if (g.empty()) {
            if (items > 0) {
                why = "a curing box with " + std::to_string(items) + " item(s) has no id";
                bad = true;
            }
            return;
        }
        void* a[] = {st};
        LegacyCuring c{call_text(papi.utils_serialize, nullptr, a), items};
        if (c.text.empty() && items > 0) {
            why = "a curing box with " + std::to_string(items) + " item(s) couldn't be written out";
            bad = true;
            return;
        }
        d.curing[g] = std::move(c);
    });
    return !bad;
}

bool read_rso(LegacyData& d, const std::string& blob, std::string& why) {
    if (trimmed(blob).empty()) return true;
    Il2CppObject* list = deserialize_object(blob, iapi.t_rso_list);
    if (!list) {
        why = "its random objects' record couldn't be read";
        return false;
    }
    each_listed(get<Il2CppObject*>(list, iapi.rl_items), [&](Il2CppObject* sd, int) {
        const std::string n = field_text(sd, iapi.rd_name);
        if (!n.empty()) d.rso.push_back({clean_name(n), get<V3>(sd, iapi.rd_pos)});
    });
    d.rso_recorded = !d.rso.empty();
    return true;
}

// SI: ProbeLegacySave + the transcode's reading half: everything that doesn't need the room, before anything is loaded.
bool prepare_import(Instance& in, std::string& why) {
    auto d = std::make_shared<LegacyData>();
    if (!resolve_key(in, d->key, why)) return false;
    std::string raw;
    if (!slot_text(d->key, raw, why)) return false;
    d->hash = hash_text(raw);
    const double t0 = now_ms();
    Il2CppObject* fmt = deserialize_object(raw, iapi.t_scene);
    if (!fmt) {
        why = "its record '" + d->key + "' (" + std::to_string(raw.size()) + " characters) couldn't be read as a scene's save";
        return false;
    }
    std::unordered_set<std::string> used;
    if (!read_gear(in, *d, field_text(fmt, iapi.s_gear), why) || !read_containers(*d, field_text(fmt, iapi.s_containers), why) ||
        !read_states(*d, "BD", field_text(fmt, iapi.s_breakdown), used, why) ||
        !read_states(*d, "HV", field_text(fmt, iapi.s_harvest), used, why) ||
        !read_states(*d, "OC", field_text(fmt, iapi.s_open), used, why) ||
        !read_states(*d, "WS", field_text(fmt, iapi.s_water), used, why) ||
        !read_placements(*d, iapi.s_placements >= 0 ? get<Il2CppObject*>(fmt, iapi.s_placements) : nullptr, why) ||
        !read_curing(*d, iapi.s_curing >= 0 ? get<Il2CppObject*>(fmt, iapi.s_curing) : nullptr, why) ||
        !read_rso(*d, field_text(fmt, iapi.s_rso), why))
        return false;
    // fires still burning in it (SI: TranscodeFires; the building's file is where 1b looks first)
    std::unordered_map<std::string, std::string> fires;
    parse_fire_records(field_text(fmt, iapi.s_fires), fires);
    if (iapi.s_junk >= 0 && get<bool>(fmt, iapi.s_junk) && used.insert("JK|cleared").second)  // A11: its junk cleared
        d->state.push_back(esc("JK|cleared") + "\t" + esc("1"));
    for (const auto& [g, payload] : fires)
        if (used.insert("FI|" + g).second) {
            d->state.push_back(esc("FI|" + g) + "\t" + esc(payload));
            ++d->fires;
        }
    int states_by_kind[5] = {};
    for (const LegacyPlacement& p : d->placements)
        if (p.state >= 0 && p.state < 5) ++states_by_kind[p.state];
    int curing_items = 0;
    for (const auto& [g, c] : d->curing) curing_items += c.items;
    logx::info("interiors: %s: the save holds its interior from before the rooms ('%s', %zu characters, read in %.0f ms): %d "
               "item(s) (%d empty record(s) skipped), %zu container(s) of which %d with %d item(s), %d object state(s) (%d "
               "without an id skipped), %d burning fire(s), %zu placement record(s) (Active %d, Removed %d, OnPlayer %d, "
               "Inactive %d, Unregistered %d), %zu curing box(es) with %d item(s), %zu random object(s) kept by the roll; "
               "building its room from it",
               in.cfg->id, d->key.c_str(), raw.size(), now_ms() - t0, d->items, d->items_skipped, d->containers.size(),
               d->containers_full, d->container_items, d->states, d->states_skipped, d->fires, d->placements.size(),
               states_by_kind[0], states_by_kind[1], states_by_kind[2], states_by_kind[3], states_by_kind[4], d->curing.size(),
               curing_items, d->rso.size());
    in.legacy_data = d;
    in.legacy = kLegacyReady;
    return true;
}

// --- the room is there (PreRoll, before the loot decision): the parts that need it ---------------------------------

// SI: ApplyLegacyRandomSpawns: of each random spawner the game's record names a candidate of, the recorded ones stay
// and the others go; a spawner the record says nothing about is left as the scene has it (SI: all off; here nothing
// is guessed).
void apply_rso(Instance& in, LegacyData& d) {
    if (!d.rso_recorded || api.rso_list < 0) return;
    int spawners = 0;
    each_in(master_of(in), api.t_rso, [&](Il2CppObject* rso) {
        Il2CppObject* list = get<Il2CppObject*>(rso, api.rso_list);
        std::vector<std::pair<Il2CppObject*, bool>> cands;
        bool any = false;
        for (size_t i = 0, n = il2::array_length(list); i < n; ++i) {
            Il2CppObject* go = il2::array_object_at(list, i);
            if (!alive(go)) continue;
            const std::string nm = clean_name(name_of(go));
            const V3 lp = to_local(in, position(go_tf(go)));
            bool chosen = false;
            for (const auto& [rn, rp] : d.rso)
                if (rn == nm && dist(rp, lp) <= 0.35f) {  // SI: RSO_MATCH_RADIUS
                    chosen = true;
                    break;
                }
            any = any || chosen;
            cands.push_back({go, chosen});
        }
        if (!any) return;
        ++spawners;
        for (auto& [go, chosen] : cands) {
            set_active(go, chosen);
            ++(chosen ? d.rso_kept : d.rso_off);
        }
    });
    logx::info("interiors: %s: the game's roll replayed on %d random spawner(s): %d object(s) kept, %d off", in.cfg->id, spawners,
               d.rso_kept, d.rso_off);
}

// The furniture file (every placeable of the room: as the record has it, else as the scene has it, so the restore
// switches nothing off for not being listed) and the player's own furniture (SI: ApplyLegacyPlaceablesRoutine), then
// all five files into memory.
bool compose_import(Instance& in, std::string& why) {
    LegacyData& d = *in.legacy_data;
    apply_rso(in, d);
    std::unordered_map<std::string, const LegacyPlacement*> rec;
    for (const LegacyPlacement& p : d.placements) rec[p.guid] = &p;
    std::vector<std::string> lines, spawned;
    std::unordered_set<std::string> in_room, with_curing;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* pl) {
        const std::string g = placeable_guid(pl);
        if (g.empty() || !in_room.insert(g).second) return;
        Il2CppObject* t = tf_of(pl);
        V3 lp = to_local(in, position(t)), sc = local_scale(t);
        Quat lr = mul(conj(in.master_rot), rotation(t));
        bool on = active_self(go_of(pl));
        if (auto it = rec.find(g); it != rec.end()) {
            const LegacyPlacement& p = *it->second;
            if (p.state == 1 || p.state == 2) {  // Removed / OnPlayer: taken away (SI: switched off in the room)
                on = false;
                ++d.removed;
            } else if (p.has_data) {  // SI (a): the scene's furniture the player moved
                lp = p.pos;
                if (!(p.rot.x == 0 && p.rot.y == 0 && p.rot.z == 0 && p.rot.w == 0)) lr = p.rot;
                if (!is_zero(p.scale)) sc = p.scale;
                on = p.active;
                ++d.moved;
            }
        }
        lines.push_back(esc(g) + "\t" + v3_fields(lp) + "\t" + q_fields(lr) + "\t" + v3_fields(sc) + "\t" + (on ? "1" : "0"));
    });
    lines.push_back(esc(kStableGuidMarker) + "\t0\t0\t0\t0\t0\t0\t1\t1\t1\t1\t1");
    std::unordered_set<std::string> seen;
    for (const LegacyPlacement& p : d.placements) {  // SI (b): what the player put down
        if (in_room.count(p.guid) || p.state == 1 || p.state == 2 || !p.has_data || !seen.insert(p.guid).second) continue;
        std::string line = esc(p.guid) + "\t" + esc(p.text);
        if (auto c = d.curing.find(p.guid); c != d.curing.end() && !c->second.text.empty()) {
            line += "\t" + esc(c->second.text);
            if (c->second.items > 0) {
                with_curing.insert(p.guid);
                ++d.curing_boxes;
                d.curing_items += c->second.items;
            }
        }
        spawned.push_back(line);
        ++d.spawned;
    }
    for (const auto& [g, c] : d.curing)
        if (c.items > 0 && !with_curing.count(g)) {
            why = "a curing box (" + g + ") with " + std::to_string(c.items) + " item(s) isn't one of the objects placed in it";
            return false;
        }
    const std::pair<const char*, const std::vector<std::string>*> files[] = {
        {"gear", &d.gear}, {"containers", &d.containers}, {"state", &d.state}, {"placeables", &lines}, {"spawned", &spawned}};
    for (const auto& [kind, rows] : files) {
        const fs::path p = building_file(in.dir, in, kind);
        if (p.empty()) {
            why = "its data folder isn't known";
            return false;
        }
        g_overlay[p.string()] = file_text(kind, *rows);
        d.overlay.push_back(p.string());
    }
    in.legacy = kLegacyHydrating;
    logx::info("interiors: %s: the old record turned into the room's files (in memory): %d item(s), %zu container(s), %zu "
               "state(s), %zu furniture position(s) (%d moved, %d taken away), %d placed object(s) of the player's (%d curing "
               "box(es) with %d item(s))",
               in.cfg->id, d.items, d.containers.size(), d.state.size(), lines.size() - 1, d.moved, d.removed, d.spawned,
               d.curing_boxes, d.curing_items);
    return true;
}

void drop_import(Instance& in) {
    if (in.legacy_data)
        for (const std::string& p : in.legacy_data->overlay) g_overlay.erase(p);
    in.legacy_data.reset();
    in.legacy = kLegacyNone;
}

// --- after the hydration: all of it, or none of it -------------------------------------------------------------------

// Every item made, every container with items filled, every placed object made, every curing box filled, no file
// refused: else why.
bool import_complete(Instance& in, std::string& why) {
    const LegacyData& d = *in.legacy_data;
    if (!in.from_files) why = "the room didn't take its contents from the record";
    else if (in.gear_blocked || in.container_blocked || in.state_blocked || in.placeable_blocked || in.spawn_blocked)
        why = "one of the files made from the record couldn't be read back";
    else if (in.gear_failed > 0)
        why = std::to_string(in.gear_failed) + " of " + std::to_string(d.items) + " item(s) couldn't be made";
    else if (!in.orphan_spawned.empty())
        why = std::to_string(in.orphan_spawned.size()) + " object(s) the player placed couldn't be made";
    else if (!in.curing_kept.empty())
        why = std::to_string(in.curing_kept.size()) + " curing box(es) couldn't take their contents";
    else {
        int lost = 0;
        for (const std::string& l : in.orphan_containers)
            if (d.containers_with_items.count(l)) ++lost;
        if (lost) why = std::to_string(lost) + " container(s) with items found no container in the room";
        for (const auto& [g, c] : d.curing)
            if (c.items > 0 && !in.curing_applied.count(g)) {
                why = "the curing box " + g + " didn't come back";
                break;
            }
    }
    return why.empty();
}

// The room built from the record is put away for good (switched off; its things are out of the game's saves and the
// co-op lists, as any room's that isn't ready); the building keeps the game's door.
void abandon_import(Instance& in, const std::string& why) {
    logx::warn("interiors: %s: importing its interior from the old save stopped: %s. Nothing was written; the game's own "
               "record is untouched and its door stays the game's (tried again on the next visit to the region)",
               in.cfg->id, why.c_str());
    drop_import(in);
    if (Il2CppObject* m = master_of(in)) set_active(m, false);
    if (Il2CppObject* s = live(in.shell)) set_active(s, true);
    in.hydrated = false;
    set_stage(in, Stage::Vanilla);
}

// The room is ready and complete: its files written (staged: they reach the disk with the game's next save, a load
// without saving throws them away and the import happens again) with the marker.
void commit_import(Instance& in) {
    if (in.legacy != kLegacyHydrating || !in.legacy_data) return;
    std::lock_guard lock(g_files);  // (P12: the files and the marker together, no commit in between)
    const std::shared_ptr<LegacyData> d = in.legacy_data;
    drop_import(in);
    bool ok = save_placeables(in) & save_gear(in) & save_containers(in) & save_state(in);
    g_rolled_ids.insert(in.cfg->id);
    std::vector<std::string> ids(g_rolled_ids.begin(), g_rolled_ids.end());
    std::sort(ids.begin(), ids.end());
    std::vector<std::string> rolled;
    for (const std::string& id : ids) rolled.push_back(esc(id));
    ok = write_staged(in.dir / "loot.rolled", file_text("rolled", rolled)) && ok;
    in.has_data = true;
    const std::string counts = std::to_string(d->items) + "\t" + std::to_string(d->containers.size()) + "\t" +
                               std::to_string(d->container_items) + "\t" + std::to_string(d->states) + "\t" +
                               std::to_string(d->spawned);
    const std::string marker = file_text("imported", {esc(d->key) + "\t" + d->hash + "\t" + counts});
    // (the same, as "<id>.imported-from": these files came from the import; should the marker never reach the disk
    // with them, old_save_gate still knows them for the import's own, item 4 of the follow-up)
    ok = write_staged(building_file(in.dir, in, "imported-from"), marker) && ok;
    // (P6) the marker only after every file of the room: else it waits for a save that writes them all (save_rooms)
    if (!ok || !write_staged(building_file(in.dir, in, "imported"), marker)) {
        in.import_marker = marker;
        logx::warn("interiors: %s: its files from the old save couldn't all be written; the import marker waits for the "
                   "next save that writes them", in.cfg->id);
        return;
    }
    logx::info("interiors: %s: IMPORTED from the old save ('%s'): %d item(s) on the floor, %d container(s) with %d item(s) "
               "(%zu container record(s) in all), %d object state(s), %d fire(s), %d furniture moved and %d taken away, %d "
               "placed object(s) of the player's, %d curing box(es) with %d item(s); written with the next save",
               in.cfg->id, d->key.c_str(), d->items, d->containers_full, d->container_items, d->containers.size(), d->states,
               d->fires, d->moved, d->removed, d->spawned, d->curing_boxes, d->curing_items);
}

// --- the door's decision (pick_building, before the room is built) --------------------------------------------------

// Our files of the building and the game's record of its interior, no marker: a session that ended between the import's
// files reaching the disk and its marker (the marker is committed last). The files are the import's when its
// "<id>.imported-from" says so, the game's record is still the one imported (its hash), and every file of the room is
// there whole (read back with its footer); then they are taken and the marker written. Else false (why: the reason,
// when the files were an import's).
bool recover_import_marker(Instance& in, const std::string& key_now, std::string& why) {
    const fs::path from = building_file(in.dir, in, "imported-from");
    if (from.empty() || !exists_preferred(from)) return false;
    const Records r = read_records(from, "imported", 2);
    auto fail = [&](const std::string& w) {
        why = "the save holds its interior ('" + key_now + "') and we have files of it from an import, without its marker, "
              "that can't be taken: " + w;
        logx::warn("interiors: %s: %s", in.cfg->id, why.c_str());
        return false;
    };
    if (!r.whole || r.rows.empty()) return fail("their import note can't be read");
    std::string key = unesc(r.rows[0][0]), now_key, raw, err;
    if (resolve_key(in, now_key, err)) key = now_key;
    if (key.empty() || !slot_text(key, raw, err)) return fail("the game's record ('" + key + "') can't be read: " + err);
    if (hash_text(raw) != r.rows[0][1])
        return fail("the game's record ('" + key + "') changed after the import; which one is newer can't be told");
    const std::pair<const char*, size_t> kinds[] = {
        {"placeables", 12}, {"spawned", 2}, {"gear", 14}, {"containers", 6}, {"state", 2}};
    for (const auto& [kind, fields] : kinds) {
        const fs::path p = building_file(in.dir, in, kind);
        if (!exists_preferred(p)) return fail(std::string("its .") + kind + " file is missing");
        const Records k = read_records(p, kind, fields);
        if (!k.whole) return fail(std::string("its .") + kind + " file isn't whole");
    }
    // the marker as the import wrote it (its text is the note's)
    std::vector<std::string> row_lines(r.lines.begin(), r.lines.end());
    const std::string marker = file_text("imported", row_lines);
    if (!write_staged(building_file(in.dir, in, "imported"), marker)) in.import_marker = marker;  // with the next save
    logx::info("interiors: %s: our files of it came from an import of the old save ('%s') whose marker never reached the "
               "disk; they are whole and the game's record is unchanged: the room comes from them, the marker %s",
               in.cfg->id, key.c_str(), in.import_marker.empty() ? "written now (staged, with the next save)"
                                                                  : "waits for the next save");
    return true;
}

// True: build the room. False: the game's door, why says why (1a/1b's has_vanilla_data rule, now with the import).
bool old_save_gate(Instance& in, std::string& why) {
    const fs::path marker = building_file(in.dir, in, "imported");
    if (exists_preferred(marker)) {
        const Records r = read_records(marker, "imported", 2);
        if (!r.ok || r.rows.empty()) {
            why = "its import marker can't be read";
            return false;
        }
        std::string key = unesc(r.rows[0][0]), now_key, raw, err;
        std::string unused;
        has_vanilla_data(in, unused);  // the slot's keys
        if (resolve_key(in, now_key, err)) key = now_key;
        if (!key.empty() && slot_text(key, raw, err) && hash_text(raw) != r.rows[0][1]) {
            if (!g_reimport_changed) {
                why = "the game's own record of its interior ('" + key +
                      "') changed after it was imported (played without the rooms?); which one is newer can't be told";
                return false;
            }
            // TLDCoop_DLC ([interiors] reimport_changed=1): the game's record is newer (someone went in through the
            // game's door since): our files of it are put aside (worlds/<save>.interiors/replaced/) and the room is
            // imported again from the game's record
            std::error_code ec;
            const fs::path aside = in.dir / "replaced" / (std::string(in.cfg->id) + "_" + std::to_string(GetTickCount64()));
            fs::create_directories(aside, ec);
            int moved = 0;
            for (const auto& e : fs::directory_iterator(in.dir, ec)) {
                const std::string n = e.path().filename().string();
                if (!e.is_regular_file() || n.rfind(std::string(in.cfg->id) + ".", 0) != 0) continue;
                fs::rename(e.path(), aside / n, ec);
                if (!ec) ++moved;
            }
            in.has_data = false;
            logx::info("interiors: %s: the game's own record of it ('%s') changed after our import (someone went in through "
                       "the game's door): %d file(s) of ours put aside in %s; imported again from the game's record",
                       in.cfg->id, key.c_str(), moved, aside.string().c_str());
            if (!iapi.ok || !papi.ok || !prepare_import(in, why)) {
                why = "imported again from the game's record, but that failed: " + why;
                return false;
            }
            return true;
        }
        logx::info("interiors: %s: imported from the old save earlier ('%s'); its room comes from our files", in.cfg->id,
                   key.c_str());
        return true;
    }
    std::string key;
    if (!has_vanilla_data(in, key)) return true;
    if (!g_scene_keys_failed && buildings_sharing_scene(in) > 1) {  // A10: which of the scene's records, if any, is ours
        std::string own, err;
        bool never = false;
        if (resolve_key(in, own, err, &never)) {
            key = own;
        } else if (never) {  // (our files, if any, are then the only record of it)
            logx::info("interiors: %s: shares %s with other buildings and was %s: %s", in.cfg->id, in.cfg->scene, err.c_str(),
                       in.has_data ? "its room from our files" : "a fresh room");
            return true;
        }
    }
    if (in.has_data && recover_import_marker(in, key, why)) return true;
    if (g_scene_keys_failed) {
        why = "the save slot's records can't be listed";
        return false;
    }
    if (!g_import_old) {
        why = "the save already holds its interior ('" + key + "') and [interiors] import_old=0";
        return false;
    }
    if (!iapi.ok || !papi.ok) {
        why = "the save already holds its interior ('" + key + "') and this build can't import it";
        return false;
    }
    if (in.has_data) {
        why = "the save holds its interior ('" + key + "') and we have files of it too; which is newer can't be told";
        return false;
    }
    if (!prepare_import(in, why)) {
        why = "its interior in the old save can't be imported: " + why;
        return false;
    }
    return true;
}

void resolve_import() {
    auto off = [](Il2CppClass* k, const char* f) { return k ? il2::field_offset(k, f) : -1; };
    Il2CppClass* slots = il2::find_class(kGame, "", "SaveGameSlots");
    iapi.slot_from_name = slots ? il2::find_method_params(slots, "GetSaveSlotFromName", {"System.String"}) : nullptr;
    if (Il2CppClass* bs = class_in({"TLD.SaveState.dll", kGame}, "TLD.SaveState", "BaseState")) iapi.slot_dict = off(bs, "m_Dict");
    if (Il2CppClass* enc = class_in({"TLD.Encryption.dll", kGame}, "TLD.Encryption", "EncryptString"))
        iapi.decompress = il2::find_method_params(enc, "DecompressBytesToString", {"System.Byte[]"});
    Il2CppClass* scene = il2::find_class(kGame, "", "SceneSaveGameFormat");
    iapi.t_scene = scene ? il2::type_object(scene) : nullptr;
    iapi.s_gear = off(scene, "m_GearManagerSerialized");
    iapi.s_containers = off(scene, "m_ContainerManagerSerialized");
    iapi.s_fires = off(scene, "m_FireManagerSerialized");
    iapi.s_rso = off(scene, "m_RandomSpawnObjectManagerSerialized");
    iapi.s_water = off(scene, "m_WaterSourceManagerSerialized");
    iapi.s_harvest = off(scene, "m_HarvestablesSerialized");
    iapi.s_open = off(scene, "m_OpenClosersSerialized");
    iapi.s_breakdown = off(scene, "m_BreakDownObjectsSerialized");
    iapi.s_placements = off(scene, "m_PlacementListSerialized");
    iapi.s_curing = off(scene, "m_CuringStorageSerialized");
    iapi.s_junk = off(scene, "m_JunkManagedSerialized");  // JunkManagerSaveData (a struct: its bool first)
    Il2CppClass* gl = il2::find_class(kGame, "", "GearSaveList");
    Il2CppClass* gd = il2::find_class(kGame, "", "GearSaveData");
    Il2CppClass* gp = il2::find_class(kGame, "", "GearItemSaveDataProxy");
    iapi.t_gear_list = gl ? il2::type_object(gl) : nullptr;
    iapi.t_gear_proxy = gp ? il2::type_object(gp) : nullptr;
    iapi.gl_items = off(gl, "m_SerializedItems");
    iapi.gd_prefab = off(gd, "m_PrefabName");
    iapi.gd_text = off(gd, "m_SearializedGear");
    iapi.gp_pos = off(gp, "m_Position");
    iapi.gp_rot = off(gp, "m_Rotation");
    Il2CppClass* cl = il2::find_class(kGame, "", "ContainerSaveList");
    Il2CppClass* cd = il2::find_class(kGame, "", "ContainerSaveData");
    Il2CppClass* cp = il2::find_class(kGame, "", "ContainerSaveDataProxy");
    iapi.t_container_list = cl ? il2::type_object(cl) : nullptr;
    iapi.t_container_proxy = cp ? il2::type_object(cp) : nullptr;
    iapi.cl_items = off(cl, "m_SerializedContainers");
    iapi.cd_pos = off(cd, "m_Position");
    iapi.cd_prefab = off(cd, "m_PrefabName");
    iapi.cd_text = off(cd, "m_SearializedContainer");
    iapi.cp_items = off(cp, "m_SerializedItems");
    iapi.cp_decor = off(cp, "m_SerializedDecorationItems");
    Il2CppClass* rl = il2::find_class(kGame, "", "RandomSpawnObjectSaveList");
    Il2CppClass* rd = il2::find_class(kGame, "", "RandomSpawnObjectSaveData");
    iapi.t_rso_list = rl ? il2::type_object(rl) : nullptr;
    iapi.rl_items = off(rl, "m_SaveDataList");
    iapi.rd_name = off(rd, "m_ObjectName");
    iapi.rd_pos = off(rd, "m_Position");
    Il2CppClass* pi = il2::find_class(kGame, "TLD.Placement", "PlaceableInfoSaveData");
    Il2CppClass* pd = il2::find_class(kGame, "TLD.Placement", "PlaceableSaveData");
    iapi.pi_guid = off(pi, "m_Guid");
    iapi.pi_data = off(pi, "m_Serialized");
    iapi.pi_state = off(pi, "m_State");
    iapi.pd_active = off(pd, "m_ActiveSelf");
    iapi.pd_scale = off(pd, "m_Scale");
    struct Need {
        const char* what;
        bool ok;
    };
    const Need needs[] = {
        {"the save slot's records (SaveGameSlots.GetSaveSlotFromName, BaseState.m_Dict, EncryptString.DecompressBytesToString)",
         iapi.slot_from_name && iapi.slot_dict >= 0 && iapi.decompress},
        {"SceneSaveGameFormat", iapi.t_scene && iapi.s_gear >= 0 && iapi.s_containers >= 0 && iapi.s_placements >= 0},
        {"the item records", iapi.t_gear_list && iapi.t_gear_proxy && iapi.gl_items >= 0 && iapi.gd_prefab >= 0 &&
                                 iapi.gd_text >= 0 && iapi.gp_pos >= 0 && iapi.gp_rot >= 0},
        {"the container records", iapi.t_container_list && iapi.t_container_proxy && iapi.cl_items >= 0 && iapi.cd_pos >= 0 &&
                                      iapi.cd_text >= 0 && iapi.cp_items >= 0},
        {"the placement records", iapi.pi_guid >= 0 && iapi.pi_data >= 0 && iapi.pi_state >= 0},
        {"the random objects' records", iapi.t_rso_list && iapi.rl_items >= 0 && iapi.rd_name >= 0 && iapi.rd_pos >= 0},
        {"the curing boxes' records", iapi.s_curing >= 0 && papi.curing_guid >= 0 && papi.curing_items >= 0},
    };
    bool ok = true;
    for (const Need& n : needs)
        if (!n.ok) {
            logx::warn("interiors: %s missing in this build: old saves aren't imported (their buildings keep the game's door)",
                       n.what);
            ok = false;
        }
    iapi.ok = ok;
    logx::info("interiors: importing old saves' interiors %s", ok ? "ready" : "OFF");
}
