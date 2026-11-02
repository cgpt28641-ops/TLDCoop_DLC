// TLDCoop_DLC: Seamless Interiors, milestone 1b: the rooms keep their state. Included by seamless_interiors.cpp inside
// its anonymous namespace (it uses that file's helpers and Instance). Each part names the SI function it ports.
//
// Where the data lives (SI: ModPaths/SaveFiles; its PlayerPrefs keys moved in): <instance_dir>/worlds/<save>.interiors/,
// <save> being game::save_id() made file-safe, the same identity world_keep's file next to it uses (slot name, the
// game's id and its seed: a slot name reused by a new game gets a new folder, so SI's save-identity stamp and
// CheckNewGameLootLock's wipe are not needed). One file per building and kind, positions local to the room (= the
// interior scene's own coordinates), so a room parked or turned differently still matches:
//   <id>.placeables  <id>.spawned  <id>.gear  <id>.containers  <id>.state   player.inside   loot.rolled
// Text, one record per line, tab-separated fields, strings escaped (\\ \t \n \r), first line "TLDCOOP-SI 2 <kind>", last
// line "#end <records>" (a file cut short is told from an empty one; "TLDCOOP-SI 1" files, from before, have none).
// Staging (SI: JsonWriteCache + SaveCommitPatches): SaveSceneData writes "<file>.pending"; OnSaveCompleted for the
// current save renames them over the real ones; RestoreGame throws them away; readers prefer ".pending".

// --- the persistence part of the game's API -----------------------------------------------------------------------------

struct PApi {
    bool ok = false;
    const MethodInfo* delta_time = nullptr, *tod_component = nullptr, *tod_seconds = nullptr, *hours_played = nullptr;
    const MethodInfo* gear_to_string = nullptr, *gear_instantiate = nullptr;
    const MethodInfo* container_save = nullptr, *container_load = nullptr, *container_clear = nullptr;
    const MethodInfo* placeable_save = nullptr, *placeable_create = nullptr, *placeable_remove = nullptr;
    const MethodInfo* utils_serialize = nullptr, *json_deserialize = nullptr, *fast_deserialize = nullptr;
    FieldInfo* use_json_net = nullptr, *json_settings = nullptr;
    Il2CppObject* t_placeable_data = nullptr;
    ptrdiff_t placeable_guid = -1, data_position = -1, data_rotation = -1;
    FieldInfo* placeables_loading = nullptr;
    const MethodInfo* pdid_get = nullptr, *pdid_registered = nullptr, *pdid_replace = nullptr;
    const MethodInfo* fire_save = nullptr, *fire_load = nullptr, *fire_burning = nullptr, *fire_left = nullptr,
                      *fire_remove = nullptr, *heat_update = nullptr;
    ptrdiff_t fire_elapsed = -1, fire_elapsed_raw = -1, fire_hours = -1, fire_embers = -1, fire_ember_len = -1,
              fire_ember_timer = -1;
    Il2CppObject* t_pot = nullptr, *t_lamp = nullptr, *t_torch = nullptr, *t_flare = nullptr, *t_evolve = nullptr;
    const MethodInfo* pot_cooking = nullptr, *pot_fire_burning = nullptr, *pot_fire = nullptr;
    ptrdiff_t pot_state = -1, pot_elapsed = -1, pot_until_cooked = -1, pot_until_ruined = -1, pot_grace = -1;
    int32_t pot_ruined = -1;
    const MethodInfo* lamp_on = nullptr, *lamp_reduce = nullptr;
    const MethodInfo* torch_burning = nullptr, *torch_life = nullptr, *flare_burning = nullptr, *flare_life = nullptr;
    ptrdiff_t torch_elapsed = -1, flare_elapsed = -1;
    const MethodInfo* evolve_can = nullptr;
    ptrdiff_t evolve_spent = -1, evolve_days = -1;
    ptrdiff_t breakdown_post_load = -1;
    // curing boxes (SI: DecorationState.cs): their contents ride along in the .spawned record
    Il2CppObject* t_curing = nullptr, *t_curing_state = nullptr;
    const MethodInfo* curing_save = nullptr, *curing_load = nullptr;
    ptrdiff_t curing_state = -1, curing_guid = -1, curing_items = -1;
    ptrdiff_t pm_save_position = -1, pm_object_to_place = -1;
    FieldInfo* save_name = nullptr;
    const MethodInfo* save_pending = nullptr, *save_wait = nullptr;  // SaveGameSystem's async save
    std::string spawned_suffix = " (PLACED)", placement_root = "DesignPlaceables";
    struct StateKind {
        const char* tag;
        const char* cls;
        Il2CppObject* type = nullptr;
        const MethodInfo* save = nullptr, *load = nullptr;
    };
    StateKind kinds[6] = {{"BD", "BreakDown"},  {"HV", "Harvestable"}, {"SM", "SmashableItem"},
                          {"OC", "OpenClose"},  {"LK", "Lock"},        {"WS", "WaterSource"}};
} papi;

// A call that tells whether it threw (il2::invoke can't for a void method).
bool call(const MethodInfo* m, void* self, void** args = nullptr, Il2CppObject** result = nullptr) {
    if (result) *result = nullptr;
    if (!m) return false;
    Il2CppObject* ex = nullptr;
    Il2CppObject* r = il2::il2cpp_runtime_invoke(m, self, args, &ex);
    if (ex) {
        static int logged = 0;
        if (logged++ < 30)
            logx::warn("interiors: %s threw %s", il2::il2cpp_method_get_name(m), il2::class_name_of(ex).c_str());
        return false;
    }
    if (result) *result = r;
    return true;
}
std::string call_text(const MethodInfo* m, void* self, void** args = nullptr) {
    Il2CppObject* r = nullptr;
    return call(m, self, args, &r) ? text(r) : std::string();
}
float call_float(const MethodInfo* m, void* self, float fallback = 0) {
    Il2CppObject* r = nullptr;
    if (!call(m, self, nullptr, &r) || !r) return fallback;
    return *static_cast<const float*>(il2::il2cpp_object_unbox(r));
}
bool call_bool(const MethodInfo* m, void* self) {
    Il2CppObject* r = nullptr;
    if (!call(m, self, nullptr, &r) || !r) return false;
    return *static_cast<const bool*>(il2::il2cpp_object_unbox(r));
}

// --- text records ------------------------------------------------------------------------------------------------------

constexpr const char* kFileMagic = "TLDCOOP-SI 2 ";      // what we write (with the "#end <n>" footer)
constexpr const char* kFileMagicOld = "TLDCOOP-SI 1 ";   // read as before (no footer)
constexpr const char* kFileFooter = "#end ";
constexpr const char* kStableGuidMarker = "#stable-guids-1";  // SI: StableGuidMarker

std::string esc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\t': o += "\\t"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    return o;
}
std::string unesc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            o += s[i];
            continue;
        }
        const char n = s[++i];
        o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return o;
}
std::vector<std::string> split_fields(const std::string& line) {
    std::vector<std::string> out;
    size_t at = 0;
    for (;;) {
        const size_t tab = line.find('\t', at);
        out.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
        if (tab == std::string::npos) break;
        at = tab + 1;
    }
    return out;
}
std::string fnum(float v) {  // shortest round-trip, never the locale's comma
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return std::string(buf, r.ptr);
}
bool pnum(const std::string& s, float& v) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}
std::string f2(float v) {  // SI's {x:F2} for ids and keys (to_chars: never the locale's comma)
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 2);
    std::string o = r.ec == std::errc() ? std::string(buf, r.ptr) : std::string("0.00");
    if (o == "-0.00") return "0.00";
    return o;
}
std::string v3_fields(V3 v) { return fnum(v.x) + "\t" + fnum(v.y) + "\t" + fnum(v.z); }
std::string q_fields(Quat q) { return fnum(q.x) + "\t" + fnum(q.y) + "\t" + fnum(q.z) + "\t" + fnum(q.w); }
bool read_v3(const std::vector<std::string>& f, size_t at, V3& v) {
    return f.size() >= at + 3 && pnum(f[at], v.x) && pnum(f[at + 1], v.y) && pnum(f[at + 2], v.z);
}
bool read_q(const std::vector<std::string>& f, size_t at, Quat& q) {
    return f.size() >= at + 4 && pnum(f[at], q.x) && pnum(f[at + 1], q.y) && pnum(f[at + 2], q.z) && pnum(f[at + 3], q.w);
}

// "GEAR_Knife(Clone)" -> "GEAR_Knife" (SI: CleanGearName); and " (2)" off too for the prefab key (CleanGearPrefabName).
std::string clean_name(std::string n) {
    for (size_t at; (at = n.find("(Clone)")) != std::string::npos;) n.erase(at, 7);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    return n;
}
std::string prefab_name(const std::string& raw) {
    std::string n = clean_name(raw);
    const size_t open = n.rfind(" (");
    if (open != std::string::npos && open > 0 && !n.empty() && n.back() == ')') n = n.substr(0, open);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n;
}

// --- files (SI: SaveFiles, JsonWriteCache, KeepUnreadableFile) ---------------------------------------------------------

namespace fs = std::filesystem;
// OnSaveCompleted may come from the game's save thread; held across a whole save_rooms / commit_import (recursive: the
// writes inside take it too).
std::recursive_mutex g_files;

std::string file_safe(const std::string& id) {
    std::string name;
    for (char c : id) name += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? c : '_';
    return name;
}
fs::path dir_for(const std::string& save_id) {
    if (save_id.empty()) return {};
    return paths::instance_dir() / "worlds" / (file_safe(save_id) + ".interiors");
}
fs::path data_dir() { return dir_for(game::save_id()); }
fs::path pending_of(const fs::path& p) {
    fs::path q = p;
    q += ".pending";
    return q;
}
bool read_whole(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
// An old save's import (seamless_import.inc) builds a room's files in memory first: the readers see them there, and
// nothing reaches the disk until the room has been built from them completely (main thread only).
std::unordered_map<std::string, std::string> g_overlay;
// SI: readers prefer the staged file (changes since the last disk save are the newest truth this session).
bool read_preferred(const fs::path& p, std::string& out) {
    if (auto it = g_overlay.find(p.string()); it != g_overlay.end()) {
        out = it->second;
        return true;
    }
    std::lock_guard lock(g_files);
    return read_whole(pending_of(p), out) || read_whole(p, out);
}
bool exists_preferred(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && (g_overlay.count(p.string()) || fs::exists(pending_of(p), ec) || fs::exists(p, ec));
}
// The whole file on the disk, flushed, or false (nothing half-written is left under that name: the caller's ".tmp").
bool write_file_durably(const fs::path& p, const std::string& content) {
#ifdef _WIN32
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t at = 0;
    while (ok && at < content.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(content.size() - at, size_t{1} << 20));
        DWORD wrote = 0;
        ok = WriteFile(h, content.data() + at, chunk, &wrote, nullptr) && wrote == chunk;
        at += wrote;
    }
    ok = FlushFileBuffers(h) && ok;
    ok = CloseHandle(h) && ok;
    return ok;
#else
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    out.close();
    return !out.fail();
#endif
}
// Renames from over to (replacing it), written through to the disk; a few tries (a virus scanner or the search
// indexer can hold a file for a moment).
bool move_replace(const fs::path& from, const fs::path& to, std::string& err) {
    for (int attempt = 0; attempt < 4; ++attempt) {
#ifdef _WIN32
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        err = "error " + std::to_string(GetLastError());
        Sleep(25 * (attempt + 1));
#else
        std::error_code ec;
        fs::rename(from, to, ec);
        if (!ec) return true;
        err = ec.message();
#endif
    }
    return false;
}

// SI: JsonWriteCache.Write: "<file>.pending", skipped when the content is what's already there. False: it isn't.
bool write_staged(const fs::path& p, const std::string& content) {
    if (p.empty()) return false;
    std::lock_guard lock(g_files);
    std::string now;
    if ((read_whole(pending_of(p), now) || read_whole(p, now)) && now == content) return true;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = pending_of(p);
    tmp += ".tmp";
    if (!write_file_durably(tmp, content)) {
        logx::warn("interiors: couldn't write %s", tmp.string().c_str());
        fs::remove(tmp, ec);
        return false;
    }
    std::string err;
    if (!move_replace(tmp, pending_of(p), err)) {
        logx::warn("interiors: couldn't stage %s (%s)", p.filename().string().c_str(), err.c_str());
        return false;
    }
    return true;
}
// A commit that didn't go through leaves this note: the staged files then belong to a save that is on the disk, so
// a load mustn't throw them away (discard_dir); the next commit tries again and takes the note away.
constexpr const char* kCommitFailed = "commit.failed";
// SI: SaveFiles.Commit (every staged file of the folder). failed: how many couldn't be.
int commit_dir(const fs::path& dir, int* failed = nullptr) {
    std::lock_guard lock(g_files);
    std::error_code ec;
    if (failed) *failed = 0;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;
    std::vector<fs::path> staged;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".pending") staged.push_back(e.path());
    // an import's marker ("<id>.imported") last: it says the room's files are there (a commit cut short leaves files
    // without it, which old_save_gate can still recognise by their "<id>.imported-from")
    auto is_marker = [](const fs::path& p) { return p.stem().extension() == ".imported"; };
    std::stable_partition(staged.begin(), staged.end(), [&](const fs::path& p) { return !is_marker(p); });
    int n = 0, bad = 0;
    for (const fs::path& p : staged) {
        fs::path final_path = p;
        final_path.replace_extension();
        if (bad && is_marker(p)) {  // a file of the room didn't make it: the marker waits with it
            logx::warn("interiors: %s not committed: a room file before it couldn't be; it stays staged",
                       p.filename().string().c_str());
            ++bad;
            continue;
        }
        std::string err;
        if (!move_replace(p, final_path, err)) {
            logx::warn("interiors: couldn't commit %s (%s); it stays staged", p.filename().string().c_str(), err.c_str());
            ++bad;
            continue;
        }
        ++n;
    }
    const fs::path note = dir / kCommitFailed;
    if (bad) {
        write_file_durably(note, std::to_string(bad) + " staged file(s) of a save on the disk couldn't be committed\n");
        logx::warn("interiors: %d room file(s) couldn't be committed; kept staged (a load won't throw them away)", bad);
    } else if (fs::exists(note, ec)) {
        fs::remove(note, ec);
    }
    if (failed) *failed = bad;
    return n;
}
int discard_dir(const fs::path& dir) {
    std::lock_guard lock(g_files);
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;
    if (fs::exists(dir / kCommitFailed, ec)) {  // they belong to a save that reached the disk: committed instead
        int bad = 0;
        const int c = commit_dir(dir, &bad);
        logx::warn("interiors: %s: an earlier commit had failed; its staged files committed now (%d, %d still not) instead "
                   "of being thrown away",
                   dir.filename().string().c_str(), c, bad);
        if (bad) return 0;
    }
    int n = 0;
    std::vector<fs::path> gone;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".pending" || e.path().extension() == ".tmp") gone.push_back(e.path());
    for (const fs::path& p : gone)
        if (fs::remove(p, ec)) ++n;
    return n;
}
// Our folders of a save slot (by its name): "<slot>_<game id>_<seed>.interiors".
std::vector<fs::path> dirs_of_slot(const std::string& slot) {
    std::vector<fs::path> out;
    std::error_code ec;
    const fs::path worlds = paths::instance_dir() / "worlds";
    if (slot.empty() || !fs::is_directory(worlds, ec)) return out;
    const std::string want = file_safe(slot);
    auto digits = [](const std::string& s) {
        return !s.empty() && s.find_first_not_of("-0123456789") == std::string::npos;
    };
    for (const auto& e : fs::directory_iterator(worlds, ec)) {
        if (!e.is_directory(ec)) continue;
        std::string n = e.path().filename().string();
        const std::string suffix = ".interiors";
        if (n.size() <= suffix.size() || n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        n.resize(n.size() - suffix.size());
        const size_t b = n.rfind('_');
        if (b == std::string::npos || !digits(n.substr(b + 1))) continue;
        const size_t a = n.rfind('_', b - 1);
        if (a == std::string::npos || b == 0 || !digits(n.substr(a + 1, b - a - 1))) continue;
        if (n.substr(0, a) == want) out.push_back(e.path());
    }
    return out;
}
// SI: KeepUnreadableFile: a file we can't read is copied aside before anything could replace it.
void keep_unreadable(const fs::path& p) {
    std::lock_guard lock(g_files);
    std::error_code ec;
    const fs::path src = fs::exists(pending_of(p), ec) ? pending_of(p) : p;
    char stamp[32];
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
    fs::path copy = p;
    copy += std::string(".unreadable_") + stamp;
    fs::copy_file(src, copy, fs::copy_options::overwrite_existing, ec);
    logx::warn("interiors: %s couldn't be read; kept as %s and never written over this session", p.filename().string().c_str(),
               copy.filename().string().c_str());
}

struct Records {
    bool exists = false, ok = true;
    bool whole = false;  // written with the footer, which matched (known not cut short)
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> lines;  // as written (for orphans)
};
Records read_records(const fs::path& p, const char* kind, size_t min_fields) {
    Records r;
    std::string data;
    if (p.empty() || !read_preferred(p, data)) return r;
    r.exists = true;
    size_t at = 0;
    bool first = true, footed = false, footer_seen = false;
    while (at < data.size()) {
        size_t nl = data.find('\n', at);
        if (nl == std::string::npos) nl = data.size();
        std::string line = data.substr(at, nl - at);
        at = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            first = false;
            if (line == std::string(kFileMagic) + kind) {
                footed = true;
            } else if (line != std::string(kFileMagicOld) + kind) {
                r.ok = false;
                return r;
            }
            continue;
        }
        if (line.empty()) continue;
        if (footer_seen) {  // something after the footer
            r.ok = false;
            return r;
        }
        if (footed && line.rfind(kFileFooter, 0) == 0) {
            footer_seen = true;
            const std::string n = line.substr(std::strlen(kFileFooter));
            size_t rows = 0;
            const auto pr = std::from_chars(n.data(), n.data() + n.size(), rows);
            if (pr.ec != std::errc() || pr.ptr != n.data() + n.size() || rows != r.rows.size()) {
                r.ok = false;
                return r;
            }
            continue;
        }
        std::vector<std::string> f = split_fields(line);
        if (f.size() < min_fields) {
            r.ok = false;
            return r;
        }
        r.rows.push_back(std::move(f));
        r.lines.push_back(line);
    }
    if (first) r.ok = false;               // no header at all
    if (footed && !footer_seen) r.ok = false;  // cut short: what's missing can't be told
    r.whole = r.ok && footed && footer_seen;
    return r;
}
std::string file_text(const char* kind, const std::vector<std::string>& lines) {
    std::string out = std::string(kFileMagic) + kind + "\n";
    size_t rows = 0;
    for (const std::string& l : lines) {
        if (l.empty()) continue;  // (the reader skips empty lines: they aren't counted)
        out += l + "\n";
        ++rows;
    }
    out += kFileFooter + std::to_string(rows) + "\n";
    return out;
}
fs::path building_file(const fs::path& dir, const Instance& in, const char* kind) {
    return dir.empty() ? fs::path() : dir / (std::string(in.cfg->id) + "." + kind);
}

