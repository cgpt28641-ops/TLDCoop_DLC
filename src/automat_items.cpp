#include "automat_items.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <vector>
#include <random>
#include <string>

#include "coop_notices.h"
#include "custom_text.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/uniques.h"

namespace automat_items {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr ptrdiff_t kStringLength = 0x10, kStringChars = 0x14;
constexpr int kDontUnload = 32;  // HideFlags.DontUnloadUnusedAsset

struct Def {
    const char* name;   // the item's (and its save's) name
    const char* word;   // command.txt: "automat daj <word>"
    const char* base;   // the game item it's made from
    const char* key;    // Localization keys: <key>_Name, <key>_Desc
    const char* title;
    const char* desc;
    float kg;
    uintptr_t data = 0;  // our GearItemData (a copy of the base's)
    uintptr_t title_s = 0, desc_s = 0;
    uintptr_t ref = 0;   // our data's m_PrefabReference (TLD makes copies by it: a stack split)
    uintptr_t tex = 0;   // its icon (Karol's picture, src/automat_icons.inc), made on first use
};

#include "automat_icons.inc"

Def g_defs[] = {
    {kBoard, "plytka", "GEAR_ScrapMetal", "TLDCOOP_Plytka", "Płytka elektroniczna",
     "Płytka z rozebranej elektroniki (radio, telewizor, komputer, kasa, mikrofalówka). Potrzebna do mechanizmu bębnów "
     "i automatu. Włożona do automatu naprawia go o 25%.",
     0.2f},
    {kMechanism, "mechanizm", "GEAR_ScrapMetal", "TLDCOOP_Mechanizm", "Mechanizm bębnów",
     "Trzy bębny z symbolami na osi z korbą — serce automatu. Robi się na frezarce: 4 złom + 1 płytka elektroniczna.",
     1.5f},
    {kAutomat, "automat", "GEAR_ScrapMetal", "TLDCOOP_Automat", "Automat",
     "Jednoręki bandyta. Włóż jeden rodzaj przedmiotu (max 10 sztuk albo 3 kg) i zamknij — bębny się kręcą.\n"
     "Czaszka na środku: przegrana. 3 złote samorodki: ×5. 3 takie same: ×3. Para: ×2. Reszta: zwrot.\n"
     "Wygrana zostaje w środku. Każde losowanie zużywa go o 3%; zużyty częściej się zacina (zabiera stawkę). "
     "Naprawa: płytka elektroniczna (+25%).",
     15.0f},
};

struct Api {
    bool ok = false;
    const MethodInfo *component_go = nullptr, *set_name = nullptr, *instantiate = nullptr, *hide_flags = nullptr;
    const MethodInfo *from_kg = nullptr, *lstring_ctor = nullptr, *player_manager = nullptr, *to_pack = nullptr;
    const MethodInfo *make = nullptr;  // GearItem.InstantiateGearItem(string)
    Il2CppClass* lstring = nullptr;
    ptrdiff_t lstring_id = -1, gear_data = -1, gear_weight = -1, name_override = -1, desc_override = -1;
    ptrdiff_t data_name = -1, data_desc = -1, data_weight = -1, data_placeable = -1, data_ref = -1;
    ptrdiff_t gear_hp = -1, data_max_hp = -1;                 // build 3c: the automat's condition, carried
    const MethodInfo *destroy = nullptr, *tf_get_pos = nullptr, *component_tf = nullptr;
    ptrdiff_t panel_breakdown = -1, breakdown_name = -1;
    // recipes (build 3b)
    bool recipes = false;
    Il2CppClass *go_class = nullptr, *required_class = nullptr, *tools_class = nullptr, *powder_class = nullptr, *liquid_class = nullptr;
    const MethodInfo *go_ctor = nullptr, *go_set_active = nullptr, *go_tf = nullptr, *keep_loaded = nullptr;
    const MethodInfo *instantiate_under = nullptr, *bpm_instance = nullptr, *required_ctor = nullptr, *init_tools = nullptr;
    const MethodInfo *get_name = nullptr, *find_all = nullptr;
    Il2CppObject* t_gear = nullptr;
    ptrdiff_t bpm_all = -1, bp_required = -1, bp_tool = -1, bp_tools = -1, bp_powder = -1, bp_liquid = -1, bp_type = -1;
    ptrdiff_t bp_decoration = -1, bp_result = -1, bp_count = -1, bp_minutes = -1, bp_locked = -1, bp_locked_prop = -1;
    ptrdiff_t bp_ignore_lock = -1, bp_story = -1, bp_survival = -1, bp_location = -1, bp_fire = -1, bp_photo = -1;
    ptrdiff_t req_item = -1, req_count = -1, req_units = -1, op_blueprint = -1, op_last = -1;
} api;

bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}

std::string text(Il2CppString* s) { return s ? il2::to_utf8(s) : std::string(); }

// Is this managed string "GEAR_TLDCOOP_..." / "TLDCOOP_..."? (checked on the UTF-16 chars, no conversion: Localization.Get
// runs many times a frame)
bool starts_with(Il2CppString* s, const char* prefix) {
    if (!s) return false;
    const int32_t len = il2::read_field<int32_t>(reinterpret_cast<Il2CppObject*>(s), kStringLength);
    const char16_t* c = reinterpret_cast<const char16_t*>(reinterpret_cast<const char*>(s) + kStringChars);
    int32_t i = 0;
    for (; prefix[i]; ++i)
        if (i >= len || c[i] != static_cast<char16_t>(prefix[i])) return false;
    return true;
}

Def* def_of(const std::string& name) {
    const std::string base(net::gear_base_name(name));
    for (Def& d : g_defs)
        if (base == d.name) return &d;
    return nullptr;
}

int64_t weight_of(float kg) {
    int64_t w = 0;
    void* a[] = {&kg};
    return api.from_kg && il2::invoke_value(api.from_kg, nullptr, w, a) ? w : 0;
}

Il2CppObject* new_lstring(const std::string& id) {
    Il2CppObject* s = il2::il2cpp_object_new(api.lstring);
    if (!s) return nullptr;
    il2::invoke(api.lstring_ctor, s);
    il2::store_ref(s, api.lstring_id, il2::il2cpp_string_new(id.c_str()));
    return s;
}

