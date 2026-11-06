#include "fuel_transfer.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "coop_lapse.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/session.h"

// TLDCoop_DLC: draining a kerosene lamp into the pack's kerosene containers (Better Fuel Management's "Drain", by
// WulfMarius and ds5678; Karol 06.11: "sama funkcja przelania paliwa z lampy naftowej do kanistra", with the author's
// permission). The examine screen's left UNLOAD button (a gun's) is ours for a lamp: DRAIN FUEL, below the others, off
// when there's nothing to drain or nowhere to put it. A click: TLD's progress bar for 3 s with the refuel sound; at its
// end (or cancelled: that share) the lamp's fuel goes into the kerosene containers, each filled to its capacity.
namespace fuel_transfer {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;                  // UnityEngine.Object.m_CachedPtr: null once gone
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;  // List<T>._items, _size
constexpr size_t kItemEntry = 16;                       // GearItemObject in an array: GearItem m_GearItem, string name
constexpr int64_t kUnitsPerLiter = 1000000000;          // TLD.IntBackedUnit.ItemLiquidVolume: 1e-9 l
constexpr int64_t kMinUnits = kUnitsPerLiter / 1000;    // 1 ml: less isn't worth a drain
constexpr float kSeconds = 3;                           // the mod's REFUEL_TIME
constexpr const char* kLabel = "DRAIN FUEL";
constexpr const char* kProgress = "Draining fuel";
constexpr const char* kSound = "Play_SndActionRefuelLantern";

struct Vec3 {
    float x, y, z;
};

struct Api {
    // Panel_Inventory_Examine
    ptrdiff_t gear = -1;            // m_GearItem
    ptrdiff_t harvest = -1, repair = -1, refuel = -1, unload = -1;  // m_Button_* (UIButton)
    ptrdiff_t spacing = -1;         // m_ButtonSpacing
    ptrdiff_t unload_item = -1;     // m_MenuItemUnload (Panel_Inventory_Examine_MenuItem)
    ptrdiff_t rifle_panel = -1;     // m_RifleUnloadPanel (GameObject)
    const MethodInfo* refresh = nullptr;  // RefreshMainWindow()
    // Panel_Inventory_Examine_MenuItem
    ptrdiff_t item_label = -1;      // m_LabelTitle (UILabel)
    ptrdiff_t item_disabled = -1;   // m_Disabled
    const MethodInfo* item_set_disabled = nullptr;  // SetDisabled(bool)
    const MethodInfo* item_set_selected = nullptr;  // SetSelected(bool)
    // gear
    ptrdiff_t lamp = -1;            // GearItem.m_KeroseneLampItem
    ptrdiff_t liquid = -1;          // GearItem.m_LiquidItem
    ptrdiff_t lamp_fuel = -1;       // KeroseneLampItem.m_CurrentFuelLiters (ItemLiquidVolume)
    ptrdiff_t liquid_type = -1;     // LiquidItem.m_LiquidType
    ptrdiff_t liquid_now = -1;      // LiquidItem.m_Liquid (ItemLiquidVolume)
    ptrdiff_t liquid_cap = -1;      // LiquidItem.m_LiquidCapacity (ItemLiquidVolume)
    const MethodInfo* kerosene = nullptr;       // static LiquidType.GetKerosene()
    const MethodInfo* inventory = nullptr;      // static GameManager.GetInventoryComponent()
    ptrdiff_t inv_items = -1;                   // Inventory.m_Items (List<GearItemObject>)
    // the progress bar
    Il2CppClass* bar_class = nullptr;
    const MethodInfo* load_panel = nullptr;     // static InterfaceManager.LoadPanel(Type)
    const MethodInfo* bar_launch = nullptr;     // Launch(string, float, float, float, string audio, string voice,
                                                //        bool, bool skipRestoreInHands, OnExitDelegate)
    ptrdiff_t bar_label = -1;                   // m_ProgressBarNameLabel (UILabel)
    // Unity / NGUI
    const MethodInfo* go_of = nullptr;          // Component.get_gameObject()
    const MethodInfo* transform_of = nullptr;   // Component.get_transform()
    const MethodInfo* in_children = nullptr;    // Component.GetComponentInChildren(Type, bool)
    const MethodInfo* active_self = nullptr;    // GameObject.get_activeSelf()
    const MethodInfo* in_hierarchy = nullptr;   // GameObject.get_activeInHierarchy()
    const MethodInfo* set_active = nullptr;     // GameObject.SetActive(bool)
    const MethodInfo* get_local = nullptr;      // Transform.get_localPosition()
    const MethodInfo* set_local = nullptr;      // Transform.set_localPosition(Vector3)
    const MethodInfo* set_enabled = nullptr;    // Behaviour.set_enabled(bool)
    const MethodInfo* label_get = nullptr;      // UILabel.get_text()
    const MethodInfo* label_set = nullptr;      // UILabel.set_text(string)
    const MethodInfo* sprite_get = nullptr;     // UIButton.get_normalSprite()
    const MethodInfo* sprite_set = nullptr;     // UIButton.set_normalSprite(string)
    Il2CppObject* localize_type = nullptr;      // typeof(UILocalize)
} api;

bool g_ready = false;
bool g_on = true;  // coop.ini [fuel] drain

// The UNLOAD button as TLD had it, while it's ours (a lamp's examine screen).
struct Taken {
    bool taken = false;
    uintptr_t sprite = 0;  // gchandle: its normalSprite (string)
} g_taken;

// A drain under way: the panel and the lamp (gchandles), until the bar ends.
struct Drain {
    uintptr_t panel = 0, gear = 0;
    double started = 0;
} g_drain;

using VoidFn = void (*)(Il2CppObject*, const MethodInfo*);
using SelectFn = void (*)(Il2CppObject*, bool, const MethodInfo*);
VoidFn g_refresh = nullptr, g_on_select = nullptr, g_on_unload = nullptr;
SelectFn g_select = nullptr;

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) { return il2::read_field<T>(o, off); }
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}

