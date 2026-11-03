// TLDCoop_DLC (v45): TLD's spawn regions under wildlife ownership (stages 2/3). Included by wildlife_sync.cpp inside
// its anonymous namespace, before wildlife_owner.inc (which uses these).
//
// A spawn region keeps its own count of its animals (SpawnRegion.m_Spawns, the active ones are its population) and
// of the dead ones still to come back (m_NumRespawnsPending, each after GetNumHoursBetweenRespawns hours):
// AdjustActiveSpawnRegionPopulation spawns (or switches back on an inactive one of its own) whenever fewer are active
// than CalculateTargetPopulation (its maximum minus the trapped and the pending ones). With animals run by several
// games, the game that spawns in a scene (its "controller") would see too few:
//   - animals it handed to another game are switched off here: it spawned new ones in their place (or switched the
//     handed one back on: the same animal twice);
//   - animals that died in another game never came off its count (no respawn wait: killed animals came back at once).
// So the controller counts, per region, the animals of it alive in other games (handed away, by id) and its own
// switched-off ones kept for handovers (population_detour), a handed animal leaves its region's list (it can't be
// switched back on behind our back), and every death anywhere is told to the controller (Kill), which waits for the
// respawn as TLD would have. A region is known everywhere by where it stands (region_key): the same in every game.
// The respawn state itself (pending, waits, trapped) goes from one controller to the next through the host (Regions),
// and with the host's save.

double now_ms();
std::unordered_map<uint32_t, uintptr_t> g_region_keys;  // this scene's spawn regions by key (gchandles)
double g_region_keys_ms = -1e18;
std::unordered_map<uintptr_t, int32_t> g_pop_extra;  // controller: SpawnRegion -> animals of it counted though not active here
int g_pop_logged = 0;

double game_hours() {
    if (!api.tod || !api.hours_played) return -1;
    Il2CppObject* tod = il2::invoke(api.tod, nullptr);
    float h = -1;
    return tod && il2::invoke_value(api.hours_played, tod, h) && std::isfinite(h) ? h : -1;
}

Il2CppObject* region_of(Il2CppObject* ai) {
    if (!ai || api.ai_region < 0) return nullptr;
    Il2CppObject* r = il2::read_field<Il2CppObject*>(ai, api.ai_region);
    return r && alive(r) ? r : nullptr;
}

// Where it stands (to 0.5 m) and what it spawns: the same in every game playing the scene.
uint32_t region_key(Il2CppObject* region) {
    if (!region) return 0;
    Vector3 p{};
    if (!il2::invoke_value(api.get_position, transform_of(region), p)) return 0;
    const int32_t v[4] = {static_cast<int32_t>(std::lround(p.x * 2)), static_cast<int32_t>(std::lround(p.y * 2)),
                          static_cast<int32_t>(std::lround(p.z * 2)), il2::read_field<int32_t>(region, api.region_kind)};
    uint32_t h = 2166136261u;
    for (int32_t x : v)
        for (int b = 0; b < 4; ++b) {
            h ^= static_cast<uint8_t>(static_cast<uint32_t>(x) >> (8 * b));
            h *= 16777619u;
        }
    return h ? h : 1;
}

std::vector<Il2CppObject*> scene_regions() {
    Il2CppObject* manager = api.spawn_manager ? il2::invoke(api.spawn_manager, nullptr) : nullptr;
    Il2CppObject* list = manager ? il2::read_field<Il2CppObject*>(manager, api.spawn_regions) : nullptr;
    return list ? list_items(list) : std::vector<Il2CppObject*>{};
}

void forget_region_keys() {
    for (auto& [k, h] : g_region_keys) drop(h);
    g_region_keys.clear();
    g_region_keys_ms = -1e18;
}

Il2CppObject* region_by_key(uint32_t key, double now) {
    if (!key) return nullptr;
    auto it = g_region_keys.find(key);
    if (it != g_region_keys.end()) {
        if (Il2CppObject* r = target(it->second); r && alive(r)) return r;
    }
    if (now - g_region_keys_ms < 3000) return nullptr;  // (looked them all up a moment ago)
    forget_region_keys();
    g_region_keys_ms = now;
    for (Il2CppObject* r : scene_regions())
        if (const uint32_t k = region_key(r)) g_region_keys.emplace(k, hold(r));
    it = g_region_keys.find(key);
    return it == g_region_keys.end() ? nullptr : target(it->second);
}

template <typename T>
void put_field(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(v));
}

