#include "world_items.h"

#include <windows.h>

#include <algorithm>
#include <initializer_list>
#include <tuple>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <array>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "coop_notices.h"
#include "core.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/claims.h"
#include "net/uniques.h"
#include "net_runtime.h"
#include "seamless_interiors.h"
#include "wildlife_carcass.h"

namespace world_items {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr double kScanMs = 500;        // loose items are checked twice a second
constexpr double kSettleMs = 1000;     // an item has to stop moving this long before the other player hears where it is
// TLDCoop_DLC: a burning flare or torch on the ground (one just thrown, mostly) is told as soon as a scan finds it
// still: it lights the way and keeps wolves off, so the friend should see it at once, not a second and a half later.
constexpr double kSettleLitMs = 0;
// TLDCoop_DLC: the host tells its friends where a pot that is cooking (food, snow, water) is this often, so it is
// ready at the same moment in every game (each game's own cooking skill and fire made it differ by minutes).
constexpr double kPotSyncMs = 5000;
// TLDCoop_DLC: the host tells how long a burning flare or torch on the ground has burnt this often, so it goes out at
// the same moment everywhere.
constexpr double kBurnSyncMs = 10000;
// TLDCoop_DLC: while a burning flare or torch flies (just thrown), loose items are checked this often and where it is
// goes out at once, not after it lands and lies still.
constexpr double kFastScanMs = 100;
constexpr double kFastScanForMs = 3000;
constexpr float kStillMetres = 0.02f;  // moving less than this between scans: still
constexpr float kMovedMetres = 0.3f;   // closer than this to where the other player has it: the same place
constexpr double kListGraceMs = 5000;  // client: our own changes this recent aren't undone by the host's list
constexpr ptrdiff_t kListItems = 0x10, kListSize = 0x18;
constexpr ptrdiff_t kCachedPtr = 0x10;  // UnityEngine.Object.m_CachedPtr: null once the native object is gone
constexpr size_t kInventoryEntry = 16;  // TLD.Gear.GearItemObject in an array: GearItem m_GearItem, string m_GearItemName

struct Vector3 {
    float x, y, z;
};
struct Quaternion {
    float x, y, z, w;
};

struct Api {
    // Unity
    const MethodInfo* component_go = nullptr;
    const MethodInfo* component_transform = nullptr;
    const MethodInfo* go_active = nullptr;     // GameObject.get_activeInHierarchy
    const MethodInfo* go_set_active = nullptr; // TLDCoop_DLC: GameObject.SetActive(bool)
    Il2CppObject* container_type = nullptr;    // TLDCoop_DLC: typeof(Container)
    const MethodInfo* go_component = nullptr;  // GameObject.GetComponent(Type)
    const MethodInfo* get_position = nullptr;
    const MethodInfo* get_rotation = nullptr;
    const MethodInfo* get_parent = nullptr;
    const MethodInfo* set_parent = nullptr;  // Transform.SetParent(Transform, bool) (TLDCoop_DLC)
    const MethodInfo* set_pose = nullptr;      // Transform.SetPositionAndRotation(Vector3, Quaternion)
    const MethodInfo* object_name = nullptr;
    // GearItem
    Il2CppObject* gear_type = nullptr;
    ptrdiff_t in_inventory = -1, no_save = -1, object_guid = -1, radial_parent = -1, place_point = -1;
    // TLDCoop_DLC (07.11): a friend's item placed on a fire's cooking slot sits on our slot too
    Il2CppObject* slot_type = nullptr;            // typeof(GearPlacePoint)
    const MethodInfo* find_all = nullptr;         // static Object.FindObjectsOfType(Type, bool)
    const MethodInfo* slot_place = nullptr;       // GearPlacePoint.PlaceGear(GearItem, bool reposition)
    ptrdiff_t slot_gear = -1;                     // GearPlacePoint.m_PlacedGear
    ptrdiff_t narrative = -1;                      // m_NarrativeCollectibleItem: a note, key, postcard... (one of a kind)
    const MethodInfo* create_save_data = nullptr;  // GearItem.CreateSaveData(): TLD's save record for it
    ptrdiff_t sd_prefab = -1, sd_gear = -1;        // GearSaveData.m_PrefabName / m_SearializedGear
    const MethodInfo* instantiate = nullptr;       // static GearItem.InstantiateAndDeserializeGearItem(string, string, Transform, bool, bool)
    const MethodInfo* sanitize = nullptr;          // static Utils.SanitizePrefabName(string)
    const MethodInfo* drop = nullptr;              // GearItem.Drop(int, bool, bool, bool)
    const MethodInfo* display_name = nullptr;      // static GearItem.GetGearDisplayName(string prefab)
    ptrdiff_t stackable = -1, units = -1;          // GearItem.m_StackableItem, StackableItem.m_Units
    // ObjectGuid / PdidTable
    ptrdiff_t guid = -1;                           // ObjectGuid.m_Guid (legacy)
    ptrdiff_t cached_pdid = -1;                    // PdidObjectBase.m_RuntimeCachedPdid (what get_PDID returns)
    const MethodInfo* guid_of_go = nullptr;        // static ObjectGuid.GetObjectGuidFromGO(GameObject)
    const MethodInfo* attach_guid = nullptr;       // static ObjectGuid.MaybeAttachObjectGuidAndRegister(GameObject, string)
    const MethodInfo* pdid_object = nullptr;       // static PdidTable.GetGameObject(string)
    const MethodInfo* new_pdid = nullptr;          // static PdidTable.GenerateNewID()
    // GearManager
    FieldInfo* all_gear = nullptr;                 // static GearManager.m_Gear
    const MethodInfo* destroy_gear = nullptr;      // static GearManager.DestroyGearObject(GameObject)
    const MethodInfo* serialize_all = nullptr;     // static GearManager.Serialize()
    Il2CppObject* held_by = nullptr;               // gear whose parent has this component isn't loose (see resolve_held_by)
    bool held_by_resolved = false;
    // Player
    const MethodInfo* player_manager = nullptr;    // static GameManager.GetPlayerManagerComponent()
    const MethodInfo* inventory = nullptr;         // static GameManager.GetInventoryComponent()
    const MethodInfo* inspected = nullptr;         // PlayerManager.GearItemBeingInspected() (the last one: check
    const MethodInfo* inspecting = nullptr;        // PlayerManager.IsInspectModeActive() first)
    const MethodInfo* put_back = nullptr;          // PlayerManager.ExitInspectGearMode(bool): what "Leave it" calls
    ptrdiff_t pickup_action = -1, put_back_action = -1;  // PlayerManager.m_PickupAction / m_PutBackAction (Action)
    ptrdiff_t placing = -1;                        // PlayerManager.m_ObjectToPlaceGearItem
    const MethodInfo* pickup = nullptr;            // PlayerManager.ProcessPickupItemInteraction(GearItem, bool, bool, bool)
    const MethodInfo* inspect = nullptr;           // PlayerManager.EnterInspectGearMode(GearItem)
    ptrdiff_t inv_items = -1;                      // Inventory.m_Items (List<GearItemObject>)
    const MethodInfo* inv_destroy = nullptr;       // Inventory.DestroyGear(GearItem)
    const MethodInfo* remove_units = nullptr;      // Inventory.RemoveUnits(GearItem, int)
    // SnareItem
    ptrdiff_t snare = -1;                          // GearItem.m_SnareItem
    ptrdiff_t flare = -1, torch = -1;              // GearItem.m_FlareItem, m_TorchItem (TLDCoop_DLC: kSettleLitMs)
    const MethodInfo* flare_burning = nullptr;     // FlareItem.IsBurning()
    // TLDCoop_DLC: FlareItem / TorchItem m_ElapsedBurnMinutes, m_BurnLifetimeMinutes
    ptrdiff_t flare_elapsed = -1, flare_life = -1, torch_elapsed = -1, torch_life = -1;
    const MethodInfo* torch_burning = nullptr;     // TorchItem.IsBurning()
    ptrdiff_t snare_state = -1;                    // SnareItem.m_State (SnareState: 0 not set, 1 set, 2 broken, 3 caught)
    ptrdiff_t snare_rabbit = -1;                   // SnareItem.m_RabbitPrefab
    const MethodInfo* snare_load = nullptr;        // SnareItem.Deserialize(string)
    // TLDCoop_DLC: lamps and the state of lights (logged: "the full lamp's fuel was gone", "the flare showed ruined")
    ptrdiff_t lamp = -1;                           // GearItem.m_KeroseneLampItem
    const MethodInfo* lamp_load = nullptr;         // KeroseneLampItem.Deserialize(string)
    const MethodInfo* lamp_pct = nullptr;          // KeroseneLampItem.GetPercentFuelRemaining()
    ptrdiff_t lamp_fuel = -1;                      // KeroseneLampItem.m_CurrentFuelLiters (ItemLiquidVolume: int64, 1e-9 l)
    const MethodInfo* lamp_on = nullptr;           // KeroseneLampItem.IsOn()
    ptrdiff_t heater = -1, heater_state = -1;      // TLDCoop_DLC: GearItem.m_WhaleOilHeaterItem, its m_State
    ptrdiff_t hs_on = -1, hs_rate = -1, hs_fuel = -1;  // WhaleOilHeaterItemState.m_IsOn, m_BurnRate, m_Fuel
    const MethodInfo* heater_on = nullptr;         // WhaleOilHeaterItem.TurnOn()
    const MethodInfo* heater_off = nullptr;        // WhaleOilHeaterItem.TurnOff()
    const MethodInfo* heater_rate = nullptr;       // WhaleOilHeaterItem.SetBurnRate(int)
    const MethodInfo* gear_condition = nullptr;    // GearItem.GetRoundedCondition()
    const MethodInfo* snare_placed = nullptr;      // SnareItem.OnSnarePlaced(): set, rolling from now
    const MethodInfo* snare_set_state = nullptr;   // SnareItem.SetState(SnareState)
    const MethodInfo* snare_roll = nullptr;        // SnareItem.DoRoll()
    const MethodInfo* snare_spawn_rabbit = nullptr;  // SnareItem.SpawnDeadRabbitOnSnare(GameObject)
    // CookingPotItem (a pot, or TLD's dummy pot under food cooked without one)
    ptrdiff_t pot = -1;                            // GearItem.m_CookingPotItem
    ptrdiff_t pot_fire = -1;                       // CookingPotItem.m_FireBeingUsed
    ptrdiff_t pot_food = -1;                       // CookingPotItem.m_GearItemBeingCooked
    ptrdiff_t pot_hours = -1;                      // CookingPotItem.m_CookingElapsedHours
    ptrdiff_t evolve = -1;                         // TLDCoop_DLC: GearItem.m_EvolveItem (curing hides, guts...)
    ptrdiff_t clothing = -1, wet = -1, frozen = -1;  // TLDCoop_DLC: GearItem.m_ClothingItem, its m_PercentWet/Frozen
    ptrdiff_t hp = -1;                               // TLDCoop_DLC: GearItem.m_CurrentHP
    ptrdiff_t evolve_hours = -1;                   // EvolveItem.m_TimeSpentEvolvingGameHours
    ptrdiff_t evolve_days = -1;                    // EvolveItem.m_TimeToEvolveGameDays
    const MethodInfo* evolve_can = nullptr;        // EvolveItem.CanEvolve()
    ptrdiff_t pot_state = -1;                      // CookingPotItem.m_CookingState (0 cooking, 1 ready, 2 ruined)
    ptrdiff_t pot_cooked = -1, pot_ruined = -1;    // CookingPotItem.m_PercentCooked, m_PercentRuined
    const MethodInfo* pot_take = nullptr;          // CookingPotItem.PickUpCookedItem()
    const MethodInfo* pot_cook = nullptr;          // CookingPotItem.StartCooking(GearItem)
    const MethodInfo* pot_boil = nullptr;          // CookingPotItem.StartBoilingWater(ItemLiquidVolume, bool doWaterRemoval)
    const MethodInfo* pot_melt = nullptr;          // CookingPotItem.StartMeltingSnow(ItemLiquidVolume)
    const MethodInfo* pot_dummy = nullptr;         // CookingPotItem.IsDummyPot()
    ptrdiff_t pot_water = -1, pot_snow = -1;       // CookingPotItem.m_LitersWaterBeingBoiled, m_LitersSnowBeingMelted
    const MethodInfo* pot_load = nullptr;          // CookingPotItem.Deserialize(string)
    const MethodInfo* pot_drop_food = nullptr;     // CookingPotItem.DestroyGearItemBeingCooked()
    // TLDCoop_DLC
    ptrdiff_t pot_inspected = -1;                  // CookingPotItem.m_IsBeingInspected
    // TLDCoop_DLC (Karol 11.11): a pot on a fire is its owner's (who put it there): only their game cooks it, only
    // they take from it
    ptrdiff_t pot_gear = -1;                       // CookingPotItem.m_GearItem
    ptrdiff_t pot_slot = -1;                       // CookingPotItem.m_GearPlacePointAttachedTo
    const MethodInfo* pot_interact = nullptr;      // CookingPotItem.PerformInteraction()
    const MethodInfo* pot_cook_tick = nullptr;     // CookingPotItem.UpdateCookingTimeAndState(float, float)
    const MethodInfo* pot_melt_tick = nullptr;     // CookingPotItem.UpdateMeltingSnow()
    const MethodInfo* pot_boil_tick = nullptr;     // CookingPotItem.UpdateBoilingWater()
    const MethodInfo* pot_burnt_out = nullptr;     // CookingPotItem.CheckForFireBurntOut()
    const MethodInfo* pot_damage = nullptr;        // CookingPotItem.DoConditionDamageToPot()
    const MethodInfo* pot_take_gear = nullptr;     // CookingPotItem.PickUpCookedGearItem(bool)
    const MethodInfo* pot_take_water = nullptr;    // CookingPotItem.PickUpWater()
    const MethodInfo* pot_take_snow = nullptr;     // CookingPotItem.PickUpSnow()
    ptrdiff_t pot_force_particles = -1;            // CookingPotItem.m_ForceParticleUpdate
    const MethodInfo* pot_visuals = nullptr;       // CookingPotItem.UpdateVisuals()
    const MethodInfo* pot_water_mesh = nullptr;    // CookingPotItem.SetUpWaterMesh()
    ptrdiff_t food = -1, food_opened = -1;         // GearItem.m_FoodItem, FoodItem.m_Opened
    ptrdiff_t smash = -1, smashed = -1;            // GearItem.m_SmashableItem, SmashableItem.m_HasBeenSmashed
} api;

bool g_ready = false;

double now_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) {
    return il2::read_field<T>(o, off);
}

template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T value) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &value, sizeof(T));
}

bool alive(Il2CppObject* o) { return o && get<void*>(o, kCachedPtr) != nullptr; }

std::string text(Il2CppString* s) { return s ? il2::to_utf8(s) : std::string(); }
std::string name_of(Il2CppObject* o) {
    return o ? text(reinterpret_cast<Il2CppString*>(il2::invoke(api.object_name, o))) : std::string();
}
std::string short_id(const std::string& pdid) { return pdid.size() > 8 ? pdid.substr(0, 8) : pdid; }

bool alive(Il2CppObject* obj);
// TLDCoop_DLC: a lamp's fuel and a flare's or torch's state, for the log (" [condition 100%, fuel 85%, lit]")
std::string light_state(Il2CppObject* gi);

float distance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

bool contains_nocase(const std::string& hay, const std::string& needle) {
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
    return needle.empty() || it != hay.end();
}

// TLD's persistent id for a GearItem: what ObjectGuid.GetGuidFromGameObject returns (the virtual get_PDID, i.e.
// PdidObjectBase.m_RuntimeCachedPdid), with the legacy ObjectGuid.m_Guid as a fallback.
std::string pdid_of(Il2CppObject* gi) {
    Il2CppObject* og = get<Il2CppObject*>(gi, api.object_guid);
    if (!alive(og)) {
        void* args[] = {il2::invoke(api.component_go, gi)};
        og = args[0] ? il2::invoke(api.guid_of_go, nullptr, args) : nullptr;
    }
    if (!alive(og)) return {};
    std::string id = (api.cached_pdid >= 0) ? text(get<Il2CppString*>(og, api.cached_pdid)) : std::string{};
    if (id.empty() && api.guid >= 0) id = text(get<Il2CppString*>(og, api.guid));
    return id;
}

Il2CppObject* transform_of(Il2CppObject* component) { return il2::invoke(api.component_transform, component); }

void pose_of(Il2CppObject* component, Vector3& pos, Quaternion& rot) {
    Il2CppObject* tf = transform_of(component);
    il2::invoke_value(api.get_position, tf, pos);
    il2::invoke_value(api.get_rotation, tf, rot);
}

void set_pose(Il2CppObject* component, Vector3 pos, Quaternion rot) {
    void* args[] = {&pos, &rot};
    il2::invoke(api.set_pose, transform_of(component), args);
}

Il2CppObject* gear_on(Il2CppObject* go) {
    if (!alive(go)) return nullptr;
    void* args[] = {api.gear_type};
    Il2CppObject* gi = il2::invoke(api.go_component, go, args);
    return alive(gi) ? gi : nullptr;
}

// What's registered under this persistent id here (any object), and its GearItem if it has one.
Il2CppObject* object_by_pdid(const std::string& pdid) {
    if (pdid.empty()) return nullptr;
    void* args[] = {il2::il2cpp_string_new(pdid.c_str())};
    Il2CppObject* go = il2::invoke(api.pdid_object, nullptr, args);
    return alive(go) ? go : nullptr;
}

// GearManager.Serialize skips gear whose parent has one particular component (gear held by something else). The
// component is a generic GetComponent<T>() whose MethodInfo the method loads right before the call
// (mov rdx, [rip+x]; mov rcx, rax; call); T is its return type. IL2CPP fills that slot the first time the method runs,
// so it is run once here (it only builds a string).
void resolve_held_by() {
    api.held_by_resolved = true;
    const double t0 = now_ms();
    Il2CppString* all = reinterpret_cast<Il2CppString*>(il2::invoke(api.serialize_all, nullptr));
    const double ms = now_ms() - t0;
    const auto* code = static_cast<const uint8_t*>(il2::method_pointer(api.serialize_all));
    for (size_t i = 0; code && i + 11 < 0x400; ++i) {
        if (code[i] != 0x48 || code[i + 1] != 0x8B || code[i + 2] != 0x15 || code[i + 7] != 0x48 || code[i + 8] != 0x8B ||
            code[i + 9] != 0xC8 || code[i + 10] != 0xE8)
            continue;
        int32_t disp = 0;
        memcpy(&disp, code + i + 3, 4);
        const auto* slot = reinterpret_cast<const uintptr_t*>(code + i + 7 + disp);
        const uintptr_t value = *slot;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!value || (value & 1) || !VirtualQuery(reinterpret_cast<void*>(value), &mbi, sizeof(mbi)) ||
            mbi.State != MEM_COMMIT)
            break;
        const auto* method = reinterpret_cast<const MethodInfo*>(value);
        const char* name = il2::il2cpp_method_get_name(method);
        const Il2CppType* type = il2::il2cpp_method_get_return_type(method);
        if (!name || strcmp(name, "GetComponent") != 0 || !type) break;
        api.held_by = il2::il2cpp_type_get_object(type);
        if (api.held_by) il2::il2cpp_gchandle_new(api.held_by, false);
        char* type_name = il2::il2cpp_type_get_name(type);
        logx::info("items: TLD's save leaves out gear whose parent has a %s (GearManager.Serialize: %d chars of loose "
                   "gear here, %.1f ms)", type_name ? type_name : "?", all ? il2::il2cpp_string_length(all) : -1, ms);
        if (type_name) il2::il2cpp_free(type_name);
        return;
    }
    logx::warn("items: couldn't read GearManager.Serialize's parent rule; any parented gear counts as loose");
}

// TLD's own rule for gear saved as lying in the world (GearManager.Serialize): alive, saved, active, not in the pack,
// and not held by its parent. Plus: not one of the sticks or stones TLD scatters around us by itself.
bool is_loose(Il2CppObject* gi) {
    if (!alive(gi) || get<bool>(gi, api.no_save) || get<bool>(gi, api.in_inventory)) return false;
    // TLDCoop_DLC: a Seamless Interiors room still building (or an interior scene loading for one) isn't shared yet; a
    // ready room's items are, under the ids the room gave them (seamless_interiors::coop_rekey).
    if (seamless_interiors::sync_skips(gi)) return false;
    if (get<Il2CppObject*>(gi, api.radial_parent)) return false;
    Il2CppObject* go = il2::invoke(api.component_go, gi);
    bool active = false;
    if (!go || !il2::invoke_value(api.go_active, go, active)) return false;
    // TLDCoop_DLC: in a Seamless Interiors room closed in our game: there all the same (a friend may be in it)
    if (!active && !seamless_interiors::active_in_closed_room(go)) return false;
    Il2CppObject* parent = il2::invoke(api.get_parent, transform_of(gi));
    if (alive(parent) && api.held_by) {
        void* args[] = {api.held_by};
        if (alive(il2::invoke(api.go_component, il2::invoke(api.component_go, parent), args))) return false;
    }
    return true;
}

