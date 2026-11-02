// TLDCoop_DLC: Seamless Interiors, B3: exterior fire effects (SI: ExteriorFireEffects.cs), chimney smoke and window glow.
// Included by seamless_interiors.cpp (inside its anonymous namespace), after seamless_auto.inc.
//
// CHIMNEY SMOKE (SI). The shells carry a Chimney with a SmokeTrail; Chimney.Update runs the smoke itself while its
// m_LifetimeGameMinutes counts down (fading over the last half hour). Vanilla hands a stove's remaining burn time to its
// chimney only when the player walks out through a loading screen, which our doors never show. So, once a second, the
// remaining time of the longest-burning fire behind each chimney (the stove's m_ChimneyGuid, Chimney.FindByGuid; else
// the building's nearest chimney) is written there. A chimney whose fire went out is left at most half an hour, so the
// smoke fades as it does in the game.
//
// WINDOW GLOW (SI). From dusk to dawn, while a fire burns inside, the shell's window glass (STR_WindowGlassMix*, not the
// see-through "Alpha" panes) goes from its grey to TodMaterial's warm night orange and its emissive strength comes up -
// through a MaterialPropertyBlock per glass slot: no material is copied or changed, an empty block puts it back. A slight
// firelight flicker only within 150 m. (SI's spot lights on the snow are left out: per-shell offline window data.)
//
// Closed rooms count (their fires burn on: tick_closed_time), open ones too. In co-op every game reads its own copy of
// the room: a friend's room has the host's fires (their records are synced by the room's co-op ids), so a friend sees
// the host's stove from outside when that friend's game has the room built (within 80 m of a player of it).
// Cost: fires, chimneys and glass found once per building (one building per frame), fires looked up again every 60 s,
// burn times read once a second, the glass written at most 10 times a second and only while it glows. Nothing is saved.
// coop.ini [interiors] fire_effects=0 turns it off.

constexpr float kGlowNightR = 1.0f, kGlowNightG = 0.5306f, kGlowNightB = 0.1176f;  // SI: TodMaterial's night colour
constexpr float kGlowNightEmissive = (kGlowNightR + kGlowNightG + kGlowNightB) / 3.0f;
constexpr float kGlowFireFadeMinutes = 30.0f;  // a dying fire dims the windows over its last half hour
constexpr float kGlowFadeSeconds = 2.5f;
constexpr double kGlowApplyMs = 100;
constexpr float kGlowFlickerAmplitude = 0.07f, kGlowFlickerDistance = 150.0f;
constexpr double kFxRescanMs = 60000;
constexpr float kChimneyWindDown = 30.0f;  // minutes left on a chimney whose fire went out

bool g_fire_fx = true;  // coop.ini [interiors] fire_effects

struct FxApi {
    bool ok = false, glow_ok = false;
    Il2CppObject* t_chimney = nullptr;
    ptrdiff_t chimney_minutes = -1, chimney_trail = -1, fire_chimney_guid = -1;
    const MethodInfo* chimney_find = nullptr;  // Chimney.FindByGuid(string)
    const MethodInfo* get_tod = nullptr, *is_dusk = nullptr, *dusk_progress = nullptr, *is_dawn = nullptr,
                      *dawn_progress = nullptr, *is_night = nullptr;
    const MethodInfo* mat_has = nullptr, *mat_color = nullptr, *mat_float = nullptr, *mat_texture = nullptr,
                      *mat_main_texture = nullptr;
    Il2CppClass* mpb_class = nullptr;
    const MethodInfo* mpb_ctor = nullptr, *mpb_clear = nullptr, *mpb_color = nullptr, *mpb_float = nullptr,
                      *mpb_texture = nullptr, *r_set_block = nullptr;
    int32_t id_color = -1, id_emissive_strength = -1, id_emissive = -1;
} fxapi;

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};

struct FireFx {
    uintptr_t shell = 0;  // the shell it was made for
    std::vector<uintptr_t> chimneys;
    std::vector<float> chimney_minutes;
    std::vector<char> chimney_fed;  // a fire of the room was behind it at the last tick
    std::vector<uintptr_t> fires;
    std::vector<int> fire_chimney;  // index into chimneys, -1 none
    struct Slot {
        uintptr_t renderer = 0, block = 0, texture = 0;  // texture: the glass's own, for glass without an emissive one
        int32_t index = 0;
        Rgba base;
        float base_emissive = 0;
    };
    std::vector<Slot> windows;
    double next_rescan = 0;
    float target = 0, level = 0, applied_level = 0, seed = 0;
    bool applied = false, failed = false, logged = false, shared = false;
    double last_update = -1, next_apply = 0;
    ~FireFx() {
        drop(shell);
        for (uintptr_t& h : chimneys) drop(h);
        for (uintptr_t& h : fires) drop(h);
        for (Slot& s : windows) {
            drop(s.renderer);
            drop(s.block);
            drop(s.texture);
        }
    }
};
float g_fx_night = 0;

