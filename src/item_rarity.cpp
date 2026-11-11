#include "item_rarity.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>

#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/uniques.h"
#include "paths.h"

// TLDCoop_DLC: our own rarities (written for this mod; the idea from Deaadman's Item Rarities, none of its code or
// lists). The square's button (UIButtonColor) gets the rarity's colour as its normal colour, a stronger one on hover.

namespace item_rarity {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;

struct Color {
    float r, g, b, a;
};

bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
void put_color(Il2CppObject* o, ptrdiff_t off, Color c) { std::memcpy(reinterpret_cast<char*>(o) + off, &c, sizeof(c)); }

struct Api {
    ptrdiff_t button = -1, gear = -1, decoration = -1;  // InventoryGridItem.m_Button / m_GearItem / m_DecorationItem
    ptrdiff_t hover = -1, pressed = -1;                 // UIButtonColor.hover / pressed
    const MethodInfo *get_default = nullptr, *set_default = nullptr, *update_color = nullptr;
    const MethodInfo *component_go = nullptr, *get_name = nullptr;
} api;

bool g_have_default = false;
Color g_default{1, 1, 1, 1}, g_default_hover{1, 1, 1, 1}, g_default_pressed{1, 1, 1, 1};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// Our list: the first rule that matches decides (the more specific rules first).
Rarity rule(const std::string& n) {
    // Mythic: the one-of-a-kind and the slot machine's
    for (const char* k : {"tldcoop", "moosebagplusscabbard", "curators", "vaughns", "barbs", "rifle_trader", "rifle_iron",
                          "revolver_fancy", "revolverfancy"})
        if (has(n, k)) return Rarity::Mythic;
    // ammunition and its parts: never weapons
    for (const char* k : {"ammo", "bullet", "cartridge", "casing", "shell", "gunpowder", "arrowshaft", "arrowhead"})
        if (has(n, k)) return Rarity::Uncommon;
    if (has(n, "cleaningkit") || has(n, "scabbard")) return Rarity::Rare;
    // Legendary: guns and the bow
    for (const char* k : {"rifle", "revolver", "flaregun", "shotgun"})
        if (has(n, k)) return Rarity::Legendary;
    if (n == "gear_bow" || n.rfind("gear_bow_", 0) == 0 || has(n, "_bow")) return Rarity::Legendary;
    // Epic: the best clothes (hides, expedition gear, ClothingExpanded's) and the best tools
    for (const char* k : {"moosehide", "wolfskin", "bearskin", "bearhide", "deerskin", "rabbitskin", "toolbelt", "crampons",
                          "expedition", "mountaineering", "mukluk", "insulated", "combat", "heavyhammer", "maplesyrup",
                          "magnifyinglens", "firestriker", "stormlantern", "flashlight", "skiboots", "skijacket",
                          "goosedown", "parka", "cowichan", "quilted", "fishermans"})
        if (has(n, k)) return Rarity::Epic;
    // Rare: tools, medicine, light
    for (const char* k : {"hatchet", "knife", "prybar", "hacksaw", "whetstone", "sewingkit", "firstaid", "antibiotic",
                          "painkiller", "antiseptic", "hydrogen", "lamp", "lantern", "binocular", "canopener", "hammer",
                          "climbingrope", "rope", "fishingline", "hookandline", "hook", "spear", "flare", "torch",
                          "accelerant", "firelog", "matches", "watertight", "tinderplug", "mre", "noodle", "condensed",
                          "chocolate", "coffee", "herbaltea", "greentea", "rosehiptea", "reishitea", "birchbarktea"})
        if (has(n, k)) return Rarity::Rare;
    // Uncommon: clothing, preserved food, the sewn and the cured
    for (const char* k : {"coat", "jacket", "sweater", "vest", "boots", "shoes", "pants", "jeans", "toque", "hat", "cap",
                          "gloves", "mittens", "scarf", "socks", "longjohns", "shirt", "hoodie", "bandage", "snare",
                          "peaches", "pinnacle", "tomatosoup", "beefjerky", "jerky", "granola", "energybar", "soda",
                          "dogfood", "sardines", "beans", "ketchup", "milk", "bag", "backpack", "leatherdried",
                          "leatherhidedried", "gutdried", "rabbitpelt", "wolfpelt", "bearhidedried"})
        if (has(n, k)) return Rarity::Uncommon;
    // keys, notes, collectibles: none
    for (const char* k : {"key", "note", "collectible", "journal", "lostandfound", "photo", "postcard", "map"})
        if (has(n, k)) return Rarity::None;
    return Rarity::Common;
}

Color colour(Rarity r, float a) {
    switch (r) {  // CS-like: grey, green, blue, purple, gold, red
        case Rarity::Common: return {0.62f, 0.62f, 0.62f, a};
        case Rarity::Uncommon: return {0.30f, 0.78f, 0.30f, a};
        case Rarity::Rare: return {0.25f, 0.52f, 1.00f, a};
        case Rarity::Epic: return {0.66f, 0.32f, 0.96f, a};
        case Rarity::Legendary: return {1.00f, 0.72f, 0.12f, a};
        case Rarity::Mythic: return {0.95f, 0.22f, 0.22f, a};
        default: return {1, 1, 1, a};
    }
}

std::string object_name(Il2CppObject* component) {
    if (!alive(component)) return {};
    Il2CppObject* go = il2::invoke(api.component_go, component);
    Il2CppObject* s = go ? il2::invoke(api.get_name, go) : nullptr;
    return s ? std::string(net::gear_base_name(il2::to_utf8(reinterpret_cast<Il2CppString*>(s)))) : std::string();
}

void tint(Il2CppObject* square) {
    if (!alive(square) || api.button < 0) return;
    Il2CppObject* button = il2::read_field<Il2CppObject*>(square, api.button);
    if (!alive(button)) return;
    if (!g_have_default) {
        if (!il2::invoke_value(api.get_default, button, g_default)) return;
        g_default_hover = il2::read_field<Color>(button, api.hover);
        g_default_pressed = il2::read_field<Color>(button, api.pressed);
        g_have_default = true;
        logx::info("rarity: the squares' own colour %.2f %.2f %.2f %.2f", g_default.r, g_default.g, g_default.b, g_default.a);
    }
    Il2CppObject* gear = api.gear >= 0 ? il2::read_field<Il2CppObject*>(square, api.gear) : nullptr;
    Il2CppObject* deco = api.decoration >= 0 ? il2::read_field<Il2CppObject*>(square, api.decoration) : nullptr;
    const std::string n = alive(gear) ? object_name(gear) : alive(deco) ? object_name(deco) : std::string();
    const Rarity r = n.empty() ? Rarity::None : of(n);
    Color c = r == Rarity::None ? g_default : colour(r, 0.125f);
    void* a[] = {&c};
    il2::invoke(api.set_default, button, a);
    put_color(button, api.hover, r == Rarity::None ? g_default_hover : colour(r, 0.20f));
    put_color(button, api.pressed, r == Rarity::None ? g_default_pressed : colour(r, 0.25f));
    bool instant = true;
    void* ua[] = {&instant};
    if (api.update_color) il2::invoke(api.update_color, button, ua);
}

using RefreshFn = void (*)(Il2CppObject*, Il2CppObject*, int32_t, const MethodInfo*);
using RefreshEmptyFn = void (*)(Il2CppObject*, int32_t, const MethodInfo*);
RefreshFn g_refresh = nullptr, g_refresh_deco = nullptr;
RefreshEmptyFn g_refresh_empty = nullptr;

void refresh_detour(Il2CppObject* self, Il2CppObject* gi, int32_t index, const MethodInfo* m) {
    g_refresh(self, gi, index, m);
    tint(self);
}
void refresh_deco_detour(Il2CppObject* self, Il2CppObject* di, int32_t index, const MethodInfo* m) {
    g_refresh_deco(self, di, index, m);
    tint(self);
}
void refresh_empty_detour(Il2CppObject* self, int32_t index, const MethodInfo* m) {
    g_refresh_empty(self, index, m);
    tint(self);
}

std::map<std::string, Rarity> g_cache;

}  // namespace