// Some gear has no persistent id at all (TLD saves it without one, e.g. items dropped in earlier play). Both games
// loaded it from the same save at the same spot, so its id is made from what it is and where it lies (to the cm),
// GUID-shaped, and registered the way TLD registers ids (ObjectGuid.MaybeAttachObjectGuidAndRegister). TLD's save
// keeps it from then on (GearItem.Serialize picks the new ObjectGuid up).
uint64_t fnv(const std::string& s, uint64_t h) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::string made_id(const std::string& what, const Vector3& at, int duplicate) {
    char key[160];
    snprintf(key, sizeof(key), "%s|%ld|%ld|%ld|%d", what.c_str(), std::lround(at.x * 100), std::lround(at.y * 100),
             std::lround(at.z * 100), duplicate);
    const uint64_t a = fnv(key, 1469598103934665603ull), b = fnv(key, 0x9E3779B97F4A7C15ull);
    char id[40];
    snprintf(id, sizeof(id), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32), static_cast<unsigned>(a >> 16) & 0xFFFF,
             static_cast<unsigned>(a) & 0xFFFF, static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return id;
}

// TLDCoop_DLC (Karol 11.11): `fresh`: made in our game just now (not loaded from the save both games share), e.g. TLD's
// dummy pot under meat put on a fire: a new random id. Made from where it lies, two players' meat on the same cooking
// slot got the same id in both games ("be1461a4" in each) and each game took the other's for its own: removed,
// put back, removed, twice a second, until the meat was gone.
std::string give_id(Il2CppObject* gi, const Vector3& at, bool fresh) {
    const std::string what = name_of(gi);
    std::string id;
    if (fresh && api.new_pdid)
        id = text(reinterpret_cast<Il2CppString*>(il2::invoke(api.new_pdid, nullptr)));
    for (int duplicate = 0; id.empty() && duplicate < 16; ++duplicate) {
        id = made_id(what, at, duplicate);
        if (object_by_pdid(id)) id.clear();
    }
    if (id.empty()) id = made_id(what, at, 16);
    void* args[] = {il2::invoke(api.component_go, gi), il2::il2cpp_string_new(id.c_str())};
    il2::invoke(api.attach_guid, nullptr, args);
    return pdid_of(gi);
}

// Units in a stack (matches, cartridges...), 0 for an item that isn't one. Taken into a pack, a stack goes onto the one
// there (its own object is gone).
int units_of(Il2CppObject* gi) {
    Il2CppObject* s = alive(gi) ? get<Il2CppObject*>(gi, api.stackable) : nullptr;
    return alive(s) ? get<int32_t>(s, api.units) : 0;
}

// One of a kind (net/uniques.h): each player takes their own where it lies, and what they drop stays theirs. Never
// shared: the other game keeps its own copy.
bool is_unique(Il2CppObject* gi) {
    return (api.narrative >= 0 && alive(get<Il2CppObject*>(gi, api.narrative))) || net::is_unique_gear(name_of(gi));
}

struct Loose {
    Il2CppObject* gi = nullptr;
    std::string pdid;
    Vector3 pos{};
    Quaternion rot{};
    int units = 0;
};

int g_without_id = 0, g_ids_given = 0;
bool baseline_now();  // g_baseline (below)

std::vector<Loose> scan_loose(bool give_ids) {
    std::vector<Loose> out;
    g_without_id = 0;
    Il2CppObject* list = nullptr;
    il2::il2cpp_field_static_get_value(api.all_gear, &list);
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    const int32_t count = get<int32_t>(list, kListSize);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* gi = il2::array_object_at(items, static_cast<size_t>(i));
        if (!is_loose(gi)) continue;
        Loose l;
        l.gi = gi;
        l.pdid = pdid_of(gi);
        pose_of(gi, l.pos, l.rot);
        l.units = units_of(gi);
        if (l.pdid.empty() && give_ids) {
            // (in the first scan of a scene, or a Seamless Interiors room just up: both games have it, the same id)
            const bool fresh = baseline_now() && !seamless_interiors::is_clone_object(gi);
            l.pdid = give_id(gi, l.pos, fresh);
            if (!l.pdid.empty()) ++g_ids_given;
        }
        if (l.pdid.empty()) {
            ++g_without_id;
            continue;
        }
        out.push_back(std::move(l));
    }
    return out;
}

// The item TLD's inspect screen shows (Take it / Leave it), if it's up. GearItemBeingInspected keeps returning the
// last one after the screen closes.
Il2CppObject* inspected_now(Il2CppObject* pm) {
    bool active = false;
    if (!pm || !il2::invoke_value(api.inspecting, pm, active) || !active) return nullptr;
    Il2CppObject* gi = il2::invoke(api.inspected, pm);
    return alive(gi) ? gi : nullptr;
}

// The items the player has in their hands right now without them being in the pack: one being looked at after
// picking it up, one being placed. They don't count as gone (or moved) while that lasts.
void busy_items(std::string& inspected, std::string& placing) {
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    if (!pm) return;
    if (Il2CppObject* gi = inspected_now(pm)) inspected = pdid_of(gi);
    if (Il2CppObject* gi = get<Il2CppObject*>(pm, api.placing); alive(gi)) placing = pdid_of(gi);
}

// --- state ---------------------------------------------------------------------------------------------------------

// A loose item in our scene, as of the last scan.
struct Known {
    std::string prefab;      // for the log
    Vector3 pos{};           // where it is
    Quaternion rot{};
    int units = 0;           // a stack's units (units_of)
    Vector3 told{};          // where the other player has it
    double moved_ms = 0;     // when it last moved
    double changed_ms = -1e18;  // when we last told the other player about it
    bool shared = false;     // the other player has it: it was here when we came in, they told us, or we told them
    bool unique = false;     // one of a kind (is_unique): never shared
    int snare = -1;          // a snare's state (snare_state), -1 for other items
    bool opened = false;     // TLDCoop_DLC: a can opened or smashed open (opened_of)
    bool burning = false;    // TLDCoop_DLC: a flare or torch burning (burning_light)
    bool foreign = false;    // TLDCoop_DLC: someone else's pot on a fire, or the food in it (foreign_pot): theirs to tell
};
std::unordered_map<std::string, Known> g_known;  // our pdid -> item
double g_door_until = 0;  // TLDCoop_DLC: a door is being used until then (door_used)
// TLDCoop_DLC: what a pot holds, to see changes the hooks don't (melting snow in 2.58, water once it's in, food ready
// or ruined). kind: 0 nothing, 1 food, 2 snow, 3 water.
struct PotSig {
    int kind = -1;  // -1: not seen yet
    int state = 0;  // CookingPotItem.m_CookingState
    std::string food;
    bool operator==(const PotSig& o) const { return kind == o.kind && state == o.state && food == o.food; }
};
std::unordered_map<Il2CppObject*, PotSig> g_pot_sig;
// TLDCoop_DLC (Shatsca 09.11: "Szymon put meat on, I came in and the meat sped up and was done"): when a pot was
// loaded from the other player's word; its food comes in a moment later, and meanwhile it reads empty
std::unordered_map<Il2CppObject*, double> g_pot_loaded_ms;
// TLDCoop_DLC (Karol 11.11, Shatsca 11.11 19:47 "dwa razy zniknęło mięso po dodaniu do ogniska"): who put each pot on a
// fire (CookingPotItem -> player id). Two games cooking one pot raced (the faster cooking skill finished it first: its
// raw meat "gone", the other game's put it back, again and again) until the meat was lost. Now only the owner's game
// cooks it and tells how it goes; the others show it and can't take from it while the owner is in the scene.
std::unordered_map<Il2CppObject*, uint8_t> g_pot_owner;
int g_applying = 0;  // inside apply_put / load_pot: TLD's own calls then aren't the player's doing
bool g_owned_pots = false;  // the hooks that keep a friend's pot from cooking here are in
std::unordered_map<Il2CppObject*, double> g_pot_heard;  // when the owner last told us of their pot
constexpr double kOwnerSilentMs = 45000;  // the owner's game says how its pot is getting on every kPotSyncMs; silent
                                          // this long (it never heard it was theirs): the pot is anyone's again
std::string g_scene;                             // the scene g_known is about ("" = none)
bool g_baseline = false;                         // g_known holds at least one full scan of g_scene
bool baseline_now() { return g_baseline; }
double g_last_scan = 0;
double g_fast_until = 0;  // TLDCoop_DLC: scan every kFastScanMs until then (a burning flare or torch flies)
uint32_t g_seq = 0;                              // our change counter
netrt::Newest g_newest;                          // wire pdid -> each player's newest change applied
// Ids differ when ours was taken (by an item in our pack, both games having loaded the same save).
std::unordered_map<std::string, std::string> g_alias_in;   // wire pdid -> ours
std::unordered_map<std::string, std::string> g_alias_out;  // ours -> wire pdid
// The other player's changes for scenes we aren't in (yet), newest per item.
struct Change {
    bool put = false;
    net::ItemPutMsg put_msg;
    net::ItemGoneMsg gone_msg;
    bool waited_room = false;  // TLDCoop_DLC: it waited for a Seamless Interiors room of ours (host)
};
std::unordered_map<std::string, std::unordered_map<std::string, Change>> g_pending;
// TLDCoop_DLC: host: the loose items of each scene we've been in (hashes of their ids, as send_list sends them), as
// we last saw them there, kept up with the friends' changes there since. A friend in a scene we aren't in gets it as
// our list (ItemList): items we took there earlier go from their game too (they were copies), and what they lack
// they ask for (answered from the changes kept for that scene). Kept with our save (world_keep).
std::unordered_map<std::string, std::unordered_set<uint64_t>> g_scene_items;
// TLDCoop_DLC (Kerel 08.11: "wszedłem do domku pierwszy i nic nie było"): host: the loose items' whole records of each
// scene we've been in (refreshed while there, a few at a time), so a friend in a scene we've left gets the items
// themselves, not only their ids. Kept with our save (world_keep ItemRecord).
std::unordered_map<std::string, std::unordered_map<uint64_t, net::ItemPutMsg>> g_scene_records;
double g_next_records = 0;
size_t g_records_cursor = 0;
bool g_scan_left_at_once = false;  // the last scan saw items leave with the scene (a door): not a list to keep
struct AwaySent {
    uint32_t link = 0;
    std::string scene;
};
std::array<AwaySent, net::kMaxPlayers> g_away_sent{};  // host: the kept list last sent to each friend (scene, link)
double g_last_away = 0;
netrt::Together g_together;          // host: friends in our scene (our list went to each as they came)
bool g_have_list = false;            // client: a host list for our scene waits for our first scan
net::ItemHashesMsg g_list;
bool g_connected = false;
// Two players taking the same item at about the same time: the host decides who gets it (net::Claims: at the same
// moment, a coin toss).
net::Claims g_claims;                // host: who took what, and when we heard of it
struct Took {
    std::string ours;                // our id for it
    std::string prefab;
    int units = 0;                   // what it added to a stack in our pack
    double at = 0;                   // TLDCoop_DLC: when
};
std::unordered_map<std::string, Took> g_took;   // wire pdid -> what we took (until the host refuses it, or someone's
                                                // take at the same moment wins the toss)
// The item in our hands (TLD's Take it / Leave it) and since when (looked at every frame): picked up at the same
// moment as another player's take, it goes to the coin toss too.
Il2CppObject* g_hold_gi = nullptr;
std::string g_hold;
double g_hold_since = 0;
std::vector<net::ItemPutMsg> g_put_back;        // client: refused takes that lie where they were again (next frame,
                                                // once our copy is gone)

std::vector<Il2CppObject*> pack_items();

const std::string& wire_id(const std::string& ours) {
    const auto it = g_alias_out.find(ours);
    return it == g_alias_out.end() ? ours : it->second;
}
const std::string& our_id(const std::string& wire) {
    const auto it = g_alias_in.find(wire);
    return it == g_alias_in.end() ? wire : it->second;
}
void forget_alias(const std::string& ours) {
    const auto it = g_alias_out.find(ours);
    if (it == g_alias_out.end()) return;
    g_alias_in.erase(it->second);
    g_alias_out.erase(it);
}

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

void reset(const char* why) {
    if (!g_known.empty()) logx::info("items: forgetting shared item state (%s)", why);
    g_known.clear();
    g_pot_sig.clear();
    g_pot_owner.clear();
    g_pot_heard.clear();
    g_scene.clear();
    g_baseline = false;
    g_newest.clear();
    g_alias_in.clear();
    g_alias_out.clear();
    g_together.clear();
    g_have_list = false;
    g_put_back.clear();
    g_hold_gi = nullptr;
    g_hold.clear();
}

// What we keep of the world for later (changes for scenes someone isn't in, who took what) belongs to this game: a
// dropped link keeps it, back at the main menu (another save may load next) it goes.
void forget_world() {
    g_claims.clear();
    g_took.clear();
    g_alias_in.clear();
    g_alias_out.clear();
    g_scene_items.clear();
    g_away_sent.fill(AwaySent{});
    if (g_pending.empty()) return;
    logx::info("items: back at the main menu; forgetting changes kept for %zu scene(s)", g_pending.size());
    g_pending.clear();
}

// --- snares --------------------------------------------------------------------------------------------------------
// A set snare rolls for a catch every few game hours (SnareItem.DoRoll; on its scene's load too, for the hours
// missed): it catches a rabbit (a dead one on it: an item soon, wildlife_carcass) or breaks. The game that runs the
// scene rolls, the host's or a friend's the host isn't in; two games rolling for one snare would disagree. The other
// game gets the outcome as the snare's new record (sent when its state changes) and loads its state from it.

// A snare's state (0 not set, 1 set, 2 broken, 3 with a rabbit); -1 for an item that isn't one.
// A flare or torch that's burning (kSettleLitMs).
// TLDCoop_DLC (Iron Road, item 11): the whale oil heater lit (WhaleOilHeaterItem.m_State.m_IsOn).
bool heater_on(Il2CppObject* gi) {
    if (api.heater < 0 || api.heater_state < 0 || api.hs_on < 0 || !alive(gi)) return false;
    Il2CppObject* h = get<Il2CppObject*>(gi, api.heater);
    Il2CppObject* st = alive(h) ? get<Il2CppObject*>(h, api.heater_state) : nullptr;
    return st && get<bool>(st, api.hs_on);
}

bool burning_light(Il2CppObject* gi) {
    if (!alive(gi)) return false;
    if (heater_on(gi)) return true;
    for (auto [part, burning] : {std::pair{api.flare, api.flare_burning}, std::pair{api.torch, api.torch_burning}}) {
        if (part < 0 || !burning) continue;
        Il2CppObject* item = get<Il2CppObject*>(gi, part);
        bool on = false;
        if (alive(item) && il2::invoke_value(burning, item, on) && on) return true;
    }
    return false;
}

int snare_state(Il2CppObject* gi) {
    Il2CppObject* s = api.snare >= 0 && api.snare_state >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.snare) : nullptr;
    return alive(s) ? get<int32_t>(s, api.snare_state) : -1;
}

const char* snare_name(int state) {
    switch (state) {
        case 0: return "not set";
        case 1: return "set";
        case 2: return "broken";
        case 3: return "with a rabbit";
    }
    return "?";
}

// A JSON string value ("key":"...") in TLD's save text, unescaped; empty if there's none.
std::string json_string(const std::string& text, const std::string& key) {
    const std::string at = "\"" + key + "\":\"";
    size_t i = text.find(at);
    if (i == std::string::npos) return {};
    std::string out;
    for (i += at.size(); i < text.size() && text[i] != '"'; ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out += text[i];
            continue;
        }
        const char c = text[++i];
        out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c;  // \" \\ \/ as themselves
    }
    return out;
}

// A JSON number value ("key":12.5) in TLD's save text; `fallback` if there's none.
double json_number(const std::string& text, const std::string& key, double fallback) {
    const std::string at = "\"" + key + "\":";
    const size_t i = text.find(at);
    if (i == std::string::npos) return fallback;
    char* end = nullptr;
    const double v = std::strtod(text.c_str() + i + at.size(), &end);
    return end == text.c_str() + i + at.size() ? fallback : v;
}

// TLDCoop_DLC: the other player's record of a flare or torch we have lying burning: how long it has burnt (the host's
// says; a friend's is loaded the same way, as it's theirs). Burnt out there: it goes out here (TLD's own Update does
// that once it has burnt its lifetime).
void load_burn(Il2CppObject* gi, const std::string& gear, Known& k) {
    struct Kind { ptrdiff_t part, elapsed, life; const char* key; int burnt_out; };
    for (const Kind& kind : {Kind{api.flare, api.flare_elapsed, api.flare_life, "m_FlareItemSerialized", 2},
                             Kind{api.torch, api.torch_elapsed, api.torch_life, "m_TorchItemSerialized", 3}}) {
        Il2CppObject* item = kind.part >= 0 ? get<Il2CppObject*>(gi, kind.part) : nullptr;
        if (!alive(item) || kind.elapsed < 0 || kind.life < 0) continue;
        const std::string text = json_string(gear, kind.key);
        if (text.empty() || !burning_light(gi)) return;
        const int state = static_cast<int>(json_number(text, "m_StateProxy", -1));
        double theirs = json_number(text, "m_ElapsedBurnMinutesProxy", -1);
        const float life = get<float>(item, kind.life);
        if (state == kind.burnt_out) theirs = life;
        if (theirs < 0) return;
        const float ours = get<float>(item, kind.elapsed);
        if (std::fabs(theirs - ours) < 0.25) return;
        put<float>(item, kind.elapsed, static_cast<float>(theirs));
        if (state == kind.burnt_out)
            logx::info("items: the other player's %s burnt out; goes out here too", name_of(gi).c_str());
        k.burning = burning_light(gi);
        return;
    }
}

// The other player's record of a snare we have: its state and when it last rolled, as loading a save does.
void load_snare(Il2CppObject* gi, const std::string& gear, const std::string& wire, Known& k) {
    Il2CppObject* s = api.snare >= 0 ? get<Il2CppObject*>(gi, api.snare) : nullptr;
    const std::string text = json_string(gear, "m_SnareItemSerialized");
    if (!alive(s) || !api.snare_load || text.empty()) return;
    const int before = snare_state(gi);
    void* args[] = {il2::il2cpp_string_new(text.c_str())};
    il2::invoke(api.snare_load, s, args);
    k.snare = snare_state(gi);
    if (k.snare != before)
        logx::info("items: the other player's snare (%s) is %s now", short_id(wire).c_str(), snare_name(k.snare));
}

// Whether the host's game runs the scene we're in (it's there too): our rolls for its snares are its.
bool host_runs_our_scene() {
    const auto role = netrt::role();
    if (!role || *role != net::Role::Client) return false;
    const auto host = netrt::remote_state();
    return host && host->scene == game::active_scene();
}

using RollFn = void (*)(Il2CppObject* self, const MethodInfo* method);
RollFn g_roll = nullptr;

void roll_detour(Il2CppObject* self, const MethodInfo* method) {
    if (host_runs_our_scene()) {
        logx::info("items: a snare's roll for a catch is the host's here (it runs this scene too); skipped");
        return;
    }
    g_roll(self, method);
}

// --- cooking pots --------------------------------------------------------------------------------------------------
// A pot on a fire (CookingPotItem; TLD's dummy pot too, under food cooked without one) cooks food, boils water or
// melts snow. The food is its own item, lying in the pot (hidden; TLD saves it as loose gear) and found by id when
// the pot's record loads (CookingPotItem.FindGearItemBeingCooked). Both games cook on their own: the same fire, the
// same clock. What a player puts in or takes out (TLD's StartCooking, StartBoilingWater, StartMeltingSnow,
// PickUpCookedItem: hooked) goes to the other game as the pot's record again, the food's own record first.

std::unordered_set<Il2CppObject*> g_pots_changed;  // CookingPotItems a player put something in or took it out of


// TLDCoop_DLC: a can (food) opened, or smashed open. The other games load it (load_opened), or a friend picking up
// the food cooked in it had to smash it open again.
bool opened_of(Il2CppObject* gi) {
    Il2CppObject* food = api.food >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.food) : nullptr;
    Il2CppObject* smash = api.smash >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.smash) : nullptr;
    return (alive(food) && get<bool>(food, api.food_opened)) || (alive(smash) && get<bool>(smash, api.smashed));
}

std::string light_state(Il2CppObject* gi) {
    if (!alive(gi)) return {};
    std::string out;
    char buf[64];
    Il2CppObject* lamp = api.lamp >= 0 ? get<Il2CppObject*>(gi, api.lamp) : nullptr;
    Il2CppObject* flare = api.flare >= 0 ? get<Il2CppObject*>(gi, api.flare) : nullptr;
    Il2CppObject* torch = api.torch >= 0 ? get<Il2CppObject*>(gi, api.torch) : nullptr;
    if (!alive(lamp) && !alive(flare) && !alive(torch)) return {};
    int32_t cond = -1;
    if (api.gear_condition) il2::invoke_value(api.gear_condition, gi, cond);
    snprintf(buf, sizeof(buf), " [condition %d%%", cond);
    out += buf;
    if (alive(lamp)) {
        float pct = -1;
        bool on = false;
        if (api.lamp_pct) il2::invoke_value(api.lamp_pct, lamp, pct);
        if (api.lamp_on) il2::invoke_value(api.lamp_on, lamp, on);
        snprintf(buf, sizeof(buf), ", fuel %.0f%%%s", pct, on ? ", lit" : "");
        out += buf;
    }
    if (alive(flare) && api.flare_elapsed >= 0 && api.flare_life >= 0) {
        snprintf(buf, sizeof(buf), ", burnt %.1f of %.1f min", get<float>(flare, api.flare_elapsed), get<float>(flare, api.flare_life));
        out += buf;
    }
    if (alive(torch) && api.torch_elapsed >= 0 && api.torch_life >= 0) {
        snprintf(buf, sizeof(buf), ", burnt %.1f of %.1f min", get<float>(torch, api.torch_elapsed), get<float>(torch, api.torch_life));
        out += buf;
    }
    if (burning_light(gi)) out += ", burning";
    return out + "]";
}