// --- the mod's own ids (SI: ModGuids.cs, StableGuids.cs, Spawning.cs GenerateDeterministicPDIDs) ---------------------------

// SI keeps its id of an object in a map keyed by the component's instance id (2.58 took ObjectGuid.m_Guid away); here
// keyed by the GameObject's.
std::unordered_map<int, std::string> g_mod_guids;
std::string mod_guid(Il2CppObject* go) {
    auto it = g_mod_guids.find(instance_id(go));
    return it == g_mod_guids.end() ? std::string() : it->second;
}
void set_mod_guid(Il2CppObject* go, const std::string& g) {
    const int id = instance_id(go);
    if (!id) return;
    if (g.empty())
        g_mod_guids.erase(id);
    else
        g_mod_guids[id] = g;
}

// MD5 (RFC 1321), for SI's GuidFromKey.
std::string md5_guid(const std::string& msg) {
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
        0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
        0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint32_t r[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                                   14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    std::string m = msg;
    const uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
    m += static_cast<char>(0x80);
    while (m.size() % 64 != 56) m += static_cast<char>(0);
    for (int i = 0; i < 8; ++i) m += static_cast<char>((bits >> (8 * i)) & 0xff);
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4])) |
                   static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 1])) << 8 |
                   static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 2])) << 16 |
                   static_cast<uint32_t>(static_cast<unsigned char>(m[off + i * 4 + 3])) << 24;
        uint32_t a = h0, b = h1, c = h2, d = h3;
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t f, g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            const uint32_t t = d;
            d = c;
            c = b;
            const uint32_t x = a + f + k[i] + w[g];
            b = b + ((x << r[i]) | (x >> (32 - r[i])));
            a = t;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
    }
    unsigned char out[16];
    const uint32_t hs[4] = {h0, h1, h2, h3};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<unsigned char>((hs[i] >> (8 * j)) & 0xff);
    char s[40];
    snprintf(s, sizeof(s), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", out[0], out[1], out[2], out[3],
             out[4], out[5], out[6], out[7], out[8], out[9], out[10], out[11], out[12], out[13], out[14], out[15]);
    return s;
}

std::string placeable_guid(Il2CppObject* p) { return papi.placeable_guid >= 0 ? text(get<Il2CppObject*>(p, papi.placeable_guid)) : ""; }

std::unordered_set<int> g_stable_checked;      // SI: s_StableGuidChecked (placeable instance ids)
std::unordered_set<std::string> g_stable_guids;  // SI: s_StableGuids

bool is_spawned_placeable(const Instance& in, Il2CppObject* p) {  // SI: IsSpawnedPlaceable
    const std::string g = placeable_guid(p);
    if (!g.empty() && in.spawned_guids.count(g)) return true;
    return name_of(go_of(p)).find(papi.spawned_suffix) != std::string::npos;
}

// SI: StabilizeRuntimeGuids: a template placeable whose guid was handed out at runtime gets one built from the building
// and its spot in the room, so the record of it still matches on the next load.
int stabilize_guids(Instance& in) {
    if (!papi.pdid_registered || !papi.pdid_get || !papi.pdid_replace || papi.placeable_guid < 0) return 0;
    std::unordered_set<std::string> used;
    int changed = 0;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* p) {
        if (!g_stable_checked.insert(instance_id(p)).second) return;
        const std::string g = placeable_guid(p);
        if (g.empty() || is_spawned_placeable(in, p)) return;
        Il2CppObject* og = component(go_of(p), api.t_object_guid);
        if (!og || !call_bool(papi.pdid_registered, og) || call_text(papi.pdid_get, og) != g) return;
        const V3 lp = to_local(in, position(tf_of(p)));
        const std::string key = std::string(in.cfg->id) + "|" + clean_name(name_of(go_of(p))) + "|" + f2(lp.x) + "|" +
                                f2(lp.y) + "|" + f2(lp.z);
        std::string guid = md5_guid(key);
        for (int n = 1; !used.insert(guid).second; ++n) guid = md5_guid(key + "#" + std::to_string(n));
        void* a[] = {og, str(guid.c_str())};
        if (!call(papi.pdid_replace, nullptr, a)) return;
        put_ref(p, papi.placeable_guid, str(guid.c_str()));
        g_stable_guids.insert(guid);
        ++changed;
    });
    if (changed) logx::info("interiors: %s: %d runtime placeable id(s) made stable", in.cfg->id, changed);
    return changed;
}

// SI: GenerateDeterministicPDIDs: every template item gets "<building>_<name>_<x>_<y>_<z>" (room-local, 2 decimals).
// Two identical items at the same spot (to the cm) get "#1", "#2"... as read_gear's ids do, so neither is taken for a
// copy of the other.
int deterministic_ids(Instance& in) {
    int n = 0;
    std::unordered_set<std::string> used;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        if (const std::string g = mod_guid(go_of(gi)); !g.empty()) used.insert(g);
    });
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        Il2CppObject* go = go_of(gi);
        if (!mod_guid(go).empty()) return;
        const V3 lp = to_local(in, position(tf_of(gi)));
        const std::string base =
            std::string(in.cfg->id) + "_" + clean_name(name_of(go)) + "_" + f2(lp.x) + "_" + f2(lp.y) + "_" + f2(lp.z);
        std::string id = base;
        for (int k = 1; !used.insert(id).second; ++k) id = base + "#" + std::to_string(k);
        set_mod_guid(go, id);
        ++n;
    });
    return n;
}

// --- who is what ------------------------------------------------------------------------------------------------------

Il2CppObject* root_of(Il2CppObject* t) { return alive(t) ? il2::invoke(api.tf_root, t) : nullptr; }
bool in_inventory(Il2CppObject* comp) {
    Il2CppObject* gi = alive(comp) && il2::class_name_of(comp) == "GearItem" ? comp : component(go_of(comp), api.t_gear);
    return gi && api.gear_in_inventory >= 0 && get<bool>(gi, api.gear_in_inventory);
}
// SI: IsPlayerOrInventory
bool player_or_inventory(Il2CppObject* t) {
    if (!alive(t)) return false;
    Il2CppObject* root = root_of(t);
    Il2CppObject* ptf = player_tf();
    if (root && ptf && root == root_of(ptf)) return true;
    if (root && name_of(root).find("WorldView") != std::string::npos) return true;
    const std::string n = name_of(t);
    if (n.find("WorldView") != std::string::npos || n.find("DesignPlaceable") != std::string::npos) return true;
    return in_inventory(t);
}
// SI: IsUnderPlacementRoot
bool under_placement_root(Il2CppObject* t) {
    for (int d = 0; t && d < 64; ++d, t = parent_of(t))
        if (name_of(t).find(papi.placement_root) != std::string::npos) return true;
    return false;
}
bool on_shell(Il2CppObject* t) { return on_any_shell(t); }  // SI: IsOnExteriorShell

// SI: BelongsToAnotherInstance
bool belongs_elsewhere(const Instance& in, Il2CppObject* t) {
    const V3 p = position(t);
    for (Instance& o : g_instances) {
        if (&o == &in) continue;
        Il2CppObject* m = master_of(o);
        if (!m) continue;
        if (child_of(t, go_tf(m))) return true;
        if (active_self(m) && in_volume(o, p, 0.5f)) return true;
    }
    return false;
}

// Another building of this region uses the same interior scene: the scene's baked ids (placeables, fires) are the
// same in both rooms, so an id alone doesn't say which room's it is.
bool scene_shared(const Instance& in) {
    for (const Instance& o : g_instances)
        if (&o != &in && std::strcmp(o.cfg->scene, in.cfg->scene) == 0) return true;
    return false;
}

// SI: TryGetWorldFilterBounds: the room's trigger box, axis-aligned in the world, a little larger.
bool filter_box(const Instance& in, Box& out, float margin = 1.35f) {
    if (!in.have_trigger || !master_of(in)) return false;
    const V3 c = to_world(in, in.trig_center);
    const V3 half = in.trig_size * (margin * 0.5f);
    out.min = c - half;
    out.max = c + half;
    return true;
}

constexpr float kRayEarlyReject = 12.0f;   // SI: RAY_EARLY_REJECT_PADDING
constexpr float kItemRayLift = 0.15f;      // SI: ITEM_RAY_ORIGIN_LIFT
constexpr float kLeakMatch = 0.12f;        // SI: LEAKED_GEAR_MATCH_RADIUS
constexpr float kDedupRadius = 0.05f;      // SI: DEDUP_RADIUS

// SI: CheckDirectionForInterior: the first real thing hit is part of this room.
bool check_direction(Instance& in, V3 origin, V3 dir, float range) {
    std::vector<Hit> hits = raycast_all(origin, dir, range, kQueryIgnore);
    if (hits.empty()) return false;
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.distance < b.distance; });
    Il2CppObject* mt = live(in.master_tf);
    Il2CppObject* shell = live(in.shell);
    Il2CppObject* shell_tf = shell ? go_tf(shell) : nullptr;
    Il2CppObject* ptf = player_tf();
    Il2CppObject* proot = ptf ? root_of(ptf) : nullptr;
    for (const Hit& h : hits) {
        Il2CppObject* ct = tf_of(h.collider);
        if (!ct) continue;
        if (proot && root_of(ct) == proot) continue;
        if (shell_tf && child_of(ct, shell_tf)) continue;
        if (in_parent(h.collider, api.t_gear) || in_parent(h.collider, api.t_placeable)) continue;
        return mt && child_of(ct, mt);
    }
    return false;
}
// SI: IsPositionInsideRaycastOnly: a floor and a roof of this room (it has to be open).
bool inside_by_rays(Instance& in, V3 pos, float lift) {
    if (!active_in_tree(master_of(in))) return false;
    if (in.have_trigger && !in_volume(in, pos, kRayEarlyReject)) return false;
    const V3 origin = pos + V3{0, lift, 0};
    return check_direction(in, origin, {0, 1, 0}, 30.0f) && check_direction(in, origin, {0, -1, 0}, 30.0f);
}

bool owns_twin_at(Instance& in, const std::string& cname, V3 at, bool active_only) {  // SI: RoomOwnsTwinAt
    bool found = false;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        if (found) return;
        if (active_only && !active_in_tree(go_of(gi))) return;
        if (dist(position(tf_of(gi)), at) > kLeakMatch) return;
        found = clean_name(name_of(go_of(gi))) == cname;
    });
    return found;
}

// SI: TryCarryStrayToParkedRoom: an object left loose where the room stood (at the building) is judged as if carried
// up into the parked room.
bool carry_point(const Instance& in, Il2CppObject* t, const Box& filter, V3& pos) {
    if (!in.hydrated || parent_of(t)) return false;
    const V3 carried = parked_point(in, pos);
    if (!filter.contains(carried)) return false;
    pos = carried;
    return true;
}

// --- adoption (SI: Spawning.cs AdoptStrayInteriorObjects / IsAdoptableStray) ------------------------------------------

template <typename F>
void each_loose(F&& f) {  // every GearItem the game tracks and every active Placeable
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (alive(gi)) f(gi);
    });
    if (api.find_objects && api.t_placeable) {
        void* a[] = {api.t_placeable};
        Il2CppObject* all = il2::invoke(api.find_objects, nullptr, a);
        for (size_t i = 0, n = il2::array_length(all); i < n; ++i)
            if (Il2CppObject* p = il2::array_object_at(all, i); alive(p)) f(p);
    }
}

void freeze_physics(Il2CppObject* gi) {  // SI: FreezeGearPhysics
    if (!gi || !api.gear_has_physics) return;
    bool physics = false;
    if (il2::invoke_value(api.gear_has_physics, gi, physics) && physics) il2::invoke(api.gear_disable_physics, gi);
}

int adopt_strays(Instance& in, const char* why) {
    Il2CppObject* m = master_of(in);
    if (!m || !in.hydrated || !in.have_trigger) return 0;
    Box filter;
    const bool has_filter = filter_box(in, filter);
    Il2CppObject* mt = go_tf(m);
    struct Cand {
        uintptr_t t;
        V3 at;
        bool carried;
    };
    std::vector<Cand> cands;
    std::unordered_set<Il2CppObject*> seen;
    // A kind whose file couldn't be read isn't written (P7): what is taken into the room is cut from the region's save,
    // so nothing of that kind is adopted (it stays the region's).
    const bool gear_ok = !in.gear_blocked, placeables_ok = !in.placeable_blocked && !in.spawn_blocked;
    each_loose([&](Il2CppObject* comp) {
        Il2CppObject* t = tf_of(comp);
        if (!t || !seen.insert(t).second) return;
        const bool is_gear = component(go_of(comp), api.t_gear) != nullptr;
        if (is_gear ? !gear_ok : !placeables_ok) return;
        if (is_gear) {  // there, untouched, since the room hydrated: purge_leaked's to judge (a leaked copy, or the region's)
            const auto snap = in.hydration_gear.find(instance_id(comp));
            if (snap != in.hydration_gear.end() && dist(snap->second, position(t)) < 0.02f) return;
        }
        V3 pos = position(t);
        bool carried = false;
        if (has_filter && !filter.contains(pos)) {
            if (!carry_point(in, t, filter, pos)) return;
            carried = true;
        }
        if (!in_volume(in, pos, -0.7f) || child_of(t, mt) || on_shell(t) || player_or_inventory(t) || belongs_elsewhere(in, t))
            return;
        cands.push_back({hold(t), pos, carried});
    });
    if (cands.empty()) return 0;
    const bool was_active = active_self(m);
    if (!was_active) set_active(m, true);
    int adopted = 0;
    for (Cand& c : cands) {
        Il2CppObject* t = live(c.t);
        if (t && inside_by_rays(in, c.at, kItemRayLift)) {
            const std::string nm = name_of(t);
            bool ok = true;
            if (c.carried) {  // SI: CarryStrayIntoParkedRoom: a twin already in the room means a leaked copy (the purge's)
                if (owns_twin_at(in, clean_name(nm), c.at, false))
                    ok = false;
                else {
                    set_position(t, c.at);
                    logx::info("interiors: %s: '%s' lay where the room stood, at the building; carried up into the room",
                               in.cfg->id, nm.c_str());
                }
            }
            if (ok) {
                bool keep_world = true;
                void* a[] = {mt, &keep_world};
                il2::invoke(api.tf_set_parent, t, a);
                freeze_physics(component(go_of(t), api.t_gear));
                if (Il2CppObject* p = component(go_of(t), api.t_placeable)) {  // SI: MarkAdoptedPlaceableAsSpawned
                    const std::string g = placeable_guid(p);
                    if (!g.empty() && in.spawned_guids.insert(g).second)
                        logx::info("interiors: %s: '%s' is saved as the player's own object from now on", in.cfg->id, nm.c_str());
                }
                if (adopted < 10) logx::info("interiors: %s: '%s' now belongs to the room (%s)", in.cfg->id, nm.c_str(), why);
                ++adopted;
            }
        }
        drop(c.t);
    }
    if (!was_active) set_active(m, false);
    if (adopted) logx::info("interiors: %s: %d object(s) left in the room taken in with it (%s)", in.cfg->id, adopted, why);
    return adopted;
}

// The region's items near the room as it begins to hydrate (the leaked copies that came with the region's save are
// among them; nothing the player drops afterwards is).
void snapshot_region_gear(Instance& in) {
    in.hydration_gear.clear();
    Il2CppObject* m = master_of(in);
    if (!m) return;
    Box filter;
    const bool has_filter = filter_box(in, filter, 1.6f);
    Il2CppObject* mt = go_tf(m);
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi)) return;
        Il2CppObject* t = tf_of(gi);
        const V3 pos = position(t);
        if (has_filter && !filter.contains(pos) && !filter.contains(parked_point(in, pos))) return;
        if (child_of(t, mt)) return;
        in.hydration_gear[instance_id(gi)] = pos;
    });
}

// SI: GearLeak.cs PurgeLeakedCloneGearDuplicates: a loose region copy of an item the room owns (same mod id, or the
// same item within 12 cm, judged at the parked spot when it lies at the building) is deleted.
int purge_leaked(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m || !in.hydrated) return 0;
    Box filter;
    const bool has_filter = filter_box(in, filter, 1.6f);
    Il2CppObject* mt = go_tf(m);
    struct Owned {
        std::string name;
        V3 pos;
    };
    std::vector<Owned> owned, owned_off;
    std::unordered_set<std::string> owned_guids;
    each_in(m, api.t_gear, [&](Il2CppObject* gi) {
        Il2CppObject* t = tf_of(gi);
        Il2CppObject* go = go_of(gi);
        if (inside_container(t, mt)) return;
        if (!active_self(go) || !active_up_to(t, mt)) {
            owned_off.push_back({clean_name(name_of(go)), position(t)});
            return;
        }
        if (const std::string g = mod_guid(go); !g.empty()) owned_guids.insert(g);
        owned.push_back({clean_name(name_of(go)), position(t)});
    });
    if (owned.empty() && owned_off.empty()) return 0;
    auto close_to = [](const std::vector<Owned>& list, const std::string& n, V3 p) {
        for (const Owned& o : list)
            if (o.name == n && dist(o.pos, p) <= kLeakMatch) return true;
        return false;
    };
    std::vector<Il2CppObject*> doomed;
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi)) return;
        Il2CppObject* t = tf_of(gi);
        Il2CppObject* go = go_of(gi);
        V3 pos = position(t);
        // only what was already there when the room hydrated, where it was then: a copy that came with the region's
        // save. Something dropped (or moved) since is the player's, however like the room's own it is (P1).
        const auto snap = in.hydration_gear.find(instance_id(gi));
        if (snap == in.hydration_gear.end() || dist(snap->second, pos) >= 0.02f) return;
        bool lifted = false;
        if (has_filter && !filter.contains(pos)) {
            const V3 up = parked_point(in, pos);
            if (!filter.contains(up)) return;
            pos = up;
            lifted = true;
        }
        if (child_of(t, mt) || !active_in_tree(go) || player_or_inventory(t) || under_placement_root(t) ||
            belongs_elsewhere(in, t))
            return;
        const std::string n = clean_name(name_of(go));
        const std::string g = mod_guid(go);
        const bool copy = (!g.empty() && owned_guids.count(g)) || close_to(owned, n, pos) || (lifted && close_to(owned_off, n, pos));
        if (copy) doomed.push_back(go);
    });
    for (Il2CppObject* go : doomed) {
        logx::info("interiors: %s: a leaked copy of the room's '%s' deleted", in.cfg->id, name_of(go).c_str());
        destroy(go);
    }
    return static_cast<int>(doomed.size());
}