bool alive(Il2CppObject* o) { return o && get<void*>(o, kCachedPtr) != nullptr; }
Il2CppObject* go_of(Il2CppObject* c) { return alive(c) ? il2::invoke(api.go_of, c) : nullptr; }

bool flag(const MethodInfo* m, Il2CppObject* o) {
    bool v = false;
    return alive(o) && il2::invoke_value(m, o, v) && v;
}

void set_active(Il2CppObject* go, bool on) {
    if (!alive(go)) return;
    void* args[] = {&on};
    il2::invoke(api.set_active, go, args);
}

void set_enabled(Il2CppObject* behaviour, bool on) {
    if (!alive(behaviour)) return;
    void* args[] = {&on};
    il2::invoke(api.set_enabled, behaviour, args);
}

Il2CppObject* in_children(Il2CppObject* component, Il2CppObject* type) {
    if (!alive(component) || !type) return nullptr;
    bool inactive_too = true;
    void* args[] = {type, &inactive_too};
    Il2CppObject* found = il2::invoke(api.in_children, component, args);
    return alive(found) ? found : nullptr;
}

void set_text(Il2CppObject* label, const char* text) {
    if (!alive(label)) return;
    void* args[] = {il2::il2cpp_string_new(text)};
    il2::invoke(api.label_set, label, args);
}

Il2CppObject* from(uintptr_t handle) { return handle ? il2::il2cpp_gchandle_get_target(handle) : nullptr; }
void drop(uintptr_t& handle) {
    if (handle) il2::il2cpp_gchandle_free(handle);
    handle = 0;
}

Il2CppObject* lamp_of(Il2CppObject* gear) {
    Il2CppObject* lamp = alive(gear) ? get<Il2CppObject*>(gear, api.lamp) : nullptr;
    return alive(lamp) ? lamp : nullptr;
}