// Our GearItemData: the base's, copied once, with our name, description and weight
Il2CppObject* data_for(Def& d, Il2CppObject* base_data) {
    if (Il2CppObject* mine = target(d.data); alive(mine)) return mine;
    if (d.data) il2::il2cpp_gchandle_free(d.data), d.data = 0;
    if (!alive(base_data)) return nullptr;
    void* a[] = {base_data};
    Il2CppObject* copy = il2::invoke(api.instantiate, nullptr, a);
    if (!copy) return nullptr;
    int32_t flags = kDontUnload;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, copy, fa);
    void* na[] = {il2::il2cpp_string_new(d.name)};
    il2::invoke(api.set_name, copy, na);
    if (Il2CppObject* n = new_lstring(std::string(d.key) + "_Name")) il2::store_ref(copy, api.data_name, n);
    if (Il2CppObject* n = new_lstring(std::string(d.key) + "_Desc")) il2::store_ref(copy, api.data_desc, n);
    if (const int64_t w = weight_of(d.kg)) put<int64_t>(copy, api.data_weight, w);
    put<bool>(copy, api.data_placeable, false);
    // Its own prefab reference (same asset): a copy TLD makes by it (a stack split, into a container) is told apart
    if (api.data_ref >= 0) {
        Il2CppObject* ref = il2::read_field<Il2CppObject*>(copy, api.data_ref);
        if (ref && ref == il2::read_field<Il2CppObject*>(base_data, api.data_ref)) {  // (shared: a new one)
            Il2CppClass* k = il2::il2cpp_object_get_class(ref);
            const MethodInfo* ctor = k ? il2::find_method_params_quiet(k, ".ctor", {"System.String"}) : nullptr;
            Il2CppObject* mine = ctor ? il2::il2cpp_object_new(k) : nullptr;
            if (mine) {
                void* ca[] = {il2::read_field<Il2CppObject*>(ref, 0x10)};  // AssetReference.m_AssetGUID
                il2::invoke(ctor, mine, ca);
                il2::store_ref(copy, api.data_ref, mine);
                ref = mine;
            } else {
                ref = nullptr;
            }
        }
        if (ref) d.ref = il2::il2cpp_gchandle_new(ref, false);
        logx::info("automat items: %s's prefab reference %s", d.name, ref ? "its own" : "SHARED (copies made by it become scrap)");
    }
    d.data = il2::il2cpp_gchandle_new(copy, false);
    logx::info("automat items: %s's data made (from %s's)", d.name, d.base);
    return copy;
}

// A base item, made into ours
void dress(Il2CppObject* gi, Def& d) {
    if (!alive(gi)) return;
    if (Il2CppObject* go = il2::invoke(api.component_go, gi)) {
        void* na[] = {il2::il2cpp_string_new(d.name)};
        il2::invoke(api.set_name, go, na);
    }
    if (Il2CppObject* data = data_for(d, il2::read_field<Il2CppObject*>(gi, api.gear_data))) il2::store_ref(gi, api.gear_data, data);
    if (const int64_t w = weight_of(d.kg)) put<int64_t>(gi, api.gear_weight, w);
    il2::store_ref(gi, api.name_override, il2::il2cpp_string_new((std::string(d.key) + "_Name").c_str()));
    il2::store_ref(gi, api.desc_override, il2::il2cpp_string_new((std::string(d.key) + "_Desc").c_str()));
}

// --- hooks ----------------------------------------------------------------------------------------------------------

using MakeFn = Il2CppObject* (*)(Il2CppString*, const MethodInfo*);
using MakeLoadFn = Il2CppObject* (*)(Il2CppString*, Il2CppObject*, Il2CppObject*, bool, bool, const MethodInfo*);
using LocFn = Il2CppString* (*)(Il2CppString*, const MethodInfo*);
using FinishFn = void (*)(Il2CppObject*, const MethodInfo*);
MakeFn g_make = nullptr;
MakeLoadFn g_make_text = nullptr, g_make_proxy = nullptr;
LocFn g_loc = nullptr;
FinishFn g_finish = nullptr;
bool g_drops = false;
std::mt19937 g_rng{std::random_device{}()};

Def* ref_def(Il2CppObject* ref) {
    if (!ref) return nullptr;
    for (Def& d : g_defs)
        if (d.ref && target(d.ref) == ref) return &d;
    return nullptr;
}

using MakeRefFn = Il2CppObject* (*)(Il2CppObject*, const MethodInfo*);
MakeRefFn g_make_ref = nullptr;
MakeLoadFn g_make_ref_text = nullptr, g_make_ref_proxy = nullptr;  // (the first argument is the AssetReference)

Il2CppObject* make_ref_detour(Il2CppObject* ref, const MethodInfo* method) {
    Il2CppObject* gi = g_make_ref(ref, method);
    if (Def* d = ref_def(ref)) dress(gi, *d);
    return gi;
}
Il2CppObject* make_ref_text_detour(Il2CppString* ref, Il2CppObject* saved, Il2CppObject* parent, bool fix, bool in_container,
                                   const MethodInfo* method) {
    Il2CppObject* gi = g_make_ref_text(ref, saved, parent, fix, in_container, method);
    if (Def* d = ref_def(reinterpret_cast<Il2CppObject*>(ref))) dress(gi, *d);
    return gi;
}
Il2CppObject* make_ref_proxy_detour(Il2CppString* ref, Il2CppObject* proxy, Il2CppObject* parent, bool fix, bool in_container,
                                    const MethodInfo* method) {
    Il2CppObject* gi = g_make_ref_proxy(ref, proxy, parent, fix, in_container, method);
    if (Def* d = ref_def(reinterpret_cast<Il2CppObject*>(ref))) dress(gi, *d);
    return gi;
}

Il2CppObject* make_detour(Il2CppString* name, const MethodInfo* method) {
    if (!starts_with(name, "GEAR_TLDCOOP_")) return g_make(name, method);
    Def* d = def_of(text(name));
    if (!d) return g_make(name, method);
    Il2CppObject* gi = g_make(il2::il2cpp_string_new(d->base), method);
    dress(gi, *d);
    return gi;
}

Il2CppObject* make_text_detour(Il2CppString* name, Il2CppObject* saved, Il2CppObject* parent, bool fix, bool in_container,
                               const MethodInfo* method) {
    if (!starts_with(name, "GEAR_TLDCOOP_")) return g_make_text(name, saved, parent, fix, in_container, method);
    Def* d = def_of(text(name));
    if (!d) return g_make_text(name, saved, parent, fix, in_container, method);
    Il2CppObject* gi = g_make_text(il2::il2cpp_string_new(d->base), saved, parent, fix, in_container, method);
    dress(gi, *d);
    return gi;
}