// TLDCoop_DLC: the other player's record of a lamp we have lying: its fuel and whether it's lit, as loading a save does
// ("a full lamp's fuel was all gone": ours kept what it had when it was first made here).
// TLDCoop_DLC (Shatsca 07.11: a lamp under half full came back full after GIVE): TLD's Deserialize of the lamp's
// record gave the wrong fuel in 2.60 (49% in the host's game came out 100% in the friend's, 99% came out 0%). The
// fuel the record names ("m_CurrentFuelLitersProxy") goes straight into the lamp's field after it.
bool record_fuel(const std::string& lamp_text, int64_t& units) {
    const size_t at = lamp_text.find("m_CurrentFuelLitersProxy");
    if (at == std::string::npos) return false;
    size_t p = at + 24;
    const size_t stop = std::min(lamp_text.size(), p + 80);
    while (p < stop && !(std::isdigit(static_cast<unsigned char>(lamp_text[p])) || lamp_text[p] == '-')) ++p;
    if (p >= stop) return false;
    char* end = nullptr;
    const std::string num = lamp_text.substr(p, 40);
    const double v = std::strtod(num.c_str(), &end);
    if (end == num.c_str() || !std::isfinite(v) || v < 0) return false;
    const bool liters = num.find('.') < static_cast<size_t>(end - num.c_str()) || v < 1000;
    units = static_cast<int64_t>(liters ? v * 1e9 : v);
    return true;
}

void set_lamp_fuel(Il2CppObject* lamp, const std::string& text) {
    static int shown = 0;
    if (shown++ < 3) logx::info("items: a lamp's record: %.300s", text.c_str());
    int64_t units = 0;
    if (api.lamp_fuel >= 0 && record_fuel(text, units)) put<int64_t>(lamp, api.lamp_fuel, units);
}

// TLDCoop_DLC: the other player's record of a lamp we have lying: its fuel and whether it's lit, as loading a save does
// ("a full lamp's fuel was all gone": ours kept what it had when it was first made here).
void load_lamp(Il2CppObject* gi, const std::string& gear) {
    Il2CppObject* lamp = api.lamp >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.lamp) : nullptr;
    const std::string text = json_string(gear, "m_KeroseneLampItemSerialized");
    if (!alive(lamp) || !api.lamp_load || text.empty()) return;
    float before = -1, after = -1;
    if (api.lamp_pct) il2::invoke_value(api.lamp_pct, lamp, before);
    void* args[] = {il2::il2cpp_string_new(text.c_str())};
    il2::invoke(api.lamp_load, lamp, args);
    set_lamp_fuel(lamp, text);
    if (api.lamp_pct) il2::invoke_value(api.lamp_pct, lamp, after);
    if (std::fabs(after - before) >= 1)
        logx::info("items: the other player's lamp has %.0f%% fuel (ours said %.0f%%)", after, before);
}

// TLDCoop_DLC (Iron Road, item 11): the other player's whale oil heater as their record has it: lit or not, its burn
// rate and its oil (WhaleOilHeaterItemState in the record: m_IsOn, m_BurnRate, m_Fuel).
void load_heater(Il2CppObject* gi, const std::string& gear) {
    Il2CppObject* h = api.heater >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.heater) : nullptr;
    Il2CppObject* st = alive(h) && api.heater_state >= 0 ? get<Il2CppObject*>(h, api.heater_state) : nullptr;
    const size_t at = gear.find("\"m_WhaleOilHeaterSerialized\"");
    if (!st || at == std::string::npos) return;
    const size_t end = gear.find('}', at);
    const std::string text = gear.substr(at, end == std::string::npos ? std::string::npos : end - at + 1);
    const bool on = text.find("\"m_IsOn\":true") != std::string::npos;
    const double rate = json_number(text, "m_BurnRate", -1);
    double fuel = json_number(text, "m_Units", -1);
    if (fuel < 0) fuel = json_number(text, "m_Fuel", -1);
    if (fuel >= 0 && api.hs_fuel >= 0) put<int64_t>(st, api.hs_fuel, static_cast<int64_t>(fuel));
    if (rate >= 0 && api.heater_rate && static_cast<int32_t>(rate) != get<int32_t>(st, api.hs_rate)) {
        int32_t r = static_cast<int32_t>(rate);
        void* a[] = {&r};
        il2::invoke(api.heater_rate, h, a);
    }
    const bool was = get<bool>(st, api.hs_on);
    if (on != was && api.heater_on && api.heater_off) {
        il2::invoke(on ? api.heater_on : api.heater_off, h);
        logx::info("items: the other player's whale oil heater is %s", on ? "lit" : "out");
    }
    static int shown = 0;
    if (shown++ < 3) logx::info("items: a heater's record: %.200s", text.c_str());
}

// TLDCoop_DLC (Karol 04.11: a hide curing on the floor showed "stopień wysuszenia" in the host's game only): how long
// the other player's has been curing (drying), as its record says; ours goes on from there.
void load_evolve(Il2CppObject* gi, const std::string& gear) {
    Il2CppObject* ev = api.evolve >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.evolve) : nullptr;
    if (!alive(ev) || api.evolve_hours < 0) return;
    const std::string text = json_string(gear, "m_EvolveItemSerialized");
    if (text.empty()) return;
    const double theirs = json_number(text, "m_TimeSpentEvolvingGameHours", -1);
    if (theirs < 0) return;
    const float ours = get<float>(ev, api.evolve_hours);
    if (std::fabs(theirs - ours) < 0.05) return;
    put<float>(ev, api.evolve_hours, static_cast<float>(theirs));
    bool can = false;
    if (api.evolve_can) il2::invoke_value(api.evolve_can, ev, can);
    static int told = 0;
    if (told++ < 20)
        logx::info("items: the other player's %s has been curing %.1f h (ours said %.1f h)%s", name_of(gi).c_str(), theirs,
                   ours, can ? "" : "; it can't cure here (TLD's CanEvolve: no)");
}

// TLDCoop_DLC: how wet (frozen) a piece of clothing is and any item's condition, as the other player has it, on our
// copy of it (Shatsca 04.11 23:25: a dry shirt he dropped was soaked for the one who picked it up: their game showed
// its own copy from before, wet, where he'd put his).
double json_first(const std::string& text, std::initializer_list<const char*> keys) {
    for (const char* key : keys)
        if (const double v = json_number(text, key, -1); v >= 0) return v;
    return -1;
}

void load_wear(Il2CppObject* gi, const std::string& gear) {
    if (!alive(gi)) return;
    std::string changed;
    if (api.hp >= 0) {
        const double theirs = json_first(gear, {"m_CurrentHPProxy", "m_CurrentHP"});
        if (theirs >= 0 && std::fabs(theirs - get<float>(gi, api.hp)) > 0.01) {
            put<float>(gi, api.hp, static_cast<float>(theirs));
            changed += " condition";
        }
    }
    Il2CppObject* cl = api.clothing >= 0 ? get<Il2CppObject*>(gi, api.clothing) : nullptr;
    if (alive(cl)) {
        const std::string text = json_string(gear, "m_ClothingItemSerialized");
        if (!text.empty()) {
            // (one key each, by value: an initializer_list nested in the loop's list dangled once the list was made, and
            // json_first read freed stack as key pointers: szymon's game crashed on a friend's moved clothes, 05.11)
            struct Part {
                ptrdiff_t off;
                const char* key;
                const char* what;
            };
            const Part parts[] = {{api.wet, "m_PercentWet", " wet"}, {api.frozen, "m_PercentFrozen", " frozen"}};
            for (const Part& p : parts) {
                const ptrdiff_t off = p.off;
                const char* what = p.what;
                if (off < 0) continue;
                const double theirs = json_first(text, {p.key});
                if (theirs >= 0 && std::fabs(theirs - get<float>(cl, off)) > 0.5) {
                    put<float>(cl, off, static_cast<float>(theirs));
                    changed += what;
                }
            }
        }
    }
    static int told = 0;
    if (!changed.empty() && told++ < 30)
        logx::info("items: the other player's %s:%s as theirs", name_of(gi).c_str(), changed.c_str());
}

void load_opened(Il2CppObject* gi, const std::string& gear, Known& k) {
    if (!alive(gi)) return;
    bool changed = false;
    Il2CppObject* food = api.food >= 0 ? get<Il2CppObject*>(gi, api.food) : nullptr;
    if (alive(food) && api.food_opened >= 0 && !get<bool>(food, api.food_opened) &&
        json_string(gear, "m_FoodItemSerialized").find("\"m_Opened\":true") != std::string::npos) {
        put<bool>(food, api.food_opened, true);
        changed = true;
    }
    Il2CppObject* smash = api.smash >= 0 ? get<Il2CppObject*>(gi, api.smash) : nullptr;
    if (alive(smash) && api.smashed >= 0 && !get<bool>(smash, api.smashed) &&
        json_string(gear, "m_SmashableItemSerialized").find("\"m_HasBeenSmashed\":true") != std::string::npos) {
        put<bool>(smash, api.smashed, true);
        changed = true;
    }
    k.opened = opened_of(gi);
    if (changed) logx::info("items: the other player opened %s; open here too", name_of(gi).c_str());
}

Il2CppObject* pot_of(Il2CppObject* gi) {
    Il2CppObject* pot = api.pot >= 0 && alive(gi) ? get<Il2CppObject*>(gi, api.pot) : nullptr;
    return alive(pot) ? pot : nullptr;
}

Il2CppObject* food_in(Il2CppObject* pot) {
    Il2CppObject* food = pot ? get<Il2CppObject*>(pot, api.pot_food) : nullptr;
    return alive(food) ? food : nullptr;
}

PotSig pot_sig(Il2CppObject* pot) {
    PotSig sig;
    Il2CppObject* food = food_in(pot);
    sig.state = api.pot_state >= 0 ? get<int32_t>(pot, api.pot_state) : 0;
    if (food) {
        sig.kind = 1;
        sig.food = pdid_of(food);
    } else {
        sig.kind = get<int64_t>(pot, api.pot_snow) > 0 ? 2 : get<int64_t>(pot, api.pot_water) > 0 ? 3 : 0;
    }
    return sig;
}

std::string describe_pot(Il2CppObject* pot) {
    Il2CppObject* food = food_in(pot);
    const int64_t water = get<int64_t>(pot, api.pot_water), snow = get<int64_t>(pot, api.pot_snow);
    char text[160];
    if (food)
        snprintf(text, sizeof(text), "is a pot cooking %s now", name_of(food).c_str());
    else if (water > 0 || snow > 0)
        snprintf(text, sizeof(text), "is a pot %s %.2f l now", snow > 0 ? "melting snow," : "boiling", (snow > 0 ? snow : water) / 1e9);
    else
        snprintf(text, sizeof(text), "is an empty pot now");
    return text;
}

// The other player's record of a pot we have, as loading a save does. Food they took out goes from ours too.
void load_pot(Il2CppObject* gi, const std::string& gear, const std::string& wire) {
    Il2CppObject* pot = pot_of(gi);
    std::string text = json_string(gear, "m_CookingPotItemSerialized");
    if (!pot || !api.pot_load || text.empty()) return;
    const std::string food_wire = json_string(text, "m_GearItemBeingCookedGUID");
    std::string food_ours = food_wire.empty() ? std::string() : our_id(food_wire);
    if (food_ours != food_wire) replace_all(text, food_wire, food_ours);
    // TLDCoop_DLC: their record names food that's in our pack now (we took it out; theirs is older): the pot mustn't
    // take it back from the pack (the next record would destroy it there), so it's loaded as not cooking it
    if (!food_ours.empty()) {
        // (or that we just took: Shatsca 05.11 20:19, Szymon's tea, a pot record 50 ms after our take put it back in
        // the pot, whose removal then destroyed it: gone from the pack)
        // (in a pot it isn't in the pack, whatever its flag says: Shatsca 05.11 21:05, Szymon's rabbit meat in a pot, both
        // games "not put back" and the empty pot gone, again and again, twice a second)
        Il2CppObject* f = gear_on(object_by_pdid(food_ours));
        const double t_now = now_ms();
        const bool just_took = std::any_of(g_took.begin(), g_took.end(), [&](const auto& t) {
            return (t.first == food_wire || t.second.ours == food_ours) && t_now - t.second.at < 3000;
        });
        bool in_a_pot = false;
        if (f)
            for (const auto& [id, known] : g_known)
                if (Il2CppObject* other = gear_on(object_by_pdid(id)))
                    if (Il2CppObject* p = pot_of(other); p && food_in(p) == f) in_a_pot = true;
        if (f && !in_a_pot && (in_pack(f) || just_took)) {
            logx::info("items: the other player's %s (%s) still cooks %s, which is in our pack now; not put back",
                       name_of(gi).c_str(), short_id(wire).c_str(), f ? name_of(f).c_str() : short_id(food_ours).c_str());
            const std::string was = text;
            replace_all(text, "\"m_GearItemBeingCookedGUID\":\"" + food_ours + "\"", "\"m_GearItemBeingCookedGUID\":\"\"");
            if (text == was) replace_all(text, food_ours, "");
            food_ours.clear();
        }
    }
    const std::string before = describe_pot(pot);
    // TLDCoop_DLC: the host's regular word on a pot we're looking into right now (same contents, its progress) waits.
    const PotSig had = pot_sig(pot);
    const bool same_contents = food_ours.empty() ? had.kind != 1 : had.food == food_ours;
    if (same_contents && api.pot_inspected >= 0 && get<bool>(pot, api.pot_inspected)) return;
    if (Il2CppObject* food = food_in(pot); food && pdid_of(food) != food_ours) {
        if (in_pack(food)) {  // TLDCoop_DLC: ours now (in the pack): only the pot lets go of it, never destroyed
            put<Il2CppObject*>(pot, api.pot_food, nullptr);
        } else {
            const std::string id = pdid_of(food);
            g_known.erase(id);  // taken out there (the other game tells us): not gone here by our doing
            il2::invoke(api.pot_drop_food, pot);
        }
    }
    void* args[] = {il2::il2cpp_string_new(text.c_str())};
    il2::invoke(api.pot_load, pot, args);
    g_pots_changed.erase(pot);
    // TLDCoop_DLC: what's in it shows (the snow or water in a can looked empty), and what we just loaded isn't
    // told back as our own change.
    if (api.pot_water_mesh && (get<int64_t>(pot, api.pot_snow) > 0 || get<int64_t>(pot, api.pot_water) > 0))
        il2::invoke(api.pot_water_mesh, pot);
    if (api.pot_visuals) il2::invoke(api.pot_visuals, pot);
    put<bool>(pot, api.pot_force_particles, true);
    PotSig sig = pot_sig(pot);
    if (!food_ours.empty()) {  // the food goes in on the pot's next frame
        sig.kind = 1;
        sig.food = food_ours;
    }
    g_pot_sig[pot] = sig;
    g_pot_loaded_ms[pot] = now_ms();
    std::string after = describe_pot(pot);  // the food goes in on the pot's next frame (FindGearItemBeingCooked)
    if (!food_ours.empty()) {
        Il2CppObject* food = gear_on(object_by_pdid(food_ours));
        after = "is a pot cooking " + (food ? name_of(food) : "food we don't have (" + short_id(food_ours) + ")") + " now";
    }
    if (after != before)
        logx::info("items: the other player's %s (%s) %s", name_of(gi).c_str(), short_id(wire).c_str(), after.c_str());
}

uint8_t self_id() { return netrt::status().self_id; }

// TLDCoop_DLC: the player's name, for what our HUD says
std::string player_name(uint8_t id) {
    for (const net::PlayerInfo& p : netrt::status().players)
        if (p.id == id && !p.name.empty()) return p.name;
    return id == 0 ? "the host" : "your friend";
}

// The pot is on a fire's cooking slot (or by a fire, cooking).
bool on_fire(Il2CppObject* pot) {
    if (!pot) return false;
    if (api.pot_slot >= 0 && alive(get<Il2CppObject*>(pot, api.pot_slot))) return true;
    return api.pot_fire >= 0 && alive(get<Il2CppObject*>(pot, api.pot_fire));
}

// The owner (someone else) is in the game and in our scene: their game runs the pot.
bool owner_with_us(uint8_t id) {
    if (id == net::kNoOwner || id == self_id()) return false;
    bool linked = false;
    for (const net::PlayerInfo& p : netrt::status().players)
        if (p.id == id) linked = true;
    if (!linked) return false;
    const auto r = netrt::remote_state(id);
    return r && r->valid && r->scene == g_scene;
}

uint8_t owner_of(Il2CppObject* pot) {
    const auto it = pot ? g_pot_owner.find(pot) : g_pot_owner.end();
    return it == g_pot_owner.end() ? net::kNoOwner : it->second;
}

// Someone else's pot on a fire here, run by their game: we don't cook it, take from it, or tell anything about it.
bool foreign_pot(Il2CppObject* pot) {
    if (!g_owned_pots || !pot || !on_fire(pot) || !owner_with_us(owner_of(pot))) return false;
    const auto heard = g_pot_heard.find(pot);
    if (heard != g_pot_heard.end() && now_ms() - heard->second > kOwnerSilentMs) {
        logx::info("items: %s's pot here has been silent %.0f s: anyone's again", player_name(owner_of(pot)).c_str(),
                   (now_ms() - heard->second) / 1000);
        g_pot_owner.erase(pot);
        g_pot_heard.erase(heard);
        return false;
    }
    return true;
}

// The pot a food item is cooking in (one of the pots whose owner we know), or null.
Il2CppObject* pot_cooking(Il2CppObject* food) {
    if (!alive(food)) return nullptr;
    for (const auto& [pot, owner] : g_pot_owner)
        if (alive(pot) && food_in(pot) == food) return pot;
    return nullptr;
}

// Who to name as the owner in an item's record: a pot on a fire, or the food in one.
uint8_t owner_for_record(Il2CppObject* gi) {
    Il2CppObject* pot = pot_of(gi);
    if (!pot) pot = pot_cooking(gi);
    if (!pot || !on_fire(pot)) return net::kNoOwner;
    return owner_of(pot);
}

// Ours from now on: we put it on the fire / put something in it.
void own_pot(Il2CppObject* pot) {
    if (!pot || g_applying) return;
    g_pot_owner[pot] = self_id();
}

// Not ours to take: says so on the HUD (once in a while).
bool refuse_foreign(Il2CppObject* pot, const char* what) {
    if (g_applying || !foreign_pot(pot)) return false;
    static double last = -1e18;
    const double t = now_ms();
    if (t - last > 2000) {
        last = t;
        const uint8_t owner = owner_of(pot);
        coop_notices::tell(player_name(owner) + " put this on the fire: only they can take it");
        Il2CppObject* food = food_in(pot);
        logx::info("items: %s on %s's pot (%s): not ours, refused", what, player_name(owner).c_str(),
                   food ? name_of(food).c_str() : "empty");
    }
    return true;
}

using PotFn = void (*)(Il2CppObject* self, const MethodInfo* method);
using PotFoodFn = void (*)(Il2CppObject* self, Il2CppObject* gi, const MethodInfo* method);
using PotBoilFn = void (*)(Il2CppObject* self, int64_t litres, bool take_water, const MethodInfo* method);
using PotMeltFn = void (*)(Il2CppObject* self, int64_t litres, const MethodInfo* method);
using PotTickFn = void (*)(Il2CppObject* self, float cook_minutes, float ready_minutes, const MethodInfo* method);
using PotBoolFn = bool (*)(Il2CppObject* self, const MethodInfo* method);
using PotTakeGearFn = void (*)(Il2CppObject* self, bool to_pack, const MethodInfo* method);
PotFn g_pot_take = nullptr;
PotFoodFn g_pot_cook = nullptr;
PotBoilFn g_pot_boil = nullptr;
PotMeltFn g_pot_melt = nullptr;
PotTickFn g_pot_cook_tick = nullptr;
PotFn g_pot_melt_tick = nullptr, g_pot_boil_tick = nullptr, g_pot_burnt_out = nullptr, g_pot_damage = nullptr;
PotFn g_pot_take_water = nullptr, g_pot_take_snow = nullptr;
PotBoolFn g_pot_interact = nullptr;
PotTakeGearFn g_pot_take_gear = nullptr;