// --- loot: the decision (SI: Spawning.cs ProcessSpawnsAndDeduplication, SpatialDeduplication, DestroyDuplicateGuidGear)

// SI: SpatialDeduplication: a room item with a loose twin outside the room (same name within 5 cm) goes.
int spatial_dedup(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return 0;
    Il2CppObject* mt = go_tf(m);
    std::vector<std::pair<std::string, V3>> inside;
    std::vector<Il2CppObject*> inside_go;
    Box box;
    each_in(m, api.t_gear, [&](Il2CppObject* gi) {
        const V3 p = position(tf_of(gi));
        inside.push_back({clean_name(name_of(go_of(gi))), p});
        inside_go.push_back(go_of(gi));
        box.add(p);
    });
    if (inside.empty()) return 0;
    box.min = box.min - V3{1, 1, 1};
    box.max = box.max + V3{1, 1, 1};
    std::vector<std::pair<std::string, V3>> outside;
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi)) return;
        Il2CppObject* t = tf_of(gi);
        const V3 p = position(t);
        if (!box.contains(p) || child_of(t, mt)) return;
        outside.push_back({clean_name(name_of(go_of(gi))), p});
    });
    int n = 0;
    for (size_t i = 0; i < inside.size(); ++i)
        for (const auto& o : outside)
            if (o.first == inside[i].first && dist(o.second, inside[i].second) <= kDedupRadius) {
                destroy(inside_go[i]);
                ++n;
                break;
            }
    if (n) logx::info("interiors: %s: %d room item(s) already lying there as region items removed", in.cfg->id, n);
    return n;
}

// SI: DestroyDuplicateGuidGear: a room item whose mod id is carried by an item outside the room too goes.
int duplicate_guid_gear(Instance& in) {
    Il2CppObject* m = master_of(in);
    if (!m) return 0;
    Il2CppObject* mt = go_tf(m);
    std::unordered_set<std::string> outside;
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi) || child_of(tf_of(gi), mt)) return;
        if (const std::string g = mod_guid(go_of(gi)); !g.empty()) outside.insert(g);
    });
    if (outside.empty()) return 0;
    int n = 0;
    each_in(m, api.t_gear, [&](Il2CppObject* gi) {
        const std::string g = mod_guid(go_of(gi));
        if (!g.empty() && outside.count(g)) {
            destroy(go_of(gi));
            ++n;
        }
    });
    if (n) logx::info("interiors: %s: %d room item(s) whose id is already outside the room removed", in.cfg->id, n);
    return n;
}

// "Loot rolled for this save" (SI: the "<id>Gen_<save>" PlayerPrefs flag), in our folder: loot.rolled.
std::unordered_set<std::string> g_rolled_ids;
bool g_rolled_read = false;
void read_rolled(const fs::path& dir) {
    g_rolled_ids.clear();
    g_rolled_read = true;
    const Records r = read_records(dir.empty() ? fs::path() : dir / "loot.rolled", "rolled", 1);
    for (const auto& row : r.rows) g_rolled_ids.insert(row[0]);
}

// SI: HasPersistedLootRecord (a gear file, even empty: the player stripped the place bare).
bool has_saved_contents(const Instance& in) { return exists_preferred(building_file(in.dir, in, "gear")); }

// SI: ProcessSpawnsAndDeduplication + the RSO clean-up of Run. True: the room's contents come from our files (no roll).
bool decide_loot(Instance& in) {
    if (!papi.ok || in.dir.empty()) {
        deterministic_ids(in);
        return false;
    }
    if (!g_rolled_read) read_rolled(in.dir);
    const bool saved = has_saved_contents(in);
    const bool flag = g_rolled_ids.count(in.cfg->id) > 0 || in.legacy == kLegacyHydrating;  // (an old save's import)
    in.from_files = flag && saved;
    if (flag && !saved)
        logx::info("interiors: %s: rolled before but no saved contents; the template's loot is kept and rolled again", in.cfg->id);
    if (in.from_files) {
        int rogue = 0;  // SI: Rogue-Cleanup: a template item without our id is a copy the file replaces
        each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
            if (mod_guid(go_of(gi)).empty()) {
                destroy(go_of(gi));
                ++rogue;
            }
        });
        int rso = 0;  // SI: RSO-CLEANUP before activation (their Start must not run)
        each_in(master_of(in), api.t_rso, [&](Il2CppObject* r) {
            destroy_now(r);
            ++rso;
        });
        in.rolled = true;
        logx::info("interiors: %s: contents come from this save's files: %d template item(s) and %d random spawner(s) removed",
                   in.cfg->id, rogue, rso);
    } else {
        const int ids = deterministic_ids(in);
        logx::info("interiors: %s: no saved contents yet: %d template item(s) given the building's own ids", in.cfg->id, ids);
    }
    spatial_dedup(in);
    duplicate_guid_gear(in);
    return in.from_files;
}

// --- carried decorations (SI: CarriedDecorations.cs) ------------------------------------------------------------------

// PlaceableManager.s_Placeables entries: key (guid), handle, state.
template <typename F>
bool each_placement(F&& f) {
    Il2CppObject* dict = static_object(api.placeables);
    if (!dict) return false;
    Il2CppClass* k = il2::il2cpp_object_get_class(dict);
    const ptrdiff_t entries_off = il2::field_offset_quiet(k, "_entries"), count_off = il2::field_offset_quiet(k, "_count");
    if (entries_off < 0 || count_off < 0) return false;
    Il2CppObject* entries = get<Il2CppObject*>(dict, entries_off);
    const int32_t count = get<int32_t>(dict, count_off);
    const size_t len = il2::array_length(entries);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < len; ++i) {
        const char* e = reinterpret_cast<const char*>(entries) + 0x20 + i * 32;
        int32_t next = 0, state = 0;
        Il2CppObject* key = nullptr, *handle = nullptr;
        std::memcpy(&next, e + 4, 4);
        std::memcpy(&key, e + 8, 8);
        std::memcpy(&handle, e + 16, 8);
        std::memcpy(&state, e + 24, 4);
        if (next < -1 || !key) continue;
        f(text(key), handle, state);
    }
    return true;
}
std::unordered_set<std::string> g_carried;           // guids in the backpack (state OnPlayer)
std::unordered_set<std::string> g_carried_unhandled;  // ... whose entry has no live object
std::unordered_set<Il2CppObject*> g_carried_handles;  // ... the objects those entries hold
double g_carried_at = -1e18;
void read_carried() {
    const double now = now_ms();
    if (now - g_carried_at <= 500) return;
    g_carried_at = now;
    g_carried.clear();
    g_carried_unhandled.clear();
    g_carried_handles.clear();
    each_placement([&](const std::string& key, Il2CppObject* handle, int32_t state) {
        if (state != 2) return;
        g_carried.insert(key);
        if (alive(handle))
            g_carried_handles.insert(handle);
        else
            g_carried_unhandled.insert(key);
    });
}
bool is_carried(const std::string& guid) {  // SI: IsCarriedDecoration (state OnPlayer)
    if (guid.empty()) return false;
    read_carried();
    return g_carried.count(guid) > 0;
}
// This very object (P9: buildings sharing an interior scene share its baked guids; another house's decoration in the
// backpack isn't this one). By the guid alone only when the entry holds no object and no other room shares the guids.
bool is_carried(const Instance& in, Il2CppObject* p) {
    Il2CppObject* ptf = player_tf();
    if (ptf && root_of(tf_of(p)) == root_of(ptf)) return true;
    read_carried();
    if (g_carried_handles.count(p)) return true;
    const std::string g = placeable_guid(p);
    return !g.empty() && g_carried_unhandled.count(g) && !scene_shared(in);
}

// SI: TickCarriedDecorationGuard: a decoration the game says is in the backpack but which stands active in the world
// (seen twice, a second apart, and not the one being placed) is switched off.
std::unordered_set<int> g_carried_wrong_prev;
double g_next_carried_check = 0;
int g_carried_hidden = 0;
void tick_carried_guard() {
    const double now = now_ms();
    if (now < g_next_carried_check) return;
    g_next_carried_check = now + 1000;
    if (papi.placeables_loading) {
        bool loading = false;
        il2::il2cpp_field_static_get_value(papi.placeables_loading, &loading);
        if (loading) return;
    }
    Il2CppObject* placing = nullptr;
    if (Il2CppObject* pm = player_manager(); pm && papi.pm_object_to_place >= 0) placing = get<Il2CppObject*>(pm, papi.pm_object_to_place);
    std::unordered_set<int> wrong;
    std::vector<Il2CppObject*> hide;
    each_placement([&](const std::string&, Il2CppObject* handle, int32_t state) {
        if (state != 2 || !alive(handle)) return;
        Il2CppObject* go = go_of(handle);
        if (!alive(go) || !active_in_tree(go) || (alive(placing) && go == placing)) return;
        const int id = instance_id(go);
        wrong.insert(id);
        if (g_carried_wrong_prev.count(id)) hide.push_back(go);
    });
    g_carried_wrong_prev = std::move(wrong);
    for (Il2CppObject* go : hide) {
        if (g_carried_hidden++ < 10)
            logx::info("interiors: '%s' is in the backpack but stood in the world; hidden", name_of(go).c_str());
        set_active(go, false);
    }
}

// --- the game's save restored items with physics on (SI: GearLeak.cs FreezeRestoredGearPhysics) ------------------------

void freeze_restored_gear() {
    int n = 0;
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi) || (api.gear_in_inventory >= 0 && get<bool>(gi, api.gear_in_inventory))) return;
        bool physics = false;
        if (api.gear_has_physics && il2::invoke_value(api.gear_has_physics, gi, physics) && physics) {
            il2::invoke(api.gear_disable_physics, gi);
            ++n;
        }
    });
    if (n) logx::info("interiors: %d item(s) the game's save restored with live physics frozen in place", n);
}

// --- saving one building (SI: SaveData.cs) ----------------------------------------------------------------------------

// SI: CanPersistContent: nothing is written for a building whose contents weren't restored this session.
bool can_persist(const Instance& in) { return in.hydrated && in.stage == Stage::Ready && master_of(in) && !in.dir.empty(); }

V3 local_scale(Il2CppObject* t) {
    V3 s{1, 1, 1};
    if (api.tf_local_scale && alive(t)) il2::invoke_value(api.tf_local_scale, t, s);
    return s;
}
void set_local_scale(Il2CppObject* t, V3 s) {
    if (!alive(t) || (s.x * s.x + s.y * s.y + s.z * s.z) < 1e-8f) return;
    void* a[] = {&s};
    il2::invoke(api.tf_set_local_scale, t, a);
}
void set_rotation(Il2CppObject* t, Quat q) {
    if (!alive(t)) return;
    void* a[] = {&q};
    il2::invoke(api.tf_set_rotation, t, a);
}

// SI: BestPlaceablePerGuid / PlaceableRank: one placeable per guid, the standing one.
std::unordered_map<std::string, Il2CppObject*> best_per_guid(const std::vector<Il2CppObject*>& all, const Instance& in) {
    std::unordered_map<std::string, Il2CppObject*> best;
    auto rank = [&](Il2CppObject* p) {
        return (active_self(go_of(p)) ? 2 : 0) + (name_of(go_of(p)).find(papi.spawned_suffix) != std::string::npos ? 1 : 0);
    };
    for (Il2CppObject* p : all) {
        const std::string g = placeable_guid(p);
        if (g.empty()) continue;
        auto it = best.find(g);
        if (it == best.end() || rank(p) > rank(it->second)) best[g] = p;
    }
    (void)in;
    return best;
}

Il2CppObject* deserialize_object(const std::string& s, Il2CppObject* type);

// SI: SerializeDecorationState: a curing box's contents ("" for anything else).
std::string curing_text(Il2CppObject* go) {
    Il2CppObject* box = papi.t_curing && papi.curing_save ? component(go, papi.t_curing) : nullptr;
    if (!box) return {};
    Il2CppObject* state = nullptr;
    if (!call(papi.curing_save, box, nullptr, &state) || !state) return {};
    void* a[] = {state};
    return call_text(papi.utils_serialize, nullptr, a);
}
int curing_count(Il2CppObject* state) {  // the items a CuringStorageState holds
    return state && papi.curing_items >= 0 ? list_count(get<Il2CppObject*>(state, papi.curing_items)) : 0;
}
// SI: ApplyCuringState: -1 not a curing box / nothing to apply, 0 failed, else 1 + the items put in.
int apply_curing(Il2CppObject* go, const std::string& serialized) {
    if (serialized.empty()) return -1;
    Il2CppObject* box = papi.t_curing ? component(go, papi.t_curing) : nullptr;
    if (!box || !papi.curing_load || !papi.t_curing_state) return box ? 0 : -1;
    Il2CppObject* state = deserialize_object(serialized, papi.t_curing_state);
    if (!state) return 0;
    if (Il2CppObject* current = papi.curing_state >= 0 ? get<Il2CppObject*>(box, papi.curing_state) : nullptr)  // its own id wins
        if (Il2CppObject* g = get<Il2CppObject*>(current, papi.curing_guid); g && !text(g).empty())
            put_ref(state, papi.curing_guid, g);
    const int items = curing_count(state);
    void* a[] = {state};
    if (!call(papi.curing_load, box, a)) return 0;
    return 1 + items;
}

// SI: SavePlaceablePositions (+ SaveSpawnedPlaceables)
bool save_placeables(Instance& in) {
    if (in.placeable_blocked && in.spawn_blocked) return true;
    std::vector<Il2CppObject*> all;
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* p) { all.push_back(p); });
    const auto best = best_per_guid(all, in);
    std::vector<std::string> lines, spawned;
    std::unordered_set<std::string> seen_spawn;
    for (Il2CppObject* p : all) {
        const std::string g = placeable_guid(p);
        if (g.empty() || best.at(g) != p) continue;
        Il2CppObject* t = tf_of(p);
        Il2CppObject* go = go_of(p);
        const V3 lp = to_local(in, position(t));
        const Quat lr = mul(conj(in.master_rot), rotation(t));
        lines.push_back(esc(g) + "\t" + v3_fields(lp) + "\t" + q_fields(lr) + "\t" + v3_fields(local_scale(t)) + "\t" +
                        (active_self(go) ? "1" : "0"));
        // SI: SaveSpawnedPlaceables: the player's own furniture is written whole (it isn't in the template).
        if (!is_spawned_placeable(in, p) || player_or_inventory(t)) continue;
        const std::string identity = clean_name(name_of(go)) + "_" + f2(lp.x) + "_" + f2(lp.y) + "_" + f2(lp.z);
        if (!seen_spawn.insert(identity).second) continue;
        Il2CppObject* data = nullptr;
        if (!call(papi.placeable_save, p, nullptr, &data) || !data) continue;
        put<V3>(data, papi.data_position, to_local(in, get<V3>(data, papi.data_position)));
        put<Quat>(data, papi.data_rotation, mul(conj(in.master_rot), get<Quat>(data, papi.data_rotation)));
        void* sa[] = {data};
        const std::string s = call_text(papi.utils_serialize, nullptr, sa);
        if (s.empty()) continue;
        auto kept = in.curing_kept.find(g);  // contents we couldn't put back: the record's, not the empty box's
        const std::string cs = kept != in.curing_kept.end() ? kept->second : curing_text(go);  // a curing box only
        spawned.push_back(esc(g) + "\t" + esc(s) + (cs.empty() ? "" : "\t" + esc(cs)));
    }
    lines.push_back(esc(kStableGuidMarker) + "\t0\t0\t0\t0\t0\t0\t1\t1\t1\t1\t1");  // SI: StableGuidMarker
    bool ok = true;
    if (!in.placeable_blocked)  // (P7: an unreadable positions file doesn't keep the player's own furniture from its file)
        ok = write_staged(building_file(in.dir, in, "placeables"), file_text("placeables", lines)) && ok;
    if (!in.spawn_blocked) {
        for (const std::string& o : in.orphan_spawned) spawned.push_back(o);
        ok = write_staged(building_file(in.dir, in, "spawned"), file_text("spawned", spawned)) && ok;
    }
    return ok;
}

// SI: ShouldPersistGear + SaveInactiveSceneGearItems (the room's own loose items; strays were adopted first)
bool save_gear(Instance& in) {
    if (in.gear_blocked) return true;
    Il2CppObject* mt = live(in.master_tf);
    std::vector<std::string> lines;
    int skipped = 0;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        Il2CppObject* t = tf_of(gi);
        Il2CppObject* go = go_of(gi);
        if (!active_self(go) || inside_container(t, mt) || !active_up_to(t, mt)) {
            ++skipped;
            return;
        }
        const std::string s = call_text(papi.gear_to_string, gi);
        lines.push_back(esc(name_of(go)) + "\t" + esc(mod_guid(go)) + "\t" + v3_fields(to_local(in, position(t))) + "\t" +
                        q_fields(mul(conj(in.master_rot), rotation(t))) + "\t" + v3_fields(local_scale(t)) + "\t1\t" + esc(s));
        // fields: 0 name, 1 our id, 2-4 position, 5-8 rotation, 9-11 scale, 12 active, 13 the game's own text
    });
    for (const std::string& o : in.orphan_gear) lines.push_back(o);
    return write_staged(building_file(in.dir, in, "gear"), file_text("gear", lines));
}

// SI: SaveContainerData
bool save_containers(Instance& in) {
    if (in.container_blocked) return true;
    std::vector<std::string> lines;
    std::unordered_map<std::string, int> counter;
    each_in(master_of(in), api.t_container, [&](Il2CppObject* c) {
        const std::string n = name_of(go_of(c));
        const int idx = counter[n]++;
        const std::string data = call_text(papi.container_save, c);
        if (data.empty()) return;
        lines.push_back(esc(n + "###" + std::to_string(idx)) + "\t" + esc(clean_name(n)) + "\t" +
                        v3_fields(to_local(in, position(tf_of(c)))) + "\t" + esc(data));
    });
    for (const std::string& o : in.orphan_containers) lines.push_back(o);
    return write_staged(building_file(in.dir, in, "containers"), file_text("containers", lines));
}