Il2CppObject* make_proxy_detour(Il2CppString* name, Il2CppObject* proxy, Il2CppObject* parent, bool fix, bool in_container,
                                const MethodInfo* method) {
    if (!starts_with(name, "GEAR_TLDCOOP_")) return g_make_proxy(name, proxy, parent, fix, in_container, method);
    Def* d = def_of(text(name));
    if (!d) return g_make_proxy(name, proxy, parent, fix, in_container, method);
    Il2CppObject* gi = g_make_proxy(il2::il2cpp_string_new(d->base), proxy, parent, fix, in_container, method);
    dress(gi, *d);
    return gi;
}

Il2CppString* loc_detour(Il2CppString* key, const MethodInfo* method) {
    if (Il2CppString* ours = custom_text::find(key)) return ours;  // TLDCoop_DLC: ClothingExpanded's names and others
    if (!starts_with(key, "TLDCOOP_")) return g_loc(key, method);
    const std::string k = text(key);
    for (Def& d : g_defs) {
        const bool title = k == std::string(d.key) + "_Name", desc = k == std::string(d.key) + "_Desc";
        if (!title && !desc) continue;
        uintptr_t& h = title ? d.title_s : d.desc_s;
        if (!target(h)) h = il2::il2cpp_gchandle_new(reinterpret_cast<Il2CppObject*>(il2::il2cpp_string_new(title ? d.title : d.desc)), false);
        return reinterpret_cast<Il2CppString*>(target(h));
    }
    return g_loc(key, method);
}

// The board from electronics: about one in three things broken down (Karol: radios, TVs, computers, tills, microwaves,
// small electronics), our own break down only (Panel_BreakDown.BreakDownFinished, not one from the other players).
bool electronic(const std::string& what) {
    std::string low = what;
    for (char& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    static const char* kWords[] = {"radio", "tv", "television", "telewiz", "computer", "monitor", "laptop", "register",
                                   "cashreg", "till", "microwave", "stereo", "speaker", "electronic", "printer",
                                   "phone", "keyboard", "console", "vcr", "amplifier", "transmitter"};
    for (const char* w : kWords)
        if (low.find(w) != std::string::npos) return true;
    return false;
}

std::string object_name(Il2CppObject* o) {
    Il2CppObject* go = alive(o) ? il2::invoke(api.component_go, o) : nullptr;
    static const MethodInfo* get_name = il2::find_method(il2::find_class(kCore, "UnityEngine", "Object"), "get_name", 0);
    return go ? text(reinterpret_cast<Il2CppString*>(il2::invoke(get_name, go))) : std::string();
}

void finish_detour(Il2CppObject* self, const MethodInfo* method) {
    Il2CppObject* bd = self && api.panel_breakdown >= 0 ? il2::read_field<Il2CppObject*>(self, api.panel_breakdown) : nullptr;
    std::string what;
    if (alive(bd)) {
        what = object_name(bd);
        if (Il2CppObject* ls = il2::read_field<Il2CppObject*>(bd, api.breakdown_name))
            what += " / " + text(il2::read_field<Il2CppString*>(ls, api.lstring_id));
    }
    g_finish(self, method);
    if (!g_drops || what.empty()) return;
    const bool elec = electronic(what);
    const bool found = elec && std::uniform_int_distribution<int>(0, 2)(g_rng) == 0;
    logx::info("automat items: broke down %s%s", what.c_str(), !elec ? "" : found ? ": a circuit board found" : ": electronics, no board this time");
    if (found && give(kBoard, 1)) coop_notices::tell("Znalazłeś płytkę elektroniczną");
}


// --- recipes (build 3b): both at the workbench (Karol 05.11: the milling machine only repairs in TLD) -------------
// A copy of one of TLD's workbench blueprints, with our needs and result, added to BlueprintManager.m_AllBlueprints
// (checked every 2 s: TLD builds the list again for a new game). The results and our needs are "templates": an item
// of ours made under an inactive object (so it's never awake, never in the world or a save), kept across scenes.

struct Need { const char* name; int count; };
struct Recipe {
    const char* result;
    int minutes;
    Need needs[5];
    uintptr_t bp = 0;
};
Recipe g_recipes[] = {
    {kMechanism, 60, {{"GEAR_ScrapMetal", 4}, {kBoard, 1}}},
    {kAutomat, 180, {{kMechanism, 1}, {kBoard, 2}, {"GEAR_ScrapMetal", 6}, {"GEAR_ReclaimedWoodB", 4}, {"GEAR_Cloth", 2}}},
};
uintptr_t g_holder = 0;                       // the inactive object our templates live under
std::map<std::string, uintptr_t> g_templates;  // ours (and base items not found in TLD's blueprints)
double g_recipes_at = 0;
bool g_recipes_logged = false;

std::string name_of_object(Il2CppObject* o) {
    return alive(o) ? text(reinterpret_cast<Il2CppString*>(il2::invoke(api.get_name, o))) : std::string();
}

Il2CppObject* list_items(Il2CppObject* list, int32_t& count) {
    count = list ? il2::read_field<int32_t>(list, 0x18) : 0;
    return list ? il2::read_field<Il2CppObject*>(list, 0x10) : nullptr;
}

Il2CppObject* holder() {
    if (Il2CppObject* h = target(g_holder); alive(h)) return h;
    if (g_holder) il2::il2cpp_gchandle_free(g_holder), g_holder = 0;
    Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
    if (!go) return nullptr;
    void* ca[] = {il2::il2cpp_string_new("MOD_TLDCoopAutomatTemplates")};
    il2::invoke(api.go_ctor, go, ca);
    bool off = false;
    void* aa[] = {&off};
    il2::invoke(api.go_set_active, go, aa);
    void* ka[] = {go};
    il2::invoke(api.keep_loaded, nullptr, ka);
    g_holder = il2::il2cpp_gchandle_new(go, false);
    return go;
}

// One of TLD's GearItems (prefab) by name, from the blueprints' needs and results
std::map<std::string, Il2CppObject*> game_items(Il2CppObject* all) {
    std::map<std::string, Il2CppObject*> out;
    int32_t n = 0;
    Il2CppObject* items = list_items(all, n);
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* bp = il2::array_object_at(items, static_cast<size_t>(i));
        if (!alive(bp)) continue;
        if (Il2CppObject* r = il2::read_field<Il2CppObject*>(bp, api.bp_result); alive(r)) out.emplace(name_of_object(r), r);
        Il2CppObject* req = il2::read_field<Il2CppObject*>(bp, api.bp_required);
        for (size_t k = 0; k < il2::array_length(req); ++k)
            if (Il2CppObject* rg = il2::array_object_at(req, k))
                if (Il2CppObject* gi = il2::read_field<Il2CppObject*>(rg, api.req_item); alive(gi)) out.emplace(name_of_object(gi), gi);
    }
    return out;
}