void resolve_fx() {
    Il2CppClass* chimney = il2::find_class(kGame, "", "Chimney");
    Il2CppClass* fire = il2::find_class(kGame, "", "Fire");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* tod = il2::find_class(kGame, "", "TimeOfDay");
    if (!chimney || !fire || !gm || !tod) {
        logx::warn("interiors: Chimney/Fire/TimeOfDay missing; no chimney smoke or window glow from the rooms' fires");
        return;
    }
    fxapi.t_chimney = il2::type_object(chimney);
    fxapi.chimney_minutes = il2::field_offset(chimney, "m_LifetimeGameMinutes");
    fxapi.chimney_trail = il2::field_offset(chimney, "m_SmokeTrail");
    fxapi.chimney_find = il2::find_method_params(chimney, "FindByGuid", {"System.String"});
    fxapi.fire_chimney_guid = il2::field_offset(fire, "m_ChimneyGuid");
    fxapi.get_tod = il2::find_method(gm, "GetTimeOfDayComponent", 0);
    fxapi.is_dusk = il2::find_method(tod, "IsDusk", 0);
    fxapi.dusk_progress = il2::find_method(tod, "GetProgressDusk", 0);
    fxapi.is_dawn = il2::find_method(tod, "IsDawn", 0);
    fxapi.dawn_progress = il2::find_method(tod, "GetProgressDawn", 0);
    fxapi.is_night = il2::find_method(tod, "IsNight", 0);
    fxapi.ok = fxapi.t_chimney && fxapi.chimney_minutes >= 0 && papi.fire_burning && papi.fire_left;
    if (Il2CppClass* mat = il2::find_class(kCore, "UnityEngine", "Material")) {
        fxapi.mat_has = il2::find_method_params(mat, "HasProperty", {"System.Int32"});
        fxapi.mat_color = il2::find_method_params(mat, "GetColor", {"System.Int32"});
        fxapi.mat_float = il2::find_method_params(mat, "GetFloat", {"System.Int32"});
        fxapi.mat_texture = il2::find_method_params(mat, "GetTexture", {"System.Int32"});
        fxapi.mat_main_texture = il2::find_method(mat, "get_mainTexture", 0);
    }
    fxapi.mpb_class = il2::find_class(kCore, "UnityEngine", "MaterialPropertyBlock");
    if (fxapi.mpb_class) {
        fxapi.mpb_ctor = il2::find_method(fxapi.mpb_class, ".ctor", 0);
        fxapi.mpb_clear = il2::find_method(fxapi.mpb_class, "Clear", 0);
        fxapi.mpb_color = il2::find_method_params(fxapi.mpb_class, "SetColor", {"System.Int32", "UnityEngine.Color"});
        fxapi.mpb_float = il2::find_method_params(fxapi.mpb_class, "SetFloat", {"System.Int32", "System.Single"});
        fxapi.mpb_texture = il2::find_method_params(fxapi.mpb_class, "SetTexture", {"System.Int32", "UnityEngine.Texture"});
    }
    if (Il2CppClass* r = il2::find_class(kCore, "UnityEngine", "Renderer"))
        fxapi.r_set_block = il2::find_method_params(r, "SetPropertyBlock", {"UnityEngine.MaterialPropertyBlock", "System.Int32"});
    if (Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader"))
        if (const MethodInfo* to_id = il2::find_method_params(shader, "PropertyToID", {"System.String"})) {
            void* a1[] = {str("_Color")};
            il2::invoke_value(to_id, nullptr, fxapi.id_color, a1);
            void* a2[] = {str("_EmissiveStrength")};
            il2::invoke_value(to_id, nullptr, fxapi.id_emissive_strength, a2);
            void* a3[] = {str("_Emissive")};
            il2::invoke_value(to_id, nullptr, fxapi.id_emissive, a3);
        }
    fxapi.glow_ok = fxapi.get_tod && fxapi.is_dusk && fxapi.dusk_progress && fxapi.is_dawn && fxapi.dawn_progress &&
                    fxapi.is_night && fxapi.mat_has && fxapi.mat_color && fxapi.mat_float && fxapi.mpb_class && fxapi.mpb_ctor &&
                    fxapi.mpb_clear && fxapi.mpb_color && fxapi.mpb_float && fxapi.r_set_block && api.r_materials &&
                    fxapi.id_color != -1 && fxapi.id_emissive_strength != -1;
    if (!fxapi.ok) logx::warn("interiors: the chimneys can't be read; no chimney smoke or window glow from the rooms' fires");
    else if (!fxapi.glow_ok) logx::warn("interiors: window glow calls missing; the rooms' fires give chimney smoke only");
}

float fx_clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// 0 in daylight, 1 at night: TodMaterial's curve, in over dusk, out over dawn.
float night_factor() {
    Il2CppObject* tod = il2::invoke(fxapi.get_tod, nullptr);
    if (!alive(tod)) return 0;
    if (call_bool(fxapi.is_dusk, tod)) return fx_clamp01(call_float(fxapi.dusk_progress, tod));
    if (call_bool(fxapi.is_dawn, tod)) return 1.0f - fx_clamp01(call_float(fxapi.dawn_progress, tod));
    return call_bool(fxapi.is_night, tod) ? 1.0f : 0.0f;
}

V3 chimney_smoke_at(Il2CppObject* chimney) {  // where the smoke leaves the roof: the trail's own spot
    Il2CppObject* trail = fxapi.chimney_trail >= 0 ? get<Il2CppObject*>(chimney, fxapi.chimney_trail) : nullptr;
    return alive(trail) ? position(tf_of(trail)) : position(tf_of(chimney));
}

void rescan_fx_fires(Instance& in, FireFx& fx, double now) {
    fx.next_rescan = now + kFxRescanMs;
    for (uintptr_t& h : fx.fires) drop(h);
    fx.fires.clear();
    fx.fire_chimney.clear();
    int via_guid = 0, via_near = 0;
    each_in(master_of(in), api.t_fire, [&](Il2CppObject* f) {
        int chimney = -1;
        if (!fx.chimneys.empty()) {
            const std::string guid = fxapi.fire_chimney_guid >= 0 ? text(get<Il2CppObject*>(f, fxapi.fire_chimney_guid)) : "";
            if (!guid.empty() && fxapi.chimney_find) {
                void* a[] = {str(guid.c_str())};
                Il2CppObject* c = il2::invoke(fxapi.chimney_find, nullptr, a);
                for (size_t i = 0; c && i < fx.chimneys.size(); ++i)
                    if (live(fx.chimneys[i]) == c) chimney = static_cast<int>(i);
            }
            if (chimney >= 0) {
                ++via_guid;
            } else {  // a stove without its own chimney here: the building's nearest one
                const V3 fp = position(tf_of(f)) - kParkOffset;
                float best = 1e30f;
                for (size_t i = 0; i < fx.chimneys.size(); ++i)
                    if (Il2CppObject* c = live(fx.chimneys[i])) {
                        const float d = dist_xz(chimney_smoke_at(c), fp);
                        if (d < best) {
                            best = d;
                            chimney = static_cast<int>(i);
                        }
                    }
                if (chimney >= 0) ++via_near;
            }
        }
        fx.fires.push_back(hold(f));
        fx.fire_chimney.push_back(chimney);
    });
    if (!fx.logged && !fx.fires.empty()) {
        fx.logged = true;
        logx::info("interiors: %s: exterior fire effects: %zu fire(s), %zu chimney(s) (%d fire(s) by their chimney's guid, %d "
                   "by the nearest), %zu window glass slot(s)%s",
                   in.cfg->id, fx.fires.size(), fx.chimneys.size(), via_guid, via_near, fx.windows.size(),
                   fx.shared ? " (its shell's effects are another building's)" : "");
    }
}

void collect_glass(FireFx& fx, Il2CppObject* shell) {
    if (!fxapi.glow_ok) return;
    each_in(shell, api.t_mesh_renderer, [&](Il2CppObject* r) {
        Il2CppObject* mats = il2::invoke(api.r_materials, r);
        for (size_t i = 0, n = il2::array_length(mats); i < n; ++i) {
            Il2CppObject* mat = il2::array_object_at(mats, i);
            if (!alive(mat)) continue;
            const std::string nm = name_of(mat);
            if (nm.rfind("STR_WindowGlassMix", 0) != 0 || nm.find("Alpha") != std::string::npos) continue;
            int32_t id = fxapi.id_color;
            void* ha[] = {&id};
            bool has_color = false, has_strength = false;
            il2::invoke_value(fxapi.mat_has, mat, has_color, ha);
            id = fxapi.id_emissive_strength;
            il2::invoke_value(fxapi.mat_has, mat, has_strength, ha);
            if (!has_color || !has_strength) continue;
            FireFx::Slot s;
            s.index = static_cast<int32_t>(i);
            id = fxapi.id_color;
            il2::invoke_value(fxapi.mat_color, mat, s.base, ha);
            id = fxapi.id_emissive_strength;
            il2::invoke_value(fxapi.mat_float, mat, s.base_emissive, ha);
            // glass shipped without an emissive texture (the camp office's, the farmhouse's) gets its own one there
            if (fxapi.id_emissive != -1 && fxapi.mat_texture && fxapi.mpb_texture) {
                id = fxapi.id_emissive;
                bool has_tex = false;
                il2::invoke_value(fxapi.mat_has, mat, has_tex, ha);
                if (has_tex && !alive(il2::invoke(fxapi.mat_texture, mat, ha)) && fxapi.mat_main_texture)
                    if (Il2CppObject* main = il2::invoke(fxapi.mat_main_texture, mat); alive(main)) s.texture = hold(main);
            }
            Il2CppObject* block = il2::il2cpp_object_new(fxapi.mpb_class);
            if (!block) continue;
            il2::invoke(fxapi.mpb_ctor, block);
            s.block = hold(block);
            s.renderer = hold(r);
            fx.windows.push_back(s);
        }
    });
}

std::shared_ptr<FireFx> build_fx(Instance& in, Il2CppObject* shell) {
    auto fx = std::make_shared<FireFx>();
    fx->shell = hold(shell);
    fx->seed = static_cast<float>(fnv1a(in.cfg->id) & 0xFFFF) * 0.001f;  // neighbours don't flicker in step
    for (const Instance& o : g_instances)  // (two buildings on one shell: the first one's fires drive it)
        if (&o != &in && o.fx && !o.fx->shared && live(o.fx->shell) == shell) fx->shared = true;
    if (!fx->shared) {
        each_in(shell, fxapi.t_chimney, [&](Il2CppObject* c) { fx->chimneys.push_back(hold(c)); });
        fx->chimney_minutes.assign(fx->chimneys.size(), 0.0f);
        fx->chimney_fed.assign(fx->chimneys.size(), 0);
        collect_glass(*fx, shell);
    }
    rescan_fx_fires(in, *fx, now_ms());
    return fx;
}

void apply_glow(FireFx& fx, float k, float flicker) {
    const float emissive = kGlowNightEmissive * k * flicker;
    for (FireFx::Slot& s : fx.windows) {
        Il2CppObject* r = live(s.renderer);
        Il2CppObject* block = target(s.block);
        if (!r || !block) continue;
        il2::invoke(fxapi.mpb_clear, block);
        int32_t id = fxapi.id_color;
        Rgba c{s.base.r + (kGlowNightR - s.base.r) * k, s.base.g + (kGlowNightG - s.base.g) * k,
               s.base.b + (kGlowNightB - s.base.b) * k, s.base.a};
        void* ca[] = {&id, &c};
        il2::invoke(fxapi.mpb_color, block, ca);
        int32_t fid = fxapi.id_emissive_strength;
        float e = s.base_emissive + emissive;
        void* fa[] = {&fid, &e};
        il2::invoke(fxapi.mpb_float, block, fa);
        if (Il2CppObject* tex = target(s.texture)) {
            int32_t tid = fxapi.id_emissive;
            void* ta[] = {&tid, tex};
            il2::invoke(fxapi.mpb_texture, block, ta);
        }
        int32_t index = s.index;
        void* ra[] = {block, &index};
        il2::invoke(fxapi.r_set_block, r, ra);
    }
}
void clear_glow(FireFx& fx) {
    for (FireFx::Slot& s : fx.windows) {  // an empty block: the glass draws from its own material again
        Il2CppObject* r = live(s.renderer);
        Il2CppObject* block = target(s.block);
        if (!r || !block) continue;
        il2::invoke(fxapi.mpb_clear, block);
        int32_t index = s.index;
        void* ra[] = {block, &index};
        il2::invoke(fxapi.r_set_block, r, ra);
    }
    fx.applied = false;
    fx.applied_level = 0;
}

void evaluate_fx(Instance& in, FireFx& fx, double now) {
    if (now >= fx.next_rescan) rescan_fx_fires(in, fx, now);
    std::fill(fx.chimney_minutes.begin(), fx.chimney_minutes.end(), 0.0f);
    float strongest = 0;
    for (size_t i = 0; i < fx.fires.size(); ++i) {
        Il2CppObject* f = live(fx.fires[i]);
        if (!f || !fire_alive(f)) continue;
        const float minutes = call_float(papi.fire_left, f) / 60.0f;
        strongest = std::max(strongest, fx_clamp01(minutes / kGlowFireFadeMinutes));
        const int c = fx.fire_chimney[i];
        if (c >= 0 && static_cast<size_t>(c) < fx.chimney_minutes.size())
            fx.chimney_minutes[c] = std::max(fx.chimney_minutes[c], minutes);
    }
    for (size_t c = 0; c < fx.chimneys.size(); ++c) {
        Il2CppObject* ch = live(fx.chimneys[c]);
        if (!ch) continue;
        if (fx.chimney_minutes[c] > 0) {
            put<float>(ch, fxapi.chimney_minutes, fx.chimney_minutes[c]);
            fx.chimney_fed[c] = 1;
        } else if (fx.chimney_fed[c]) {  // its fire went out: the smoke winds down as the game's does
            fx.chimney_fed[c] = 0;
            if (get<float>(ch, fxapi.chimney_minutes) > kChimneyWindDown) put<float>(ch, fxapi.chimney_minutes, kChimneyWindDown);
        }
    }
    fx.target = fx.windows.empty() ? 0 : g_fx_night * strongest;
}

void update_glow(Instance& in, FireFx& fx, Il2CppObject* shell, V3 player, double now) {
    if (fx.windows.empty()) return;
    float dt = fx.last_update < 0 ? 0 : static_cast<float>((now - fx.last_update) / 1000.0);
    fx.last_update = now;
    dt = std::min(dt, 0.25f);
    const float step = dt / kGlowFadeSeconds;
    fx.level = fx.level < fx.target ? std::min(fx.target, fx.level + step) : std::max(fx.target, fx.level - step);
    if (fx.level <= 0) {
        if (fx.applied) clear_glow(fx);
        return;
    }
    if (now < fx.next_apply) return;
    fx.next_apply = now + kGlowApplyMs;
    if (!active_in_tree(shell)) return;  // the player is inside this very building: the shell is off
    const bool flicker = dist(player, in.cfg->fallback) <= kGlowFlickerDistance;
    if (!flicker && fx.applied && std::fabs(fx.applied_level - fx.level) < 0.01f) return;
    float scale = 1;
    if (flicker) {
        const double t = now / 1000.0, s = fx.seed;
        scale += kGlowFlickerAmplitude *
                 static_cast<float>(0.55 * std::sin(t * 2.3 + s) + 0.30 * std::sin(t * 5.3 + s * 1.7) + 0.15 * std::sin(t * 11.9 + s * 2.9));
    }
    apply_glow(fx, fx.level, scale);
    fx.applied = true;
    fx.applied_level = fx.level;
}

void tick_exterior_fx() {
    if (!g_fire_fx || !fxapi.ok) return;
    const double now = now_ms();
    static double next_slow = 0;
    const bool slow = now >= next_slow;
    if (slow) {
        next_slow = now + 1000;
        g_fx_night = fxapi.glow_ok ? night_factor() : 0.0f;
    }
    bool built = false;
    V3 player;
    bool have_player = false;
    for (Instance& in : g_instances) {
        if (in.stage != Stage::Ready || !in.hydrated) continue;
        // per frame only for a building whose glass glows (or fades); the rest once a second
        if (!slow && !(in.fx && (in.fx->applied || in.fx->target > 0))) continue;
        Il2CppObject* shell = live(in.shell);
        if (!shell || !master_of(in)) continue;
        if (!in.fx || live(in.fx->shell) != shell) {
            if (built) continue;  // (one building's shell and room looked through per frame)
            built = true;
            in.fx = build_fx(in, shell);
        }
        FireFx& fx = *in.fx;
        if (fx.failed || fx.shared) continue;
        if (slow) evaluate_fx(in, fx, now);
        if (!have_player) {
            player = player_pos();
            have_player = true;
        }
        update_glow(in, fx, shell, player, now);
    }
}