// The pack's kerosene containers (their LiquidItems), not the lamp.
template <typename F>
void each_kerosene(F&& f) {
    Il2CppObject* kerosene = il2::invoke(api.kerosene, nullptr);
    Il2CppObject* inv = kerosene ? il2::invoke(api.inventory, nullptr) : nullptr;
    Il2CppObject* list = inv ? get<Il2CppObject*>(inv, api.inv_items) : nullptr;
    if (!list) return;
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    const int32_t count = get<int32_t>(list, kListSize);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* gi = *reinterpret_cast<Il2CppObject* const*>(reinterpret_cast<const char*>(items) + 0x20 +
                                                                   static_cast<size_t>(i) * kItemEntry);
        Il2CppObject* liquid = alive(gi) ? get<Il2CppObject*>(gi, api.liquid) : nullptr;
        if (alive(liquid) && get<Il2CppObject*>(liquid, api.liquid_type) == kerosene) f(liquid);
    }
}

int64_t room_in_pack() {
    int64_t room = 0;
    each_kerosene([&](Il2CppObject* l) { room += std::max<int64_t>(0, get<int64_t>(l, api.liquid_cap) - get<int64_t>(l, api.liquid_now)); });
    return room;
}

// What a drain would move now: the lamp's fuel, as much as the containers hold.
int64_t drainable(Il2CppObject* gear) {
    Il2CppObject* lamp = lamp_of(gear);
    if (!lamp) return 0;
    return std::min(std::max<int64_t>(0, get<int64_t>(lamp, api.lamp_fuel)), room_in_pack());
}

bool is_lamp_screen(Il2CppObject* panel) { return g_ready && g_on && alive(panel) && lamp_of(get<Il2CppObject*>(panel, api.gear)); }

Il2CppObject* button(Il2CppObject* panel, ptrdiff_t off) {
    Il2CppObject* b = get<Il2CppObject*>(panel, off);
    return alive(b) ? b : nullptr;
}

// The UNLOAD button's words and icon: ours (DRAIN FUEL, the refuel icon) or TLD's back.
void dress(Il2CppObject* panel, bool ours) {
    Il2CppObject* unload = button(panel, api.unload);
    Il2CppObject* item = get<Il2CppObject*>(panel, api.unload_item);
    Il2CppObject* label = alive(item) ? get<Il2CppObject*>(item, api.item_label) : nullptr;
    if (!unload || !alive(label)) return;
    if (ours) {
        if (!g_taken.taken) {
            Il2CppObject* sprite = il2::invoke(api.sprite_get, unload);
            g_taken.sprite = sprite ? il2::il2cpp_gchandle_new(sprite, false) : 0;
            g_taken.taken = true;
        }
        // Its UILocalize would put "Unload" back: off meanwhile (on again, it puts TLD's word back itself).
        set_enabled(in_children(label, api.localize_type), false);
        set_text(label, kLabel);
        if (Il2CppObject* refuel = button(panel, api.refuel)) {
            if (Il2CppObject* icon = il2::invoke(api.sprite_get, refuel)) {
                void* args[] = {icon};
                il2::invoke(api.sprite_set, unload, args);
            }
        }
    } else if (g_taken.taken) {
        set_enabled(in_children(label, api.localize_type), true);
        if (Il2CppObject* sprite = from(g_taken.sprite)) {
            void* args[] = {sprite};
            il2::invoke(api.sprite_set, unload, args);
        }
        drop(g_taken.sprite);
        g_taken.taken = false;
    }
}

// The mod's place for it: below the lowest of HARVEST, REFUEL and REPAIR.
void place(Il2CppObject* panel) {
    Il2CppObject* unload = button(panel, api.unload);
    Il2CppObject* unload_tf = unload ? il2::invoke(api.transform_of, unload) : nullptr;
    if (!unload_tf) return;
    bool found = false;
    Vec3 low{0, 1000, 0};
    for (ptrdiff_t off : {api.harvest, api.refuel, api.repair}) {
        Il2CppObject* b = button(panel, off);
        if (!b || !flag(api.active_self, go_of(b))) continue;
        Vec3 p{};
        if (il2::invoke_value(api.get_local, il2::invoke(api.transform_of, b), p) && p.y < low.y) low = p, found = true;
    }
    if (!found) return;  // nothing to line up with: where TLD has it
    low.y += get<float>(panel, api.spacing);
    void* args[] = {&low};
    il2::invoke(api.set_local, unload_tf, args);
}