// A template of `name`: ours dressed, or a copy of a game item's prefab (when TLD's blueprints don't hold it)
Il2CppObject* template_of(const std::string& name, const std::map<std::string, Il2CppObject*>& game) {
    if (const auto it = g_templates.find(name); it != g_templates.end())
        if (Il2CppObject* t = target(it->second); alive(t)) return t;
    Def* d = def_of(name);
    const std::string base = d ? d->base : name;
    const auto g = game.find(base);
    if (!d && g != game.end()) return g->second;  // (the game's own prefab: what TLD's blueprints use)
    Il2CppObject* from = g != game.end() ? g->second : nullptr;
    if (!from) {  // not in any blueprint: a loaded one of that name (a prefab, or one in the world: copied)
        void* fa[] = {api.t_gear};
        Il2CppObject* all = api.find_all ? il2::invoke(api.find_all, nullptr, fa) : nullptr;
        for (size_t i = 0; i < il2::array_length(all) && !from; ++i) {
            Il2CppObject* gi = il2::array_object_at(all, i);
            if (alive(gi) && std::string(net::gear_base_name(name_of_object(gi))) == base) from = gi;
        }
    }
    Il2CppObject* h = holder();
    if (!from || !h) return nullptr;
    void* a[] = {from, il2::invoke(api.go_tf, h)};
    Il2CppObject* gi = il2::invoke(api.instantiate_under, nullptr, a);  // (a GearItem's copy is its GearItem)
    if (!alive(gi)) return nullptr;
    if (d) {
        dress(gi, *d);
    } else {
        void* na[] = {il2::il2cpp_string_new(base.c_str())};
        il2::invoke(api.set_name, il2::invoke(api.component_go, gi), na);
    }
    g_templates[name] = il2::il2cpp_gchandle_new(gi, false);
    logx::info("automat items: template %s (from %s)", name.c_str(), g != game.end() ? "a blueprint's item" : "a loaded item");
    return gi;
}

Il2CppObject* make_blueprint(Recipe& r, Il2CppObject* source, const std::map<std::string, Il2CppObject*>& game) {
    Il2CppObject* result = template_of(r.result, game);
    if (!result) return nullptr;
    size_t n = 0;
    while (n < std::size(r.needs) && r.needs[n].name) ++n;
    Il2CppObject* needs = il2::il2cpp_array_new ? il2::il2cpp_array_new(api.required_class, n) : nullptr;
    if (!needs) return nullptr;
    for (size_t i = 0; i < n; ++i) {
        Il2CppObject* item = template_of(r.needs[i].name, game);
        if (!item) {
            logx::warn("automat items: recipe for %s: %s not found", r.result, r.needs[i].name);
            return nullptr;
        }
        Il2CppObject* rg = il2::il2cpp_object_new(api.required_class);
        il2::invoke(api.required_ctor, rg);
        il2::store_ref(rg, api.req_item, item);
        put<int32_t>(rg, api.req_count, r.needs[i].count);
        put<int32_t>(rg, api.req_units, 0);  // Count
        il2::store_ref(needs, 0x20 + static_cast<ptrdiff_t>(i * sizeof(void*)), rg);
    }
    void* ia[] = {source};
    Il2CppObject* bp = il2::invoke(api.instantiate, nullptr, ia);
    if (!bp) return nullptr;
    int32_t flags = kDontUnload;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, bp, fa);
    void* na[] = {il2::il2cpp_string_new((std::string("BLUEPRINT_") + r.result).c_str())};
    il2::invoke(api.set_name, bp, na);
    il2::store_ref(bp, api.bp_required, needs);
    il2::store_ref(bp, api.bp_tool, nullptr);
    if (api.tools_class) il2::store_ref(bp, api.bp_tools, il2::il2cpp_array_new(api.tools_class, 0));
    if (api.powder_class) il2::store_ref(bp, api.bp_powder, il2::il2cpp_array_new(api.powder_class, 0));
    if (api.liquid_class) il2::store_ref(bp, api.bp_liquid, il2::il2cpp_array_new(api.liquid_class, 0));
    put<int32_t>(bp, api.bp_type, 0);  // StandardGear
    il2::store_ref(bp, api.bp_decoration, nullptr);
    il2::store_ref(bp, api.bp_result, result);
    put<int32_t>(bp, api.bp_count, 1);
    put<int32_t>(bp, api.bp_minutes, r.minutes);
    put<bool>(bp, api.bp_locked, false);
    put<bool>(bp, api.bp_locked_prop, false);
    put<bool>(bp, api.bp_ignore_lock, true);
    put<bool>(bp, api.bp_story, false);
    put<bool>(bp, api.bp_survival, false);
    put<int32_t>(bp, api.bp_location, 1);  // Workbench
    put<bool>(bp, api.bp_fire, false);
    put<bool>(bp, api.bp_photo, false);
    if (api.init_tools) il2::invoke(api.init_tools, bp);
    r.bp = il2::il2cpp_gchandle_new(bp, false);
    return bp;
}

void keep_recipes() {
    Il2CppObject* bpm = il2::invoke(api.bpm_instance, nullptr);
    Il2CppObject* all = alive(bpm) ? il2::read_field<Il2CppObject*>(bpm, api.bpm_all) : nullptr;
    int32_t n = 0;
    Il2CppObject* items = list_items(all, n);
    if (!all || n <= 0) return;  // (not loaded yet)
    Il2CppObject* source = nullptr;
    std::vector<Recipe*> missing;
    for (Recipe& r : g_recipes) {
        Il2CppObject* bp = target(r.bp);
        bool in = false;
        for (int32_t i = 0; bp && i < n && static_cast<size_t>(i) < il2::array_length(items) && !in; ++i)
            in = il2::array_object_at(items, static_cast<size_t>(i)) == bp;
        if (!in) missing.push_back(&r);
    }
    if (missing.empty()) return;
    for (int32_t i = 0; i < n && static_cast<size_t>(i) < il2::array_length(items) && !source; ++i) {
        Il2CppObject* bp = il2::array_object_at(items, static_cast<size_t>(i));
        if (alive(bp) && il2::read_field<int32_t>(bp, api.bp_location) == 1 && il2::read_field<int32_t>(bp, api.bp_type) == 0 &&
            alive(il2::read_field<Il2CppObject*>(bp, api.bp_result)))
            source = bp;
    }
    if (!source) {
        if (!g_recipes_logged) logx::warn("automat items: no workbench blueprint to copy; no recipes"), g_recipes_logged = true;
        return;
    }
    const auto game = game_items(all);
    static const MethodInfo* add = nullptr;
    if (!add) add = il2::find_method(il2::il2cpp_object_get_class(all), "Add", 1);
    for (Recipe* r : missing) {
        Il2CppObject* bp = target(r->bp);
        if (!alive(bp)) {
            if (r->bp) il2::il2cpp_gchandle_free(r->bp), r->bp = 0;
            bp = make_blueprint(*r, source, game);
        }
        if (!bp || !add) continue;
        void* a[] = {bp};
        il2::invoke(add, all, a);
        logx::info("automat items: recipe for %s added at the workbench (%d min; from %s's copy)", r->result, r->minutes,
                   name_of_object(source).c_str());
    }
}