// SI: InteractiveState.cs BuildStateKey (by the guid in the serialized text, else by name and room-local spot).
std::string guid_in(const std::string& s) {
    const size_t i = s.find("\"m_Guid\"");
    if (i == std::string::npos) return {};
    const size_t colon = s.find(':', i + 8);
    const size_t q1 = colon == std::string::npos ? colon : s.find('"', colon);
    const size_t q2 = q1 == std::string::npos ? q1 : s.find('"', q1 + 1);
    return q2 == std::string::npos ? std::string() : s.substr(q1 + 1, q2 - q1 - 1);
}
std::string state_key(const char* tag, const Instance& in, Il2CppObject* c, const std::string& serialized) {
    const std::string g = guid_in(serialized);
    if (!g.empty()) return std::string(tag) + "|g:" + g;
    const V3 lp = to_local(in, position(tf_of(c)));
    return std::string(tag) + "|p:" + name_of(go_of(c)) + "|" + f2(lp.x) + "," + f2(lp.y) + "," + f2(lp.z);
}
// SI: GetFireKey: the fire's ObjectGuid (its own or a parent's): our id, else the game's PDID.
std::string fire_key(Il2CppObject* fire) {
    Il2CppObject* og = in_parent(fire, api.t_object_guid);
    if (!og) return {};
    if (const std::string g = mod_guid(go_of(og)); !g.empty()) return g;
    return call_text(papi.pdid_get, og);
}
bool fire_alive(Il2CppObject* f) {  // SI: IsFireStillAlive
    return alive(f) && call_bool(papi.fire_burning, f) && call_float(papi.fire_left, f) > 0;
}
// SI: RecordSaysBurning
bool record_burning(const std::string& payload) {
    const std::string m = "\"m_FireStateProxy\":\"";
    const size_t i = payload.find(m);
    if (i == std::string::npos) return false;
    const size_t e = payload.find('"', i + m.size());
    const std::string st = payload.substr(i + m.size(), e == std::string::npos ? std::string::npos : e - i - m.size());
    return !st.empty() && st != "Off" && st != "Blownout";
}

// --- A11: the safehouse "clear junk" in rooms (SI: Junk.cs + SafehouseFixes.cs #22/#37/#38) ----------------------------
// TLD keeps ONE "junk cleared" flag (JunkManager.m_ClearedJunk), saved with the scene, and ClearJunk switches off every
// JunkTag of every loaded scene. With the rooms in the region scene: while the player customizes inside a room the flag
// holds that room's state (the region's own value comes back when they stop, and around every scene save); around
// ClearJunk, junk the call had no business touching is switched back on (only the room being customized is cleared, a
// region's own clear takes no room's); each room keeps its state in its .state file ("JK|cleared").
struct JunkApi {
    const MethodInfo *get_jm = nullptr, *get_sm = nullptr, *is_customizing = nullptr, *restore_busy = nullptr,
                     *scene_restore_busy = nullptr, *find_all = nullptr;
    ptrdiff_t cleared = -1;
    Il2CppObject* t_junk = nullptr;
    bool ok = false;
} japi;
Instance* g_junk_owner = nullptr;  // SI: s_JunkFlagOwner: the room whose state m_ClearedJunk holds now
bool g_region_junk = false;        // SI: s_RegionJunkCleared
double g_junk_recheck = 0;
struct JunkBefore {
    uintptr_t go = 0;
    Instance* in = nullptr;
};
std::vector<JunkBefore> g_junk_before;
Instance* instance_of_tf(Il2CppObject* t);
template <typename Fn>
bool hook1b(const char* label, const MethodInfo* m, Fn detour, Fn* original);

Il2CppObject* junk_manager() { return japi.ok ? il2::invoke(japi.get_jm, nullptr) : nullptr; }
bool plain_junk(Il2CppObject* tag) {  // SI: IsPlainJunk
    Il2CppObject* go = go_of(tag);
    if (!go || component(go, api.t_placeable)) return false;
    return in_parent(tag, api.t_gear) == nullptr;
}
int clear_junk_in(Instance& in) {  // SI: ClearJunkInInstance
    int n = 0;
    if (japi.ok && master_of(in))
        each_in(master_of(in), japi.t_junk, [&](Il2CppObject* tag) {
            Il2CppObject* go = go_of(tag);
            if (!plain_junk(tag) || !active_self(go)) return;
            set_active(go, false);
            in.junk_off.push_back(hold(go));
            ++n;
        });
    in.junk_cleared = true;
    return n;
}
int restore_junk_in(Instance& in) {  // SI: RestoreJunkInInstance
    int n = 0;
    for (uintptr_t& h : in.junk_off) {
        if (Il2CppObject* go = live(h); go && !active_self(go)) {
            set_active(go, true);
            ++n;
        }
        drop(h);
    }
    in.junk_off.clear();
    in.junk_cleared = false;
    return n;
}
void begin_junk_view(Instance& in) {
    if (g_junk_owner == &in) return;
    end_junk_view();
    Il2CppObject* jm = junk_manager();
    if (!jm) return;
    g_region_junk = get<bool>(jm, japi.cleared);
    g_junk_owner = &in;
    put<bool>(jm, japi.cleared, in.junk_cleared);
    logx::info("interiors: %s: customizing; the junk flag is the room's (%s; the region's %s)", in.cfg->id,
               in.junk_cleared ? "cleared" : "not cleared", g_region_junk ? "cleared" : "not cleared");
}
void end_junk_view() {
    if (!g_junk_owner) return;
    if (Il2CppObject* jm = junk_manager()) put<bool>(jm, japi.cleared, g_region_junk);
    g_junk_owner = nullptr;
}
bool customizing() {
    if (!japi.ok) return false;
    Il2CppObject* sm = il2::invoke(japi.get_sm, nullptr);
    bool on = false;
    return sm && il2::invoke_value(japi.is_customizing, sm, on) && on;
}
Instance* junk_room_now() { return g_inside && in_room(g_inside) && g_inside->hydrated ? g_inside : nullptr; }
void tick_junk_view() {  // SI: TickJunkFlagView
    if (!japi.ok) return;
    if (!g_junk_owner && !g_inside) return;  // (nearly always: nothing to do)
    if (!customizing()) {
        end_junk_view();
        return;
    }
    if (now_ms() < g_junk_recheck) return;
    g_junk_recheck = now_ms() + 500;
    Instance* inside = junk_room_now();
    if (inside == g_junk_owner) return;
    end_junk_view();
    if (inside) begin_junk_view(*inside);
}
VoidFn g_start_customizing = nullptr;
void start_customizing_detour(Il2CppObject* self, const MethodInfo* method) {  // SI #37: the "R" prompt is decided in it
    if (engaged())
        if (Instance* in = junk_room_now()) {
            begin_junk_view(*in);
            g_junk_recheck = now_ms() + 500;
        }
    g_start_customizing(self, method);
}
VoidFn g_clear_junk = nullptr;
void clear_junk_detour(Il2CppObject* self, const MethodInfo* method) {  // SI #38
    if (!engaged() || g_instances.empty() || !japi.find_all) return g_clear_junk(self, method);
    g_junk_before.clear();
    {
        bool inactive = true;
        void* a[] = {japi.t_junk, &inactive};
        Il2CppObject* all = il2::invoke(japi.find_all, nullptr, a);
        for (size_t i = 0, n = all ? il2::array_length(all) : 0; i < n; ++i) {
            Il2CppObject* tag = il2::array_object_at(all, i);
            Il2CppObject* go = go_of(tag);
            if (!go || !active_self(go)) continue;
            g_junk_before.push_back({hold(go), is_clone_object(tag) ? instance_of_tf(tf_of(tag)) : nullptr});
        }
    }
    g_clear_junk(self, method);
    Instance* target = g_junk_owner;
    bool restoring = false;
    if ((japi.restore_busy && il2::invoke_value(japi.restore_busy, nullptr, restoring) && restoring) ||
        (japi.scene_restore_busy && il2::invoke_value(japi.scene_restore_busy, nullptr, restoring) && restoring))
        target = nullptr;  // LoadSceneData putting the region's saved flag back: the region's clear
    int here = 0, back = 0;
    for (JunkBefore& b : g_junk_before) {
        Il2CppObject* go = live(b.go);
        if (go && !active_self(go)) {
            if (!b.in) {
                if (target) {
                    set_active(go, true);  // the region's junk: not a room's clear
                    ++back;
                }
            } else if (b.in == target) {
                target->junk_off.push_back(b.go);
                b.go = 0;
                ++here;
            } else {
                set_active(go, true);  // another room's (or any room's, for the region's clear)
                ++back;
            }
        }
        drop(b.go);
    }
    g_junk_before.clear();
    if (target) {
        here += clear_junk_in(*target);
        logx::info("interiors: %s: junk cleared (%d object(s)); %d switched back on elsewhere", target->cfg->id, here, back);
    } else if (back) {
        logx::info("interiors: the game's junk clear reached %d object(s) of the rooms; switched back on (each room keeps its own)", back);
    }
}
void resolve_junk() {
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* jm = il2::find_class(kGame, "", "JunkManager");
    Il2CppClass* sm = il2::find_class(kGame, "", "SafehouseManager");
    Il2CppClass* saves = il2::find_class(kGame, "", "SaveGameSystem");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    japi.get_jm = gm ? il2::find_method(gm, "GetJunkManager", 0) : nullptr;
    japi.get_sm = gm ? il2::find_method(gm, "GetSafehouseManager", 0) : nullptr;
    japi.is_customizing = sm ? il2::find_method(sm, "IsCustomizing", 0) : nullptr;
    japi.restore_busy = saves ? il2::find_method(saves, "IsRestoreInProgress", 0) : nullptr;
    japi.scene_restore_busy = saves ? il2::find_method(saves, "IsSceneRestoreInProgress", 0) : nullptr;
    japi.find_all = object ? il2::find_method_params(object, "FindObjectsOfType", {"System.Type", "System.Boolean"}) : nullptr;
    japi.cleared = jm ? il2::field_offset(jm, "m_ClearedJunk") : -1;
    Il2CppClass* junk_tag = il2::find_class(kGame, "", "JunkTag");
    japi.t_junk = junk_tag ? il2::type_object(junk_tag) : nullptr;
    japi.ok = japi.get_jm && japi.get_sm && japi.is_customizing && japi.cleared >= 0 && japi.t_junk;
    if (!japi.ok) {
        logx::info("interiors: JunkManager/SafehouseManager API missing: rooms' junk isn't cleared on its own");
        return;
    }
    if (sm) hook1b("SafehouseManager.StartCustomizing", il2::find_method(sm, "StartCustomizing", 0), &start_customizing_detour,
                   &g_start_customizing);
    if (jm) hook1b("JunkManager.ClearJunk", il2::find_method(jm, "ClearJunk", 0), &clear_junk_detour, &g_clear_junk);
}

// SI: SaveInteractiveState (+ the room's fires: the port keeps them in the building's file as well as in the region's
// save, because a room is built only when the player comes near: a save made before that would lose them otherwise).
bool save_state(Instance& in) {
    if (in.state_blocked) return true;
    std::vector<std::string> lines;
    std::unordered_set<std::string> used;
    for (auto& k : papi.kinds) {
        if (!k.type || !k.save) continue;
        each_in(master_of(in), k.type, [&](Il2CppObject* c) {
            const std::string s = call_text(k.save, c);
            if (s.empty()) return;
            const std::string key = state_key(k.tag, in, c, s);
            if (!used.insert(key).second) return;  // SI: ambiguous identity, skipped
            lines.push_back(esc(key) + "\t" + esc(s));
        });
    }
    each_in(master_of(in), api.t_fire, [&](Il2CppObject* f) {
        const std::string key = fire_key(f);
        if (key.empty()) return;
        const std::string s = call_text(papi.fire_save, f);
        if (s.empty() || !used.insert("FI|" + key).second) return;
        lines.push_back(esc("FI|" + key) + "\t" + esc(s));
    });
    if (in.junk_cleared && used.insert("JK|cleared").second) lines.push_back(esc("JK|cleared") + "\t" + esc("1"));  // A11
    used.insert("JK|cleared");  // (an older "JK|cleared" among the orphans isn't written back)
    for (const std::string& o : in.orphan_state)
        if (const auto f = split_fields(o); !f.empty() && !used.count(unesc(f[0]))) lines.push_back(o);
    return write_staged(building_file(in.dir, in, "state"), file_text("state", lines));
}

// --- restoring one building (SI: Hydration.cs HydrateRoutine and the restores it runs) -------------------------------

// Utils.DeserializeObject<T> is a shared generic (no MethodInfo to call): the same branch it takes, by hand:
// SerializationUtils.s_UseJsonNet ? JsonConvert.DeserializeObject(text, type, m_SerializerSettings)
//                                 : FastJson.Deserialize(text, type)   (read from 2.58's SerializeObject).
// The other one is tried when the game's own choice gives nothing (a call missing in this build, or a text the other
// serializer wrote).
Il2CppObject* deserialize_object(const std::string& s, Il2CppObject* type) {
    if (s.empty() || !type) return nullptr;
    bool json_net = false;
    if (papi.use_json_net) il2::il2cpp_field_static_get_value(papi.use_json_net, &json_net);
    auto by_json_net = [&]() -> Il2CppObject* {
        if (!papi.json_deserialize) return nullptr;
        Il2CppObject* r = nullptr;
        void* a[] = {str(s.c_str()), type, static_object(papi.json_settings)};
        call(papi.json_deserialize, nullptr, a, &r);
        return r;
    };
    auto by_fast_json = [&]() -> Il2CppObject* {
        if (!papi.fast_deserialize) return nullptr;
        Il2CppObject* r = nullptr;
        void* a[] = {str(s.c_str()), type};
        call(papi.fast_deserialize, nullptr, a, &r);
        return r;
    };
    Il2CppObject* r = json_net ? by_json_net() : by_fast_json();
    if (!r) r = json_net ? by_fast_json() : by_json_net();
    return r;
}

// What we bring back is out of sight until the room is shown (it stands 80 m up while it's built); renderers that came
// off join the room's keep-off list, as the scene's own did.
void hide_new_renderers(Instance& in, Il2CppObject* go) {
    each_in(go, api.t_renderer, [&](Il2CppObject* r) {
        if (!renderer_on(r))
            in.keep_off.push_back(hold(r));
        else
            set_renderer(r, false);
    });
}

// SI: RestoreSpawnedPlaceablesRoutine + ApplySpawnedPlaceable
void restore_spawned(Instance& in) {
    const fs::path p = building_file(in.dir, in, "spawned");
    Records r = read_records(p, "spawned", 2);
    in.orphan_spawned.clear();
    if (!r.exists) return;
    if (!r.ok) {
        keep_unreadable(p);
        in.spawn_blocked = true;
        return;
    }
    Il2CppObject* mt = live(in.master_tf);
    int applied = 0, carried = 0, failed = 0, curing_ok = 0, curing_failed = 0, curing_items = 0;
    in.curing_applied.clear();
    in.curing_kept.clear();
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < r.rows.size(); ++i) {
        const std::string guid = unesc(r.rows[i][0]);
        if (guid.empty() || !seen.insert(guid).second) continue;
        in.spawned_guids.insert(guid);
        if (is_carried(guid)) {
            ++carried;
            continue;
        }
        Il2CppObject* data = papi.t_placeable_data ? deserialize_object(unesc(r.rows[i][1]), papi.t_placeable_data) : nullptr;
        Il2CppObject* placed = nullptr;
        if (data) {
            put<V3>(data, papi.data_position, to_world(in, get<V3>(data, papi.data_position)));
            put<Quat>(data, papi.data_rotation, mul(in.master_rot, get<Quat>(data, papi.data_rotation)));
            void* a[] = {str(guid.c_str()), data};
            g_creating_placeable_1b = true;
            call(papi.placeable_create, nullptr, a, &placed);
            g_creating_placeable_1b = false;
        }
        if (!alive(placed)) {
            ++failed;
            in.orphan_spawned.push_back(r.lines[i]);  // SI: OrphanSpawnEntries, written back as they were
            continue;
        }
        Il2CppObject* t = tf_of(placed);
        Il2CppObject* ptf = player_tf();
        if (ptf && root_of(t) == root_of(ptf)) {  // on the player: carried, not ours to place
            ++carried;
            continue;
        }
        if (!child_of(t, mt)) {
            bool keep_world = true;
            void* a[] = {mt, &keep_world};
            il2::invoke(api.tf_set_parent, t, a);
        }
        hide_new_renderers(in, go_of(placed));
        put<bool>(placed, api.placeable_invalidated, true);
        void* ra[] = {placed};
        call(papi.placeable_remove, nullptr, ra);
        ++applied;
        if (r.rows[i].size() >= 3 && !r.rows[i][2].empty()) {  // a curing box's contents (SI: RestoreDecorationState)
            const int c = apply_curing(go_of(placed), unesc(r.rows[i][2]));
            if (c > 0) {
                ++curing_ok;
                curing_items += c - 1;
                in.curing_applied.insert(guid);
            } else if (c == 0) {
                ++curing_failed;
                logx::warn("interiors: %s: the curing box '%s' couldn't take its contents back (kept in the file)", in.cfg->id,
                           name_of(go_of(placed)).c_str());
                in.curing_kept[guid] = unesc(r.rows[i][2]);  // written back as it was, never the empty box's
            }
        }
    }
    logx::info("interiors: %s: %d of %zu placed object(s) of the player's put back (%d in the backpack, %d could not be made)%s",
               in.cfg->id, applied, r.rows.size(), carried, failed,
               curing_ok || curing_failed
                   ? (", " + std::to_string(curing_ok) + " curing box(es) with " + std::to_string(curing_items) + " item(s), " +
                      std::to_string(curing_failed) + " not")
                         .c_str()
                   : "");
}

// SI: RestorePlaceablePositions
void restore_placeables(Instance& in) {
    const fs::path p = building_file(in.dir, in, "placeables");
    Records r = read_records(p, "placeables", 12);
    if (!r.exists) return;
    if (!r.ok) {
        keep_unreadable(p);
        in.placeable_blocked = true;
        return;
    }
    struct Entry {
        V3 pos, scale;
        Quat rot;
        bool active = true;
    };
    std::unordered_map<std::string, Entry> entries;
    for (const auto& row : r.rows) {
        Entry e;
        if (!read_v3(row, 1, e.pos) || !read_q(row, 4, e.rot) || !read_v3(row, 8, e.scale)) continue;
        e.active = row[11] == "1";
        entries[unesc(row[0])] = e;
    }
    const bool stable_file = entries.erase(kStableGuidMarker) > 0;
    int restored = 0, off = 0, protected_spawned = 0, protected_unstable = 0, carried = 0;
    std::unordered_set<std::string> done;
    auto apply = [&](Il2CppObject* pl, const Entry& e) {
        Il2CppObject* t = tf_of(pl);
        set_position(t, to_world(in, e.pos));
        set_rotation(t, mul(in.master_rot, e.rot));
        set_local_scale(t, e.scale);
        set_active(go_of(pl), e.active);
        put<bool>(pl, api.placeable_invalidated, false);
        ++restored;
    };
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject* pl) {
        const std::string g = placeable_guid(pl);
        if (g.empty()) return;
        auto it = entries.find(g);
        if (it == entries.end()) {
            if (is_spawned_placeable(in, pl)) {
                ++protected_spawned;
                return;
            }
            if (!stable_file && g_stable_guids.count(g)) {
                ++protected_unstable;
                return;
            }
            set_active(go_of(pl), false);  // not in the record: the player took it away
            ++off;
            return;
        }
        done.insert(g);
        if (is_carried(in, pl)) {
            set_active(go_of(pl), false);
            ++carried;
            return;
        }
        apply(pl, it->second);
    });
    if (done.size() < entries.size() && api.find_objects) {  // moved out of the room's own objects: anywhere in the scene
        void* a[] = {api.t_placeable};
        Il2CppObject* all = il2::invoke(api.find_objects, nullptr, a);
        for (size_t i = 0, n = il2::array_length(all); i < n; ++i) {
            Il2CppObject* pl = il2::array_object_at(all, i);
            if (!alive(pl)) continue;
            const std::string g = placeable_guid(pl);
            auto it = entries.find(g);
            if (g.empty() || done.count(g) || it == entries.end()) continue;
            Il2CppObject* t = tf_of(pl);
            // (P9: not another room's: a building sharing the scene has the same guids)
            if (player_or_inventory(t) || on_shell(t) || under_any_room(pl) || belongs_elsewhere(in, t)) continue;
            done.insert(g);
            apply(pl, it->second);
        }
    }
    logx::info("interiors: %s: %d of %zu furniture position(s) restored; %d switched off (taken away), %d of the player's "
               "and %d with unstable ids kept, %d in the backpack hidden",
               in.cfg->id, restored, entries.size(), off, protected_spawned, protected_unstable, carried);
}