void pot_take_detour(Il2CppObject* self, const MethodInfo* method) {
    if (refuse_foreign(self, "take")) return;
    g_pot_take(self, method);
    g_pots_changed.insert(self);
}
void pot_cook_detour(Il2CppObject* self, Il2CppObject* gi, const MethodInfo* method) {
    g_pot_cook(self, gi, method);
    own_pot(self);
    g_pots_changed.insert(self);
}
void pot_boil_detour(Il2CppObject* self, int64_t litres, bool take_water, const MethodInfo* method) {
    g_pot_boil(self, litres, take_water, method);
    own_pot(self);
    g_pots_changed.insert(self);
}
void pot_melt_detour(Il2CppObject* self, int64_t litres, const MethodInfo* method) {
    g_pot_melt(self, litres, method);
    own_pot(self);
    g_pots_changed.insert(self);
}
// The owner's game cooks it (its cooking skill, its fire): ours only shows what it tells.
void pot_cook_tick_detour(Il2CppObject* self, float cook_minutes, float ready_minutes, const MethodInfo* method) {
    if (foreign_pot(self)) return;
    g_pot_cook_tick(self, cook_minutes, ready_minutes, method);
}
void pot_melt_tick_detour(Il2CppObject* self, const MethodInfo* method) {
    if (foreign_pot(self)) return;
    g_pot_melt_tick(self, method);
}
void pot_boil_tick_detour(Il2CppObject* self, const MethodInfo* method) {
    if (foreign_pot(self)) return;
    g_pot_boil_tick(self, method);
}
void pot_burnt_out_detour(Il2CppObject* self, const MethodInfo* method) {
    if (foreign_pot(self)) return;
    g_pot_burnt_out(self, method);
}
void pot_damage_detour(Il2CppObject* self, const MethodInfo* method) {
    if (foreign_pot(self)) return;
    g_pot_damage(self, method);
}
bool pot_interact_detour(Il2CppObject* self, const MethodInfo* method) {
    if (refuse_foreign(self, "interact")) return false;
    return g_pot_interact(self, method);
}
void pot_take_gear_detour(Il2CppObject* self, bool to_pack, const MethodInfo* method) {
    if (refuse_foreign(self, "take the food")) return;
    g_pot_take_gear(self, to_pack, method);
}
void pot_take_water_detour(Il2CppObject* self, const MethodInfo* method) {
    if (refuse_foreign(self, "take the water")) return;
    g_pot_take_water(self, method);
    g_pots_changed.insert(self);
}
void pot_take_snow_detour(Il2CppObject* self, const MethodInfo* method) {
    if (refuse_foreign(self, "take the snow")) return;
    g_pot_take_snow(self, method);
    g_pots_changed.insert(self);
}
// --- sending -------------------------------------------------------------------------------------------------------

// The item as ItemPut: TLD's save record for it, lying at `pos` / `rot`. False if TLD made none or it's too big.
bool make_put(Il2CppObject* gi, const std::string& pdid, const std::string& prefab, uint8_t flags, const Vector3& pos,
              const Quaternion& rot, net::ItemPutMsg& m) {
    Il2CppObject* data = il2::invoke(api.create_save_data, gi);
    if (!data) {
        logx::warn("items: TLD made no save record for %s (%s); the other player won't see it", prefab.c_str(),
                   short_id(pdid).c_str());
        return false;
    }
    m.scene = g_scene;
    m.seq = ++g_seq;
    m.flags = flags;
    m.pdid = wire_id(pdid);
    m.prefab = text(get<Il2CppString*>(data, api.sd_prefab));
    m.gear = text(get<Il2CppString*>(data, api.sd_gear));
    if (m.pdid != pdid) replace_all(m.gear, pdid, m.pdid);  // our copy goes by another id here
    m.pos[0] = pos.x, m.pos[1] = pos.y, m.pos[2] = pos.z;
    m.rot[0] = rot.x, m.rot[1] = rot.y, m.rot[2] = rot.z, m.rot[3] = rot.w;
    m.owner = owner_for_record(gi);  // TLDCoop_DLC: a pot on a fire (or the food in it): whose it is
    if (m.gear.size() > net::kGearTextMax) {
        logx::warn("items: %s's save record is too big to send (%zu chars)", m.prefab.c_str(), m.gear.size());
        return false;
    }
    return true;
}

// To every friend (the host) or, `to`, one friend (an answer). `change`: what changed, for the log (else it's new or
// it moved).
void send_put(Il2CppObject* gi, const std::string& pdid, Known& k, double now, uint8_t flags, uint8_t to = 0,
              const char* change = nullptr) {
    Quaternion rot{};
    Vector3 pos{};
    pose_of(gi, pos, rot);
    net::ItemPutMsg m;
    if (!make_put(gi, pdid, k.prefab, flags, pos, rot, m)) return;
    net::Writer w(net::Msg::ItemPut, net::kMaxMessage);
    net::write_item_put(w, m);
    if (to)
        netrt::send_packet_to(to, w, true);
    else
        netrt::send_packet(w, true);
    if (hosting()) g_claims.lies_again(g_scene, m.pdid);  // whoever took it before, it's lying here now
    if (hosting() && !k.unique)  // TLDCoop_DLC: (also when a door's scan doesn't keep the list)
        if (const auto kept = g_scene_items.find(g_scene); kept != g_scene_items.end()) kept->second.insert(net::pdid_hash(m.pdid));
    const bool was_shared = k.shared;
    k.told = pos;
    k.shared = true;
    k.changed_ms = now;
    if (change) {
        if (*change)  // ("" : a regular word, not logged)
            logx::info("items: %s (%s) %s; told the other player (%zu chars)", m.prefab.c_str(),
                       short_id(m.pdid).c_str(), change, m.gear.size());
    }
    else if (!(flags & net::kItemAnswer))
        logx::info("items: %s %s (%s) at (%.1f, %.1f, %.1f); told the other player (%zu chars)%s", m.prefab.c_str(),
                   was_shared ? "moved to" : "is new here,", short_id(m.pdid).c_str(), pos.x, pos.y, pos.z, m.gear.size(),
                   light_state(gi).c_str());
}

// `taken_now`: we just took it into our pack (TLD's Take it), whatever the pack says yet.
void send_gone(const std::string& pdid, const Known& k, bool taken_now = false) {
    net::ItemGoneMsg m;
    m.scene = g_scene;
    m.seq = ++g_seq;
    m.pdid = wire_id(pdid);
    Il2CppObject* gi = gear_on(object_by_pdid(pdid));
    // In our pack, or (a stack) gone onto one there.
    // TLDCoop_DLC (20:34 logs: 11 birch saplings done curing after a sleep gave everyone "Coin toss: someone else gets
    // the Birch Sapling" 5-6 times): an item that was curing and goes is TLD's cure finishing (a new item takes its place
    // in every game), never a take, whatever the pack flag says while TLD swaps it.
    Il2CppObject* ev = gi && api.evolve >= 0 && api.evolve_hours >= 0 ? get<Il2CppObject*>(gi, api.evolve) : nullptr;
    const float days = alive(ev) && api.evolve_days >= 0 ? get<float>(ev, api.evolve_days) : 0;
    const bool cured = days > 0 && get<float>(ev, api.evolve_hours) >= days * 24 - 0.01f;  // its whole time cured
    const bool taken = !cured && (taken_now || (gi ? get<bool>(gi, api.in_inventory) : k.units > 0));
    m.reason = taken ? net::ItemGoneReason::Taken : net::ItemGoneReason::Destroyed;
    net::Writer w(net::Msg::ItemGone);
    net::write_item_gone(w, m);
    netrt::send_packet(w, true);
    if (taken) g_took[m.pdid] = Took{pdid, k.prefab, k.units, now_ms()};  // it may go to someone's take at the same moment
    if (taken && hosting()) g_claims.taken_by_host(g_scene, m.pdid, now_ms());
    if (hosting())  // TLDCoop_DLC: (also when a door's scan doesn't keep the list)
        if (const auto kept = g_scene_items.find(g_scene); kept != g_scene_items.end()) kept->second.erase(net::pdid_hash(m.pdid));
    logx::info("items: %s (%s) %s; told the other player%s", k.prefab.c_str(), short_id(m.pdid).c_str(),
               m.reason == net::ItemGoneReason::Taken ? "went into our pack" : "is gone from the world",
               taken ? light_state(gi).c_str() : "");
    forget_alias(pdid);
}

// TLDCoop_DLC: host: a friend (linked, caught up) in a scene we aren't in, that we have a kept list of, gets it once
// per arrival there (per link).
void send_away_lists() {
    const net::Status st = netrt::status();
    for (const net::PlayerInfo& p : st.players) {
        if (p.id == 0 || p.id >= net::kMaxPlayers) continue;
        AwaySent& sent = g_away_sent[p.id];
        const auto r = p.linked && p.caught_up ? netrt::remote_state(p.id) : std::nullopt;
        if (!r) {
            sent = AwaySent{};
            continue;
        }
        if (!r->valid || r->scene.empty()) continue;  // loading: where they arrive counts
        if (r->scene == g_scene) {  // with us: our live list (g_together)
            sent = AwaySent{};
            continue;
        }
        if (sent.link == p.link && sent.scene == r->scene) continue;
        sent = AwaySent{p.link, r->scene};
        const auto kept = g_scene_items.find(r->scene);
        if (kept == g_scene_items.end()) continue;
        net::ItemHashesMsg m;
        m.scene = r->scene;
        m.hashes.assign(kept->second.begin(), kept->second.end());
        net::Writer w(net::Msg::ItemList, net::kMaxMessage);
        net::write_item_hashes(w, m);
        if (!w.ok()) continue;
        netrt::send_packet_to(p.id, w, true);
        logx::info("items: player %u is in %s, where we aren't; sent the %zu loose item(s) we know there", p.id,
                   m.scene.c_str(), m.hashes.size());
    }
}

void send_list(uint8_t to) {
    net::ItemHashesMsg m;
    m.scene = g_scene;
    for (const auto& [pdid, k] : g_known)
        if (!k.unique) m.hashes.push_back(net::pdid_hash(wire_id(pdid)));
    net::Writer w(net::Msg::ItemList, net::kMaxMessage);
    net::write_item_hashes(w, m);
    netrt::send_packet_to(to, w, true);
    logx::info("items: the other player is in %s with us; sent our %zu loose items (player %u)", g_scene.c_str(),
               m.hashes.size(), to);
}

// Host: what one friend changed goes on to the others, as a change of ours (our counter: each friend's is its own).
void pass_on(uint8_t from, Change c) {
    net::Writer w(c.put ? net::Msg::ItemPut : net::Msg::ItemGone, net::kMaxMessage);
    if (c.put) {
        c.put_msg.seq = ++g_seq;
        net::write_item_put(w, c.put_msg);
    } else {
        c.gone_msg.seq = ++g_seq;
        net::write_item_gone(w, c.gone_msg);
    }
    netrt::send_packet_except(from, w, true);
}

// --- applying the other player's changes ---------------------------------------------------------------------------

bool held_now(const std::string& pdid) {
    std::string inspected, placing;
    busy_items(inspected, placing);
    return pdid == inspected || pdid == placing;
}

void destroy_loose(Il2CppObject* gi) {
    void* args[] = {il2::invoke(api.component_go, gi)};
    il2::invoke(api.destroy_gear, nullptr, args);
}

// TLD's inspect screen buttons (PlayerManager.UpdateInspectGear): Take it / Leave it run the action the screen was
// opened with (m_PickupAction / m_PutBackAction: e.g. back into the container it came from), then
// ExitInspectGearMode(false) (which shows a search's next find).
void press_inspect(Il2CppObject* pm, bool take) {
    const ptrdiff_t field = take ? api.pickup_action : api.put_back_action;
    Il2CppObject* action = field >= 0 ? get<Il2CppObject*>(pm, field) : nullptr;
    if (action) il2::invoke(il2::find_method(il2::il2cpp_object_get_class(action), "Invoke", 0), action);
    bool hard_exit = false;
    void* args[] = {&hard_exit};
    il2::invoke(api.put_back, pm, args);
}

std::string display_name(const std::string& prefab) {
    void* args[] = {il2::il2cpp_string_new(prefab.c_str())};
    const std::string name = api.display_name ? text(reinterpret_cast<Il2CppString*>(il2::invoke(api.display_name, nullptr, args)))
                                              : std::string();
    return name.empty() ? prefab : name;
}

// Host: what our game says about an item a friend just took.
net::TakeFacts facts_for(const std::string& scene, const std::string& wire) {
    net::TakeFacts f;
    f.host_scene = scene == g_scene && g_baseline;
    if (!f.host_scene) return f;
    const std::string& ours = our_id(wire);
    Il2CppObject* gi = gear_on(object_by_pdid(ours));
    f.held = held_now(ours);
    f.held_since_ms = f.held && g_hold == ours ? g_hold_since : -1e18;
    f.loose = gi && is_loose(gi) && !f.held;
    f.known = g_known.count(ours) > 0;
    return f;
}

// Host: friend `to` loses the item: out of their pack (LostToss: someone took it at the same moment and won the toss).
void send_not_yours(uint8_t to, const std::string& scene, const std::string& wire, bool tossed) {
    net::ItemGoneMsg m;
    m.scene = scene;
    m.seq = ++g_seq;
    m.reason = tossed ? net::ItemGoneReason::LostToss : net::ItemGoneReason::NotYours;
    m.pdid = wire;
    net::Writer w(net::Msg::ItemGone);
    net::write_item_gone(w, m);
    netrt::send_packet_to(to, w, true);
}

// Host: friend `to` took an item that isn't theirs: someone's take came first or won the coin toss (out of their
// pack), or we have it in our hands (out of their pack, and it lies where it was for them: we may still leave it).
void refuse(uint8_t to, const net::ItemGoneMsg& take, const net::TakeDecision& d) {
    const std::string& ours = our_id(take.pdid);
    const auto k = g_known.find(ours);
    const auto took = g_took.find(take.pdid);
    const std::string prefab = k != g_known.end() ? k->second.prefab : took != g_took.end() ? took->second.prefab
                                                                                            : short_id(take.pdid);
    if (d.answer == net::TakeAnswer::PutBack && k != g_known.end()) {
        net::ItemPutMsg m;
        if (Il2CppObject* gi = gear_on(object_by_pdid(ours));
            gi && make_put(gi, ours, prefab, net::kItemNotYours, k->second.pos, k->second.rot, m)) {
            m.scene = take.scene;
            net::Writer w(net::Msg::ItemPut, net::kMaxMessage);
            net::write_item_put(w, m);
            netrt::send_packet_to(to, w, true);
            logx::info("items: the other player took %s (%s), but it's in our hands%s; refused: out of their pack, "
                       "back where it lay (player %u)", prefab.c_str(), short_id(take.pdid).c_str(),
                       d.tossed ? " (picked up at the same moment: we won the coin toss)" : "", to);
            return;
        }
    }
    send_not_yours(to, take.scene, take.pdid, d.tossed);
    const int taker = g_claims.taker(take.scene, take.pdid);
    const bool ours_won = taker == net::Claims::kHost;
    if (d.tossed)
        logx::info("items: the other player took %s (%s) at the same moment as %s; the coin said %s: out of their pack "
                   "(player %u)", prefab.c_str(), short_id(take.pdid).c_str(), ours_won ? "we did" : "another player",
                   ours_won ? "ours" : "theirs", to);
    else
        logx::info("items: the other player took %s (%s) after %s did; refused: out of their pack (player %u)",
                   prefab.c_str(), short_id(take.pdid).c_str(), ours_won ? "we" : "another player", to);
}

bool out_of_pack(const Took& took);

// Host: friend `winner`'s take at the same moment as an earlier one won the coin toss: the earlier one is undone, out
// of that player's pack (ours, or a friend's told so) or, ours still in our hands, left.
void undo_take(int loser, uint8_t winner, const net::ItemGoneMsg& take) {
    if (loser != net::Claims::kHost) {
        send_not_yours(static_cast<uint8_t>(loser), take.scene, take.pdid, true);
        logx::info("items: player %u took %s at the same moment as player %d and won the coin toss: out of player %d's pack",
                   winner, short_id(take.pdid).c_str(), loser, loser);
        return;
    }
    const std::string ours = our_id(take.pdid);
    Took took{ours, std::string(), 0};
    bool held = false;
    if (const auto k = g_known.find(ours); k != g_known.end()) {  // just now (our look hasn't seen it go yet), or in hand
        took = Took{ours, k->second.prefab, k->second.units};
        held = held_now(ours);
        if (!held) g_known.erase(k);
    } else if (const auto t = g_took.find(take.pdid); t != g_took.end()) {
        took = t->second;
        g_took.erase(t);
    }
    const std::string what = took.prefab.empty() ? short_id(take.pdid) : took.prefab;
    coop_notices::tell("Coin toss: someone else gets the " + display_name(what));
    if (held) {  // left where it lay; the friend's take, applied next, removes it here
        press_inspect(il2::invoke(api.player_manager, nullptr), false);
        logx::info("items: player %u took %s (%s) as we picked it up and won the coin toss: left it", winner, what.c_str(),
                   short_id(take.pdid).c_str());
        return;
    }
    forget_alias(ours);
    const bool out = out_of_pack(took);
    logx::info("items: player %u took %s (%s) at the same moment as we did and won the coin toss: %s", winner, what.c_str(),
               short_id(take.pdid).c_str(), out ? "out of our pack" : "it isn't in our pack");
}

// Our take of an item is undone: out of our pack (the item, or the units it added to a stack there).
bool out_of_pack(const Took& took) {
    Il2CppObject* inv = il2::invoke(api.inventory, nullptr);
    if (!inv) return false;
    if (Il2CppObject* gi = gear_on(object_by_pdid(took.ours)); gi && get<bool>(gi, api.in_inventory)) {
        void* args[] = {gi};
        il2::invoke(api.inv_destroy, inv, args);
        return true;
    }
    if (took.units <= 0) return false;
    for (Il2CppObject* s : pack_items()) {
        if (name_of(s) != took.prefab || units_of(s) < took.units) continue;
        int32_t n = took.units;
        void* args[] = {s, &n};
        il2::invoke(api.remove_units, inv, args);
        return true;
    }
    return false;
}

// Client: the host refused our take (ItemGone NotYours: someone else's came first; LostToss: someone took it at the
// same moment and won the coin toss; ItemPut kItemNotYours: the host has it in its hands, so it lies where it was
// again).
void refused(const Change& c) {
    const std::string& wire = c.put ? c.put_msg.pdid : c.gone_msg.pdid;
    const auto it = g_took.find(wire);
    const Took took = it != g_took.end() ? it->second : Took{our_id(wire), c.put ? c.put_msg.prefab : std::string(), 0};
    if (it != g_took.end()) g_took.erase(it);
    const bool out = out_of_pack(took);
    const std::string what = took.prefab.empty() ? short_id(wire) : took.prefab;
    if (c.put) {
        if (c.put_msg.scene == g_scene && g_baseline) g_put_back.push_back(c.put_msg);  // (else the host's list, there)
        coop_notices::tell("Someone else had the " + display_name(what) + " in hand first");
    } else if (c.gone_msg.reason == net::ItemGoneReason::LostToss) {
        coop_notices::tell("Coin toss: someone else gets the " + display_name(what));
    } else {
        coop_notices::tell("Someone else took the " + display_name(what) + " first");
    }
    logx::info("items: our take of %s (%s) is refused: %s; %s%s", what.c_str(), short_id(wire).c_str(),
               c.put ? "the host has it in its hands"
               : c.gone_msg.reason == net::ItemGoneReason::LostToss ? "taken at the same moment, and the coin said theirs"
                                                                     : "someone else's came first",
               out ? "out of our pack" : "it isn't in our pack", c.put ? ", back where it lay" : "");
}

// TLDCoop_DLC: inside a container (it lies in it, hidden), up to a few parents.
bool in_a_container(Il2CppObject* gi) {
    if (!api.container_type) return false;
    Il2CppObject* t = transform_of(gi);
    for (int up = 0; up < 4 && t; ++up) {
        void* args[] = {api.container_type};
        if (alive(il2::invoke(api.go_component, il2::invoke(api.component_go, t), args))) return true;
        t = il2::invoke(api.get_parent, t);
    }
    return false;
}

// TLDCoop_DLC (07.11, Shatsca: "na jednym slocie ogniska robi się woda oraz mięso, i jak Szymon kładzie puszki to
// lądują pod ogniskiem"): the other player's item on a cooking slot (its record names a place point) was only put at
// that spot here: not on our slot (the can fell under the fire, and our slot stayed free for another pot). It goes on
// our slot nearest that spot (TLD's GearPlacePoint.PlaceGear), unless something of ours is on it already.
void seat_on_slot(Il2CppObject* gi, const std::string& gear, const Vector3& pos, const char* prefab) {
    if (!gi || !api.slot_type || !api.find_all || !api.slot_place || api.slot_gear < 0) return;
    if (json_string(gear, "m_PlacePointGuidSerialized").empty() && json_string(gear, "m_PlacePointGuid").empty()) return;
    bool inactive_too = false;
    void* fa[] = {api.slot_type, &inactive_too};
    Il2CppObject* slots = il2::invoke(api.find_all, nullptr, fa);
    Il2CppObject* best = nullptr;
    float best_d = 0.6f;
    for (size_t i = 0; i < il2::array_length(slots); ++i) {
        Il2CppObject* slot = il2::array_object_at(slots, i);
        if (!alive(slot)) continue;
        Vector3 at{};
        Quaternion r{};
        pose_of(slot, at, r);
        const float d = distance(at, pos);
        if (d < best_d) best_d = d, best = slot;
    }
    if (!best) {
        logx::info("items: the other player's %s is on a cooking slot in their game; no slot here near (%.1f, %.1f, %.1f)",
                   prefab, pos.x, pos.y, pos.z);
        return;
    }
    Il2CppObject* on = get<Il2CppObject*>(best, api.slot_gear);
    if (on == gi) return;
    if (alive(on)) {
        logx::info("items: the other player's %s is on a cooking slot in their game; ours has %s on it, left beside",
                   prefab, name_of(on).c_str());
        return;
    }
    bool reposition = true;
    void* pa[] = {gi, &reposition};
    il2::invoke(api.slot_place, best, pa);
    logx::info("items: the other player's %s put on our cooking slot there (%.2f m off)", prefab, best_d);
}

