#include "automat_items.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <random>
#include <string>

#include "coop_notices.h"
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
};

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
    ptrdiff_t data_name = -1, data_desc = -1, data_weight = -1, data_placeable = -1;
    ptrdiff_t panel_breakdown = -1, breakdown_name = -1;
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

}  // namespace

bool ready() { return api.ok; }

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
    const bool loads = api.ok &&
                       hook("GearItem.InstantiateAndDeserializeGearItem(string, string) (automat)", make_text, &make_text_detour, &g_make_text) &&
                       hook("GearItem.InstantiateAndDeserializeGearItem(string, proxy) (automat)", make_proxy, &make_proxy_detour, &g_make_proxy);
    if (api.ok && !loads) logx::warn("automat items: loading saved ones isn't hooked (one InstantiateAndDeserializeGearItem missing)");
    const MethodInfo* finish = panel ? il2::find_method(panel, "BreakDownFinished", 0) : nullptr;
    const bool boards = api.ok && hook("Panel_BreakDown.BreakDownFinished (automat)", finish, &finish_detour, &g_finish);
    logx::info("automat items: %s; boards from electronics: %s", api.ok ? "ready (3 items)" : "UNAVAILABLE (hooks failed)",
               !boards ? "unavailable" : drops ? "on (1 in 3)" : "off (test=0)");
}

}  // namespace automat_items