// SI: RestoreInactiveSceneGearItemsRoutine, first half: the file read, the room's loose template items and any region
// item standing in the room cleared (the file is the truth). The records then come in a few per frame.
bool begin_gear(Instance& in) {
    const fs::path p = building_file(in.dir, in, "gear");
    Records r = read_records(p, "gear", 14);
    in.gear_rows.clear();
    in.gear_lines.clear();
    in.orphan_gear.clear();
    if (!r.exists) return false;
    if (!r.ok) {
        keep_unreadable(p);
        in.gear_blocked = true;
        return false;
    }
    Il2CppObject* m = master_of(in);
    Il2CppObject* mt = go_tf(m);
    int deleted = 0;
    std::vector<Il2CppObject*> doomed;  // the room's own loose template items (the file is the truth)
    each_in(m, api.t_gear, [&](Il2CppObject* gi) {
        if (!inside_container(tf_of(gi), mt)) doomed.push_back(go_of(gi));
    });
    // A region item standing in the room is deleted only when the file has it (the same item within 12 cm: a copy that
    // got into the region's save); anything else is the player's and is taken into the room (TLDCoop_DLC: SI deleted
    // every one, which lost an item left where the adoption's rays didn't reach).
    struct Rec {
        std::string name;
        V3 local;
    };
    std::vector<Rec> recs;
    for (const auto& row : r.rows) {
        V3 lp;
        if (read_v3(row, 2, lp)) recs.push_back({prefab_name(unesc(row[0])), lp});
    }
    Box filter;
    const bool has_filter = filter_box(in, filter);
    each_listed(static_object(api.gear_list), [&](Il2CppObject* gi, int) {
        if (!alive(gi)) return;
        Il2CppObject* t = tf_of(gi);
        const V3 pos = position(t);
        if (child_of(t, mt) || (has_filter && !filter.contains(pos)) || player_or_inventory(t) || on_shell(t) ||
            under_placement_root(t) || !in_parked_room(in, pos))
            return;
        const std::string n = prefab_name(name_of(go_of(gi)));
        const V3 lp = to_local(in, pos);
        bool twin = false;
        for (const Rec& rec : recs)
            if (rec.name == n && dist(rec.local, lp) <= kLeakMatch) {
                twin = true;
                break;
            }
        // (P8: the region's items are left as they are until all of the room's contents are back: settle_region_items)
        (twin ? in.deferred_twins : in.deferred_taken).push_back({hold(gi), pos});
    });
    for (Il2CppObject* go : doomed) {
        destroy_now(go);
        ++deleted;
    }
    in.gear_rows = std::move(r.rows);
    in.gear_lines = std::move(r.lines);
    in.gear_restored = in.gear_failed = 0;
    in.gear_next = 0;
    logx::info("interiors: %s: %zu saved item record(s); %d template item(s) in the room cleared first; of the region's "
               "lying in it, %zu copies of saved ones go and %zu of the player's are taken in once the room is complete",
               in.cfg->id, in.gear_rows.size(), deleted, in.deferred_twins.size(), in.deferred_taken.size());
    return true;
}
// Still the region's, loose where begin_gear saw it (not picked up or moved since).
Il2CppObject* untouched_region_item(Instance& in, const Instance::RegionItem& r) {
    Il2CppObject* gi = live(r.gi);
    if (!gi) return nullptr;
    Il2CppObject* t = tf_of(gi);
    Il2CppObject* mt = live(in.master_tf);
    if (!t || (mt && child_of(t, mt)) || player_or_inventory(t) || dist(position(t), r.at) > 0.05f) return nullptr;
    return gi;
}
// P8: the room came back whole: the region's copies of its saved items deleted, the player's things lying in it taken
// in. (Given up on, drop_region_items: all of them stay the region's.)
void settle_region_items(Instance& in) {
    Il2CppObject* mt = live(in.master_tf);
    int deleted = 0, kept = 0;
    for (Instance::RegionItem& r : in.deferred_twins) {
        if (Il2CppObject* gi = untouched_region_item(in, r)) {
            destroy_now(go_of(gi));
            ++deleted;
        }
        drop(r.gi);
    }
    for (Instance::RegionItem& r : in.deferred_taken) {
        Il2CppObject* gi = untouched_region_item(in, r);
        if (gi && mt && !in.gear_blocked) {
            const std::string nm = name_of(go_of(gi));
            bool keep_world = true;
            void* pa[] = {mt, &keep_world};
            il2::invoke(api.tf_set_parent, tf_of(gi), pa);
            freeze_physics(gi);
            if (kept++ < 10)
                logx::info("interiors: %s: '%s' lay in the room but isn't in its file: kept, the room's from now on", in.cfg->id,
                           nm.c_str());
        }
        drop(r.gi);
    }
    in.deferred_twins.clear();
    in.deferred_taken.clear();
    if (deleted || kept)
        logx::info("interiors: %s: %d copy(ies) of saved items deleted from the region, %d of the player's taken in", in.cfg->id,
                   deleted, kept);
}
void drop_region_items(Instance& in) {
    for (auto* list : {&in.deferred_twins, &in.deferred_taken})
        for (Instance::RegionItem& r : *list) drop(r.gi);
    in.deferred_twins.clear();
    in.deferred_taken.clear();
}
// SI: SpawnGearRecord (the stateful path; a record without the game's own text can't be made: kept as an orphan)
bool spawn_gear(Instance& in, const std::vector<std::string>& row) {
    const std::string name = unesc(row[0]), guid = unesc(row[1]), s = unesc(row[13]);
    V3 lp, sc;
    Quat lr;
    if (!read_v3(row, 2, lp) || !read_q(row, 5, lr) || !read_v3(row, 9, sc) || s.empty()) return false;
    Il2CppObject* mt = live(in.master_tf);
    bool f = false;
    void* a[] = {str(prefab_name(name).c_str()), str(s.c_str()), mt, &f, &f};
    Il2CppObject* gi = nullptr;
    if (!call(papi.gear_instantiate, nullptr, a, &gi) || !alive(gi)) return false;
    Il2CppObject* t = tf_of(gi);
    Il2CppObject* go = go_of(gi);
    if (parent_of(t) != mt) {
        bool keep_world = true;
        void* pa[] = {mt, &keep_world};
        il2::invoke(api.tf_set_parent, t, pa);
    }
    set_position(t, to_world(in, lp));
    set_rotation(t, mul(in.master_rot, lr));
    set_local_scale(t, sc);
    hide_new_renderers(in, go);
    set_active(go, row[12] == "1");
    freeze_physics(gi);
    if (!guid.empty()) set_mod_guid(go, guid);
    return true;
}

// SI: RestoreContainerDataRoutine, the matching: by the room-local spot (25 cm), then the old key, then name within 3 m.
bool begin_containers(Instance& in) {
    const fs::path p = building_file(in.dir, in, "containers");
    Records r = read_records(p, "containers", 6);
    in.container_jobs.clear();
    in.orphan_containers.clear();
    if (!r.exists) return false;
    if (!r.ok) {
        keep_unreadable(p);
        in.container_blocked = true;
        return false;
    }
    struct C {
        Il2CppObject* c;
        std::string key, name;
        V3 pos;
        bool claimed = false;
    };
    std::vector<C> cs;
    std::unordered_map<std::string, int> counter;
    each_in(master_of(in), api.t_container, [&](Il2CppObject* c) {
        const std::string n = name_of(go_of(c));
        const int idx = counter[n]++;
        cs.push_back({c, n + "###" + std::to_string(idx), clean_name(n), to_local(in, position(tf_of(c)))});
    });
    const size_t nr = r.rows.size();
    std::vector<int> target(nr, -1);
    std::vector<V3> rpos(nr);
    std::vector<bool> has_pos(nr, false);
    for (size_t i = 0; i < nr; ++i) has_pos[i] = read_v3(r.rows[i], 2, rpos[i]);
    int by_pos = 0, by_key = 0, by_name = 0;
    for (size_t i = 0; i < nr; ++i) {
        if (!has_pos[i]) continue;
        int best = -1;
        float bd = 0.25f;
        for (size_t k = 0; k < cs.size(); ++k)
            if (!cs[k].claimed && dist(cs[k].pos, rpos[i]) < bd) {
                bd = dist(cs[k].pos, rpos[i]);
                best = static_cast<int>(k);
            }
        if (best >= 0) {
            target[i] = best;
            cs[best].claimed = true;
            ++by_pos;
        }
    }
    for (size_t i = 0; i < nr; ++i) {
        if (target[i] >= 0) continue;
        const std::string key = unesc(r.rows[i][0]);
        for (size_t k = 0; k < cs.size(); ++k)
            if (!cs[k].claimed && cs[k].key == key) {
                target[i] = static_cast<int>(k);
                cs[k].claimed = true;
                ++by_key;
                break;
            }
    }
    for (size_t i = 0; i < nr; ++i) {
        if (target[i] >= 0 || !has_pos[i]) continue;
        const std::string n = unesc(r.rows[i][1]);
        int best = -1;
        float bd = 3.0f;
        for (size_t k = 0; k < cs.size(); ++k)
            if (!cs[k].claimed && prefab_name(cs[k].name) == prefab_name(n) && dist(cs[k].pos, rpos[i]) < bd) {
                bd = dist(cs[k].pos, rpos[i]);
                best = static_cast<int>(k);
            }
        if (best >= 0) {
            target[i] = best;
            cs[best].claimed = true;
            ++by_name;
        }
    }
    int missed = 0;
    for (size_t i = 0; i < nr; ++i) {
        if (target[i] < 0) {
            ++missed;
            in.orphan_containers.push_back(r.lines[i]);
            continue;
        }
        in.container_jobs.push_back({hold(cs[target[i]].c), unesc(r.rows[i][5]), r.lines[i]});
    }
    logx::info("interiors: %s: %zu container record(s): %d matched by spot, %d by key, %d by name, %d unmatched (kept)",
               in.cfg->id, nr, by_pos, by_key, by_name, missed);
    return true;
}
void restore_one_container(Instance& in, Instance::ContainerJob& job) {
    Il2CppObject* c = live(job.container);
    bool ok = false;
    if (c) {
        call(papi.container_clear, c);
        void* a[] = {str(job.data.c_str()), nullptr};
        ok = call(papi.container_load, c, a);
    }
    if (!ok) in.orphan_containers.push_back(job.line);
    drop(job.container);
}

// SI: RestoreInteractiveState
void restore_state(Instance& in) {
    const fs::path p = building_file(in.dir, in, "state");
    Records r = read_records(p, "state", 2);
    in.orphan_state.clear();
    in.state_fires.clear();
    if (!r.exists) return;
    if (!r.ok) {
        keep_unreadable(p);
        in.state_blocked = true;
        return;
    }
    std::unordered_map<std::string, std::string> data;
    std::unordered_map<std::string, std::string> raw;
    for (size_t i = 0; i < r.rows.size(); ++i) {
        const std::string key = unesc(r.rows[i][0]);
        data[key] = unesc(r.rows[i][1]);
        raw[key] = r.lines[i];
    }
    int restored = 0, missed = 0;
    std::unordered_set<std::string> used;
    for (auto& k : papi.kinds) {
        if (!k.type || !k.save || !k.load) continue;
        each_in(master_of(in), k.type, [&](Il2CppObject* c) {
            const std::string now = call_text(k.save, c);
            const std::string key = state_key(k.tag, in, c, now);
            if (!used.insert(key).second) return;
            auto it = data.find(key);
            if (it == data.end()) {
                ++missed;
                return;
            }
            void* a[] = {str(it->second.c_str())};
            if (call(k.load, c, a)) {
                ++restored;
                if (std::strcmp(k.tag, "BD") == 0) put<bool>(c, papi.breakdown_post_load, false);
            }
        });
    }
    for (auto& [key, d] : data) {
        if (key.rfind("FI|", 0) == 0) {
            in.state_fires[key.substr(3)] = d;
            used.insert(key);
        }
        if (key == "JK|cleared") {  // A11 (SI: RestoreJunkState)
            used.insert(key);
            if (d == "1") {
                const int n = clear_junk_in(in);
                logx::info("interiors: %s: its junk was cleared (%d object(s) switched off)", in.cfg->id, n);
                if (g_junk_owner == &in)
                    if (Il2CppObject* jm = junk_manager()) put<bool>(jm, japi.cleared, true);
            }
        }
        if (!used.count(key)) in.orphan_state.push_back(raw[key]);
    }
    logx::info("interiors: %s: %d of %zu object state(s) restored (%d unmatched in the room, %zu fire record(s))", in.cfg->id,
               restored, data.size(), missed, in.state_fires.size());
}

// --- fires (SI: SaveData.cs FireManagerStealerPatch / DelayedFireRestore / RestoreCloneFireStates / PruneDuplicateFires)

std::unordered_map<std::string, std::string> g_region_fires;  // burning records of the region's save, by fire key
double g_region_fires_at = -1e18;

// SI: ParseBurningFireRecords: "m_SearializedFire":"<escaped>","m_Guid":"<guid>" pairs whose state burns.
std::string json_unescape(const std::string& t) {
    if (t.find('\\') == std::string::npos) return t;
    std::string o;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] != '\\' || i + 1 >= t.size()) {
            o += t[i];
            continue;
        }
        const char n = t[++i];
        switch (n) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'u': {  // \uXXXX as UTF-8 (a surrogate half as it stands)
                unsigned cp = 0;
                bool ok = i + 4 < t.size();
                for (size_t k = 1; ok && k <= 4; ++k) {
                    const char h = t[i + k];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                    else ok = false;
                }
                if (!ok) {
                    o += "\\u";
                    break;
                }
                i += 4;
                if (cp < 0x80) {
                    o += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    o += static_cast<char>(0xC0 | (cp >> 6));
                    o += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    o += static_cast<char>(0xE0 | (cp >> 12));
                    o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    o += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: o += '\\'; o += n;
        }
    }
    return o;
}
// The burning ones of a FireManager blob, by guid.
int parse_fire_records(const std::string& blob, std::unordered_map<std::string, std::string>& out) {
    const std::string pm = "\"m_SearializedFire\":\"", gm = "\",\"m_Guid\":\"";
    int n = 0;
    for (size_t idx = 0;;) {
        size_t start = blob.find(pm, idx);
        if (start == std::string::npos) break;
        start += pm.size();
        const size_t end = blob.find(gm, start);
        if (end == std::string::npos) break;
        const size_t gs = end + gm.size();
        const size_t ge = blob.find('"', gs);
        if (ge == std::string::npos) break;
        idx = ge + 1;
        const std::string guid = blob.substr(gs, ge - gs);
        if (guid.empty()) continue;
        const std::string payload = json_unescape(blob.substr(start, end - start));
        if (!record_burning(payload)) continue;
        out[guid] = payload;
        ++n;
    }
    return n;
}
int keep_region_fires(const std::string& blob) { return parse_fire_records(blob, g_region_fires); }

// SI: RestoreCloneFireStates for one room: our building's record first, else the region save's; only burning records,
// only onto a fire not burning already; the fire's place kept.
int restore_fires(Instance& in) {
    int restored = 0, from_file = 0;
    // The region save's records are known by the fire's id only, which buildings sharing the scene share: used only
    // when this building's file has no fire records and no other building has its scene (P9).
    const bool region_ok = in.state_fires.empty() && !scene_shared(in);
    each_in(master_of(in), api.t_fire, [&](Il2CppObject* f) {
        const std::string key = fire_key(f);
        if (key.empty() || call_bool(papi.fire_burning, f)) return;
        const std::string* rec = nullptr;
        bool ours = false;
        if (auto it = in.state_fires.find(key); it != in.state_fires.end() && record_burning(it->second)) {
            rec = &it->second;
            ours = true;
        } else if (auto it2 = region_ok ? g_region_fires.find(key) : g_region_fires.end(); it2 != g_region_fires.end()) {
            rec = &it2->second;
        }
        if (!rec) return;
        Il2CppObject* t = tf_of(f);
        const V3 p = position(t);
        const Quat q = rotation(t);
        void* a[] = {str(rec->c_str())};
        if (!call(papi.fire_load, f, a)) return;
        set_position(t, p);
        set_rotation(t, q);
        ++restored;
        if (ours) ++from_file;
    });
    if (restored)
        logx::info("interiors: %s: %d fire(s) lit again (%d from the building's file, %d from the region's save)", in.cfg->id,
                   restored, from_file, restored - from_file);
    return restored;
}