// Crafted: the result (made from our template by TLD) gets our name and data again, and is awake
// The crafting panel keeps its own copy of the list (Panel_Crafting.m_Blueprints, made when it was first set up, before
// ours were added): ours put in before it refreshes.
using RefreshFn = void (*)(Il2CppObject*, const MethodInfo*);
RefreshFn g_refresh = nullptr;
ptrdiff_t g_panel_list = -1;
void refresh_detour(Il2CppObject* self, const MethodInfo* method) {
    Il2CppObject* list = self && g_panel_list >= 0 ? il2::read_field<Il2CppObject*>(self, g_panel_list) : nullptr;
    if (list) {
        static const MethodInfo* add = nullptr;
        if (!add) add = il2::find_method(il2::il2cpp_object_get_class(list), "Add", 1);
        int32_t n = 0;
        Il2CppObject* items = list_items(list, n);
        for (Recipe& r : g_recipes) {
            Il2CppObject* bp = target(r.bp);
            if (!alive(bp) || !add) continue;
            bool in = false;
            for (int32_t i = 0; i < n && static_cast<size_t>(i) < il2::array_length(items) && !in; ++i)
                in = il2::array_object_at(items, static_cast<size_t>(i)) == bp;
            if (in) continue;
            void* a[] = {bp};
            il2::invoke(add, list, a);
            logx::info("automat items: recipe for %s put in the crafting panel's list", r.result);
        }
    }
    g_refresh(self, method);
}

using ApplyFn = void (*)(Il2CppObject*, float, const MethodInfo*);
ApplyFn g_apply = nullptr;
void apply_detour(Il2CppObject* self, float hours, const MethodInfo* method) {
    g_apply(self, hours, method);
    Il2CppObject* bp = self ? il2::read_field<Il2CppObject*>(self, api.op_blueprint) : nullptr;
    if (!bp) return;
    for (Recipe& r : g_recipes) {
        if (target(r.bp) != bp) continue;
        Il2CppObject* gi = il2::read_field<Il2CppObject*>(self, api.op_last);
        Def* d = def_of(r.result);
        if (!alive(gi) || !d) return;
        Il2CppObject* go = il2::invoke(api.component_go, gi);
        if (name_of_object(go) != d->name) {
            dress(gi, *d);
            logx::info("automat items: crafted %s", d->name);
        }
        return;
    }
}

// --- icons: Karol's pictures (assets/automat, 256 px PNG in src/automat_icons.inc) ---------------------------------
// TLD finds an item's icon by its prefab's name (Addressables): ours has none, the slot was empty. Utils' icon calls
// (inventory, by data or by prefab name), the inspect picture (coverflow) and the recipe's icon give ours.

struct IconApi {
    bool ok = false;
    Il2CppClass *tex_class = nullptr, *byte_class = nullptr;
    const MethodInfo *tex_ctor = nullptr, *load_image = nullptr;
} icon_api;

const unsigned char* png_of(const Def& d, size_t& n) {
    if (std::string(d.name) == kBoard) return n = sizeof(kIconBoard), kIconBoard;
    if (std::string(d.name) == kMechanism) return n = sizeof(kIconMechanism), kIconMechanism;
    return n = sizeof(kIconAutomat), kIconAutomat;
}

Il2CppObject* icon_of(Def& d) {
    if (Il2CppObject* t = target(d.tex); alive(t)) return t;
    if (d.tex) il2::il2cpp_gchandle_free(d.tex), d.tex = 0;
    if (!icon_api.ok) return nullptr;
    size_t n = 0;
    const unsigned char* png = png_of(d, n);
    Il2CppObject* bytes = il2::il2cpp_array_new(icon_api.byte_class, n);
    Il2CppObject* tex = il2::il2cpp_object_new(icon_api.tex_class);
    if (!bytes || !tex) return nullptr;
    memcpy(reinterpret_cast<char*>(bytes) + 0x20, png, n);
    int32_t w = 2, h = 2;
    void* ca[] = {&w, &h};
    il2::invoke(icon_api.tex_ctor, tex, ca);
    void* la[] = {tex, bytes};
    bool loaded = false;
    if (!il2::invoke_value(icon_api.load_image, nullptr, loaded, la) || !loaded) {
        logx::warn("automat items: %s's icon couldn't be loaded", d.name);
        return nullptr;
    }
    int32_t flags = kDontUnload;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, tex, fa);
    d.tex = il2::il2cpp_gchandle_new(tex, false);
    logx::info("automat items: %s's icon made (%zu bytes)", d.name, n);
    return tex;
}

Def* def_of_data(Il2CppObject* data) {
    if (!data) return nullptr;
    for (Def& d : g_defs)
        if (d.data && target(d.data) == data) return &d;
    return nullptr;
}

using IconDataFn = Il2CppObject* (*)(Il2CppObject*, float, const MethodInfo*);
using IconNameFn = Il2CppObject* (*)(Il2CppString*, bool, const MethodInfo*);
using IconGearFn = Il2CppObject* (*)(Il2CppObject*, const MethodInfo*);
IconDataFn g_icon_data = nullptr;
IconNameFn g_icon_name = nullptr;
IconGearFn g_cover = nullptr, g_cover_damage = nullptr, g_craft_icon = nullptr;

Il2CppObject* icon_data_detour(Il2CppObject* data, float condition, const MethodInfo* method) {
    if (Def* d = def_of_data(data))
        if (Il2CppObject* t = icon_of(*d)) return t;
    return g_icon_data(data, condition, method);
}
Il2CppObject* icon_name_detour(Il2CppString* name, bool opened, const MethodInfo* method) {
    if (starts_with(name, "GEAR_TLDCOOP_"))
        if (Def* d = def_of(text(name)))
            if (Il2CppObject* t = icon_of(*d)) return t;
    return g_icon_name(name, opened, method);
}
Il2CppObject* gear_icon(Il2CppObject* gi) {
    Def* d = alive(gi) ? def_of_data(il2::read_field<Il2CppObject*>(gi, api.gear_data)) : nullptr;
    return d ? icon_of(*d) : nullptr;
}
Il2CppObject* cover_detour(Il2CppObject* gi, const MethodInfo* method) {
    if (Il2CppObject* t = gear_icon(gi)) return t;
    return g_cover(gi, method);
}
Il2CppObject* cover_damage_detour(Il2CppObject* gi, const MethodInfo* method) {
    if (Il2CppObject* t = gear_icon(gi)) return t;
    return g_cover_damage(gi, method);
}
Il2CppObject* craft_icon_detour(Il2CppObject* bp, const MethodInfo* method) {
    for (Recipe& r : g_recipes)
        if (r.bp && target(r.bp) == bp)
            if (Def* d = def_of(r.result))
                if (Il2CppObject* t = icon_of(*d)) return t;
    return g_craft_icon(bp, method);
}