void refresh_detour(Il2CppObject* self, const MethodInfo* m) {
    g_refresh(self, m);
    if (!is_lamp_screen(self)) {
        if (g_ready && alive(self)) dress(self, false);
        return;
    }
    Il2CppObject* unload = button(self, api.unload);
    if (!unload) return;
    dress(self, true);
    place(self);
    set_active(go_of(unload), true);
    Il2CppObject* item = get<Il2CppObject*>(self, api.unload_item);
    bool off = drainable(get<Il2CppObject*>(self, api.gear)) < kMinUnits;
    if (alive(item)) {
        void* args[] = {&off};
        il2::invoke(api.item_set_disabled, item, args);
    }
}

// The bar's end: the lamp's fuel (that share of it, if cancelled) into the pack's kerosene containers.
void drained(bool success, bool cancel, float progress) {
    Il2CppObject* panel = from(g_drain.panel);
    Il2CppObject* gear = from(g_drain.gear);
    drop(g_drain.panel);
    drop(g_drain.gear);
    const bool stale = net::now_ms() - g_drain.started > (kSeconds + 60) * 1000.0;
    Il2CppObject* lamp = lamp_of(gear);
    if (!lamp || stale) return;
    int64_t move = static_cast<int64_t>(static_cast<double>(drainable(gear)) * std::clamp(progress, 0.0f, 1.0f));
    if (move < kMinUnits) {
        logx::info("fuel: drain %s, nothing moved", cancel ? "cancelled" : success ? "done" : "failed");
        return;
    }
    int64_t left = move;
    each_kerosene([&](Il2CppObject* l) {
        const int64_t now = get<int64_t>(l, api.liquid_now);
        const int64_t add = std::min(left, std::max<int64_t>(0, get<int64_t>(l, api.liquid_cap) - now));
        if (add <= 0) return;
        put<int64_t>(l, api.liquid_now, now + add);
        left -= add;
    });
    const int64_t moved = move - left;
    put<int64_t>(lamp, api.lamp_fuel, std::max<int64_t>(0, get<int64_t>(lamp, api.lamp_fuel) - moved));
    logx::info("fuel: drained %.3f l from the lamp into the pack's kerosene%s", moved / double(kUnitsPerLiter),
               cancel ? " (cancelled part way)" : "");
    if (alive(panel) && flag(api.in_hierarchy, go_of(panel))) il2::invoke(api.refresh, panel);
}

void start_drain(Il2CppObject* panel) {
    Il2CppObject* gear = get<Il2CppObject*>(panel, api.gear);
    if (g_drain.gear || drainable(gear) < kMinUnits) return;
    g_drain.panel = il2::il2cpp_gchandle_new(panel, false);
    g_drain.gear = il2::il2cpp_gchandle_new(gear, false);
    g_drain.started = net::now_ms();
    void* load_args[] = {il2::type_object(api.bar_class)};
    Il2CppObject* bar = api.load_panel ? il2::invoke(api.load_panel, nullptr, load_args) : nullptr;
    if (!bar || !api.bar_launch || !coop_lapse::on_bar_end(&drained)) {  // no bar to wait on: at once
        logx::info("fuel: draining at once (TLD's progress bar isn't available)");
        drained(true, false, 1.0f);
        return;
    }
    float seconds = kSeconds, minutes = 0, failure = 0;
    bool quiet_breath = false, skip_restore = false;
    void* args[] = {il2::il2cpp_string_new(kProgress), &seconds, &minutes, &failure, il2::il2cpp_string_new(kSound),
                    nullptr, &quiet_breath, &skip_restore, nullptr};
    il2::invoke(api.bar_launch, bar, args);
    set_text(get<Il2CppObject*>(bar, api.bar_label), kProgress);
}