// SI: PruneDuplicateFires (+ ChooseFireToKeep): fires within 5 cm of each other: one kept; the others go unless burning
// or a room's own.
int prune_duplicate_fires() {
    Il2CppObject* list = static_object(api.fire_list);
    std::vector<Il2CppObject*> fires;
    each_listed(list, [&](Il2CppObject* f, int) {
        if (alive(f)) fires.push_back(f);
    });
    if (fires.size() < 2) return 0;
    std::vector<bool> claimed(fires.size(), false);
    int removed = 0;
    auto score = [&](Il2CppObject* f) {
        int s = 0;
        if (fire_alive(f)) s += 8;
        if (under_any_room(f)) s += 4;
        if (Il2CppObject* p = parent_of(tf_of(f))) {
            s += 1;
            if (in_parent(p, api.t_stove) || in_parent(p, api.t_campfire)) s += 2;
        }
        return s;
    };
    for (size_t i = 0; i < fires.size(); ++i) {
        if (claimed[i]) continue;
        claimed[i] = true;
        std::vector<size_t> group{i};
        const V3 anchor = position(tf_of(fires[i]));
        for (size_t j = i + 1; j < fires.size(); ++j)
            if (!claimed[j] && dist(position(tf_of(fires[j])), anchor) <= 0.05f) {
                claimed[j] = true;
                group.push_back(j);
            }
        if (group.size() < 2) continue;
        size_t keeper = group[0];
        int best = -1;
        for (size_t g : group)
            if (const int s = score(fires[g]); s > best) {
                best = s;
                keeper = g;
            }
        for (size_t g : group) {
            Il2CppObject* dup = fires[g];
            if (g == keeper || fire_alive(dup) || under_any_room(dup)) continue;
            const std::string nm = name_of(go_of(dup));
            void* a[] = {dup};
            call(papi.fire_remove, nullptr, a);
            destroy(go_of(dup));
            ++removed;
            logx::info("interiors: a copy fire '%s' at (%.2f, %.2f, %.2f) deleted", nm.c_str(), anchor.x, anchor.y, anchor.z);
        }
    }
    return removed;
}

// --- hydration as a step-per-frame machine (SI: HydrateRoutine; the eager path: during the build) --------------------

// True when done.
bool tick_hydrate(Instance& in) {
    if (!papi.ok) {  // the game's save calls are missing: 1a's behaviour (the template every visit, nothing adopted)
        logx::info("interiors: %s: rooms aren't saved in this build; the template's contents", in.cfg->id);
        return true;
    }
    if (in.coop_friend) {  // milestone 2: a friend's room is the template; the host's lists bring what is in it
        stabilize_guids(in);
        snapshot_region_gear(in);
        in.hydrated = true;
        in.closed_dirty = true;
        purge_leaked(in);
        logx::info("interiors: %s: a friend's room: no files of ours; its contents come from the host's game", in.cfg->id);
        return true;
    }
    switch (in.hydrate_step) {
        case 0:  // SI: 0/7: late runtime ids
            stabilize_guids(in);
            snapshot_region_gear(in);
            in.hydrate_started = now_ms();
            in.hydrate_step = 1;
            return false;
        case 1:  // 2/7: the player's furniture (before the positions: those switch off what they don't know)
            restore_spawned(in);
            in.hydrate_step = 2;
            return false;
        case 2:  // 3/7: the scene's furniture the player moved
            restore_placeables(in);
            in.hydrate_step = begin_gear(in) ? 3 : 4;
            return false;
        case 3: {  // 4/7: loose items, a few per frame
            const size_t until = std::min(in.gear_rows.size(), in.gear_next + 25);
            for (; in.gear_next < until; ++in.gear_next) {
                if (spawn_gear(in, in.gear_rows[in.gear_next]))
                    ++in.gear_restored;
                else {
                    ++in.gear_failed;
                    in.orphan_gear.push_back(in.gear_lines[in.gear_next]);
                }
            }
            if (in.gear_next < in.gear_rows.size()) return false;
            logx::info("interiors: %s: %d item(s) put back, %d could not be made (kept in the file)", in.cfg->id,
                       in.gear_restored, in.gear_failed);
            in.gear_rows.clear();
            in.gear_lines.clear();
            in.gear_next = 0;
            in.hydrate_step = 4;
            return false;
        }
        case 4:  // 5/7: containers (one per frame)
            in.hydrate_step = begin_containers(in) ? 5 : 6;
            return false;
        case 5:
            if (!in.container_jobs.empty()) {
                restore_one_container(in, in.container_jobs.back());
                in.container_jobs.pop_back();
                return false;
            }
            in.hydrate_step = 6;
            return false;
        case 6:  // 6/7: broken / harvested / opened objects, then the fires
            restore_state(in);
            restore_fires(in);
            prune_duplicate_fires();
            in.hydrate_step = 7;
            return false;
        default:
            if (in.legacy == kLegacyHydrating) {  // an old save's import: all of it or none of it
                std::string why;
                if (!import_complete(in, why)) {
                    drop_region_items(in);
                    abandon_import(in, why);
                    return true;
                }
            }
            settle_region_items(in);
            in.hydrated = true;  // SI: ContentHydrated
            in.closed_dirty = true;
            purge_leaked(in);
            logx::info("interiors: %s: contents restored in %.0f ms%s", in.cfg->id, now_ms() - in.hydrate_started,
                       in.from_files ? "" : " (nothing saved yet: the template's)");
            return true;
    }
}

// --- the player's place in the save (SI: SaveData.cs SavePlayerInsideState / GetSavedPlayer* /
//     RepairInsideRecordFromSavePosition; Main.cs Run's early activation) ------------------------------------------------

struct InsideRecord {
    bool valid = false, have_local = false;
    std::string region, id;
    V3 local, door;
    Quat rot;
} g_record;
struct Hold {  // the player held outside its building, the screen black, until the room is built
    bool active = false;
    std::string id;
    V3 at;
    double started = 0, next_fade = 0;
    bool use_save_pos = false;
    V3 save_pos;
} g_hold;
bool g_record_checked = false;

V3 save_game_position() {
    Il2CppObject* pm = player_manager();
    return pm && papi.pm_save_position >= 0 ? get<V3>(pm, papi.pm_save_position) : V3{};
}

bool safe_door_point(Instance& room, V3 near_pos, V3& safe);  // the door point PlayerManager.Serialize saves (below)

void save_player_inside(const fs::path& dir) {
    std::vector<std::string> lines;
    Instance* in = g_inside && in_room(g_inside) ? g_inside : nullptr;
    if (in) {
        Il2CppObject* ptf = player_tf();
        const V3 here = position(ptf);
        V3 door = here;
        safe_door_point(*in, here, door);
        lines.push_back(esc(g_region) + "\t" + esc(in->cfg->id) + "\t" + v3_fields(to_local(*in, here)) + "\t" +
                        q_fields(mul(conj(in->master_rot), rotation(ptf))) + "\t" + v3_fields(door));
    } else {
        lines.push_back(esc(g_region) + "\t\t0\t0\t0\t0\t0\t0\t1\t0\t0\t0");  // SI: outside: the old record goes
    }
    write_staged(dir / "player.inside", file_text("inside", lines));
}

// SI: RepairInsideRecordFromSavePosition: the game's own saved position, if it stands in a parked room of this region
// (a save we didn't steer to the door), names the room.
const Config* parked_spot_of(V3 save_pos) {
    const Config* best = nullptr;
    float best_side = 12.0f;
    for (const Instance& in : g_instances) {
        const V3 parked = in.cfg->fallback + kParkOffset;
        const float up = save_pos.y - parked.y;
        if (up < -5.0f || up > 12.0f) continue;
        const float side = dist_xz(save_pos, parked);
        if (side <= best_side) {
            best_side = side;
            best = in.cfg;
        }
    }
    return best;
}

void fade_now(bool out, float seconds) { fade(out, seconds); }

// On a region load: is the player to be put back in a room?
void check_inside_record() {
    if (g_record_checked || g_region.empty()) return;
    const std::string sid = game::save_id();
    if (sid.empty() || game::seconds_since_level_load() < 1.0f) return;  // after the game has put the player down
    g_record_checked = true;
    g_record = {};
    const fs::path dir = dir_for(sid);
    const Records r = read_records(dir / "player.inside", "inside", 12);
    const V3 save_pos = save_game_position();
    if (r.exists && r.ok && !r.rows.empty()) {
        const auto& row = r.rows[0];
        g_record.region = unesc(row[0]);
        g_record.id = unesc(row[1]);
        g_record.have_local = read_v3(row, 2, g_record.local) && read_q(row, 5, g_record.rot);
        g_record.valid = read_v3(row, 9, g_record.door) && !g_record.id.empty();
    }
    std::string why;
    if (g_record.valid) {
        const bool here = g_record.region == g_region && find_instance(g_record.id.c_str());
        const bool near_door = dist(save_pos, g_record.door) < 30.0f || dist(player_pos(), g_record.door) < 30.0f;
        if (!here || !near_door) {
            logx::info("interiors: the save's room record (%s in %s) doesn't match where the game put the player; ignored",
                       g_record.id.c_str(), g_record.region.c_str());
            g_record.valid = false;
        } else {
            why = "the save's room record";
        }
    }
    bool use_save_pos = false;
    if (!g_record.valid && (save_pos.x != 0 || save_pos.y != 0 || save_pos.z != 0)) {
        if (const Config* c = parked_spot_of(save_pos)) {  // SI: the save file outranks the record
            g_record = {};
            g_record.valid = true;
            g_record.region = g_region;
            g_record.id = c->id;
            use_save_pos = true;
            why = "the game's own save position stands in its parked room";
            logx::warn("interiors: the game's save puts the player in %s's parked room (%.1f, %.1f, %.1f); the room is built "
                       "first and the player put back there",
                       c->id, save_pos.x, save_pos.y, save_pos.z);
        }
    }
    if (!g_record.valid) {
        logx::info("interiors: the save was made outside the rooms");
        return;
    }
    g_hold = {};
    g_hold.active = true;
    g_hold.id = g_record.id;
    g_hold.at = is_zero(save_pos) ? player_pos() : save_pos;
    g_hold.started = now_ms();
    g_hold.use_save_pos = use_save_pos;
    g_hold.save_pos = save_pos;
    fade_now(true, 0);
    logx::info("interiors: the player was inside %s when the game was saved (%s); held at its door, the room is built first",
               g_record.id.c_str(), why.c_str());
}

void release_hold(const char* why) {
    if (!g_hold.active) return;
    g_hold.active = false;
    fade_now(false, 0.5f);
    logx::info("interiors: the player is free again (%s)", why);
}

// The held player is put in the room once it's built (SI: early activation: the saved room-local spot first, then the
// save's own position, then the entry point).
void enter_from_record(Instance& in);

void tick_hold() {
    if (!g_hold.active) return;
    const double now = now_ms();
    Instance* in = find_instance(g_hold.id.c_str());
    if (!in || in->stage == Stage::Vanilla) {
        release_hold("its room could not be built; the player stays at the door");
        return;
    }
    if (in->stage == Stage::Ready && master_of(*in)) {  // built before the hold began
        enter_from_record(*in);
        return;
    }
    if (now - g_hold.started > 60000) {
        release_hold("the room took over a minute; the player stays at the door");
        return;
    }
    if (coop() == Coop::Friend && !host_has(*in) && now - g_hold.started > 10000) {  // milestone 2
        release_hold("the host's game has no such room; the player stays at the door");
        return;
    }
    if (now >= g_hold.next_fade) {  // the game fades in after its load; black is kept
        g_hold.next_fade = now + 500;
        fade_now(true, 0);
    }
    const V3 p = player_pos();
    if (is_zero(g_hold.at)) g_hold.at = p;
    if (!is_zero(p) && !is_zero(g_hold.at) && dist_xz(p, g_hold.at) > 2.0f) {
        Il2CppObject* ptf = player_tf();
        teleport(g_hold.at, rotation(ptf));
    }
}

// --- closed-room time (SI: FrozenTime.cs) -------------------------------------------------------------------------------

constexpr double kClosedRescanMs = 60000;  // SI: CLOSED_INTERIOR_RESCAN_INTERVAL

void rebuild_closed_cache(Instance& in) {
    in.closed_dirty = false;
    in.closed_next_rescan = now_ms() + kClosedRescanMs;
    for (auto* list : {&in.closed_fires, &in.closed_pots, &in.closed_lamps, &in.closed_torches, &in.closed_flares, &in.closed_evolve})
        for (uintptr_t& h : *list) drop(h);
    in.closed_fires.clear();
    in.closed_pots.clear();
    in.closed_lamps.clear();
    in.closed_torches.clear();
    in.closed_flares.clear();
    in.closed_evolve.clear();
    Il2CppObject* m = master_of(in);
    auto collect = [&](Il2CppObject* type, std::vector<uintptr_t>& out) {
        if (type) each_in(m, type, [&](Il2CppObject* c) { out.push_back(hold(c)); });
    };
    collect(api.t_fire, in.closed_fires);
    collect(papi.t_pot, in.closed_pots);
    collect(papi.t_lamp, in.closed_lamps);
    collect(papi.t_torch, in.closed_torches);
    collect(papi.t_flare, in.closed_flares);
    collect(papi.t_evolve, in.closed_evolve);
}

void advance_closed(Instance& in, float tod_seconds) {
    const float hours = tod_seconds / 3600.0f, minutes = hours * 60.0f;
    for (uintptr_t h : in.closed_fires) {
        Il2CppObject* f = live(h);
        if (!f) continue;
        if (!fire_alive(f)) {  // SI: AdvanceClosedEmbers
            if (!call_bool(papi.fire_burning, f) || papi.fire_embers < 0 || !get<bool>(f, papi.fire_embers)) continue;
            const float len = get<float>(f, papi.fire_ember_len), timer = get<float>(f, papi.fire_ember_timer);
            if (timer >= len) continue;
            const float step = std::min(tod_seconds, len - timer);
            put<float>(f, papi.fire_ember_timer, timer + step);
            put<float>(f, papi.fire_elapsed, get<float>(f, papi.fire_elapsed) + step);
            put<float>(f, papi.fire_elapsed_raw, get<float>(f, papi.fire_elapsed_raw) + step);
            continue;
        }
        put<float>(f, papi.fire_elapsed, get<float>(f, papi.fire_elapsed) + tod_seconds);
        put<float>(f, papi.fire_elapsed_raw, get<float>(f, papi.fire_elapsed_raw) + tod_seconds);
        put<float>(f, papi.fire_hours, get<float>(f, papi.fire_hours) + hours);
        if (Il2CppObject* heat = api.fire_heat >= 0 ? get<Il2CppObject*>(f, api.fire_heat) : nullptr; alive(heat))
            call(papi.heat_update, heat);
    }
    for (uintptr_t h : in.closed_pots) {
        Il2CppObject* pot = live(h);
        if (!pot || !call_bool(papi.pot_cooking, pot)) continue;
        if (papi.pot_ruined >= 0 && get<int32_t>(pot, papi.pot_state) == papi.pot_ruined) continue;
        Il2CppObject* fire = nullptr;
        const bool burning = call_bool(papi.pot_fire_burning, pot) && call(papi.pot_fire, pot, nullptr, &fire) && fire_alive(fire);
        if (burning) {
            put<float>(pot, papi.pot_elapsed, get<float>(pot, papi.pot_elapsed) + hours);
            put<float>(pot, papi.pot_until_cooked, std::max(0.0f, get<float>(pot, papi.pot_until_cooked) - minutes));
            put<float>(pot, papi.pot_until_ruined, std::max(0.0f, get<float>(pot, papi.pot_until_ruined) - minutes));
        } else {
            put<float>(pot, papi.pot_grace, get<float>(pot, papi.pot_grace) + hours);
        }
    }
    for (uintptr_t h : in.closed_lamps) {  // SI: AdvanceClosedLightSources
        Il2CppObject* lamp = live(h);
        if (!lamp || !call_bool(papi.lamp_on, lamp)) continue;
        float hb = hours;
        void* a[] = {&hb};
        call(papi.lamp_reduce, lamp, a);
    }
    auto burn = [&](std::vector<uintptr_t>& list, const MethodInfo* burning, const MethodInfo* life, ptrdiff_t elapsed) {
        if (elapsed < 0) return;
        for (uintptr_t h : list) {
            Il2CppObject* x = live(h);
            if (!x || !call_bool(burning, x)) continue;
            const float e = get<float>(x, elapsed);
            if (e >= call_float(life, x, 0)) continue;
            put<float>(x, elapsed, e + minutes);
        }
    };
    burn(in.closed_torches, papi.torch_burning, papi.torch_life, papi.torch_elapsed);
    burn(in.closed_flares, papi.flare_burning, papi.flare_life, papi.flare_elapsed);
    for (uintptr_t h : in.closed_evolve) {  // SI: AdvanceClosedEvolveItems
        Il2CppObject* ev = live(h);
        if (!ev || papi.evolve_spent < 0) continue;
        const float spent = get<float>(ev, papi.evolve_spent);
        if (spent >= get<float>(ev, papi.evolve_days) * 24.0f || !call_bool(papi.evolve_can, ev)) continue;
        put<float>(ev, papi.evolve_spent, spent + hours);
    }
}

// SI: TickClosedInteriorTime: a closed room is inactive (no Update), so its fires, pots, lamps, torches, flares and
// curing items are moved on by the game time that passed.
void tick_closed_time() {
    if (!papi.tod_seconds || !papi.delta_time || g_hold.active) return;
    Il2CppObject* tod = il2::invoke(papi.tod_component, nullptr);
    if (!tod) return;
    float dt = 0;
    if (!il2::invoke_value(papi.delta_time, nullptr, dt) || dt <= 0) return;
    float tod_seconds = 0;
    void* a[] = {&dt};
    if (!il2::invoke_value(papi.tod_seconds, tod, tod_seconds, a) || tod_seconds <= 0) return;
    for (Instance& in : g_instances) {
        if (in.stage != Stage::Ready || !in.hydrated) continue;
        Il2CppObject* m = master_of(in);
        if (!m) continue;
        if (active_in_tree(m)) {
            in.closed_was_open = true;
            continue;
        }
        if (in.closed_was_open || in.closed_dirty || now_ms() >= in.closed_next_rescan) {
            in.closed_was_open = false;
            rebuild_closed_cache(in);
            const size_t n = in.closed_fires.size() + in.closed_pots.size() + in.closed_lamps.size() + in.closed_torches.size() +
                             in.closed_flares.size() + in.closed_evolve.size();
            if (n && in.closed_logged++ < 3)
                logx::info("interiors: %s closed: its time goes on for %zu fire(s), %zu pot(s), %zu lamp(s), %zu torch(es), "
                           "%zu flare(s), %zu curing item(s)",
                           in.cfg->id, in.closed_fires.size(), in.closed_pots.size(), in.closed_lamps.size(),
                           in.closed_torches.size(), in.closed_flares.size(), in.closed_evolve.size());
        }
        advance_closed(in, tod_seconds);
    }
}

// --- the save as a whole (SI: SavePlaceablePositionsPatch = SaveGameSystem.SaveSceneData postfix) ---------------------