void apply_put_record(const net::ItemPutMsg& m, double now) {
    if (net::is_unique_gear(m.prefab)) {  // (never sent; a game from before keeps theirs to themselves too)
        logx::info("items: the other player's %s (%s) is one of a kind: ours stays ours", m.prefab.c_str(),
                   short_id(m.pdid).c_str());
        return;
    }
    const Vector3 pos{m.pos[0], m.pos[1], m.pos[2]};
    const Quaternion rot{m.rot[0], m.rot[1], m.rot[2], m.rot[3]};
    if (seamless_interiors::unbuilt_room_at(pos.x, pos.y, pos.z)) {  // TLDCoop_DLC: it'd fall 80 m from an empty spot
        if (hosting()) {  // the host's game is the one that keeps it: it waits for the room (applied once it's up)
            Change c;
            c.put = true;
            c.put_msg = m;
            c.waited_room = true;
            g_pending[m.scene][m.pdid] = std::move(c);
            logx::info("items: the other player's %s (%s) is in a Seamless Interiors room not built here yet; kept until "
                       "the room is up", m.prefab.c_str(), short_id(m.pdid).c_str());
            return;
        }
        logx::info("items: the other player's %s (%s) is in a Seamless Interiors room not built here yet; it comes with "
                   "the host's list once the room is up",
                   m.prefab.c_str(), short_id(m.pdid).c_str());
        return;
    }
    const std::string ours = our_id(m.pdid);
    Il2CppObject* go = object_by_pdid(ours);
    Il2CppObject* gi = gear_on(go);
    if (gi && is_loose(gi)) {  // we have it lying around: it moved, or (a snare) its state changed
        if (held_now(ours)) return;
        Known& k = g_known[ours];
        const bool moved = distance(k.pos, pos) > kStillMetres;
        // TLDCoop_DLC: on a place point here (a pot in a fire's cooking slot) and still about there in theirs: ours stays
        // put (Shatsca 05.11 20:21: back from a cabin, his cans on the fire were moved to Szymon's spot for them, under
        // the fire and no longer cooking)
        Vector3 here{};
        Quaternion here_rot{};
        pose_of(gi, here, here_rot);
        const bool placed = api.place_point >= 0 && !text(get<Il2CppString*>(gi, api.place_point)).empty() &&
                            distance(here, pos) < 1.0f;
        if (!placed) {
            set_pose(gi, pos, rot);
            seamless_interiors::settle_in_room(gi);  // TLDCoop_DLC: into a room closed here
            if (moved) seat_on_slot(gi, m.gear, pos, m.prefab.c_str());
        }
        k.prefab = m.prefab;
        k.pos = k.told = pos;
        k.rot = rot;
        k.moved_ms = now;
        k.shared = true;
        if (moved || (snare_state(gi) < 0 && !pot_of(gi)))  // (a snare's or a pot's change says itself)
            logx::info("items: the other player's %s (%s) moved to (%.1f, %.1f, %.1f)%s", m.prefab.c_str(),
                       short_id(m.pdid).c_str(), pos.x, pos.y, pos.z, light_state(gi).c_str());
        load_snare(gi, m.gear, m.pdid, k);
        load_opened(gi, m.gear, k);
        load_burn(gi, m.gear, k);
        load_lamp(gi, m.gear);
        load_heater(gi, m.gear);
        load_evolve(gi, m.gear);
        load_wear(gi, m.gear);
        load_pot(gi, m.gear, m.pdid);
        return;
    }
    // TLDCoop_DLC: we have it too, but switched off (a random spawn our game rolled away, the host's kept): that one
    // is shown where the host has it, instead of a second copy beside it ("the same items twice, and anyone could
    // pick up mine").
    if (gi && !get<bool>(gi, api.in_inventory) && !in_a_container(gi) && api.go_set_active) {
        bool on = true;
        il2::invoke_value(api.go_active, go, on);
        if (!on) {
            bool yes = true;
            void* args[] = {&yes};
            il2::invoke(api.go_set_active, go, args);
            set_pose(gi, pos, rot);
            seamless_interiors::settle_in_room(gi);  // TLDCoop_DLC: into a room closed here
            if (!is_loose(gi)) {  // a parent of it is off (not a room's): not what lies in the world; a copy is made
                bool no = false;
                void* off[] = {&no};
                il2::invoke(api.go_set_active, go, off);
            } else {
                Known& k = g_known[ours];
                k.prefab = m.prefab;
                k.pos = k.told = pos;
                k.rot = rot;
                k.units = units_of(gi);
                k.snare = snare_state(gi);
                k.moved_ms = now;
                k.shared = true;
                load_snare(gi, m.gear, m.pdid, k);
                load_opened(gi, m.gear, k);
                load_burn(gi, m.gear, k);
                load_lamp(gi, m.gear);
        load_heater(gi, m.gear);
                load_evolve(gi, m.gear);
        load_wear(gi, m.gear);
                load_pot(gi, m.gear, m.pdid);
                logx::info("items: the other player's %s (%s) was switched off here (our game rolled it away); shown at "
                           "(%.1f, %.1f, %.1f) instead of a second copy", m.prefab.c_str(), short_id(m.pdid).c_str(), pos.x,
                           pos.y, pos.z);
                return;
            }
        }
    }
    // TLDCoop_DLC: we have it in the world, on, but not as a loose item (Kerel 04.11 21:53: the crowbar he'd put on a
    // shelf in the farmhouse an hour before, his own and the host's: "ours: ac665f21" made beside it, both picked up,
    // both dropped: two crowbars). That one is shown where the host has it, freed from what holds it, not a second copy.
    if (gi && !get<bool>(gi, api.in_inventory) && !get<bool>(gi, api.no_save) && !in_a_container(gi) && !held_now(ours)) {
        bool on = false;
        il2::invoke_value(api.go_active, go, on);
        if (on) {
            Il2CppObject* parent = api.get_parent ? il2::invoke(api.get_parent, transform_of(gi)) : nullptr;
            const std::string under = alive(parent) ? name_of(il2::invoke(api.component_go, parent)) : std::string("nothing");
            const bool radial = get<Il2CppObject*>(gi, api.radial_parent) != nullptr;
            if (radial && api.radial_parent >= 0) il2::store_ref(gi, api.radial_parent, nullptr);
            if (alive(parent) && api.set_parent) {
                bool keep = true;
                void* pa[] = {nullptr, &keep};
                il2::invoke(api.set_parent, transform_of(gi), pa);
            }
            set_pose(gi, pos, rot);
            seamless_interiors::settle_in_room(gi);
            if (is_loose(gi)) {
                Known& k = g_known[ours];
                k.prefab = m.prefab;
                k.pos = k.told = pos;
                k.rot = rot;
                k.units = units_of(gi);
                k.snare = snare_state(gi);
                k.moved_ms = now;
                k.shared = true;
                load_snare(gi, m.gear, m.pdid, k);
                load_opened(gi, m.gear, k);
                load_burn(gi, m.gear, k);
                load_lamp(gi, m.gear);
        load_heater(gi, m.gear);
                load_evolve(gi, m.gear);
        load_wear(gi, m.gear);
                load_pot(gi, m.gear, m.pdid);
                logx::info("items: the other player's %s (%s) lay here too but not as a loose item (under %s%s); shown "
                           "at (%.1f, %.1f, %.1f) instead of a second copy", m.prefab.c_str(), short_id(m.pdid).c_str(),
                           under.c_str(), radial ? ", a spawner's" : "", pos.x, pos.y, pos.z);
                return;
            }
            logx::warn("items: the other player's %s (%s) lies here under %s and isn't loose even freed; this one goes, "
                       "a copy of the host's is made", m.prefab.c_str(), short_id(m.pdid).c_str(), under.c_str());
            destroy_loose(gi);
            go = nullptr;
            gi = nullptr;
        }
    }
    // TLDCoop_DLC: a friend's fresh carcass where our game just made one of the same dead animal: ours goes
    wildlife_carcass::remote_carcass_near(m.prefab, pos.x, pos.y, pos.z);
    // Make it from TLD's save record, under the same id unless something here already goes by it.
    std::string id = m.pdid, gear = m.gear;
    forget_alias(ours);
    if (object_by_pdid(m.pdid)) {
        id = text(reinterpret_cast<Il2CppString*>(il2::invoke(api.new_pdid, nullptr)));
        replace_all(gear, m.pdid, id);
    }
    void* name_args[] = {il2::il2cpp_string_new(m.prefab.c_str())};
    Il2CppObject* prefab = il2::invoke(api.sanitize, nullptr, name_args);
    bool positioning_fix = false, in_container = false;
    void* args[] = {prefab ? prefab : name_args[0], il2::il2cpp_string_new(gear.c_str()), nullptr, &positioning_fix,
                    &in_container};
    Il2CppObject* made = il2::invoke(api.instantiate, nullptr, args);
    if (!alive(made)) {
        logx::warn("items: the other player's %s (%s) couldn't be made here", m.prefab.c_str(), short_id(m.pdid).c_str());
        return;
    }
    set_pose(made, pos, rot);
    seamless_interiors::settle_in_room(made);  // TLDCoop_DLC: into a room closed here
    seat_on_slot(made, gear, pos, m.prefab.c_str());
    load_heater(made, gear);
    load_lamp(made, gear);  // TLDCoop_DLC: its fuel as the record says (TLD's own load got it wrong)
    const std::string got = pdid_of(made);
    if (got != m.pdid && !got.empty()) {
        g_alias_in[m.pdid] = got;
        g_alias_out[got] = m.pdid;
    }
    if (got != id) logx::warn("items: our copy of %s came out as %s (wanted %s)", m.prefab.c_str(), got.c_str(), id.c_str());
    Known& k = g_known[got.empty() ? id : got];
    k.prefab = m.prefab;
    k.pos = k.told = pos;
    k.rot = rot;
    k.units = units_of(made);
    k.snare = snare_state(made);
    k.moved_ms = now;
    k.shared = true;
    logx::info("items: the other player's %s (%s%s%s) %s at (%.1f, %.1f, %.1f)%s", m.prefab.c_str(), short_id(m.pdid).c_str(),
               got != m.pdid ? ", ours: " : "", got != m.pdid ? short_id(got).c_str() : "",
               (m.flags & net::kItemAnswer) ? "was missing here; now it's"
               : (m.flags & net::kItemNotYours) ? "lies where it was again"
                                                : "is here now",
               pos.x, pos.y, pos.z, light_state(made).c_str());
}

// TLDCoop_DLC: the other player's record, applied; a pot on a fire it names an owner for is theirs (or ours) from now
// on, and TLD's own calls meanwhile (a slot taking it, the pot loading) aren't ours.
void apply_put(const net::ItemPutMsg& m, double now) {
    ++g_applying;
    apply_put_record(m, now);
    --g_applying;
    Il2CppObject* pot = pot_of(gear_on(object_by_pdid(our_id(m.pdid))));
    if (!pot) return;
    if (m.owner != net::kNoOwner && on_fire(pot)) {
        const uint8_t had = owner_of(pot);
        g_pot_owner[pot] = m.owner;
        g_pot_heard[pot] = now;
        if (had != m.owner && m.owner != self_id())
            logx::info("items: the pot %s (%s) on the fire is %s's: their game cooks it, only they take from it",
                       m.prefab.c_str(), short_id(m.pdid).c_str(), player_name(m.owner).c_str());
    } else if (!on_fire(pot)) {
        g_pot_owner.erase(pot);
        g_pot_heard.erase(pot);
    }
}

void remove_ours(const std::string& ours, const char* why) {
    Il2CppObject* gi = gear_on(object_by_pdid(ours));
    const auto known = g_known.find(ours);
    const std::string prefab = known != g_known.end() ? known->second.prefab : gi ? name_of(gi) : "item";
    if (known != g_known.end()) g_known.erase(known);
    forget_alias(ours);
    // In our hands (TLD's Take it / Leave it): the other take came first (the host decided). We leave it, then it goes.
    const bool held = held_now(ours);
    if (held) {
        Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
        if (inspected_now(pm) == gi) press_inspect(pm, false);
        if (held_now(ours)) {
            logx::warn("items: %s (%s) %s while we hold it; now both of you have one", prefab.c_str(), short_id(ours).c_str(), why);
            return;
        }
    }
    if (!gi || !is_loose(gi)) {
        // TLDCoop_DLC (Shatsca 08.11: "łom wyrzucony przez grzyba się zbugował, nagle były cztery na ziemi"): the id
        // can name two of ours, one in the pack and the one just dropped; TLD's lookup gave the pack's, the dropped
        // one stayed and our next scan sent it again (the others got it once more each time). The loose one goes.
        for (const Loose& l : scan_loose(false))
            if (l.pdid == ours) {
                gi = l.gi;
                break;
            }
    }
    if (!gi || !is_loose(gi)) {
        logx::info("items: %s (%s) %s, but it isn't lying here (already gone or in a pack)", prefab.c_str(),
                   short_id(ours).c_str(), why);
        return;
    }
    destroy_loose(gi);
    if (held) coop_notices::tell("Someone else took the " + display_name(prefab) + " first");
    logx::info("items: %s (%s) %s; %sgone here too", prefab.c_str(), short_id(ours).c_str(), why,
               held ? "it was in our hands: left it, " : "");
}

// Client: someone's take reaches us before our scan has told the host that we took the same item: we both took it
// before hearing of the other's take. Ours goes to the host now and stays in our pack: the host tosses a coin (or, if
// ours comes too late for the same moment, refuses it).
bool report_our_take(const std::string& ours, bool taken_now = false) {
    const auto k = g_known.find(ours);
    if (k == g_known.end()) return false;
    Il2CppObject* gi = gear_on(object_by_pdid(ours));
    if (!taken_now && (gi ? !get<bool>(gi, api.in_inventory) : k->second.units <= 0)) return false;  // (send_gone's rule)
    const std::string prefab = k->second.prefab;
    send_gone(ours, k->second, true);
    g_known.erase(k);
    logx::info("items: %s (%s) was taken by the other player as we %s it; ours went to the host too (a coin decides)",
               prefab.c_str(), short_id(ours).c_str(), taken_now ? "picked" : "took");
    return true;
}

// Client: the same, with it in our hands (Take it / Leave it) since the same moment: we take it, and the coin decides.
// In our hands from before, theirs came after ours but we hadn't said: we leave it (remove_ours).
bool take_held_at_same_moment(const std::string& ours, double now) {
    if (g_hold != ours || now - g_hold_since > net::Claims::kSameMomentMs || !held_now(ours)) return false;
    press_inspect(il2::invoke(api.player_manager, nullptr), true);
    return report_our_take(ours, true);
}

constexpr double kOursWinsMs = 8000;  // TLDCoop_DLC (see apply_gone)

void apply_gone(const net::ItemGoneMsg& m, double now, bool waited_room = false) {
    const std::string ours = our_id(m.pdid);
    if (const auto k = g_known.find(ours); k != g_known.end() && k->second.unique) {
        logx::info("items: the other player took their %s (%s); one of a kind: ours stays", k->second.prefab.c_str(),
                   short_id(m.pdid).c_str());
        return;
    }
    if (!hosting() && m.reason == net::ItemGoneReason::Taken &&
        (take_held_at_same_moment(ours, now) || report_our_take(ours)))
        return;
    // TLDCoop_DLC: one of a Seamless Interiors room's items, the room not built in our game yet: it would come back with
    // the room (a take made twice); the change waits for the room
    // (A room never saved has no file: its items are the template's, rolled with our seed, the same ids the friend's
    // game gave them; so anything not here while a room of the region isn't built waits too, until every room of the
    // region is up here: then it is either here or not a room's.)
    if (hosting() && !object_by_pdid(ours)) {
        const bool in_file =
            seamless_interiors::unbuilt_room_has_item(m.pdid) || seamless_interiors::unbuilt_room_has_item(ours);
        if (in_file || seamless_interiors::rooms_pending_build(m.scene)) {
            Change c;
            c.gone_msg = m;
            c.waited_room = true;
            g_pending[m.scene][m.pdid] = std::move(c);
            logx::info("items: the other player's %s of %s isn't here; %s; kept until %s (applied again as rooms come up)",
                       m.reason == net::ItemGoneReason::Taken ? "take" : "removal", short_id(m.pdid).c_str(),
                       in_file ? "it is in the saved file of a Seamless Interiors room not built here yet"
                               : "a Seamless Interiors room of the region isn't built here yet (it may be one of its items)",
                       in_file ? "the room is up" : "the region's rooms are all up");
            return;
        }
        if (waited_room) {
            logx::info("items: the other player's %s of %s waited for the Seamless Interiors rooms; they are all up here and "
                       "it isn't in any: nothing to remove (dropped)",
                       m.reason == net::ItemGoneReason::Taken ? "take" : "removal", short_id(m.pdid).c_str());
            return;
        }
    }
    // TLDCoop_DLC: removed in the other player's game a moment after we changed it ourselves (Shatsca 04.11 22:59: his
    // wolf meat put on the fire, Szymon cooking at the same spot the same moment: Szymon's game dropped ours and said
    // so, and the host's meat was gone): ours stays, and goes to everyone again (the food first: the pot finds it by id)
    if (m.reason != net::ItemGoneReason::Taken) {
        const auto k = g_known.find(ours);
        Il2CppObject* gi = gear_on(object_by_pdid(ours));
        // (only a pot, or food cooking in one: that was the case. Anything else removed there was picked up there, or
        // drunk: Shatsca 05.11 20:22, Szymon's water bottles, picked up by Shatsca (TLD pours them into the pack's water
        // and destroys them, so it came as "gone", not "taken"), stayed on Szymon's side: picked up twice.)
        auto cooking_here = [&](Il2CppObject* item) {
            if (pot_of(item)) return true;
            for (const auto& [id, known] : g_known)
                if (Il2CppObject* other = gear_on(object_by_pdid(id)))
                    if (Il2CppObject* pot = pot_of(other); pot && food_in(pot) == item) return true;
            return false;
        };
        if (k != g_known.end() && gi && !get<bool>(gi, api.in_inventory) && now - k->second.changed_ms < kOursWinsMs &&
            cooking_here(gi)) {
            if (Il2CppObject* pot = pot_of(gi)) {
                if (Il2CppObject* food = food_in(pot)) {
                    const std::string fid = pdid_of(food);
                    if (auto fk = g_known.find(fid); fk != g_known.end()) send_put(food, fid, fk->second, now, 0, 0, "went into a pot");
                }
                send_put(gi, ours, k->second, now, 0, 0, describe_pot(pot).c_str());
            }
            logx::info("items: the other player's game removed our %s (%s) a moment after we changed it; ours stays%s",
                       k->second.prefab.c_str(), short_id(ours).c_str(), pot_of(gi) ? ", told everyone again" : "");
            return;
        }
    }
    remove_ours(ours, m.reason == net::ItemGoneReason::Taken ? "was taken by the other player"
                                                             : "is gone from the other player's world");
}

void apply_pending() {
    const auto it = g_pending.find(g_scene);
    if (it == g_pending.end()) return;
    std::vector<Change> changes;
    for (auto& [pdid, c] : it->second) changes.push_back(std::move(c));
    g_pending.erase(it);
    std::sort(changes.begin(), changes.end(), [](const Change& a, const Change& b) {
        return static_cast<int32_t>((a.put ? a.put_msg.seq : a.gone_msg.seq) - (b.put ? b.put_msg.seq : b.gone_msg.seq)) < 0;
    });
    size_t waited = 0;
    for (const Change& c : changes) waited += c.waited_room ? 1 : 0;
    if (waited)  // TLDCoop_DLC (each one either applies, or waits again: applying one twice changes nothing)
        logx::info("items: %zu change(s) the other player made in %s, %zu of them waiting for Seamless Interiors rooms: "
                   "applied now (rooms here: generation %u)",
                   changes.size(), g_scene.c_str(), waited, seamless_interiors::rooms_generation());
    else
        logx::info("items: %zu change(s) the other player made in %s while we were elsewhere", changes.size(), g_scene.c_str());
    const double now = now_ms();
    for (const Change& c : changes) {
        if (c.put)
            apply_put(c.put_msg, now);
        else
            apply_gone(c.gone_msg, now, c.waited_room);
    }
}