// --- build 3c: the automat dropped from the pack becomes a machine (slot_machine takes it from here) ----------------
using DropFn = Il2CppObject* (*)(Il2CppObject*, int32_t, bool, bool, bool, const MethodInfo*);
DropFn g_drop = nullptr;
std::vector<uintptr_t> g_dropped;

Il2CppObject* drop_detour(Il2CppObject* self, int32_t units, bool sound, bool to_feet, bool force, const MethodInfo* method) {
    Il2CppObject* gi = g_drop(self, units, sound, to_feet, force, method);
    if (alive(gi))
        if (Def* d = def_of_data(il2::read_field<Il2CppObject*>(gi, api.gear_data)); d && std::string(d->name) == kAutomat) {
            g_dropped.push_back(il2::il2cpp_gchandle_new(gi, false));
            logx::info("automat items: an automat dropped from the pack");
        }
    return gi;
}
}  // namespace

bool ready() { return api.ok; }

Il2CppObject* take_dropped() {
    while (!g_dropped.empty()) {
        const uintptr_t h = g_dropped.front();
        g_dropped.erase(g_dropped.begin());
        Il2CppObject* gi = target(h);
        il2::il2cpp_gchandle_free(h);
        if (alive(gi)) return gi;
    }
    return nullptr;
}

float condition_of(Il2CppObject* gi) {
    Il2CppObject* data = alive(gi) ? il2::read_field<Il2CppObject*>(gi, api.gear_data) : nullptr;
    const float max = data && api.data_max_hp >= 0 ? il2::read_field<float>(data, api.data_max_hp) : 0;
    if (max <= 0 || api.gear_hp < 0) return 1.0f;
    return std::clamp(il2::read_field<float>(gi, api.gear_hp) / max, 0.0f, 1.0f);
}

bool position_of(Il2CppObject* gi, float& x, float& y, float& z) {
    struct V { float x, y, z; } v{};
    Il2CppObject* tf = alive(gi) ? il2::invoke(api.component_tf, gi) : nullptr;
    if (!tf || !il2::invoke_value(api.tf_get_pos, tf, v)) return false;
    x = v.x, y = v.y, z = v.z;
    return true;
}

void destroy(Il2CppObject* gi) {
    Il2CppObject* go = alive(gi) ? il2::invoke(api.component_go, gi) : nullptr;
    if (!go || !api.destroy) return;
    void* a[] = {go};
    il2::invoke(api.destroy, nullptr, a);
}

bool give_automat(float condition) {
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    if (!api.ok || !pm) return false;
    void* a[] = {il2::il2cpp_string_new(kAutomat)};
    Il2CppObject* gi = il2::invoke(api.make, nullptr, a);
    if (!alive(gi)) return false;
    Il2CppObject* data = il2::read_field<Il2CppObject*>(gi, api.gear_data);
    const float max = data && api.data_max_hp >= 0 ? il2::read_field<float>(data, api.data_max_hp) : 0;
    if (max > 0 && api.gear_hp >= 0) put<float>(gi, api.gear_hp, std::clamp(condition, 0.0f, 1.0f) * max);
    bool track = false, notify = true;
    void* pa[] = {gi, &track, &notify};
    il2::invoke(api.to_pack, pm, pa);
    logx::info("automat items: the automat into the pack (condition %.0f%%)", condition * 100);
    return true;
}

void on_frame(bool in_game) {
    if (!api.recipes || !in_game) return;
    const double now = static_cast<double>(GetTickCount64());
    if (now < g_recipes_at) return;
    g_recipes_at = now + 2000;
    keep_recipes();
}

std::string resolve_name(const std::string& which) {
    for (const Def& d : g_defs)
        if (which == d.word || which == d.name) return d.name;
    return {};
}

bool give(const std::string& which, int n) {
    const std::string name = resolve_name(which);
    if (!api.ok || name.empty()) return false;
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    if (!pm) return false;
    int made = 0;
    for (int i = 0; i < std::clamp(n, 1, 20); ++i) {
        void* a[] = {il2::il2cpp_string_new(name.c_str())};
        Il2CppObject* gi = il2::invoke(api.make, nullptr, a);  // (through our hook)
        if (!alive(gi)) break;
        bool track = false, notify = true;
        void* pa[] = {gi, &track, &notify};
        il2::invoke(api.to_pack, pm, pa);
        ++made;
    }
    logx::info("automat items: %d x %s into the pack", made, name.c_str());
    return made > 0;
}