uint64_t room_print(Instance& in);
void save_rooms(const char* scene_name) {
    std::lock_guard lock(g_files);  // (P12: no commit between one room's files and the next)
    const std::string sid = game::save_id();
    const fs::path dir = dir_for(sid);
    if (dir.empty()) {
        logx::warn("interiors: the game saved (%s) before the save's identity was known; rooms not written", scene_name);
        return;
    }
    int written = 0, skipped = 0;
    for (Instance& in : g_instances) {
        if (!can_persist(in)) {
            if (in.stage != Stage::Idle && in.stage != Stage::Vanilla) ++skipped;
            continue;
        }
        if (in.dir != dir) {
            logx::warn("interiors: %s: its data folder changed during the session; not written", in.cfg->id);
            continue;
        }
        const bool ok = save_placeables(in) & save_gear(in) & save_containers(in) & save_state(in);
        if (!ok) logx::warn("interiors: %s: some of its files couldn't be written; the ones on the disk stay as they were", in.cfg->id);
        if (ok && !in.import_marker.empty() && write_staged(building_file(in.dir, in, "imported-from"), in.import_marker) &&
            write_staged(building_file(in.dir, in, "imported"), in.import_marker)) {
            in.import_marker.clear();
            logx::info("interiors: %s: its import marker written with this save", in.cfg->id);
        }
        if (in.rolled) g_rolled_ids.insert(in.cfg->id);
        in.has_data = true;
        if (ok) in.last_saved_ms = now_ms();  // A8: written by this save
        if (ok && in.persist_pending) in.persist_print = room_print(in);  // (adopted strays are in it now)
        ++written;
    }
    if (written) {
        std::vector<std::string> ids(g_rolled_ids.begin(), g_rolled_ids.end());
        std::sort(ids.begin(), ids.end());
        std::vector<std::string> lines;
        for (const std::string& id : ids) lines.push_back(esc(id));
        write_staged(dir / "loot.rolled", file_text("rolled", lines));
    }
    save_player_inside(dir);
    logx::info("interiors: saved with the game (%s): %d room(s) written (staged until the save reaches the disk), %d not "
               "restored this session left as they were; the player %s",
               scene_name, written, skipped, g_inside ? "inside" : "outside");
}

// SI: AdoptStrayInteriorObjects before the game writes the region: what was dropped in a room this visit belongs to the
// room (and is kept out of the region's save by the gear/placeable surgery).
void adopt_before_save(const fs::path& dir) {  // (P11: only rooms whose files this save writes)
    for (Instance& in : g_instances)
        if (can_persist(in) && in.dir == dir) adopt_strays(in, "before the save");
}

// --- glue -------------------------------------------------------------------------------------------------------------

bool hold_targets(const Instance& in) { return g_hold.active && g_hold.id == in.cfg->id; }
bool hold_active() { return g_hold.active; }

// The building's data folder (fixed for the visit) and whether it has any file in it.
void ensure_dir(Instance& in) {
    if (!in.dir.empty()) return;
    if (in.coop_friend || coop() == Coop::Friend) {  // milestone 2: a friend's rooms have no files (the host's have)
        in.coop_friend = true;
        return;
    }
    in.dir = data_dir();
    if (in.dir.empty()) return;
    for (const char* kind : {"placeables", "spawned", "gear", "containers", "state"})
        if (exists_preferred(building_file(in.dir, in, kind))) in.has_data = true;
}

void enter_from_record(Instance& in) {
    open_room(in);
    if (!live(in.shell))  // a sub-interior: its parent's shell goes too
        for (Instance& parent : g_instances)
            if (is_sub_linked(parent, in.cfg->id) && in_room(&parent))
                if (Il2CppObject* s = live(parent.shell)) set_active(s, false);
    set_outsiders_visible(in, true);
    set_audio_occlusion(true);
    mark_inside(&in, "the save was made in it");
    Il2CppObject* ptf = player_tf();
    V3 spot;
    Quat rot = rotation(ptf);
    const char* from = "";
    if (g_record.have_local && !g_hold.use_save_pos) {
        spot = to_world(in, g_record.local);
        rot = mul(in.master_rot, g_record.rot);
        from = "the saved spot in the room";
    } else if (g_hold.use_save_pos && in_parked_room(in, g_hold.save_pos, 0)) {
        spot = g_hold.save_pos;
        from = "the game's own save position";
    } else if (!is_zero(in.cfg->entry)) {
        spot = parked_point(in, in.cfg->entry);
        from = "the building's entry point";
    } else {
        spot = player_pos() + kParkOffset;
        from = "above the door";
    }
    spot = snap_to_ground(spot);
    teleport(spot, rot);
    g_last_portal = now_ms();
    tick_fog();
    tick_snow_emitters();
    tick_snow_planes();
    logx::info("interiors: the player is back inside %s at (%.1f, %.1f, %.1f), %s", in.cfg->id, spot.x, spot.y, spot.z, from);
    release_hold("in the room");
}

// A door of a building whose room isn't built yet but which has saved contents: the game's own interior would come up
// fresh and its save would then shut the room out for good (1a's vanilla fallback), so the door waits for the room.
Instance* unready_with_data(Il2CppObject* door) {
    const std::string to = door_scene(door);
    const V3 dp = position(door_tf(door));
    for (Instance& in : g_instances) {
        if (in.stage == Stage::Ready || in.stage == Stage::Vanilla || to != in.cfg->scene) continue;
        if (dist(dp, in.cfg->fallback) > 40.0f) continue;
        ensure_dir(in);
        if (in.has_data) return &in;
    }
    return nullptr;
}

// --- hooks of 1b --------------------------------------------------------------------------------------------------------

// SI: SavePlaceablePositionsPatch (SaveGameSystem.SaveSceneData postfix), with the adoption before the game writes.
using SaveSceneFn = void (*)(Il2CppObject* slot, Il2CppString* scene, const MethodInfo* method);
SaveSceneFn g_save_scene = nullptr;
void save_scene_detour(Il2CppObject* slot, Il2CppString* scene, const MethodInfo* method) {
    const bool on = engaged() && !g_region.empty();
    if (on) {
        const fs::path dir = dir_for(game::save_id());
        if (!dir.empty()) adopt_before_save(dir);
    }
    // A11 (SI #22): the scene's save writes the REGION's junk flag, never a room's
    Il2CppObject* jm = g_junk_owner ? junk_manager() : nullptr;
    if (jm) put<bool>(jm, japi.cleared, g_region_junk);
    {
        struct Back {  // (put back however the call ends)
            Il2CppObject* jm;
            ~Back() {
                if (jm && g_junk_owner) put<bool>(jm, japi.cleared, g_junk_owner->junk_cleared);
            }
        } back{jm};
        g_save_scene(slot, scene, method);
    }
    if (on) save_rooms(scene ? il2::to_utf8(scene).c_str() : "?");
}

// SI: CommitSiFilesOnDiskSavePatch: the save reached the disk: what we staged for it becomes the real files. Only when
// the game says it did (SaveGameOperationReturnValue: ASYNC 0, BUSY 1, ERROR 2, ERROR_SILENT 3, SUCCESS 4): on 1-3
// the files stay staged (a load throws them away, as the game's save didn't happen).
using SaveDoneFn = int32_t (*)(Il2CppString* name, int32_t size, const MethodInfo* method);
SaveDoneFn g_save_done = nullptr;
int32_t save_done_detour(Il2CppString* name, int32_t size, const MethodInfo* method) {
    const int32_t r = g_save_done(name, size, method);
    if (!api.ok || !papi.save_name) return r;
    Il2CppString* current = nullptr;
    il2::il2cpp_field_static_get_value(papi.save_name, &current);
    const std::string n = name ? il2::to_utf8(name) : std::string();
    if (n.empty() || n != (current ? il2::to_utf8(current) : std::string())) return r;
    static int told = 0;
    if (told++ < 1 || (r != 0 && r != 4)) logx::info("interiors: the game's save '%s' completed: %d", n.c_str(), r);
    if (r != 0 && r != 4) {
        logx::warn("interiors: the game's save '%s' didn't reach the disk (%d); the rooms' files stay staged", n.c_str(), r);
        return r;
    }
    int failed = 0;
    if (const int c = commit_dir(data_dir(), &failed))
        logx::info("interiors: the save '%s' is on the disk: %d room file(s) committed%s", n.c_str(), c,
                   failed ? " (some couldn't be: kept staged)" : "");
    return r;
}

// SI: DiscardSiFilesOnDiskLoadPatch: a save loaded: what was staged since its last disk save never happened. A save of
// that slot still being written finishes first (its OnSaveCompleted commits what belongs to it).
using RestoreFn = void (*)(Il2CppString* name, const MethodInfo* method);
RestoreFn g_restore_game = nullptr;
void restore_game_detour(Il2CppString* name, const MethodInfo* method) {
    ++g_restore_gen;  // A8: the kept rooms belong to the play being left
    g_last_restore_ms = now_ms();
    if (api.ok) drop_kept("a save is being loaded");
    if (api.ok && name) {
        const std::string n = il2::to_utf8(name);
        bool pending = false;
        if (papi.save_pending) {
            void* a[] = {name};
            Il2CppObject* r = nullptr;
            if (call(papi.save_pending, nullptr, a, &r) && r) pending = *static_cast<const bool*>(il2::il2cpp_object_unbox(r));
            if (pending && papi.save_wait) {
                logx::info("interiors: loading '%s' while its save is still being written: waited for it first", n.c_str());
                call(papi.save_wait, nullptr, a);
                r = nullptr;
                pending = call(papi.save_pending, nullptr, a, &r) && r && *static_cast<const bool*>(il2::il2cpp_object_unbox(r));
            }
        }
        g_overlay.clear();  // an import in progress belonged to the play that's being left
        if (pending) {  // (its OnSaveCompleted, later, commits them: they are that save's)
            logx::warn("interiors: loading '%s' while its save is still being written; the rooms' staged files are kept (not "
                       "thrown away)", n.c_str());
        } else {
            int c = 0;
            for (const fs::path& d : dirs_of_slot(n)) c += discard_dir(d);
            if (c) logx::info("interiors: loading '%s': %d staged room file(s) from unsaved play thrown away", n.c_str(), c);
        }
    }
    g_restore_game(name, method);
}

// SI: SaveDeletionPatches (DeleteSaveFiles only): a deleted save's room data goes with it - moved aside to
// worlds/deleted/<folder>_<stamp> (never deleted), and never the save being played (SI: IsSaveCurrentlyBeingPlayed: the
// game rewriting the slot it plays).
RestoreFn g_delete_save = nullptr;
void delete_save_detour(Il2CppString* name, const MethodInfo* method) {
    g_delete_save(name, method);
    if (!api.ok || !name) return;
    const std::string n = il2::to_utf8(name);
    Il2CppString* current = nullptr;
    if (papi.save_name) il2::il2cpp_field_static_get_value(papi.save_name, &current);
    if (current && il2::to_utf8(current) == n && game::is_gameplay_scene(game::active_scene())) {
        logx::warn("interiors: the game deleted the files of '%s', the save being played; its room data is left alone",
                   n.c_str());
        return;
    }
    std::lock_guard lock(g_files);
    for (const fs::path& d : dirs_of_slot(n)) {
        std::error_code ec;
        const fs::path aside_dir = paths::instance_dir() / "worlds" / "deleted";
        fs::create_directories(aside_dir, ec);
        char stamp[32];
        const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmv);
        const fs::path aside = aside_dir / (d.filename().string() + "_" + stamp);
        std::error_code mec;
        fs::rename(d, aside, mec);  // (a folder: MoveFileEx's REPLACE_EXISTING can't take one)
        const bool moved = !mec;
        const std::string err = mec.message();
        logx::info("interiors: the save '%s' was deleted: its room data %s (%s)", n.c_str(),
                   moved ? ("moved aside to worlds/deleted/" + aside.filename().string()).c_str()
                         : ("left where it is: " + err).c_str(),
                   d.filename().string().c_str());
    }
}

// SI: FireManagerStealerPatch: the fire records of the region's save are kept (the rooms aren't built when they come).
using FireLoadFn = void (*)(Il2CppString* text, const MethodInfo* method);
FireLoadFn g_fire_load = nullptr;
void fire_load_detour(Il2CppString* blob, const MethodInfo* method) {
    g_fire_load(blob, method);
    if (!api.ok || !g_feature_on || !blob) return;
    const float since = game::seconds_since_level_load();
    if (since > 20.0f) return;  // only the scene load's own call (world_fires applies a friend's fires later)
    const double now = now_ms();
    if (now - g_region_fires_at > 30000) g_region_fires.clear();
    g_region_fires_at = now;
    const int n = keep_region_fires(il2::to_utf8(blob));
    logx::info("interiors: the region's save holds %d burning fire record(s); kept for the rooms", n);
}