// Client: the host's list of what lies in this scene. What we have and the host doesn't goes (unless we changed it
// just now: the host may not have heard yet); what the host has and we don't, we ask for.
void apply_list(const net::ItemHashesMsg& m, double now) {
    const std::unordered_set<uint64_t> host(m.hashes.begin(), m.hashes.end());
    std::unordered_set<uint64_t> ours;
    std::vector<std::string> extra;
    for (const auto& [pdid, k] : g_known) {
        if (k.unique) continue;  // ours, whatever the host has
        const uint64_t h = net::pdid_hash(wire_id(pdid));
        ours.insert(h);
        if (!host.count(h) && k.shared && now - k.changed_ms > kListGraceMs) {
            // TLDCoop_DLC: in a Seamless Interiors room the host hasn't said is up in its game: not in its list yet
            if (seamless_interiors::host_lacks_room_of(object_by_pdid(pdid))) continue;
            extra.push_back(pdid);
        }
    }
    for (const std::string& pdid : extra) remove_ours(pdid, "isn't in the host's world");
    net::ItemHashesMsg want;
    want.scene = g_scene;
    for (uint64_t h : m.hashes)
        if (!ours.count(h)) want.hashes.push_back(h);
    logx::info("items: the host has %zu loose items in %s, we had %zu: removed %zu, asking for %zu", m.hashes.size(),
               g_scene.c_str(), ours.size(), extra.size(), want.hashes.size());
    if (want.hashes.empty()) return;
    net::Writer w(net::Msg::ItemWant, net::kMaxMessage);
    net::write_item_hashes(w, want);
    netrt::send_packet(w, true);
}

// Host: friend `from` asks for these.
void answer_want(const net::ItemHashesMsg& m, double now, uint8_t from) {
    if (m.scene != g_scene) {
        // TLDCoop_DLC: answered from the changes kept for that scene (what a friend put there while we were away)
        int sent = 0;
        if (const auto pend = g_pending.find(m.scene); pend != g_pending.end()) {
            const std::unordered_set<uint64_t> asked(m.hashes.begin(), m.hashes.end());
            for (const auto& [pdid, c] : pend->second) {
                if (!c.put || !asked.count(net::pdid_hash(c.put_msg.pdid))) continue;
                net::ItemPutMsg put = c.put_msg;
                put.seq = ++g_seq;
                put.flags |= net::kItemAnswer;
                net::Writer w(net::Msg::ItemPut, net::kMaxMessage);
                net::write_item_put(w, put);
                netrt::send_packet_to(from, w, true);
                ++sent;
            }
        }
        int from_records = 0;
        if (const auto recs = g_scene_records.find(m.scene); recs != g_scene_records.end()) {
            std::unordered_set<uint64_t> done;
            if (const auto pend = g_pending.find(m.scene); pend != g_pending.end())
                for (const auto& [pdid, c] : pend->second) done.insert(net::pdid_hash(c.put ? c.put_msg.pdid : c.gone_msg.pdid));
            for (uint64_t h : m.hashes) {
                const auto it = recs->second.find(h);
                if (it == recs->second.end() || done.count(h)) continue;
                net::ItemPutMsg put = it->second;
                put.seq = ++g_seq;
                put.flags |= net::kItemAnswer;
                net::Writer w(net::Msg::ItemPut, net::kMaxMessage);
                net::write_item_put(w, put);
                netrt::send_packet_to(from, w, true);
                ++from_records;
            }
        }
        logx::info("items: the other player asked for %zu item(s) in %s; we're in %s now: sent %d kept from the friends' "
                   "changes there, %d from what we saw there", m.hashes.size(), m.scene.c_str(), g_scene.c_str(), sent,
                   from_records);
        return;
    }
    std::unordered_map<uint64_t, std::string> by_hash;
    for (const auto& [pdid, k] : g_known)
        if (!k.unique) by_hash[net::pdid_hash(wire_id(pdid))] = pdid;
    int sent = 0;
    for (uint64_t h : m.hashes) {
        const auto it = by_hash.find(h);
        if (it == by_hash.end()) continue;
        Il2CppObject* gi = gear_on(object_by_pdid(it->second));
        if (!gi) continue;
        send_put(gi, it->second, g_known[it->second], now, net::kItemAnswer, from);
        ++sent;
    }
    logx::info("items: the other player was missing %zu of our items; sent %d", m.hashes.size(), sent);
}

void receive(double now) {
    std::vector<net::Packet> packets;
    netrt::drain_packets(net::Msg::ItemPut, packets);
    netrt::drain_packets(net::Msg::ItemGone, packets);
    netrt::drain_packets(net::Msg::ItemList, packets);
    netrt::drain_packets(net::Msg::ItemWant, packets);
    for (const net::Packet& p : packets) {
        net::Reader r = net::packet_body(p);
        if (p.type == net::Msg::ItemPut || p.type == net::Msg::ItemGone) {
            Change c;
            c.put = p.type == net::Msg::ItemPut;
            if (c.put ? !net::read_item_put(r, c.put_msg) : !net::read_item_gone(r, c.gone_msg)) continue;
            const std::string& scene = c.put ? c.put_msg.scene : c.gone_msg.scene;
            const std::string& pdid = c.put ? c.put_msg.pdid : c.gone_msg.pdid;
            const uint32_t seq = c.put ? c.put_msg.seq : c.gone_msg.seq;
            if (!g_newest.take(p, pdid, seq)) continue;  // a late older change
            if (c.put ? (c.put_msg.flags & net::kItemNotYours) != 0
                      : c.gone_msg.reason == net::ItemGoneReason::NotYours || c.gone_msg.reason == net::ItemGoneReason::LostToss) {
                if (!hosting()) refused(c);
                continue;
            }
            if (hosting()) {
                if (c.put) {
                    g_claims.lies_again(scene, pdid);
                } else if (c.gone_msg.reason == net::ItemGoneReason::Taken) {
                    const net::TakeDecision d = g_claims.take(scene, pdid, p.from, facts_for(scene, pdid), now);
                    if (d.answer != net::TakeAnswer::Yours) {  // not theirs: the others never hear of it
                        refuse(p.from, c.gone_msg, d);
                        continue;
                    }
                    if (d.loser >= 0) undo_take(d.loser, p.from, c.gone_msg);
                }
                pass_on(p.from, c);
            }
            if (scene == g_scene && g_baseline) {
                // TLDCoop_DLC: a change of it kept for a room (an older one) is overtaken by this one
                if (auto w = g_pending.find(scene); w != g_pending.end() && w->second.erase(pdid)) {
                    logx::info("items: an older change to %s that waited for a Seamless Interiors room is replaced by this "
                               "one", short_id(pdid).c_str());
                    if (w->second.empty()) g_pending.erase(w);
                }
                if (c.put)
                    apply_put(c.put_msg, now);
                else
                    apply_gone(c.gone_msg, now);
            } else {
                // TLDCoop_DLC: the list kept for that scene follows it
                if (hosting() && scene != g_scene)
                    if (const auto kept = g_scene_items.find(scene); kept != g_scene_items.end()) {
                        if (c.put)
                            kept->second.insert(net::pdid_hash(pdid));
                        else
                            kept->second.erase(net::pdid_hash(pdid));
                    }
                logx::info("items: the other player's change to %s in %s waits until we get there", short_id(pdid).c_str(),
                           scene.c_str());
                g_pending[scene][pdid] = std::move(c);
            }
        } else if (p.type == net::Msg::ItemList) {
            net::ItemHashesMsg m;
            if (!net::read_item_hashes(r, m) || hosting()) continue;
            if (m.scene == g_scene && g_baseline) {
                apply_list(m, now);
            } else if (m.scene == g_scene) {
                g_list = std::move(m);
                g_have_list = true;
            }
        } else if (p.type == net::Msg::ItemWant) {
            net::ItemHashesMsg m;
            if (net::read_item_hashes(r, m) && hosting()) answer_want(m, now, p.from);
        }
    }
}

// --- scanning ------------------------------------------------------------------------------------------------------

void scan(double now) {
    if (!api.held_by_resolved) resolve_held_by();
    const bool first = !g_baseline;
    // TLDCoop_DLC: a Seamless Interiors room came up in our game: what is in it is taken in as both games' (the host's
    // lists, sent again, settle what differs), not told as new items of ours.
    static uint32_t rooms_seen = 0;
    const uint32_t rooms_now = seamless_interiors::rooms_generation();
    const bool room_up = rooms_now != rooms_seen;
    rooms_seen = rooms_now;
    int room_items = 0;
    if (first) apply_pending();  // what the other player changed here while we were elsewhere
    else if (room_up && hosting()) apply_pending();  // TLDCoop_DLC: changes that waited for a room of ours
    const double t0 = now_ms();
    for (auto it = g_pot_owner.begin(); it != g_pot_owner.end();) {  // TLDCoop_DLC: pots gone from the world
        if (alive(it->first)) {
            ++it;
        } else {
            g_pot_heard.erase(it->first);
            it = g_pot_owner.erase(it);
        }
    }
    std::vector<Loose> loose = scan_loose(true);
    std::string inspected, placing;
    busy_items(inspected, placing);
    std::unordered_set<std::string> present;
    for (const Loose& l : loose) {
        present.insert(l.pdid);
        if (l.pdid == inspected || l.pdid == placing) continue;
        auto it = g_known.find(l.pdid);
        if (it == g_known.end()) {
            Known k;
            k.prefab = name_of(l.gi);
            k.pos = l.pos;
            k.rot = l.rot;
            k.units = l.units;
            k.moved_ms = now;
            k.snare = snare_state(l.gi);
            k.unique = is_unique(l.gi);
            k.opened = opened_of(l.gi);
            k.burning = burning_light(l.gi);
            if (k.unique) {
                logx::info("items: %s (%s) at (%.1f, %.1f, %.1f) is one of a kind: %s", k.prefab.c_str(),
                           short_id(l.pdid).c_str(), l.pos.x, l.pos.y, l.pos.z,
                           first ? "each player has their own" : "it lies here for us only");
            } else if (first) {  // it was here when we came in: the other player's game has it too
                k.told = l.pos;
                k.shared = true;
            } else if (room_up && seamless_interiors::is_clone_object(l.gi)) {  // TLDCoop_DLC: a room's, just up
                k.told = l.pos;
                k.shared = true;
                ++room_items;
            }
            g_known.emplace(l.pdid, std::move(k));
            continue;
        }
        Known& k = it->second;
        // TLDCoop_DLC (Karol 11.11): someone else's pot on a fire here, or the food in it: their game tells how it goes
        k.foreign = foreign_pot(pot_of(l.gi)) || foreign_pot(pot_cooking(l.gi));
        if (k.foreign) {
            k.pos = l.pos;
            k.rot = l.rot;
            if (Il2CppObject* pot = pot_of(l.gi)) g_pot_sig[pot] = pot_sig(pot);
            continue;
        }
        if (distance(l.pos, k.pos) > kStillMetres) {
            k.pos = l.pos;
            k.rot = l.rot;
            k.moved_ms = now;
            // TLDCoop_DLC: a burning flare or torch in flight goes out as it flies (and we look again soon).
            if (!k.unique && burning_light(l.gi)) {
                g_fast_until = now + kFastScanForMs;
                if (now - k.changed_ms >= kFastScanMs * 2 && (!k.shared || distance(k.pos, k.told) > kMovedMetres))
                    send_put(l.gi, l.pdid, k, now, 0, 0, "");
            }
            continue;
        }
        if (k.unique) continue;
        if (const int snare = snare_state(l.gi); snare != k.snare) {  // set, caught or broken here
            k.snare = snare;
            if (k.shared) {
                const std::string change = std::string("is a snare ") + snare_name(snare) + " now";
                send_put(l.gi, l.pdid, k, now, 0, 0, change.c_str());
                continue;
            }
        }
        if (const bool opened = opened_of(l.gi); opened != k.opened) {  // TLDCoop_DLC: a can opened here
            k.opened = opened;
            if (k.shared && opened) {
                send_put(l.gi, l.pdid, k, now, 0, 0, "was opened");
                continue;
            }
        }
        // TLDCoop_DLC: the host says how a lying item that cures (a hide, gut) is getting on every 30 s
        if (hosting() && k.shared && api.evolve >= 0 && api.evolve_hours >= 0 && now - k.changed_ms >= 30000) {
            Il2CppObject* ev = get<Il2CppObject*>(l.gi, api.evolve);
            if (alive(ev) && get<float>(ev, api.evolve_hours) > 0) {
                send_put(l.gi, l.pdid, k, now, 0, 0, "is curing");
                continue;
            }
        }
        if (Il2CppObject* pot = pot_of(l.gi)) {
            // Put in or taken out (the hooks), or (TLDCoop_DLC) what's in it changed another way: snow put in, or
            // (the host: its game decides how cooking goes) snow melted, food ready or ruined. The host also says
            // how a pot that is cooking is getting on, every kPotSyncMs.
            const PotSig sig = pot_sig(pot);
            PotSig& last = g_pot_sig[pot];
            // (the host coming in told every pot "empty" 26 ms after loading the friend's cooking: the meat went in
            // again, its cooking started over; not while the food we were told of is still on its way)
            if (const auto ld = g_pot_loaded_ms.find(pot); ld != g_pot_loaded_ms.end()) {
                if (now - ld->second < 5000 && last.kind == 1 && sig.kind != 1 && !g_pots_changed.count(pot)) continue;
                if (now - ld->second >= 5000) g_pot_loaded_ms.erase(ld);
            }
            bool tell = g_pots_changed.erase(pot) > 0;
            // TLDCoop_DLC: the pot's owner (who put it on the fire) tells how it goes; the host for a pot nobody here owns
            const uint8_t owner = on_fire(pot) ? owner_of(pot) : net::kNoOwner;
            const bool ours = owner != net::kNoOwner && owner == self_id();
            const bool runs_it = ours || (hosting() && !owner_with_us(owner));
            if (last.kind >= 0 && !(sig == last)) {
                if (runs_it)
                    tell = true;
                else if (sig.kind != last.kind && !(last.kind == 2 && sig.kind == 3))  // (snow melts by itself)
                    tell = true;
            }
            last = sig;
            // TLDCoop_DLC: never while the food it names is in our pack (a stale record would bind it there)
            Il2CppObject* cooking = food_in(pot);
            const bool food_packed = cooking && in_pack(cooking);
            // (the owner's word also while the food is ready or ruined: others know it's still theirs)
            const bool regular = !tell && runs_it && sig.kind > 0 && (sig.state == 0 || ours) &&
                                 now - k.changed_ms >= kPotSyncMs && !food_packed;
            if ((tell || regular) && k.shared && !food_packed) {
                if (Il2CppObject* food = cooking; food && tell) {  // the food first: the pot finds it by id
                    const std::string id = pdid_of(food);
                    Known& fk = g_known[id];
                    fk.prefab = name_of(food);
                    pose_of(food, fk.pos, fk.rot);
                    fk.units = units_of(food);
                    fk.opened = opened_of(food);
                    fk.moved_ms = now;
                    // (always: its record changed, opened for one, even if the other player had it lying before)
                    send_put(food, id, fk, now, 0, 0, "went into a pot");
                }
                send_put(l.gi, l.pdid, k, now, 0, 0, tell ? describe_pot(pot).c_str() : "");
                continue;
            }
        }
        if (const bool burning = burning_light(l.gi); burning != k.burning || (burning && k.shared && hosting() &&
                                                                               now - k.changed_ms >= kBurnSyncMs)) {
            // TLDCoop_DLC: lit or gone out here (a friend's game goes by the host's), or how long it has burnt.
            const bool went_out = k.burning && !burning;
            const bool lit = !k.burning && burning;
            k.burning = burning;
            if (k.shared && (hosting() || lit)) {
                send_put(l.gi, l.pdid, k, now, 0, 0, went_out ? "went out" : lit ? "was lit" : "");
                continue;
            }
        }
        const double settle = burning_light(l.gi) ? kSettleLitMs : kSettleMs;
        if (now - k.moved_ms >= settle && (!k.shared || distance(k.pos, k.told) > kMovedMetres)) {
            // TLDCoop_DLC: a new pot with food in it (a cup of coffee put on a stove to warm: TLD wraps it in a
            // CookingPotDummy) - the food goes first, or the other game's pot had nothing to show (the food unknown there)
            if (Il2CppObject* food = !k.shared ? food_in(pot_of(l.gi)) : nullptr) {
                const std::string id = pdid_of(food);
                Known& fk = g_known[id];
                fk.prefab = name_of(food);
                pose_of(food, fk.pos, fk.rot);
                fk.units = units_of(food);
                fk.opened = opened_of(food);
                fk.moved_ms = now;
                send_put(food, id, fk, now, 0, 0, "went into a new pot");
            }
            send_put(l.gi, l.pdid, k, now, 0);
        }
    }
    // TLDCoop_DLC: going through a door, TLD takes every item of the scene away a moment before the scene changes:
    // that isn't destroying them ("all the water was gone": a friend's door told the host 14 items were destroyed,
    // and the host's game, still in that house, removed them). Not told while a door is being used, nor when many
    // items leave at once; the other player's list sorts it out when we come back.
    size_t shared_known = 0, shared_gone = 0;
    for (const auto& [id, k] : g_known) {
        if (!k.shared || k.unique) continue;
        ++shared_known;
        if (!present.count(id) && id != inspected && id != placing) ++shared_gone;
    }
    const bool through_door = now < g_door_until;
    g_scan_left_at_once = false;
    // (TLDCoop_DLC: every one of them at once is the scene going too, however few)
    const bool all_at_once = (shared_gone >= 4 && shared_gone * 2 >= shared_known) || (shared_gone > 0 && shared_gone == shared_known);
    if ((through_door || all_at_once) && shared_gone > 0) g_scan_left_at_once = true;
    if ((through_door || all_at_once) && shared_gone > 0)
        logx::info("items: %zu of our %zu item(s) here left our view at once (%s); not told as destroyed", shared_gone,
                   shared_known, through_door ? "a door is being used" : "the scene is going");
    for (auto it = g_known.begin(); it != g_known.end();) {
        if (present.count(it->first) || it->first == inspected || it->first == placing) {
            ++it;
            continue;
        }
        if (it->second.unique)
            logx::info("items: %s (%s), one of a kind, is gone from our world; the other player keeps theirs",
                       it->second.prefab.c_str(), short_id(it->first).c_str());
        else if (it->second.foreign)  // TLDCoop_DLC: someone else's pot or food on a fire: theirs to tell
            logx::info("items: %s (%s), on someone else's fire slot, left our view; their game tells what became of it",
                       it->second.prefab.c_str(), short_id(it->first).c_str());
        else if (it->second.shared && !through_door && !all_at_once)
            send_gone(it->first, it->second);
        else if (it->second.shared) {  // TLDCoop_DLC: through a door / all at once: what went into our pack is still told
            Il2CppObject* gi = gear_on(object_by_pdid(it->first));
            if (gi && get<bool>(gi, api.in_inventory)) send_gone(it->first, it->second, true);
        }
        it = g_known.erase(it);
    }
    if (room_items)
        logx::info("items: a Seamless Interiors room came up here: its %d item(s) taken in as both games'%s", room_items,
                   hosting() ? "" : " (the host's list settles what differs)");
    if (first) {
        g_baseline = true;
        logx::info("items: %zu loose items in %s (%d given an id now, %d without one), scan %.1f ms", g_known.size(),
                   g_scene.c_str(), g_ids_given, g_without_id, now_ms() - t0);
        g_ids_given = 0;
        if (g_have_list) {
            g_have_list = false;
            apply_list(g_list, now);
        }
    }
}

// --- test helpers --------------------------------------------------------------------------------------------------

Vector3 player_pos() {
    const game::PlayerState p = game::read_player();
    return Vector3{p.position.x, p.position.y, p.position.z};
}

std::vector<Il2CppObject*> pack_items() {
    std::vector<Il2CppObject*> out;
    Il2CppObject* inv = il2::invoke(api.inventory, nullptr);
    Il2CppObject* list = get<Il2CppObject*>(inv, api.inv_items);
    Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
    const int32_t count = get<int32_t>(list, kListSize);
    for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* gi = *reinterpret_cast<Il2CppObject* const*>(reinterpret_cast<const char*>(items) + 0x20 +
                                                                   static_cast<size_t>(i) * kInventoryEntry);
        if (alive(gi)) out.push_back(gi);
    }
    return out;
}

}  // namespace