// A click on DRAIN FUEL (a gun's UNLOAD otherwise).
void on_select_detour(Il2CppObject* self, const MethodInfo* m) {
    if (!is_lamp_screen(self)) return g_on_select(self, m);
    Il2CppObject* item = get<Il2CppObject*>(self, api.unload_item);
    if (alive(item) && get<bool>(item, api.item_disabled)) return;
    start_drain(self);
}

// The action of the selected button (a controller's Continue): DRAIN FUEL for a lamp.
void on_unload_detour(Il2CppObject* self, const MethodInfo* m) {
    if (!is_lamp_screen(self)) return g_on_unload(self, m);
    start_drain(self);
}

// Selected for a lamp: the button lit, no gun's unload panel on the right.
void select_detour(Il2CppObject* self, bool selected, const MethodInfo* m) {
    if (!is_lamp_screen(self)) return g_select(self, selected, m);
    Il2CppObject* item = get<Il2CppObject*>(self, api.unload_item);
    if (alive(item)) {
        void* args[] = {&selected};
        il2::invoke(api.item_set_selected, item, args);
    }
    set_active(get<Il2CppObject*>(self, api.rifle_panel), false);
}

}  // namespace

bool init() {
    Il2CppClass* panel = il2::find_class(kGame, "", "Panel_Inventory_Examine");
    Il2CppClass* item = il2::find_class(kGame, "", "Panel_Inventory_Examine_MenuItem");
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* lamp = il2::find_class(kGame, "TLD.Gear", "KeroseneLampItem");
    Il2CppClass* liquid = il2::find_class(kGame, "", "LiquidItem");
    Il2CppClass* liquid_type = il2::find_class(kGame, "TLD.Gear", "LiquidType");
    Il2CppClass* inventory = il2::find_class(kGame, "", "Inventory");
    Il2CppClass* game_manager = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* game_object = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* label = il2::find_class(kGame, "", "UILabel");
    Il2CppClass* ui_button = il2::find_class(kGame, "", "UIButton");
    if (!panel || !item || !gear || !lamp || !liquid || !liquid_type || !inventory || !game_manager || !component ||
        !game_object || !transform || !label || !ui_button) {
        logx::warn("fuel: UNAVAILABLE (TLD classes missing)");
        return false;
    }
    api.gear = il2::field_offset(panel, "m_GearItem");
    api.harvest = il2::field_offset(panel, "m_Button_Harvest");
    api.repair = il2::field_offset(panel, "m_Button_Repair");
    api.refuel = il2::field_offset(panel, "m_Button_Refuel");
    api.unload = il2::field_offset(panel, "m_Button_Unload");
    api.spacing = il2::field_offset(panel, "m_ButtonSpacing");
    api.unload_item = il2::field_offset(panel, "m_MenuItemUnload");
    api.rifle_panel = il2::field_offset(panel, "m_RifleUnloadPanel");
    api.refresh = il2::find_method(panel, "RefreshMainWindow", 0);
    api.item_label = il2::field_offset(item, "m_LabelTitle");
    api.item_disabled = il2::field_offset(item, "m_Disabled");
    api.item_set_disabled = il2::find_method(item, "SetDisabled", 1);
    api.item_set_selected = il2::find_method(item, "SetSelected", 1);
    api.lamp = il2::field_offset(gear, "m_KeroseneLampItem");
    api.liquid = il2::field_offset(gear, "m_LiquidItem");
    api.lamp_fuel = il2::field_offset(lamp, "m_CurrentFuelLiters");
    api.liquid_type = il2::field_offset(liquid, "m_LiquidType");
    api.liquid_now = il2::field_offset(liquid, "m_Liquid");
    api.liquid_cap = il2::field_offset(liquid, "m_LiquidCapacity");
    api.kerosene = il2::find_method(liquid_type, "GetKerosene", 0);
    api.inventory = il2::find_method(game_manager, "GetInventoryComponent", 0);
    api.inv_items = il2::field_offset(inventory, "m_Items");
    api.bar_class = il2::find_class(kGame, "", "Panel_GenericProgressBar");
    api.load_panel = il2::find_method_params(il2::find_class(kGame, "", "InterfaceManager"), "LoadPanel", {"System.Type"});
    api.bar_launch = api.bar_class ? il2::find_method_params(api.bar_class, "Launch",
                                                             {"System.String", "System.Single", "System.Single",
                                                              "System.Single", "System.String", "System.String",
                                                              "System.Boolean", "System.Boolean", "OnExitDelegate"})
                                   : nullptr;
    api.bar_label = api.bar_class ? il2::field_offset(api.bar_class, "m_ProgressBarNameLabel") : -1;
    api.go_of = il2::find_method(component, "get_gameObject", 0);
    api.transform_of = il2::find_method(component, "get_transform", 0);
    api.in_children = il2::find_method_params(component, "GetComponentInChildren", {"System.Type", "System.Boolean"});
    api.active_self = il2::find_method(game_object, "get_activeSelf", 0);
    api.in_hierarchy = il2::find_method(game_object, "get_activeInHierarchy", 0);
    api.set_active = il2::find_method(game_object, "SetActive", 1);
    api.get_local = il2::find_method(transform, "get_localPosition", 0);
    api.set_local = il2::find_method(transform, "set_localPosition", 1);
    api.set_enabled = il2::find_method(il2::find_class(kCore, "UnityEngine", "Behaviour"), "set_enabled", 1);
    api.label_get = il2::find_method(label, "get_text", 0);
    api.label_set = il2::find_method(label, "set_text", 1);
    api.sprite_get = il2::find_method(ui_button, "get_normalSprite", 0);
    api.sprite_set = il2::find_method(ui_button, "set_normalSprite", 1);
    if (Il2CppClass* localize = il2::find_class(kGame, "", "UILocalize")) {
        api.localize_type = il2::type_object(localize);
        if (api.localize_type) il2::il2cpp_gchandle_new(api.localize_type, false);
    }
    const MethodInfo* on_select = il2::find_method(panel, "OnSelectUnload", 0);
    const MethodInfo* on_unload = il2::find_method(panel, "OnUnload", 0);
    const MethodInfo* select = il2::find_method(panel, "SelectUnloadButton", 1);
    const bool fields = api.gear >= 0 && api.harvest >= 0 && api.repair >= 0 && api.refuel >= 0 && api.unload >= 0 &&
                        api.spacing >= 0 && api.unload_item >= 0 && api.rifle_panel >= 0 && api.refresh &&
                        api.item_label >= 0 && api.item_disabled >= 0 && api.item_set_disabled &&
                        api.item_set_selected && api.lamp >= 0 && api.liquid >= 0 && api.lamp_fuel >= 0 &&
                        api.liquid_type >= 0 && api.liquid_now >= 0 && api.liquid_cap >= 0 && api.kerosene &&
                        api.inventory && api.inv_items >= 0 && api.go_of && api.transform_of && api.in_children &&
                        api.active_self && api.in_hierarchy && api.set_active && api.get_local && api.set_local &&
                        api.set_enabled && api.label_set && api.sprite_get && api.sprite_set && api.localize_type &&
                        on_select && on_unload && select;
    if (!fields) {
        logx::warn("fuel: UNAVAILABLE (TLD API missing; see warnings above)");
        return false;
    }
    auto hook = [](const char* name, const MethodInfo* m, auto detour, auto original) {
        return hooks::install_method(name, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    g_ready = hook("Panel_Inventory_Examine.RefreshMainWindow (fuel)", api.refresh, &refresh_detour, &g_refresh) &&
              hook("Panel_Inventory_Examine.OnSelectUnload (fuel)", on_select, &on_select_detour, &g_on_select) &&
              hook("Panel_Inventory_Examine.OnUnload (fuel)", on_unload, &on_unload_detour, &g_on_unload) &&
              hook("Panel_Inventory_Examine.SelectUnloadButton (fuel)", select, &select_detour, &g_select);
    logx::info("fuel: %s", g_ready ? "ready (a lamp's examine screen: DRAIN FUEL)" : "UNAVAILABLE (hooks failed)");
    return g_ready;
}

void set_settings(const config::Settings& s) { g_on = s.fuel_drain; }

}  // namespace fuel_transfer