void init(bool drops) {
    g_drops = drops;
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* data = il2::find_class(kGame, "TLD.Gear", "GearItemData");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* loc = il2::find_class(kGame, "", "Localization");
    Il2CppClass* lstring = il2::find_class(kGame, "", "LocalizedString");
    Il2CppClass* weight = il2::find_class("TLD.IntBackedUnit.dll", "TLD.IntBackedUnit", "ItemWeight");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* panel = il2::find_class(kGame, "", "Panel_BreakDown");
    Il2CppClass* breakdown = il2::find_class(kGame, "", "BreakDown");
    if (!gear || !data || !object || !component || !loc || !lstring || !gm || !pm) {
        logx::warn("automat items: UNAVAILABLE (TLD classes missing)");
        return;
    }
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    api.from_kg = weight ? il2::find_method_quiet(weight, "FromKilograms", 1) : nullptr;
    api.lstring = lstring;
    api.lstring_ctor = il2::find_method(lstring, ".ctor", 0);
    api.lstring_id = il2::field_offset(lstring, "m_LocalizationID");
    api.player_manager = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    api.to_pack = il2::find_method(pm, "AddItemToPlayerInventory", 3);
    api.make = il2::find_method_params(gear, "InstantiateGearItem", {"System.String"});
    api.gear_data = il2::field_offset(gear, "m_GearItemData");
    api.gear_weight = il2::field_offset_quiet(gear, "<WeightKG>k__BackingField");
    api.name_override = il2::field_offset_quiet(gear, "m_DisplayNameOverrideLocID");
    api.desc_override = il2::field_offset_quiet(gear, "m_DescriptionOverrideLocID");
    api.data_name = il2::field_offset(data, "m_LocalizedName");
    api.data_desc = il2::field_offset(data, "m_LocalizedDescription");
    api.data_weight = il2::field_offset_quiet(data, "m_BaseWeight");
    api.data_placeable = il2::field_offset_quiet(data, "m_IsPlaceable");
    api.data_ref = il2::field_offset_quiet(data, "m_PrefabReference");
    api.data_max_hp = il2::field_offset_quiet(data, "m_MaxHP");
    api.gear_hp = il2::field_offset_quiet(gear, "m_CurrentHP");
    api.destroy = il2::find_method_params(object, "Destroy", {"UnityEngine.Object"});
    api.component_tf = il2::find_method(component, "get_transform", 0);
    if (Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform")) api.tf_get_pos = il2::find_method(tf, "get_position", 0);
    api.panel_breakdown = panel ? il2::field_offset_quiet(panel, "m_BreakDown") : -1;
    api.breakdown_name = breakdown ? il2::field_offset_quiet(breakdown, "m_LocalizedDisplayName") : -1;
    const MethodInfo* make_text = il2::find_method_params(gear, "InstantiateAndDeserializeGearItem",
                                                         {"System.String", "System.String", "UnityEngine.Transform", "System.Boolean", "System.Boolean"});
    const MethodInfo* make_proxy = il2::find_method_params(gear, "InstantiateAndDeserializeGearItem",
                                                          {"System.String", "GearItemSaveDataProxy", "UnityEngine.Transform", "System.Boolean", "System.Boolean"});
    const MethodInfo* get = il2::find_method_params(loc, "Get", {"System.String"});
    if (!api.component_go || !api.set_name || !api.instantiate || !api.lstring_ctor || api.lstring_id < 0 || !api.make ||
        api.gear_data < 0 || api.data_name < 0 || api.data_desc < 0 || !get || !api.to_pack) {
        logx::warn("automat items: UNAVAILABLE (TLD calls missing)");
        return;
    }
    auto hook = [](const char* label, const MethodInfo* m, auto detour, auto* original) {
        return m && hooks::install_method(label, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    api.ok = hook("GearItem.InstantiateGearItem(string) (automat)", api.make, &make_detour, &g_make) &&
             hook("Localization.Get (automat)", get, &loc_detour, &g_loc);
    // (loading: saves and containers; without these a saved item of ours wouldn't come back)
    {  // copies made by the prefab reference (a stack split into a container, etc.)
        constexpr const char* kRef = "UnityEngine.AddressableAssets.AssetReference";
        const MethodInfo* by_ref = il2::find_method_params(gear, "InstantiateGearItem", {kRef});
        const MethodInfo* by_ref_text = il2::find_method_params(gear, "InstantiateAndDeserializeGearItem",
                                                                {kRef, "System.String", "UnityEngine.Transform", "System.Boolean", "System.Boolean"});
        const MethodInfo* by_ref_proxy = il2::find_method_params(gear, "InstantiateAndDeserializeGearItem",
                                                                 {kRef, "GearItemSaveDataProxy", "UnityEngine.Transform", "System.Boolean", "System.Boolean"});
        const bool refs = api.ok && api.data_ref >= 0 &&
                          hook("GearItem.InstantiateGearItem(AssetReference) (automat)", by_ref, &make_ref_detour, &g_make_ref) &&
                          hook("GearItem.InstantiateAndDeserializeGearItem(AssetReference, string) (automat)", by_ref_text, &make_ref_text_detour, &g_make_ref_text) &&
                          hook("GearItem.InstantiateAndDeserializeGearItem(AssetReference, proxy) (automat)", by_ref_proxy, &make_ref_proxy_detour, &g_make_ref_proxy);
        if (api.ok && !refs) logx::warn("automat items: copies by prefab reference aren't hooked (a split stack of ours becomes scrap)");
    }
    const bool loads = api.ok &&
                       hook("GearItem.InstantiateAndDeserializeGearItem(string, string) (automat)", make_text, &make_text_detour, &g_make_text) &&
                       hook("GearItem.InstantiateAndDeserializeGearItem(string, proxy) (automat)", make_proxy, &make_proxy_detour, &g_make_proxy);
    if (api.ok && !loads) logx::warn("automat items: loading saved ones isn't hooked (one InstantiateAndDeserializeGearItem missing)");
    const MethodInfo* finish = panel ? il2::find_method(panel, "BreakDownFinished", 0) : nullptr;
    const bool boards = api.ok && hook("Panel_BreakDown.BreakDownFinished (automat)", finish, &finish_detour, &g_finish);
    // recipes (test only for now)
    if (api.ok && drops) {
        Il2CppClass* bpd = il2::find_class(kGame, "TLD.Gear", "BlueprintData");
        Il2CppClass* bpm = il2::find_class(kGame, "TLD.Gear", "BlueprintManager");
        Il2CppClass* op = il2::find_class(kGame, "TLD.Gear", "CraftingOperation");
        api.go_class = il2::find_class(kCore, "UnityEngine", "GameObject");
        api.required_class = bpd ? il2::find_nested_class(bpd, "RequiredGearItem") : nullptr;
        api.powder_class = bpd ? il2::find_nested_class(bpd, "RequiredPowder") : nullptr;
        api.liquid_class = bpd ? il2::find_nested_class(bpd, "RequiredLiquid") : nullptr;
        api.tools_class = il2::find_class(kGame, "", "ToolsItem");
        api.go_ctor = api.go_class ? il2::find_method_params(api.go_class, ".ctor", {"System.String"}) : nullptr;
        api.go_set_active = api.go_class ? il2::find_method(api.go_class, "SetActive", 1) : nullptr;
        api.go_tf = api.go_class ? il2::find_method(api.go_class, "get_transform", 0) : nullptr;
        api.keep_loaded = il2::find_method_params(object, "DontDestroyOnLoad", {"UnityEngine.Object"});
        api.instantiate_under = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"});
        api.get_name = il2::find_method(object, "get_name", 0);
        api.t_gear = il2::type_object(gear);
        if (Il2CppClass* res = il2::find_class(kCore, "UnityEngine", "Resources"))
            api.find_all = il2::find_method_params(res, "FindObjectsOfTypeAll", {"System.Type"});
        api.bpm_instance = bpm ? il2::find_method(bpm, "get_Instance", 0) : nullptr;
        api.bpm_all = bpm ? il2::field_offset(bpm, "m_AllBlueprints") : -1;
        api.required_ctor = api.required_class ? il2::find_method(api.required_class, ".ctor", 0) : nullptr;
        api.init_tools = bpd ? il2::find_method_quiet(bpd, "InitializeOptionalTools", 0) : nullptr;
        struct F { ptrdiff_t* at; const char* name; };
        if (bpd)
            for (const F& f : {F{&api.bp_required, "m_RequiredGear"}, F{&api.bp_tool, "m_RequiredTool"}, F{&api.bp_tools, "m_OptionalTools"},
                               F{&api.bp_powder, "m_RequiredPowder"}, F{&api.bp_liquid, "m_RequiredLiquid"},
                               F{&api.bp_type, "m_CraftingResultType"}, F{&api.bp_decoration, "m_CraftedResultDecoration"},
                               F{&api.bp_result, "m_CraftedResultGear"}, F{&api.bp_count, "m_CraftedResultCount"},
                               F{&api.bp_minutes, "m_DurationMinutes"}, F{&api.bp_locked, "m_Locked"},
                               F{&api.bp_locked_prop, "<Locked>k__BackingField"}, F{&api.bp_ignore_lock, "m_IgnoreLockInSurvival"},
                               F{&api.bp_story, "m_AppearsInStoryOnly"}, F{&api.bp_survival, "m_AppearsInSurvivalOnly"},
                               F{&api.bp_location, "m_RequiredCraftingLocation"}, F{&api.bp_fire, "m_RequiresLitFire"},
                               F{&api.bp_photo, "m_UsesPhoto"}})
                *f.at = il2::field_offset_quiet(bpd, f.name);
        if (api.required_class) {
            api.req_item = il2::field_offset_quiet(api.required_class, "m_Item");
            api.req_count = il2::field_offset_quiet(api.required_class, "m_Count");
            api.req_units = il2::field_offset_quiet(api.required_class, "m_Units");
        }
        api.op_blueprint = op ? il2::field_offset_quiet(op, "<Blueprint>k__BackingField") : -1;
        api.op_last = op ? il2::field_offset_quiet(op, "m_LastCraftedItem") : -1;
        const MethodInfo* apply = op ? il2::find_method(op, "ApplyCraftingProgress", 1) : nullptr;
        api.recipes = il2::il2cpp_array_new && api.go_ctor && api.go_set_active && api.go_tf && api.keep_loaded &&
                      api.instantiate_under && api.get_name && api.bpm_instance && api.bpm_all >= 0 && api.required_ctor &&
                      api.bp_required >= 0 && api.bp_result >= 0 && api.bp_location >= 0 && api.bp_type >= 0 &&
                      api.bp_count >= 0 && api.bp_minutes >= 0 && api.req_item >= 0 && api.req_count >= 0 &&
                      api.op_blueprint >= 0 && api.op_last >= 0 &&
                      hook("CraftingOperation.ApplyCraftingProgress (automat)", apply, &apply_detour, &g_apply);
        if (Il2CppClass* pc = il2::find_class(kGame, "", "Panel_Crafting")) {
            g_panel_list = il2::field_offset_quiet(pc, "m_Blueprints");
            const MethodInfo* refresh = il2::find_method(pc, "RefreshAvailableBlueprints", 0);
            if (!(api.recipes && g_panel_list >= 0 && hook("Panel_Crafting.RefreshAvailableBlueprints (automat)", refresh, &refresh_detour, &g_refresh)))
                logx::warn("automat items: the crafting panel's list isn't hooked (the recipes may not show)");
        }
        logx::info("automat items: recipes %s", api.recipes ? "on (workbench: mechanism, automat)" : "UNAVAILABLE (TLD calls missing)");
    }
    // icons
    if (api.ok) {
        icon_api.tex_class = il2::find_class(kCore, "UnityEngine", "Texture2D");
        icon_api.byte_class = il2::find_class("mscorlib.dll", "System", "Byte");
        Il2CppClass* conv = il2::find_class("UnityEngine.ImageConversionModule.dll", "UnityEngine", "ImageConversion");
        icon_api.tex_ctor = icon_api.tex_class ? il2::find_method_params(icon_api.tex_class, ".ctor", {"System.Int32", "System.Int32"}) : nullptr;
        icon_api.load_image = conv ? il2::find_method_params(conv, "LoadImage", {"UnityEngine.Texture2D", "System.Byte[]"}) : nullptr;
        icon_api.ok = il2::il2cpp_array_new && icon_api.byte_class && icon_api.tex_ctor && icon_api.load_image;
        Il2CppClass* utils = il2::find_class(kGame, "", "Utils");
        Il2CppClass* bpd = il2::find_class(kGame, "TLD.Gear", "BlueprintData");
        int n = 0;
        if (icon_api.ok && utils) {
            n += hook("Utils.GetInventoryIconTexture(data) (automat)",
                      il2::find_method_params(utils, "GetInventoryIconTexture", {"TLD.Gear.GearItemData", "System.Single"}),
                      &icon_data_detour, &g_icon_data);
            n += hook("Utils.GetInventoryIconTextureFromPrefabName (automat)",
                      il2::find_method(utils, "GetInventoryIconTextureFromPrefabName", 2), &icon_name_detour, &g_icon_name);
            n += hook("Utils.GetGearCoverflowTexture (automat)", il2::find_method(utils, "GetGearCoverflowTexture", 1), &cover_detour, &g_cover);
            n += hook("Utils.GetGearCoverflowDamageTexture (automat)", il2::find_method(utils, "GetGearCoverflowDamageTexture", 1),
                      &cover_damage_detour, &g_cover_damage);
        }
        if (icon_api.ok && bpd) n += hook("BlueprintData.GetCraftingIcon (automat)", il2::find_method(bpd, "GetCraftingIcon", 0), &craft_icon_detour, &g_craft_icon);
        logx::info("automat items: icons %s (%d of 5 calls)", icon_api.ok ? "on" : "UNAVAILABLE", n);
    }
    if (api.ok && drops) {  // build 3c (test only for now): the automat dropped becomes a machine
        const MethodInfo* drop = il2::find_method_params(gear, "Drop", {"System.Int32", "System.Boolean", "System.Boolean", "System.Boolean"});
        logx::info("automat items: placing by dropping %s", hook("GearItem.Drop (automat)", drop, &drop_detour, &g_drop) ? "on" : "UNAVAILABLE");
    }
    logx::info("automat items: %s; boards from electronics: %s", api.ok ? "ready (3 items)" : "UNAVAILABLE (hooks failed)",
               !boards ? "unavailable" : drops ? "on (1 in 3)" : "off (test=0)");
}

}  // namespace automat_items