template <typename Fn>
bool hook1b(const char* label, const MethodInfo* m, Fn detour, Fn* original) {
    if (!m) {
        logx::warn("interiors: %s not found; that part of milestone 1b is off", label);
        return false;
    }
    return hooks::install_method(label, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
}

// A class from the first of these assemblies that has it (quiet: the others are fallbacks).
Il2CppClass* class_in(std::initializer_list<const char*> dlls, const char* ns, const char* name) {
    for (const char* dll : dlls)
        if (const Il2CppImage* image = il2::find_image(dll))
            if (Il2CppClass* k = il2::il2cpp_class_from_name(image, ns, name)) return k;
    logx::warn("interiors: class %s%s%s not found", ns, *ns ? "." : "", name);
    return nullptr;
}

// --- A8: rooms kept across a region change (SI: OnSceneWasUnloaded's PERSIST + ReattachPersistedInterior) ------------
// When the game loads another scene with its loading screen (a region's edge, a game's door, not a save being loaded),
// the finished rooms of the region we leave that are closed and have their folder are moved out of the region
// (DontDestroyOnLoad) before it goes. The save the game makes on the way out writes them as usual; only a room written
// by it is kept (the others are destroyed: their files are what comes back). Coming back to that region (same save, no
// load in between) each kept room is checked (still whole: the same items, furniture, containers and fires as when it
// left), moved back into the region, put on its building again, its fires and items registered with the game again and
// its closed time moved on by the hours that passed. Anything off: destroyed and built again from its files, as without.
// Only the region last left is kept, at most kKeepRoomsMax rooms; solo and host only (a friend's rooms are the host's).
constexpr size_t kKeepRoomsMax = 6;
bool g_keep_rooms = true;  // coop.ini [interiors] keep_rooms
struct KeepApi {
    bool tried = false;
    const MethodInfo *keep = nullptr, *hours = nullptr, *gear_add = nullptr;
    FieldInfo* gear_list = nullptr;
} kapi;
void resolve_keep() {
    if (kapi.tried) return;
    kapi.tried = true;
    if (Il2CppClass* o = il2::find_class(kCore, "UnityEngine", "Object")) kapi.keep = il2::find_method(o, "DontDestroyOnLoad", 1);
    if (Il2CppClass* t = il2::find_class(kGame, "", "TimeOfDay")) kapi.hours = il2::find_method(t, "GetHoursPlayedNotPaused", 0);
    if (Il2CppClass* g = il2::find_class(kGame, "", "GearManager")) {
        kapi.gear_add = il2::find_method_params(g, "Add", {"GearItem"});
        kapi.gear_list = il2::il2cpp_class_get_field_from_name(g, "m_Gear");
    }
    if (!kapi.keep || !kapi.hours || !kapi.gear_add || !kapi.gear_list)
        logx::info("interiors: keeping rooms across regions unavailable (TLD API missing); rooms are built again");
}
bool keep_ready() {
    resolve_keep();
    return kapi.keep && kapi.hours && kapi.gear_add && kapi.gear_list;
}
double hours_played() {
    Il2CppObject* tod = il2::invoke(papi.tod_component, nullptr);
    float h = -1;
    return tod && il2::invoke_value(kapi.hours, tod, h) ? h : -1;
}
// What a room holds, in short: if Unity or the game took anything out of it while it was kept, this changes.
uint64_t room_print(Instance& in) {
    uint64_t gear = 0, ids = 0, places = 0, boxes = 0, fires = 0;
    each_in(master_of(in), api.t_gear, [&](Il2CppObject* gi) {
        ++gear;
        ids += static_cast<uint64_t>(static_cast<int64_t>(instance_id(gi)));
    });
    each_in(master_of(in), api.t_placeable, [&](Il2CppObject*) { ++places; });
    each_in(master_of(in), api.t_container, [&](Il2CppObject*) { ++boxes; });
    each_in(master_of(in), api.t_fire, [&](Il2CppObject*) { ++fires; });
    return (gear * 0x9E3779B97F4A7C15ull) ^ (ids * 0xC2B2AE3D27D4EB4Full) ^ (places << 40) ^ (boxes << 20) ^ fires;
}
void destroy_kept(Persisted& p, const char* why) {
    if (Il2CppObject* m = live(p.in.master)) destroy(m);
    logx::info("interiors: %s (kept from %s): destroyed (%s); built again from its files next time", p.in.cfg->id, p.region.c_str(), why);
    for (uintptr_t* h : {&p.in.master, &p.in.master_tf, &p.in.trigger, &p.in.space}) drop(*h);
    for (uintptr_t& h : p.in.keep_off) drop(h);
    for (uintptr_t& h : p.in.junk_off) drop(h);
    for (auto* list : {&p.in.closed_fires, &p.in.closed_pots, &p.in.closed_lamps, &p.in.closed_torches, &p.in.closed_flares,
                       &p.in.closed_evolve})
        for (uintptr_t& h : *list) drop(h);
}
void drop_kept(const char* why) {
    for (Persisted& p : g_persisted) destroy_kept(p, why);
    if (!g_persisted.empty()) rooms_changed();
    g_persisted.clear();
}

// LoadSceneWithLoadingScreen (to another scene): the rooms to keep are taken out of the region before it goes.
void persist_before_load(const std::string& to) {
    for (Instance& in : g_instances) in.persist_pending = false;
    if (!g_keep_rooms || !engaged() || g_region.empty() || coop() == Coop::Friend || to.empty() || to == g_region) return;
    if (now_ms() - g_last_restore_ms < 15000) return;  // a save being loaded: its rooms come from its files
    bool restoring = false;
    if ((japi.restore_busy && il2::invoke_value(japi.restore_busy, nullptr, restoring) && restoring)) return;
    if (!keep_ready()) return;
    const double hours = hours_played();
    if (hours < 0) return;
    std::vector<std::pair<float, Instance*>> fit;
    const V3 me = player_pos();
    for (Instance& in : g_instances) {
        Il2CppObject* m = master_of(in);
        if (!m || !can_persist(in) || in.coop_friend || in.legacy != kLegacyNone || !in.import_marker.empty() ||
            in.placeable_blocked || in.spawn_blocked || in.gear_blocked || in.container_blocked || in.state_blocked ||
            active_self(m) || g_inside == &in || g_building == &in)
            continue;
        fit.emplace_back(distance_from(in, me), &in);
    }
    std::sort(fit.begin(), fit.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (fit.size() > kKeepRoomsMax) fit.resize(kKeepRoomsMax);
    int kept = 0;
    for (auto& [d, inp] : fit) {
        Instance& in = *inp;
        Il2CppObject* m = master_of(in);
        in.persist_print = room_print(in);
        void* a[] = {m};
        il2::invoke(kapi.keep, nullptr, a);
        in.persist_pending = true;
        in.persist_hours = hours;
        in.persist_at = now_ms();
        ++kept;
    }
    if (kept) logx::info("interiors: leaving %s for %s: %d room(s) kept out of the region (put back if we come back)", g_region.c_str(),
                         to.c_str(), kept);
}

// forget_region: the kept rooms the save on the way out wrote move to g_persisted; the others go.
void keep_on_forget() {
    bool any = false;
    for (Instance& in : g_instances) any = any || in.persist_pending;
    if (!any) return;
    drop_kept("another region was left since");  // only the region last left
    const std::string save = game::save_id();
    for (Instance& in : g_instances) {
        if (!in.persist_pending) continue;
        in.persist_pending = false;
        if (!master_of(in)) {
            logx::info("interiors: %s: the room kept out of %s didn't survive the region's unload", in.cfg->id, g_region.c_str());
            continue;
        }
        if (in.last_saved_ms < in.persist_at - 5000) {  // not written on the way out: its files are older than it
            Persisted p;
            p.region = g_region;
            p.in = std::move(in);
            destroy_kept(p, "the game didn't save it on the way out");
            in = Instance{};
            in.cfg = p.in.cfg;
            continue;
        }
        Persisted p;
        p.region = g_region;
        p.save = save;
        p.restore_gen = g_restore_gen;
        // what belonged to the region goes (dead with it); the room's own objects stay
        drop(in.shell);
        for (uintptr_t& h : in.outsiders) drop(h);
        for (uintptr_t& h : in.colliders_off) drop(h);
        in.outsiders.clear();
        in.colliders_off.clear();
        drop(in.cached_lightmaps);
        drop(in.cached_probes);
        for (Instance::ContainerJob& j : in.container_jobs) drop(j.container);
        in.container_jobs.clear();
        for (auto* list : {&in.deferred_twins, &in.deferred_taken})
            for (Instance::RegionItem& r : *list) drop(r.gi);
        in.deferred_twins.clear();
        in.deferred_taken.clear();
        in.hydration_gear.clear();
        in.shell_box_cached = false;
        p.in = std::move(in);
        in = Instance{};
        in.cfg = p.in.cfg;
        g_persisted.push_back(std::move(p));
    }
    if (!g_persisted.empty()) {
        rooms_changed();
        logx::info("interiors: %zu room(s) of %s kept while we're away", g_persisted.size(), g_persisted.front().region.c_str());
    }
}

// enter_region: a kept room of this building, if it can come back whole. True: `in` is it, back in the region.
bool reattach_kept(Instance& in, const std::string& region) {
    auto it = std::find_if(g_persisted.begin(), g_persisted.end(),
                           [&](const Persisted& p) { return p.region == region && p.in.cfg == in.cfg; });
    if (it == g_persisted.end()) return false;
    Persisted p = std::move(*it);
    g_persisted.erase(it);
    rooms_changed();
    auto fail = [&](const char* why) {
        destroy_kept(p, why);
        return false;
    };
    if (!g_feature_on) return fail("Seamless Interiors is off now");
    if (!master_of(p.in)) return fail("it didn't survive");
    if (p.save != game::save_id()) return fail("another save is being played");
    if (p.restore_gen != g_restore_gen) return fail("a save was loaded since");
    if (coop() == Coop::Friend) return fail("we're a friend in co-op now (the host's rooms count)");
    if (room_print(p.in) != p.in.persist_print) return fail("something in it changed or went while it was kept");
    const int32_t h = scene_by_name(region);
    Il2CppObject* m = master_of(p.in);
    move_to_scene(m, h);
    if (!h || scene_of(m) != h) return fail("it couldn't be moved back into the region");
    in = std::move(p.in);
    Il2CppObject* shell = find_shell(in);
    in.shell = hold(shell);
    align_room(in);
    set_active(m, false);
    // the game's lists: fires, items (GearManager.Reset at the scene change)
    register_fires(in);
    int gear_added = 0;
    if (Il2CppObject* list = static_object(kapi.gear_list))
        each_in(m, api.t_gear, [&](Il2CppObject* gi) {
            if (list_contains(list, gi)) return;
            void* a[] = {gi};
            il2::invoke(kapi.gear_add, nullptr, a);
            ++gear_added;
        });
    safehouse_fix(in);
    // the hours that passed while we were away (SI: FrozenTime for a kept room)
    const double now_h = hours_played();
    const float away = now_h > in.persist_hours ? static_cast<float>((now_h - in.persist_hours) * 3600.0) : 0.0f;
    in.closed_dirty = true;
    rebuild_closed_cache(in);
    if (away > 0) advance_closed(in, away);
    in.closed_was_open = false;
    in.stage_since = now_ms();
    if (Il2CppObject* s = live(in.shell)) set_active(s, true);
    rooms_changed();
    logx::info("interiors: %s: its room kept from our last visit is back (%.1f h passed; %d item(s) back in the game's list)",
               in.cfg->id, away / 3600.0f, gear_added);
    return true;
}

void resolve_persist() {
    Il2CppClass* time = il2::find_class(kCore, "UnityEngine", "Time");
    papi.delta_time = time ? il2::find_method(time, "get_deltaTime", 0) : nullptr;
    if (Il2CppClass* gm = il2::find_class(kGame, "", "GameManager")) papi.tod_component = il2::find_method(gm, "GetTimeOfDayComponent", 0);
    if (Il2CppClass* tod = il2::find_class(kGame, "", "TimeOfDay")) {
        papi.tod_seconds = il2::find_method_params(tod, "GetTODSeconds", {"System.Single"});
        papi.hours_played = il2::find_method(tod, "GetHoursPlayedNotPaused", 0);
    }
    if (Il2CppClass* gear = il2::find_class(kGame, "", "GearItem")) {
        papi.gear_to_string = il2::find_method(gear, "SerializeToString", 0);
        papi.gear_instantiate = il2::find_method_params(
            gear, "InstantiateAndDeserializeGearItem",
            {"System.String", "System.String", "UnityEngine.Transform", "System.Boolean", "System.Boolean"});
    }
    if (Il2CppClass* c = il2::find_class(kGame, "", "Container")) {
        papi.container_save = il2::find_method(c, "Serialize", 0);
        papi.container_load = il2::find_method(c, "Deserialize", 2);
        papi.container_clear = il2::find_method(c, "DestroyAllGear", 0);
    }
    Il2CppClass* placeable = il2::find_class(kGame, "TLD.Placement", "Placeable");
    if (placeable) {
        papi.placeable_save = il2::find_method(placeable, "Serialize", 0);
        papi.placeable_create = il2::find_method(placeable, "FindOrCreateAndDeserialize", 2);
        papi.placeable_guid = il2::field_offset(placeable, "m_Guid");
        if (FieldInfo* f = il2::il2cpp_class_get_field_from_name(placeable, "SPAWNED_NAME_SUFFIX")) {
            Il2CppString* v = nullptr;
            il2::il2cpp_field_static_get_value(f, &v);
            if (v && !il2::to_utf8(v).empty()) papi.spawned_suffix = il2::to_utf8(v);
        }
    }
    if (Il2CppClass* pm = il2::find_class(kGame, "TLD.Placement", "PlaceableManager")) {
        papi.placeable_remove = il2::find_method(pm, "Remove", 1);
        papi.placeables_loading = il2::il2cpp_class_get_field_from_name(pm, "s_IsDeserializing");
        if (FieldInfo* f = il2::il2cpp_class_get_field_from_name(pm, "CATEGORY_NAME")) {
            Il2CppString* v = nullptr;
            il2::il2cpp_field_static_get_value(f, &v);
            if (v && !il2::to_utf8(v).empty()) papi.placement_root = il2::to_utf8(v);
        }
    }
    if (Il2CppClass* data = il2::find_class(kGame, "TLD.Placement", "PlaceableSaveData")) {
        papi.t_placeable_data = il2::type_object(data);
        papi.data_position = il2::field_offset(data, "m_Position");
        papi.data_rotation = il2::field_offset(data, "m_Rotation");
    }
    if (Il2CppClass* u = il2::find_class(kGame, "", "Utils")) papi.utils_serialize = il2::find_method_params(u, "SerializeObject", {"System.Object"});
    // (SerializationUtils and FastJson live in TLD.Serialization.dll; looked for in Assembly-CSharp too, in case)
    if (Il2CppClass* su = class_in({"TLD.Serialization.dll", kGame}, "TLD.Serialization", "SerializationUtils")) {
        papi.use_json_net = il2::il2cpp_class_get_field_from_name(su, "s_UseJsonNet");
        papi.json_settings = il2::il2cpp_class_get_field_from_name(su, "m_SerializerSettings");
    }
    if (Il2CppClass* jc = il2::find_class("Newtonsoft.Json.dll", "Newtonsoft.Json", "JsonConvert"))
        papi.json_deserialize = il2::find_method_params(jc, "DeserializeObject",
                                                        {"System.String", "System.Type", "Newtonsoft.Json.JsonSerializerSettings"});
    if (Il2CppClass* fj = class_in({"TLD.Serialization.dll", kGame}, "", "FastJson"))
        papi.fast_deserialize = il2::find_method_params(fj, "Deserialize", {"System.String", "System.Type"});
    if (Il2CppClass* pdid = il2::find_class("TLD.PDID.dll", "TLD.PDID", "PdidObjectBase")) {
        papi.pdid_get = il2::find_method(pdid, "GetPDID", 0);
        papi.pdid_registered = il2::find_method(pdid, "get_RuntimeRegistered", 0);
    }
    if (Il2CppClass* table = il2::find_class("TLD.PDID.dll", "TLD.PDID", "PdidTable"))
        papi.pdid_replace = il2::find_method(table, "RuntimeAddOrReplace", 2);
    if (Il2CppClass* f = il2::find_class(kGame, "", "Fire")) {
        papi.fire_save = il2::find_method(f, "Serialize", 0);
        papi.fire_load = il2::find_method_params(f, "Deserialize", {"System.String"});
        papi.fire_burning = il2::find_method(f, "IsBurning", 0);
        papi.fire_left = il2::find_method(f, "GetRemainingLifeTimeSeconds", 0);
        papi.fire_elapsed = il2::field_offset(f, "m_ElapsedOnTODSeconds");
        papi.fire_elapsed_raw = il2::field_offset(f, "m_ElapsedOnTODSecondsUnmodified");
        papi.fire_hours = il2::field_offset(f, "m_BurningTimeTODHours");
        papi.fire_embers = il2::field_offset(f, "m_UseEmbers");
        papi.fire_ember_len = il2::field_offset(f, "m_EmberDurationSecondsTOD");
        papi.fire_ember_timer = il2::field_offset(f, "m_EmberTimer");
    }
    if (Il2CppClass* fm = il2::find_class(kGame, "", "FireManager"))
        papi.fire_remove = il2::find_method_params(fm, "RemoveFire", {"Fire"});
    if (Il2CppClass* hs = il2::find_class(kGame, "", "HeatSource")) papi.heat_update = il2::find_method(hs, "Update", 0);
    if (Il2CppClass* pot = il2::find_class(kGame, "", "CookingPotItem")) {
        papi.t_pot = il2::type_object(pot);
        papi.pot_cooking = il2::find_method(pot, "IsCookingSomething", 0);
        papi.pot_fire_burning = il2::find_method(pot, "AttachedFireIsBurning", 0);
        papi.pot_fire = il2::find_method(pot, "GetFireBeingUsed", 0);
        papi.pot_state = il2::field_offset(pot, "m_CookingState");
        papi.pot_elapsed = il2::field_offset(pot, "m_CookingElapsedHours");
        papi.pot_until_cooked = il2::field_offset(pot, "m_MinutesUntilCooked");
        papi.pot_until_ruined = il2::field_offset(pot, "m_MinutesUntilRuined");
        papi.pot_grace = il2::field_offset(pot, "m_GracePeriodElapsedHours");
        int64_t v = -1;
        if (Il2CppClass* st = il2::find_nested_class(pot, "CookingState"); st && il2::enum_value(st, "Ruined", v))
            papi.pot_ruined = static_cast<int32_t>(v);
    }
    if (Il2CppClass* lamp = il2::find_class(kGame, "TLD.Gear", "KeroseneLampItem")) {
        papi.t_lamp = il2::type_object(lamp);
        papi.lamp_on = il2::find_method(lamp, "IsOn", 0);
        papi.lamp_reduce = il2::find_method_params(lamp, "ReduceFuel", {"System.Single"});
    }
    if (Il2CppClass* torch = il2::find_class(kGame, "", "TorchItem")) {
        papi.t_torch = il2::type_object(torch);
        papi.torch_burning = il2::find_method(torch, "IsBurning", 0);
        papi.torch_life = il2::find_method(torch, "GetModifiedBurnLifetimeMinutes", 0);
        papi.torch_elapsed = il2::field_offset(torch, "m_ElapsedBurnMinutes");
    }
    if (Il2CppClass* flare = il2::find_class(kGame, "", "FlareItem")) {
        papi.t_flare = il2::type_object(flare);
        papi.flare_burning = il2::find_method(flare, "IsBurning", 0);
        papi.flare_life = il2::find_method(flare, "GetModifiedBurnLifetimeMinutes", 0);
        papi.flare_elapsed = il2::field_offset(flare, "m_ElapsedBurnMinutes");
    }
    if (Il2CppClass* ev = il2::find_class(kGame, "", "EvolveItem")) {
        papi.t_evolve = il2::type_object(ev);
        papi.evolve_can = il2::find_method(ev, "CanEvolve", 0);
        papi.evolve_spent = il2::field_offset(ev, "m_TimeSpentEvolvingGameHours");
        papi.evolve_days = il2::field_offset(ev, "m_TimeToEvolveGameDays");
    }
    for (auto& k : papi.kinds) {
        Il2CppClass* c = il2::find_class(kGame, "", k.cls);
        if (!c) continue;
        k.type = il2::type_object(c);
        k.save = il2::find_method(c, "Serialize", 0);
        k.load = il2::find_method_params(c, "Deserialize", {"System.String"});
        if (std::strcmp(k.tag, "BD") == 0) papi.breakdown_post_load = il2::field_offset(c, "m_PostLoadBreakdown");
    }
    if (Il2CppClass* cs = il2::find_class(kGame, "", "CuringStorage")) {
        papi.t_curing = il2::type_object(cs);
        papi.curing_save = il2::find_method(cs, "SerializeCuringStorage", 0);
        papi.curing_load = il2::find_method_params(cs, "DeserializeCuringStorage", {"CuringStorageState"});
        papi.curing_state = il2::field_offset(cs, "m_State");
    }
    if (Il2CppClass* st = il2::find_class(kGame, "", "CuringStorageState")) {
        papi.t_curing_state = il2::type_object(st);
        papi.curing_guid = il2::field_offset(st, "m_GUID");
        papi.curing_items = il2::field_offset(st, "m_CuringItems");
    }
    if (!papi.curing_save || !papi.curing_load || !papi.t_curing_state || papi.curing_guid < 0)
        logx::warn("interiors: CuringStorage calls missing: a curing box in a room comes back empty");
    Il2CppClass* pmc = il2::find_class(kGame, "", "PlayerManager");
    papi.pm_save_position = pmc ? il2::field_offset(pmc, "m_SaveGamePosition") : -1;
    papi.pm_object_to_place = pmc ? il2::field_offset(pmc, "m_ObjectToPlace") : -1;
    Il2CppClass* saves = il2::find_class(kGame, "", "SaveGameSystem");
    papi.save_name = saves ? il2::il2cpp_class_get_field_from_name(saves, "m_CurrentSaveName") : nullptr;
    if (saves) {
        papi.save_pending = il2::find_method_params(saves, "IsAsyncSavePending", {"System.String"});
        papi.save_wait = il2::find_method_params(saves, "WaitAsyncSaveComplete", {"System.String"});
    }

    struct Need {
        const char* what;
        bool ok;
    };
    const Need needs[] = {
        {"item save calls (GearItem.SerializeToString / InstantiateAndDeserializeGearItem)",
         papi.gear_to_string && papi.gear_instantiate},
        {"container save calls", papi.container_save && papi.container_load && papi.container_clear},
        {"placeable calls", papi.placeable_save && papi.placeable_create && papi.placeable_remove && papi.placeable_guid >= 0 &&
                                papi.data_position >= 0 && papi.data_rotation >= 0 && papi.utils_serialize &&
                                (papi.json_deserialize || papi.fast_deserialize)},
        {"fire calls", papi.fire_save && papi.fire_load && papi.fire_burning && papi.fire_left},
        {"the save's name", papi.save_name != nullptr},
    };
    bool ok = true;
    for (const Need& n : needs)
        if (!n.ok) {
            logx::warn("interiors: %s are missing in this build; rooms are not saved (milestone 1b off)", n.what);
            ok = false;
        }
    if (!papi.tod_seconds || !papi.delta_time) logx::warn("interiors: TimeOfDay.GetTODSeconds missing: closed rooms' time stands still");
    if (!papi.pdid_replace || !papi.pdid_registered) logx::info("interiors: PDID calls missing: runtime placeable ids aren't made stable");
    for (auto& k : papi.kinds)
        if (!k.save || !k.load) logx::info("interiors: %s.Serialize/Deserialize missing: its state isn't kept", k.cls);
    if (!papi.use_json_net || !papi.json_settings)
        logx::info("interiors: SerializationUtils' settings not found; the player's furniture is read back with FastJson");
    papi.ok = ok;
    if (!ok) return;

    if (saves) {
        hook1b("SaveGameSystem.SaveSceneData",
               il2::find_method_params(saves, "SaveSceneData", {"SlotData", "System.String"}), &save_scene_detour, &g_save_scene);
        hook1b("SaveGameSystem.OnSaveCompleted", il2::find_method_params(saves, "OnSaveCompleted", {"System.String", "System.Int32"}),
               &save_done_detour, &g_save_done);
        hook1b("SaveGameSystem.RestoreGame", il2::find_method_params(saves, "RestoreGame", {"System.String"}),
               &restore_game_detour, &g_restore_game);
        hook1b("SaveGameSystem.DeleteSaveFiles", il2::find_method_params(saves, "DeleteSaveFiles", {"System.String"}),
               &delete_save_detour, &g_delete_save);
    }
    resolve_junk();  // A11
    if (Il2CppClass* fm = il2::find_class(kGame, "", "FireManager"))
        hook1b("FireManager.Deserialize", il2::find_method_params(fm, "Deserialize", {"System.String"}), &fire_load_detour, &g_fire_load);
    papi.ok = g_save_scene != nullptr;
    logx::info("interiors: milestone 1b (rooms saved with the game) %s; spawned-name suffix '%s', placement root '%s'",
               papi.ok ? "ready" : "OFF (SaveSceneData couldn't be hooked)", papi.spawned_suffix.c_str(),
               papi.placement_root.c_str());
}