Rarity of(const std::string& gear_name) {
    const auto it = g_cache.find(gear_name);
    if (it != g_cache.end()) return it->second;
    const Rarity r = rule(lower(gear_name));
    g_cache.emplace(gear_name, r);
    return r;
}

const char* name(Rarity r) {
    static const char* kNames[] = {"none", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Mythic"};
    return kNames[static_cast<int>(r)];
}

bool init() {
    const auto ini = paths::instance_dir() / L"coop.ini";
    if (!GetPrivateProfileIntW(L"rarity", L"enabled", 1, ini.c_str())) {
        logx::info("rarity: off (coop.ini [rarity] enabled=0)");
        return false;
    }
    Il2CppClass* square = il2::find_class(kGame, "", "InventoryGridItem");
    Il2CppClass* bc = il2::find_class(kGame, "", "UIButtonColor");
    Il2CppClass* comp = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    if (!square || !bc || !comp || !object) {
        logx::warn("rarity: UNAVAILABLE (TLD classes missing)");
        return false;
    }
    api.button = il2::field_offset_quiet(square, "m_Button");
    api.gear = il2::field_offset_quiet(square, "m_GearItem");
    api.decoration = il2::field_offset_quiet(square, "m_DecorationItem");
    api.hover = il2::field_offset_quiet(bc, "hover");
    api.pressed = il2::field_offset_quiet(bc, "pressed");
    api.get_default = il2::find_method_quiet(bc, "get_defaultColor", 0);
    api.set_default = il2::find_method_quiet(bc, "set_defaultColor", 1);
    api.update_color = il2::find_method_quiet(bc, "UpdateColor", 1);
    api.component_go = il2::find_method(comp, "get_gameObject", 0);
    api.get_name = il2::find_method(object, "get_name", 0);
    const MethodInfo* refresh = il2::find_method_params_quiet(square, "Refresh", {"GearItem", "System.Int32"});
    const MethodInfo* deco = il2::find_method_quiet(square, "RefreshDecorationItem", 2);
    const MethodInfo* empty = il2::find_method_quiet(square, "RefreshEmptyItem", 1);
    if (api.button < 0 || api.gear < 0 || api.hover < 0 || api.pressed < 0 || !api.get_default || !api.set_default ||
        !refresh || !empty) {
        logx::warn("rarity: UNAVAILABLE (TLD API missing)");
        return false;
    }
    bool ok = hooks::install_method("InventoryGridItem.Refresh (rarity)", refresh, reinterpret_cast<void*>(&refresh_detour),
                                    reinterpret_cast<void**>(&g_refresh)) &&
              hooks::install_method("InventoryGridItem.RefreshEmptyItem (rarity)", empty,
                                    reinterpret_cast<void*>(&refresh_empty_detour), reinterpret_cast<void**>(&g_refresh_empty));
    if (ok && deco)
        hooks::install_method("InventoryGridItem.RefreshDecorationItem (rarity)", deco, reinterpret_cast<void*>(&refresh_deco_detour),
                              reinterpret_cast<void**>(&g_refresh_deco));
    logx::info("rarity: %s (inventory and container squares tinted by rarity; coop.ini [rarity] enabled)", ok ? "ready" : "hooks FAILED");
    return ok;
}

}  // namespace item_rarity