// Its respawn state, times relative to now (game hours).
net::WildRegionState read_region(Il2CppObject* r, uint32_t key, double now_h) {
    net::WildRegionState g;
    g.key = key;
    if (api.region_pending >= 0) g.pending = il2::read_field<int32_t>(r, api.region_pending);
    if (api.region_trapped >= 0) g.trapped = il2::read_field<int32_t>(r, api.region_trapped);
    auto rel = [&](ptrdiff_t off) {
        if (off < 0) return 0.0f;
        const float v = il2::read_field<float>(r, off);
        const float d = std::isfinite(v) ? static_cast<float>(v - now_h) : 0.0f;
        return std::clamp(d, -1.0e5f, 1.0e5f);
    };
    g.next_h = rel(api.region_next);
    g.trap_reset_h = rel(api.region_trap_reset);
    g.cooldown_h = rel(api.region_cooldown);
    return g;
}

void apply_region(Il2CppObject* r, const net::WildRegionState& g, double now_h) {
    put_field<int32_t>(r, api.region_pending, std::clamp(g.pending, 0, 64));
    put_field<int32_t>(r, api.region_trapped, std::clamp(g.trapped, 0, 64));
    put_field<float>(r, api.region_next, static_cast<float>(now_h + g.next_h));
    put_field<float>(r, api.region_trap_reset, static_cast<float>(now_h + g.trap_reset_h));
    put_field<float>(r, api.region_cooldown, static_cast<float>(now_h + g.cooldown_h));
}

// What TLD's RemoveFromSpawnRegion does for a dead animal, for one that died in another game: one more to come back,
// after the region's wait.
void count_death(Il2CppObject* r, double now_h) {
    if (!r || api.region_pending < 0) return;
    put_field<int32_t>(r, api.region_pending, il2::read_field<int32_t>(r, api.region_pending) + 1);
    float between = 0;
    if (api.region_next >= 0 && now_h >= 0 && api.region_between && il2::invoke_value(api.region_between, r, between) &&
        std::isfinite(between))
        put_field<float>(r, api.region_next, static_cast<float>(now_h + between));
}

// Our real animal handed to another game: out of its region's list (TLD can't switch it back on, it doesn't count
// as dead either: RemoveFromSpawnRegion's count and wait are put back as they were).
std::unordered_set<uintptr_t> g_detached;  // GameObjects taken out of their region's list (they stay out)
void detach_from_region(Il2CppObject* ai) {
    Il2CppObject* r = region_of(ai);
    Il2CppObject* go = go_of(ai);
    if (!r || !go || !api.region_remove || g_detached.count(reinterpret_cast<uintptr_t>(go))) return;
    const int32_t pending = api.region_pending >= 0 ? il2::read_field<int32_t>(r, api.region_pending) : 0;
    const float next = api.region_next >= 0 ? il2::read_field<float>(r, api.region_next) : 0;
    void* args[] = {ai};
    il2::invoke(api.region_remove, r, args);
    put_field<int32_t>(r, api.region_pending, pending);
    put_field<float>(r, api.region_next, next);
    g_detached.insert(reinterpret_cast<uintptr_t>(go));
}

// SpawnRegion.GetCurrentActivePopulation, on the game that spawns in the scene: the region's animals alive elsewhere
// (or kept switched off here for handovers) count as active, up to its target (never more: TLD would switch off ours).
using PopulationFn = int32_t (*)(Il2CppObject* self, int32_t mode, const MethodInfo* method);
PopulationFn g_population = nullptr;
// TLDCoop_DLC: TLD's CalculateTargetPopulation asks GetCurrentActivePopulation itself, so asking it from here went
// round and round (logged at 48 deep; before the loop guard: the stack overflow crashes right after animal handovers,
// the first moment a region counts animals elsewhere). Inside our own ask, TLD's count as it is.
thread_local bool g_pop_asking = false;
int32_t population_detour(Il2CppObject* self, int32_t mode, const MethodInfo* method) {
    const int32_t v = g_population(self, mode, method);
    if (g_pop_asking || g_pop_extra.empty() || !self) return v;
    const auto it = g_pop_extra.find(reinterpret_cast<uintptr_t>(self));
    if (it == g_pop_extra.end() || it->second <= 0) return v;
    if (api.region_mode >= 0 && il2::read_field<int32_t>(self, api.region_mode) != mode) return v;
    int32_t want = 0;
    g_pop_asking = true;
    const bool got = il2::invoke_value(api.region_target, self, want);
    g_pop_asking = false;
    if (!got) return v;
    const int32_t out = std::max(v, std::min(v + it->second, want));
    if (out != v && g_pop_logged++ < 20)
        logx::info("wildlife: a spawn region counts %d animal(s) of it alive in another game or kept for handovers: %d "
                   "active, %d counted (target %d)", it->second, v, out, want);
    return out;
}