void describe(float metres) {
    if (!g_ready) return;
    const Vector3 me = player_pos();
    std::vector<std::pair<float, Loose>> nearby;
    for (Loose& l : scan_loose(false)) {
        const float d = distance(l.pos, me);
        if (d <= metres) nearby.emplace_back(d, std::move(l));
    }
    std::sort(nearby.begin(), nearby.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    logx::info("dev: items: %zu loose within %.0f m (%zu known in %s)", nearby.size(), metres, g_known.size(),
               g_scene.empty() ? "no scene" : g_scene.c_str());
    // Loose gear with no persistent id (not shared): what it is, so we can tell why.
    {
        Il2CppObject* list = nullptr;
        il2::il2cpp_field_static_get_value(api.all_gear, &list);
        Il2CppObject* items = get<Il2CppObject*>(list, kListItems);
        const int32_t count = get<int32_t>(list, kListSize);
        int shown = 0;
        for (int32_t i = 0; i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
            Il2CppObject* gi = il2::array_object_at(items, static_cast<size_t>(i));
            if (!is_loose(gi) || !pdid_of(gi).empty() || shown >= 12) continue;
            ++shown;
            Vector3 pos{};
            Quaternion rot{};
            pose_of(gi, pos, rot);
            Il2CppObject* og = get<Il2CppObject*>(gi, api.object_guid);
            Il2CppObject* parent = il2::invoke(api.get_parent, transform_of(gi));
            logx::info("dev:   no id: %s at (%.1f, %.1f, %.1f), %.0f m, ObjectGuid %s%s%s", name_of(gi).c_str(), pos.x, pos.y,
                       pos.z, distance(pos, me), alive(og) ? "present" : "none", alive(parent) ? ", under " : "",
                       alive(parent) ? name_of(parent).c_str() : "");
        }
    }
    for (const auto& [d, l] : nearby) {
        const auto k = g_known.find(l.pdid);
        Il2CppObject* parent = il2::invoke(api.get_parent, transform_of(l.gi));
        logx::info("dev:   %s %s at (%.1f, %.1f, %.1f), %.1f m%s%s%s", short_id(l.pdid).c_str(), name_of(l.gi).c_str(),
                   l.pos.x, l.pos.y, l.pos.z, d, k == g_known.end() ? ", not scanned yet" : k->second.shared ? ", shared" : ", ours only",
                   alive(parent) ? ", under " : "", alive(parent) ? name_of(parent).c_str() : "");
    }
}

void describe_pack() {
    if (!g_ready) return;
    const std::vector<Il2CppObject*> items = pack_items();
    logx::info("dev: pack: %zu items", items.size());
    for (Il2CppObject* gi : items) logx::info("dev:   %s %s", short_id(pdid_of(gi)).c_str(), name_of(gi).c_str());
}

bool pickup(const std::string& name) {
    if (!g_ready) return false;
    const Vector3 me = player_pos();
    Il2CppObject* best = nullptr;
    float best_d = 1e9f;
    for (const Loose& l : scan_loose(false)) {
        const float d = distance(l.pos, me);
        if (d < best_d && contains_nocase(name_of(l.gi), name)) best = l.gi, best_d = d;
    }
    if (!best || best_d > 6.0f) {
        logx::warn("dev: pickup: no loose %s within 6 m", name.empty() ? "item" : name.c_str());
        return false;
    }
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    const std::string what = name_of(best), id = pdid_of(best);
    bool force_equip = false, equip_now = false, skip_audio = false, taken = false;
    void* args[] = {best, &force_equip, &equip_now, &skip_audio};
    il2::invoke_value(api.pickup, pm, taken, args);
    Il2CppObject* after = gear_on(object_by_pdid(id));
    logx::info("dev: pickup: %s (%s) %.1f m away: TLD %s; %s", what.c_str(), short_id(id).c_str(), best_d,
               taken ? "took it" : "said no",
               after && get<bool>(after, api.in_inventory) ? "it's in our pack" : alive(after) ? "it's still lying there" : "it's gone (into a stack)");
    return taken;
}

bool press_inspect_button(bool take) {
    const char* verb = take ? "takeit" : "putback";
    if (!g_ready || !api.put_back) return false;
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    Il2CppObject* gi = inspected_now(pm);
    if (!gi) {
        logx::info("dev: %s: not looking at an item", verb);
        return false;
    }
    const std::string what = name_of(gi), id = pdid_of(gi);
    press_inspect(pm, take);
    Il2CppObject* after = gear_on(object_by_pdid(id));
    Il2CppObject* next = inspected_now(pm);
    logx::info("dev: %s: %s (%s), as TLD's %s button does; %s%s%s", verb, what.c_str(), short_id(id).c_str(),
               take ? "Take it" : "Leave it",
               after && get<bool>(after, api.in_inventory) ? "it's in our pack" : alive(after) ? "it's out of our hands" : "it's gone",
               alive(next) ? "; now looking at " : "", alive(next) ? name_of(next).c_str() : "");
    return true;
}

bool put_back() { return press_inspect_button(false); }
bool take_it() { return press_inspect_button(true); }

bool snare(const std::string& what) {
    const Vector3 me = player_pos();
    Loose best;
    float best_m = 6;
    for (const Loose& l : scan_loose(false))
        if (snare_state(l.gi) >= 0 && distance(l.pos, me) < best_m) {
            best = l;
            best_m = distance(l.pos, me);
        }
    if (!best.gi) {
        logx::warn("dev: snare: no snare lying within 6 m");
        return false;
    }
    Il2CppObject* s = get<Il2CppObject*>(best.gi, api.snare);
    if (what == "set") {
        il2::invoke(api.snare_placed, s);  // as placing it does: set, rolling from now
    } else if (what == "break" || what == "catch") {
        int32_t state = what == "break" ? 2 : 3;
        void* args[] = {&state};
        il2::invoke(api.snare_set_state, s, args);
        if (what == "catch") {  // what a roll that catches does (DoRoll)
            void* rabbit[] = {api.snare_rabbit >= 0 ? get<Il2CppObject*>(s, api.snare_rabbit) : nullptr};
            il2::invoke(api.snare_spawn_rabbit, s, rabbit);
        }
    } else if (what == "roll") {
        il2::invoke(api.snare_roll, s);  // TLD's own roll (skipped in a friend's game while the host runs the scene)
    } else if (!what.empty()) {
        logx::warn("dev: snare: say set, catch, break or roll (or nothing: its state)");
        return false;
    }
    logx::info("dev: snare: %s (%s), %.1f m away, is %s", name_of(best.gi).c_str(), short_id(best.pdid).c_str(), best_m,
               snare_name(snare_state(best.gi)));
    return true;
}

bool record(const std::string& name) {
    const Vector3 me = player_pos();
    Loose best;
    float best_m = 6;
    for (const Loose& l : scan_loose(false))
        if ((name.empty() || contains_nocase(name_of(l.gi), name)) && distance(l.pos, me) < best_m) {
            best = l;
            best_m = distance(l.pos, me);
        }
    net::ItemPutMsg m;
    if (!best.gi || !make_put(best.gi, best.pdid, name_of(best.gi), 0, best.pos, best.rot, m)) {
        logx::warn("dev: record: no loose item%s%s within 6 m with a save record", name.empty() ? "" : " like ",
                   name.c_str());
        return false;
    }
    logx::info("dev: record: %s (%s), %zu chars: %.1500s", m.prefab.c_str(), short_id(best.pdid).c_str(), m.gear.size(),
               m.gear.c_str());
    return true;
}

bool gear_record(Il2CppObject* gi, std::string& prefab, std::string& record) {
    if (!g_ready || !alive(gi)) return false;
    Il2CppObject* data = il2::invoke(api.create_save_data, gi);
    if (!data) return false;
    prefab = text(get<Il2CppString*>(data, api.sd_prefab));
    record = text(get<Il2CppString*>(data, api.sd_gear));
    const std::string pdid = pdid_of(gi);
    const std::string fresh = text(reinterpret_cast<Il2CppString*>(il2::invoke(api.new_pdid, nullptr)));
    if (!pdid.empty() && !fresh.empty()) replace_all(record, pdid, fresh);  // the given one is another item
    return !prefab.empty() && record.size() <= net::kGearTextMax;
}

Il2CppObject* gear_from_record(const std::string& prefab, const std::string& record) {
    if (!g_ready) return nullptr;
    // (Its id is new: gear_record gave it one, so nothing here goes by it.)
    void* name_args[] = {il2::il2cpp_string_new(prefab.c_str())};
    Il2CppObject* sane = il2::invoke(api.sanitize, nullptr, name_args);
    bool positioning_fix = false, in_container = false;
    void* args[] = {sane ? sane : name_args[0], il2::il2cpp_string_new(record.c_str()), nullptr, &positioning_fix,
                    &in_container};
    Il2CppObject* made = il2::invoke(api.instantiate, nullptr, args);
    if (!alive(made)) return nullptr;
    load_heater(made, record);
    load_lamp(made, record);  // TLDCoop_DLC: a lamp given keeps its fuel (Shatsca 07.11: under half, came back full)
    return made;
}

std::vector<Il2CppObject*> pack() { return g_ready ? pack_items() : std::vector<Il2CppObject*>{}; }

bool in_pack(Il2CppObject* gi) { return alive(gi) && api.in_inventory >= 0 && get<bool>(gi, api.in_inventory); }

int units(Il2CppObject* gi) { return units_of(gi); }

bool one_of_a_kind(Il2CppObject* gi) { return alive(gi) && is_unique(gi); }

std::string gear_name(const std::string& prefab) { return display_name(prefab); }

std::string prefab_of(Il2CppObject* gi) {
    if (!alive(gi)) return {};
    void* args[] = {il2::il2cpp_string_new(name_of(gi).c_str())};
    Il2CppString* sane = reinterpret_cast<Il2CppString*>(il2::invoke(api.sanitize, nullptr, args));
    return sane ? text(sane) : name_of(gi);
}

bool cooking(const std::string& what, const std::string& arg) {
    if (api.pot < 0 || api.pot_hours < 0 || api.pot_state < 0) return false;
    const Vector3 me = player_pos();
    const bool act = what == "take" || what == "put" || what == "boil" || what == "melt";
    const float hours = what.empty() || act ? 0.0f : static_cast<float>(atof(what.c_str()));
    int pots = 0;
    for (const Loose& l : scan_loose(false)) {
        Il2CppObject* pot = get<Il2CppObject*>(l.gi, api.pot);
        if (!alive(pot) || distance(l.pos, me) > 6) continue;
        ++pots;
        if (hours > 0) put<float>(pot, api.pot_hours, get<float>(pot, api.pot_hours) + hours);  // as if they'd passed
        Il2CppObject* food = get<Il2CppObject*>(pot, api.pot_food);
        Il2CppObject* fire = get<Il2CppObject*>(pot, api.pot_fire);
        const int state = get<int32_t>(pot, api.pot_state);
        logx::info("dev: cooking: %s (%s), %.1f m away, on %s: %s %s, %.2f h, %.0f%% cooked, %.0f%% ruined, water %.2f l, "
                   "snow %.2f l", name_of(l.gi).c_str(), short_id(l.pdid).c_str(), distance(l.pos, me),
                   alive(fire) ? "a fire" : "no fire", alive(food) ? name_of(food).c_str() : "nothing",
                   state == 0 ? "cooking" : state == 1 ? "ready" : "ruined", get<float>(pot, api.pot_hours),
                   get<float>(pot, api.pot_cooked) * 100, get<float>(pot, api.pot_ruined) * 100,
                   get<int64_t>(pot, api.pot_water) / 1e9, get<int64_t>(pot, api.pot_snow) / 1e9);
        if (!act || !alive(fire)) continue;
        bool dummy = false;
        if (what != "take" && api.pot_dummy && il2::invoke_value(api.pot_dummy, pot, dummy) && dummy) continue;
        if (what == "take") {
            if (!api.pot_take) return false;
            il2::invoke(api.pot_take, pot);  // what taking it from the fire does (CookingPotItem.PerformInteraction)
            logx::info("dev: cooking: took what was in the %s", name_of(l.gi).c_str());
        } else if (what == "put") {
            Il2CppObject* gi = nullptr;
            for (Il2CppObject* candidate : pack_items())
                if (!gi && contains_nocase(name_of(candidate), arg)) gi = candidate;
            if (!gi || !api.pot_cook) {
                logx::warn("dev: cooking: nothing like %s in our pack", arg.c_str());
                return false;
            }
            const std::string name = name_of(gi);
            void* args[] = {gi};
            il2::invoke(api.pot_cook, pot, args);  // what the cooking panel does with a pot (OnCookNormalFood)
            logx::info("dev: cooking: put the %s in the %s", name.c_str(), name_of(l.gi).c_str());
        } else {
            int64_t units = static_cast<int64_t>(atof(arg.c_str()) * 1e9);  // ItemLiquidVolume.m_Units: 1e-9 l
            bool take_water = true;
            void* boil_args[] = {&units, &take_water};
            void* melt_args[] = {&units};
            il2::invoke(what == "boil" ? api.pot_boil : api.pot_melt, pot, what == "boil" ? boil_args : melt_args);
            logx::info("dev: cooking: %s %.2f l in the %s", what == "boil" ? "boiling" : "melting", units / 1e9,
                       name_of(l.gi).c_str());
        }
        return true;
    }
    if (!pots) logx::warn("dev: cooking: no pot or food on a fire within 6 m");
    else if (act) logx::warn("dev: cooking: no %s on a fire within 6 m", what == "take" ? "pot or food" : "pot");
    return pots > 0 && !act;
}

bool coin(const std::string& how) {
    const int which = how == "fair" ? -1 : how == "first" ? 0 : how == "last" ? 1 : -2;
    if (which == -2) {
        logx::warn("dev: coin: say fair, first (the earlier take or hold keeps it) or last (the newcomer gets it)");
        return false;
    }
    g_claims.force(which);
    logx::info("dev: coin: %s", which < 0 ? "fair" : which ? "the newcomer gets it" : "the earlier take keeps it");
    return true;
}

bool inspect(const std::string& name) {
    if (!g_ready || !api.inspect) return false;
    const Vector3 me = player_pos();
    Il2CppObject* best = nullptr;
    float best_d = 1e9f;
    for (const Loose& l : scan_loose(false)) {
        const float d = distance(l.pos, me);
        if (d < best_d && contains_nocase(name_of(l.gi), name)) best = l.gi, best_d = d;
    }
    if (!best || best_d > 6.0f) {
        logx::warn("dev: inspect: no loose %s within 6 m", name.empty() ? "item" : name.c_str());
        return false;
    }
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    void* args[] = {best};
    il2::invoke(api.inspect, pm, args);
    logx::info("dev: inspect: %s (%s) %.1f m away: %s", name_of(best).c_str(), short_id(pdid_of(best)).c_str(), best_d,
               inspected_now(pm) == best ? "in our hands (Take it / Leave it)" : "TLD didn't show it");
    return inspected_now(pm) == best;
}

bool drop(const std::string& name, int units) {
    if (!g_ready) return false;
    Il2CppObject* gi = nullptr;
    for (Il2CppObject* candidate : pack_items())
        if (contains_nocase(name_of(candidate), name)) gi = candidate;
    if (!gi) {
        logx::warn("dev: drop: no %s in our pack", name.c_str());
        return false;
    }
    int32_t n = std::max(1, units);
    bool sound = true, at_feet = false, force = true;
    void* args[] = {&n, &sound, &at_feet, &force};
    Il2CppObject* dropped = il2::invoke(api.drop, gi, args);
    Vector3 pos{};
    Quaternion rot{};
    if (alive(dropped)) pose_of(dropped, pos, rot);
    logx::info("dev: drop: %s x%d -> %s (%s) at (%.1f, %.1f, %.1f)", name_of(gi).c_str(), n,
               alive(dropped) ? name_of(dropped).c_str() : "nothing", alive(dropped) ? short_id(pdid_of(dropped)).c_str() : "-",
               pos.x, pos.y, pos.z);
    return alive(dropped);
}

// TLDCoop_DLC: a door (LoadScene.Activate) was used: what leaves our view now goes with the scene.
void door_used() { g_door_until = now_ms() + 6000; }

namespace {
// TLDCoop_DLC: quitting to the main menu takes the scene's items away too (not destroyed): as a door.
using StaticVoidFn = void (*)(const MethodInfo* method);
StaticVoidFn g_load_menu = nullptr, g_async_load_menu = nullptr;
void load_menu_detour(const MethodInfo* method) {
    door_used();
    g_load_menu(method);
}
void async_load_menu_detour(const MethodInfo* method) {
    door_used();
    g_async_load_menu(method);
}
}  // namespace

bool init() {
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* game_object = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* save_data = il2::find_class(kGame, "", "GearSaveData");
    Il2CppClass* object_guid = il2::find_class(kGame, "", "ObjectGuid");
    Il2CppClass* pdid_table = il2::find_class("TLD.PDID.dll", "TLD.PDID", "PdidTable");
    Il2CppClass* gear_manager = il2::find_class(kGame, "", "GearManager");
    Il2CppClass* game_manager = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* player_manager = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* inventory = il2::find_class(kGame, "", "Inventory");
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.component_transform = il2::find_method(component, "get_transform", 0);
    api.go_active = il2::find_method(game_object, "get_activeInHierarchy", 0);
    api.go_set_active = il2::find_method(game_object, "SetActive", 1);
    if (Il2CppClass* container = il2::find_class(kGame, "", "Container")) api.container_type = il2::type_object(container);
    api.go_component = il2::find_method_params(game_object, "GetComponent", {"System.Type"});
    api.get_position = il2::find_method(transform, "get_position", 0);
    api.get_rotation = il2::find_method(transform, "get_rotation", 0);
    api.get_parent = il2::find_method(transform, "get_parent", 0);
    api.set_parent = il2::find_method(transform, "SetParent", 2);
    api.set_pose = il2::find_method(transform, "SetPositionAndRotation", 2);
    api.object_name = il2::find_method(object, "get_name", 0);
    api.gear_type = il2::type_object(gear);
    if (api.gear_type) il2::il2cpp_gchandle_new(api.gear_type, false);
    api.in_inventory = il2::field_offset(gear, "m_InPlayerInventory");
    api.no_save = il2::field_offset(gear, "m_DisableSerialization");
    api.object_guid = il2::field_offset(gear, "m_ObjectGuid");
    api.radial_parent = il2::field_offset(gear, "m_RadialObjectSpawnerParent");
    api.place_point = il2::field_offset_quiet(gear, "m_PlacePointGuid");
    if (Il2CppClass* slot = il2::find_class(kGame, "", "GearPlacePoint")) {
        api.slot_type = il2::type_object(slot);
        if (api.slot_type) il2::il2cpp_gchandle_new(api.slot_type, false);
        api.slot_place = il2::find_method_params(slot, "PlaceGear", {"GearItem", "System.Boolean"});
        api.slot_gear = il2::field_offset(slot, "m_PlacedGear");
    }
    api.find_all = il2::find_method_params(object, "FindObjectsOfType", {"System.Type", "System.Boolean"});
    api.narrative = il2::field_offset(gear, "m_NarrativeCollectibleItem");
    api.create_save_data = il2::find_method(gear, "CreateSaveData", 0);
    api.sd_prefab = il2::field_offset(save_data, "m_PrefabName");
    api.sd_gear = il2::field_offset(save_data, "m_SearializedGear");
    api.instantiate = il2::find_method_params(gear, "InstantiateAndDeserializeGearItem",
                                              {"System.String", "System.String", "UnityEngine.Transform", "System.Boolean",
                                               "System.Boolean"});
    api.sanitize = il2::find_method_params(il2::find_class(kGame, "", "Utils"), "SanitizePrefabName", {"System.String"});
    api.drop = il2::find_method(gear, "Drop", 4);
    api.display_name = il2::find_method_quiet(gear, "GetGearDisplayName", 1);
    api.stackable = il2::field_offset(gear, "m_StackableItem");
    api.units = il2::field_offset(il2::find_class(kGame, "", "StackableItem"), "m_Units");
    api.guid = il2::il2cpp_class_get_field_from_name(object_guid, "m_Guid") ? il2::field_offset(object_guid, "m_Guid") : -1;
    api.cached_pdid = il2::field_offset(il2::find_class("TLD.PDID.dll", "TLD.PDID", "PdidObjectBase"), "m_RuntimeCachedPdid");
    api.guid_of_go = il2::find_method(object_guid, "GetObjectGuidFromGO", 1);
    api.attach_guid = il2::find_method(object_guid, "MaybeAttachObjectGuidAndRegister", 2);
    api.pdid_object = il2::find_method_params(pdid_table, "GetGameObject", {"System.String"});
    api.new_pdid = il2::find_method(pdid_table, "GenerateNewID", 0);
    if (gear_manager) api.all_gear = il2::il2cpp_class_get_field_from_name(gear_manager, "m_Gear");
    api.destroy_gear = il2::find_method_params(gear_manager, "DestroyGearObject", {"UnityEngine.GameObject"});
    api.serialize_all = il2::find_method(gear_manager, "Serialize", 0);
    api.player_manager = il2::find_method(game_manager, "GetPlayerManagerComponent", 0);
    api.inventory = il2::find_method(game_manager, "GetInventoryComponent", 0);
    api.inspected = il2::find_method(player_manager, "GearItemBeingInspected", 0);
    api.inspecting = il2::find_method(player_manager, "IsInspectModeActive", 0);
    api.put_back = il2::find_method(player_manager, "ExitInspectGearMode", 1);
    api.pickup_action = il2::field_offset(player_manager, "m_PickupAction");
    api.put_back_action = il2::field_offset(player_manager, "m_PutBackAction");
    api.placing = il2::field_offset(player_manager, "m_ObjectToPlaceGearItem");
    api.pickup = il2::find_method(player_manager, "ProcessPickupItemInteraction", 4);
    api.inspect = il2::find_method_params(player_manager, "EnterInspectGearMode", {"GearItem"});
    api.inv_items = il2::field_offset(inventory, "m_Items");
    api.inv_destroy = il2::find_method_params(inventory, "DestroyGear", {"GearItem"});
    api.remove_units = il2::find_method(inventory, "RemoveUnits", 2);
    if (Il2CppClass* flare = il2::find_class(kGame, "", "FlareItem")) {
        api.flare = il2::field_offset(gear, "m_FlareItem");
        api.flare_burning = il2::find_method_quiet(flare, "IsBurning", 0);
        api.flare_elapsed = il2::field_offset(flare, "m_ElapsedBurnMinutes");
        api.flare_life = il2::field_offset(flare, "m_BurnLifetimeMinutes");
    }
    if (Il2CppClass* heater = il2::find_class(kGame, "", "WhaleOilHeaterItem")) {  // TLDCoop_DLC: Iron Road
        api.heater = il2::field_offset_quiet(gear, "m_WhaleOilHeaterItem");
        api.heater_state = il2::field_offset_quiet(heater, "m_State");
        api.heater_on = il2::find_method_quiet(heater, "TurnOn", 0);
        api.heater_off = il2::find_method_quiet(heater, "TurnOff", 0);
        api.heater_rate = il2::find_method_quiet(heater, "SetBurnRate", 1);
        if (Il2CppClass* hs = il2::find_class(kGame, "", "WhaleOilHeaterItemState")) {
            api.hs_on = il2::field_offset_quiet(hs, "m_IsOn");
            api.hs_rate = il2::field_offset_quiet(hs, "m_BurnRate");
            api.hs_fuel = il2::field_offset_quiet(hs, "m_Fuel");
        }
    }
    if (Il2CppClass* torch = il2::find_class(kGame, "", "TorchItem")) {
        api.torch = il2::field_offset(gear, "m_TorchItem");
        api.torch_burning = il2::find_method_quiet(torch, "IsBurning", 0);
        api.torch_elapsed = il2::field_offset(torch, "m_ElapsedBurnMinutes");
        api.torch_life = il2::field_offset(torch, "m_BurnLifetimeMinutes");
    }
    if (Il2CppClass* lamp = il2::find_class(kGame, "TLD.Gear", "KeroseneLampItem")) {
        api.lamp = il2::field_offset_quiet(gear, "m_KeroseneLampItem");
        api.evolve = il2::field_offset_quiet(gear, "m_EvolveItem");  // TLDCoop_DLC: curing
        api.hp = il2::field_offset_quiet(gear, "m_CurrentHP");
        api.clothing = il2::field_offset_quiet(gear, "m_ClothingItem");
        Il2CppClass* cloth = il2::find_class(kGame, "", "ClothingItem");
        if (!cloth) cloth = il2::find_class(kGame, "TLD.Gear", "ClothingItem");
        if (cloth) {
            api.wet = il2::field_offset_quiet(cloth, "m_PercentWet");
            api.frozen = il2::field_offset_quiet(cloth, "m_PercentFrozen");
        }
        logx::info("items: wetness and condition %s", api.clothing >= 0 && api.wet >= 0 && api.hp >= 0 ? "shared" : "partly shared (fields missing)");
        if (Il2CppClass* evolve = il2::find_class(kGame, "", "EvolveItem")) {
            api.evolve_hours = il2::field_offset_quiet(evolve, "m_TimeSpentEvolvingGameHours");
            api.evolve_days = il2::field_offset_quiet(evolve, "m_TimeToEvolveGameDays");
            api.evolve_can = il2::find_method(evolve, "CanEvolve", 0);
        }
        api.lamp_load = il2::find_method_params(lamp, "Deserialize", {"System.String"});
        api.lamp_pct = il2::find_method_quiet(lamp, "GetPercentFuelRemaining", 0);
        api.lamp_fuel = il2::field_offset_quiet(lamp, "m_CurrentFuelLiters");
        api.lamp_on = il2::find_method_quiet(lamp, "IsOn", 0);
    }
    api.gear_condition = il2::find_method_quiet(gear, "GetRoundedCondition", 0);
    if (Il2CppClass* snare = il2::find_class(kGame, "", "SnareItem")) {
        api.snare = il2::field_offset(gear, "m_SnareItem");
        api.snare_state = il2::field_offset(snare, "m_State");
        api.snare_rabbit = il2::il2cpp_class_get_field_from_name(snare, "m_RabbitAssetReference")
                               ? il2::field_offset(snare, "m_RabbitAssetReference")
                               : il2::field_offset(snare, "m_RabbitPrefab");
        api.snare_load = il2::find_method_params(snare, "Deserialize", {"System.String"});
        api.snare_placed = il2::find_method(snare, "OnSnarePlaced", 0);
        api.snare_set_state = il2::find_method(snare, "SetState", 1);
        api.snare_roll = il2::find_method(snare, "DoRoll", 0);
        api.snare_spawn_rabbit = il2::find_method(snare, "SpawnDeadRabbitOnSnare", 1);
    }
    if (Il2CppClass* pot = il2::find_class(kGame, "", "CookingPotItem")) {
        api.pot = il2::field_offset(gear, "m_CookingPotItem");
        api.pot_fire = il2::field_offset(pot, "m_FireBeingUsed");
        api.pot_food = il2::field_offset(pot, "m_GearItemBeingCooked");
        api.pot_hours = il2::field_offset(pot, "m_CookingElapsedHours");
        api.pot_state = il2::field_offset(pot, "m_CookingState");
        api.pot_cooked = il2::field_offset(pot, "m_PercentCooked");
        api.pot_ruined = il2::field_offset(pot, "m_PercentRuined");
        api.pot_take = il2::find_method(pot, "PickUpCookedItem", 0);
        api.pot_cook = il2::find_method(pot, "StartCooking", 1);
        api.pot_boil = il2::find_method(pot, "StartBoilingWater", 2);
        api.pot_melt = il2::find_method(pot, "StartMeltingSnow", 1);
        api.pot_dummy = il2::find_method(pot, "IsDummyPot", 0);
        api.pot_water = il2::field_offset(pot, "m_LitersWaterBeingBoiled");
        api.pot_snow = il2::field_offset(pot, "m_LitersSnowBeingMelted");
        api.pot_load = il2::find_method_params(pot, "Deserialize", {"System.String"});
        api.pot_drop_food = il2::find_method(pot, "DestroyGearItemBeingCooked", 0);
        api.pot_inspected = il2::field_offset(pot, "m_IsBeingInspected");
        api.pot_force_particles = il2::field_offset(pot, "m_ForceParticleUpdate");
        api.pot_visuals = il2::find_method(pot, "UpdateVisuals", 0);
        api.pot_water_mesh = il2::find_method(pot, "SetUpWaterMesh", 0);
        api.pot_gear = il2::field_offset_quiet(pot, "m_GearItem");
        api.pot_slot = il2::field_offset_quiet(pot, "m_GearPlacePointAttachedTo");
        api.pot_interact = il2::find_method_quiet(pot, "PerformInteraction", 0);
        api.pot_cook_tick = il2::find_method_quiet(pot, "UpdateCookingTimeAndState", 2);
        api.pot_melt_tick = il2::find_method_quiet(pot, "UpdateMeltingSnow", 0);
        api.pot_boil_tick = il2::find_method_quiet(pot, "UpdateBoilingWater", 0);
        api.pot_burnt_out = il2::find_method_quiet(pot, "CheckForFireBurntOut", 0);
        api.pot_damage = il2::find_method_quiet(pot, "DoConditionDamageToPot", 0);
        api.pot_take_gear = il2::find_method_quiet(pot, "PickUpCookedGearItem", 1);
        api.pot_take_water = il2::find_method_quiet(pot, "PickUpWater", 0);
        api.pot_take_snow = il2::find_method_quiet(pot, "PickUpSnow", 0);
    }
    api.food = il2::field_offset(gear, "m_FoodItem");
    if (Il2CppClass* food = il2::find_class(kGame, "", "FoodItem")) api.food_opened = il2::field_offset(food, "m_Opened");
    api.smash = il2::field_offset(gear, "m_SmashableItem");
    if (Il2CppClass* smash = il2::find_class(kGame, "", "SmashableItem"))
        api.smashed = il2::field_offset(smash, "m_HasBeenSmashed");
    g_ready = api.component_go && api.go_active && api.go_component && api.get_position && api.get_parent && api.set_pose &&
              api.gear_type && api.in_inventory >= 0 && api.no_save >= 0 && api.object_guid >= 0 &&
              (api.guid >= 0 || api.cached_pdid >= 0) && api.guid_of_go && api.attach_guid &&
              api.create_save_data && api.sd_prefab >= 0 && api.sd_gear >= 0 && api.instantiate && api.pdid_object &&
              api.new_pdid && api.all_gear && api.destroy_gear && api.serialize_all && api.player_manager && api.inspected &&
              api.inspecting && api.stackable >= 0 && api.units >= 0 && api.inventory && api.inv_destroy && api.remove_units;
    const bool snares = g_ready && api.snare >= 0 && api.snare_state >= 0 && api.snare_load && api.snare_roll &&
                        hooks::install_method("SnareItem.DoRoll", api.snare_roll, reinterpret_cast<void*>(&roll_detour),
                                              reinterpret_cast<void**>(&g_roll));
    const bool pots =
        g_ready && api.pot >= 0 && api.pot_food >= 0 && api.pot_water >= 0 && api.pot_snow >= 0 && api.pot_load &&
        api.pot_drop_food && api.pot_take && api.pot_cook && api.pot_boil && api.pot_melt &&
        hooks::install_method("CookingPotItem.PickUpCookedItem", api.pot_take, reinterpret_cast<void*>(&pot_take_detour),
                              reinterpret_cast<void**>(&g_pot_take)) &&
        hooks::install_method("CookingPotItem.StartCooking", api.pot_cook, reinterpret_cast<void*>(&pot_cook_detour),
                              reinterpret_cast<void**>(&g_pot_cook)) &&
        hooks::install_method("CookingPotItem.StartBoilingWater", api.pot_boil, reinterpret_cast<void*>(&pot_boil_detour),
                              reinterpret_cast<void**>(&g_pot_boil)) &&
        hooks::install_method("CookingPotItem.StartMeltingSnow", api.pot_melt, reinterpret_cast<void*>(&pot_melt_detour),
                              reinterpret_cast<void**>(&g_pot_melt));
    // TLDCoop_DLC (Karol 11.11): a pot on a fire is its owner's: only their game cooks it, only they take from it
    const auto pot_hook = [](const char* name, const MethodInfo* m, void* detour, void** orig) {
        return m && hooks::install_method(name, m, detour, orig);
    };
    const bool owned_pots =
        pots && api.pot_slot >= 0 &&
        pot_hook("CookingPotItem.UpdateCookingTimeAndState", api.pot_cook_tick, reinterpret_cast<void*>(&pot_cook_tick_detour),
                 reinterpret_cast<void**>(&g_pot_cook_tick)) &&
        pot_hook("CookingPotItem.UpdateMeltingSnow", api.pot_melt_tick, reinterpret_cast<void*>(&pot_melt_tick_detour),
                 reinterpret_cast<void**>(&g_pot_melt_tick)) &&
        pot_hook("CookingPotItem.UpdateBoilingWater", api.pot_boil_tick, reinterpret_cast<void*>(&pot_boil_tick_detour),
                 reinterpret_cast<void**>(&g_pot_boil_tick)) &&
        pot_hook("CookingPotItem.PerformInteraction", api.pot_interact, reinterpret_cast<void*>(&pot_interact_detour),
                 reinterpret_cast<void**>(&g_pot_interact));
    g_owned_pots = owned_pots;
    // (the rest: extra safety, each on its own)
    if (owned_pots) {
        pot_hook("CookingPotItem.CheckForFireBurntOut", api.pot_burnt_out, reinterpret_cast<void*>(&pot_burnt_out_detour),
                 reinterpret_cast<void**>(&g_pot_burnt_out));
        pot_hook("CookingPotItem.DoConditionDamageToPot", api.pot_damage, reinterpret_cast<void*>(&pot_damage_detour),
                 reinterpret_cast<void**>(&g_pot_damage));
        pot_hook("CookingPotItem.PickUpCookedGearItem", api.pot_take_gear, reinterpret_cast<void*>(&pot_take_gear_detour),
                 reinterpret_cast<void**>(&g_pot_take_gear));
        pot_hook("CookingPotItem.PickUpWater", api.pot_take_water, reinterpret_cast<void*>(&pot_take_water_detour),
                 reinterpret_cast<void**>(&g_pot_take_water));
        pot_hook("CookingPotItem.PickUpSnow", api.pot_take_snow, reinterpret_cast<void*>(&pot_take_snow_detour),
                 reinterpret_cast<void**>(&g_pot_take_snow));
    }
    logx::info("world items: pots on a fire %s", owned_pots ? "are their owner's (only they take from it, their game cooks it)"
                                                           : "are NOT owned (hooks missing): every game cooks them");
    if (g_ready) {
        Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
        const MethodInfo* load_menu = il2::find_method(gm, "LoadMainMenu", 0);
        const MethodInfo* async_load_menu = il2::find_method(gm, "AsyncLoadMainMenu", 0);
        const bool menu = load_menu && async_load_menu &&
                          hooks::install_method("GameManager.LoadMainMenu", load_menu, reinterpret_cast<void*>(&load_menu_detour),
                                                reinterpret_cast<void**>(&g_load_menu)) &&
                          hooks::install_method("GameManager.AsyncLoadMainMenu", async_load_menu,
                                                reinterpret_cast<void*>(&async_load_menu_detour),
                                                reinterpret_cast<void**>(&g_async_load_menu));
        if (!menu) logx::warn("world items: quitting to the main menu isn't seen (items leaving then count as destroyed if many don't)");
    }
    logx::info("world items: %s%s%s", g_ready ? "ready (loose items are shared)" : "UNAVAILABLE (TLD API missing)",
               g_ready && !snares ? "; snares roll in each game (not shared)" : "",
               g_ready && !pots ? "; what goes in a pot isn't shared" : "");
    return g_ready;
}

void on_frame(bool in_game) {
    if (!g_ready) return;
    if (!in_game && game::active_scene() == "MainMenu") forget_world();
    const bool connected = netrt::status().connected;
    if (!connected && !netrt::offline()) {
        if (g_connected) reset("not connected");
        g_connected = false;
        return;
    }
    // Offline (a friend's link dropped mid-game, or we host with nobody linked): we go on. The friend's changes go to
    // the host once it's back; the host applies what it kept for a scene it walks into (kept with its save, too).
    if (!connected) g_newest.clear();  // the host's change counters may start over meanwhile (its game restarted)
    g_connected = true;
    const double now = now_ms();
    const std::string& settled = in_game ? core::settled_scene() : std::string();
    if (settled != g_scene) {
        if (!g_scene.empty()) logx::info("items: left %s", g_scene.c_str());
        g_known.clear();
        g_pot_sig.clear();
        g_pot_loaded_ms.clear();
        g_pot_owner.clear();
        g_pot_heard.clear();
        // TLDCoop_DLC: the aliases stay (ids are the game's own, one per item in every scene): cleared here, coming
        // back made the other player's item again beside ours (a copy on the floor)
        g_scene = settled;
        g_baseline = false;
        g_together.clear();
        g_have_list = false;
        g_put_back.clear();  // the host's list there brings them
        g_last_scan = now - kScanMs;  // first scan on the next frame
    }
    if (Il2CppObject* gi = inspected_now(il2::invoke(api.player_manager, nullptr)); gi != g_hold_gi) {
        g_hold_gi = gi;
        g_hold = gi ? pdid_of(gi) : std::string();
        g_hold_since = now;
    }
    if (!g_put_back.empty()) {  // refused takes: our copy went at the end of the last frame
        std::vector<net::ItemPutMsg> back;
        back.swap(g_put_back);
        for (const net::ItemPutMsg& m : back) apply_put(m, now);
    }
    receive(now);
    if (g_scene.empty()) return;
    if (now - g_last_scan >= (now < g_fast_until ? kFastScanMs : kScanMs)) {
        g_last_scan = now;
        scan(now);
        // TLDCoop_DLC: host: what lies here now, kept for while we're elsewhere (not from a scan that saw the scene go)
        if (hosting() && g_baseline && !g_scan_left_at_once) {
            std::unordered_set<uint64_t>& kept = g_scene_items[g_scene];
            kept.clear();
            for (const auto& [pdid, k] : g_known)
                if (!k.unique) kept.insert(net::pdid_hash(wire_id(pdid)));
            // TLDCoop_DLC: the records, 20 items a pass; those no longer lying here dropped
            auto& recs = g_scene_records[g_scene];
            for (auto it = recs.begin(); it != recs.end();) it = kept.count(it->first) ? std::next(it) : recs.erase(it);
            if (now >= g_next_records) {
                g_next_records = now + 1000;
                std::vector<const std::string*> ids;
                for (const auto& [pdid, k] : g_known)
                    if (!k.unique) ids.push_back(&pdid);
                for (size_t n = 0; n < 20 && !ids.empty(); ++n, ++g_records_cursor) {
                    const std::string& pdid = *ids[g_records_cursor % ids.size()];
                    Il2CppObject* gi = gear_on(object_by_pdid(pdid));
                    if (!gi || get<bool>(gi, api.in_inventory)) continue;
                    Quaternion rot{};
                    Vector3 pos{};
                    pose_of(gi, pos, rot);
                    net::ItemPutMsg m;
                    if (make_put(gi, pdid, g_known[pdid].prefab, 0, pos, rot, m)) recs[net::pdid_hash(m.pdid)] = std::move(m);
                }
            }
        }
    }
    if (hosting() && now - g_last_away >= 500) {
        g_last_away = now;
        send_away_lists();
    }
    // Host: once a friend's changes from while the link was down are in, our list has them.
    if (hosting())
        for (uint8_t id : g_together.arrived(g_baseline ? g_scene : std::string())) send_list(id);
    // TLDCoop_DLC: a Seamless Interiors room came up (ours or a friend's): its items as full records, not only hashes
    // (where they lie, opened, lit...), as answers (applied as the host's, not passed on).
    if (hosting() && g_baseline)
        for (auto& [to, ids] : seamless_interiors::take_room_puts()) {
            const std::unordered_set<std::string> rooms(ids.begin(), ids.end());
            int sent = 0;
            for (auto& [pdid, k] : g_known) {
                if (k.unique) continue;
                Il2CppObject* gi = gear_on(object_by_pdid(pdid));
                if (!gi || !rooms.count(seamless_interiors::room_of(gi))) continue;
                send_put(gi, pdid, k, now, net::kItemAnswer, to);
                ++sent;
            }
            if (sent) logx::info("items: %d item(s) of the room(s) just up sent as they are here (player %u)", sent, to);
        }
}

void save_kept(std::vector<world_keep::Record>& out) {
    for (const auto& [scene, changes] : g_pending) {
        for (const auto& [pdid, c] : changes) {
            net::Writer w(c.put ? net::Msg::ItemPut : net::Msg::ItemGone, net::kMaxMessage);
            if (c.put)
                net::write_item_put(w, c.put_msg);
            else
                net::write_item_gone(w, c.gone_msg);
            if (w.ok()) out.push_back(world_keep::record(world_keep::Tag::ItemChange, w));
        }
    }
    // TLDCoop_DLC: the items' records per scene
    for (const auto& [scene, recs] : g_scene_records)
        for (const auto& [h, m] : recs) {
            net::Writer w(net::Msg::ItemPut, net::kMaxMessage);
            net::write_item_put(w, m);
            if (w.ok()) out.push_back(world_keep::record(world_keep::Tag::ItemRecord, w));
        }
    // TLDCoop_DLC: the loose items kept per scene (as our ItemList)
    for (const auto& [scene, hashes] : g_scene_items) {
        net::ItemHashesMsg m;
        m.scene = scene;
        m.hashes.assign(hashes.begin(), hashes.end());
        net::Writer w(net::Msg::ItemList, net::kMaxMessage);
        net::write_item_hashes(w, m);
        if (w.ok()) out.push_back(world_keep::record(world_keep::Tag::ItemList, w));
    }
}

bool load_kept(const world_keep::Record& r) {
    const net::Packet p = world_keep::packet(r);
    net::Reader body = net::packet_body(p);
    if (r.tag == world_keep::Tag::ItemRecord) {
        net::ItemPutMsg m;
        if (p.type != net::Msg::ItemPut || !net::read_item_put(body, m)) return false;
        g_scene_records[m.scene][net::pdid_hash(m.pdid)] = std::move(m);
        return true;
    }
    if (r.tag == world_keep::Tag::ItemList) {
        net::ItemHashesMsg m;
        if (p.type != net::Msg::ItemList || !net::read_item_hashes(body, m)) return false;
        if (m.scene != g_scene || !g_baseline) g_scene_items[m.scene] = std::unordered_set<uint64_t>(m.hashes.begin(), m.hashes.end());
        return true;
    }
    Change c;
    c.put = p.type == net::Msg::ItemPut;
    if (c.put ? !net::read_item_put(body, c.put_msg) : p.type != net::Msg::ItemGone || !net::read_item_gone(body, c.gone_msg))
        return false;
    const std::string scene = c.put ? c.put_msg.scene : c.gone_msg.scene;
    const std::string pdid = c.put ? c.put_msg.pdid : c.gone_msg.pdid;
    g_pending[scene][pdid] = std::move(c);
    return true;
}

}  // namespace world_items
