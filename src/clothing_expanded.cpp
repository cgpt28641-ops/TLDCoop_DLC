#include "clothing_expanded.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "coop_lapse.h"
#include "custom_text.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "config.h"
#include "net/session.h"
#include "net_runtime.h"
#include "paths.h"

// TLDCoop_DLC: ClothingExpanded (TonisGaming; ModComponent pack, "discontinued"; Karol 06.11 on Discord: "Dobra rób 1",
// the whole mod). ModComponent isn't here, so this does its part: the pack's Addressables catalog is loaded
// (Addressables.LoadContentCatalogAsync, its bundles written next to the DLL), and each of its 28 bare prefabs (a model,
// no gear components) is made into a TLD item the way ModComponent's ItemMapper/ClothingMapper would: GearItem with its
// own GearItemData (a copy of the base item's: TLD's audio and the rest, with the mod's weight, condition, names, icon),
// ClothingItem (the base item's settings, the mod's warmth, layers and paper-doll pictures), Repairable/Harvest/Inspect
// (the mod's), and the accessory's buff copied from TLD's item (a tool belt's WeightReductionBuff, the crampons'
// ClimbingBuff, the rifle scabbard's WeightReductionBuff and the satchel's CarryingCapacityBuff).
//   Mapped once the catalog is in, again on a scene change if Addressables lets a prefab go (a bare one back).
namespace clothing_expanded {
namespace {

struct V3 {
    float x, y, z;
};

// One item (tools/clothing/gen_items.py from the pack's auto-mapped JSON).
struct Item {
    const char *key, *base, *extra, *name_id, *desc_id;
    float weight, days_decay, max_hp;
    const char *start, *region, *min_layer, *max_layer;
    float days_out, days_in, warmth, warmth_wet, windproof, waterproof, toughness, sprint, dry_fire, dry_nofire, freeze;
    const char *main_tex, *blend_tex, *normal_model, *inspect_model;
    float inspect_dist;
    V3 angles, offset, scale;
    int repairable, repair_min;
    const char *repair_audio, *repair_mats, *repair_counts;
    int repair_cond;
    const char *repair_tools, *harvest_audio, *harvest;  // harvest: "minutes;names;counts"
};
struct Text {
    const char *key, *en, *pl;
};
#include "clothing_items.inc"

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kAddressables = "Unity.Addressables.dll";
constexpr const char* kBundleName = "clothingexpanded_assets_all";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr int kResBundle = 301, kResShaders = 302, kResCatalog = 303;  // src/clothing_expanded.rc
constexpr int kDontUnloadUnusedAsset = 32;                            // HideFlags
constexpr double kStartDelayMs = 2000, kCatalogTimeoutMs = 120000;

struct Api {
    // Unity
    const MethodInfo *obj_name = nullptr, *obj_set_name = nullptr, *instantiate = nullptr, *hide_flags = nullptr;
    const MethodInfo *go_add = nullptr, *go_get = nullptr, *go_layer = nullptr, *go_in_children_all = nullptr;
    const MethodInfo *comp_go = nullptr, *comp_tf = nullptr;
    const MethodInfo *tf_count = nullptr, *tf_child = nullptr;
    const MethodInfo *so_create = nullptr;
    const MethodInfo *shared_materials = nullptr, *shared_material = nullptr;
    const MethodInfo *mat_shader = nullptr, *mat_set_shader = nullptr, *mat_keywords = nullptr, *mat_set_keywords = nullptr;
    const MethodInfo *bundles = nullptr, *bundle_load = nullptr;
    const MethodInfo* bundle_names = nullptr;  // TLDCoop_DLC: AssetBundle.GetAllAssetNames()
    const MethodInfo* grid_icon = nullptr;     // TLDCoop_DLC: Utils.GetInventoryGridIconTexture(string), TLD's Addressables load
    Il2CppObject *t_gear = nullptr, *t_clothing = nullptr, *t_repair = nullptr, *t_harvest = nullptr, *t_inspect = nullptr,
                 *t_tools = nullptr, *t_hover = nullptr, *t_weight_buff = nullptr, *t_climb_buff = nullptr,
                 *t_carry_buff = nullptr, *t_mesh_renderer = nullptr, *t_texture = nullptr, *t_icon_data = nullptr;
    Il2CppClass *gear_class = nullptr, *tools_class = nullptr, *int_class = nullptr;
    Il2CppClass *clothing_class = nullptr, *weight_buff_class = nullptr, *climb_buff_class = nullptr,
                *carry_buff_class = nullptr, *hover_class = nullptr, *mono_class = nullptr;
    // Addressables, TLD
    const MethodInfo* load_catalog = nullptr;  // static Addressables.LoadContentCatalogAsync(string, bool, string)
    const MethodInfo* prefab_by_name = nullptr;  // static RadialSpawnManager.GetPrefabFromName(string)
    Il2CppClass *gear_ref_class = nullptr, *tex_ref_class = nullptr, *lstring_class = nullptr;
    const MethodInfo *gear_ref_ctor = nullptr, *tex_ref_ctor = nullptr, *lstring_ctor = nullptr;
    const MethodInfo* from_kg = nullptr;  // static ItemWeight.FromKilograms(float)
    const MethodInfo* gear_awake = nullptr;
    ptrdiff_t lstring_id = -1;
    // GearItem, GearItemData
    ptrdiff_t gear_data = -1, gear_start = -1;
    ptrdiff_t d_ref = -1, d_tag = -1, d_weight = -1, d_max_hp = -1, d_decay = -1, d_name = -1, d_desc = -1, d_icon = -1,
              d_cover[4] = {-1, -1, -1, -1};
    ptrdiff_t icon_tex = -1;  // GearItemInventoryIconSimpleData.m_Icon
    // ClothingItem
    ptrdiff_t c_region = -1, c_min = -1, c_max = -1, c_decay_out = -1, c_decay_in = -1, c_warmth = -1, c_warmth_wet = -1,
              c_windproof = -1, c_toughness = -1, c_sprint = -1, c_waterproof = -1, c_dry = -1, c_dry_nofire = -1,
              c_freeze = -1, c_dry_bonus = -1, c_tex = -1, c_blend = -1;
    // Repairable, Harvest, Inspect, buffs
    ptrdiff_t r_gear = -1, r_items = -1, r_units = -1, r_cond = -1, r_minutes = -1, r_audio = -1, r_tools = -1,
              r_needs_tool = -1;
    ptrdiff_t h_skill = -1, h_minutes = -1, h_audio = -1, h_tools = -1, h_yield = -1, h_units = -1;
    ptrdiff_t i_inspect_mesh = -1, i_normal_mesh = -1, i_dist = -1, i_offset = -1, i_angles = -1, i_scale = -1;
    ptrdiff_t wb_items = -1, wb_reduction = -1, wb_gear = -1;
    int64_t layer_gear = 0;
    Il2CppClass *region_enum = nullptr, *layer_enum = nullptr, *start_enum = nullptr;
} api;

enum class State { Waiting, Loading, Ready, Failed };
State g_state = State::Waiting;
bool g_ready = false;
double g_first_frame = 0, g_load_started = 0;
uintptr_t g_catalog_op = 0;  // gchandle: the catalog's AsyncOperationBase
std::string g_scene;
std::filesystem::path g_dir;
int g_mapped = 0, g_failed = 0;
const char* g_step = "";  // map_item's step now (a fault's log line names it)
bool g_fault_inside = false;  // a fault caught inside map_item that stops the pack (set_data)
std::vector<uintptr_t> g_keep;  // made objects held for good (GearItemData copies, icon data, the prefabs)
using VoidFn = void (*)(Il2CppObject*, const MethodInfo*);
using UpdateButtonsFn = void (*)(Il2CppObject*, Il2CppObject*, bool, const MethodInfo*);

// --- small helpers ------------------------------------------------------------------------------------------------

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) { return il2::read_field<T>(o, off); }
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}
void put_ref(Il2CppObject* o, ptrdiff_t off, void* v) { il2::store_ref(o, off, v); }
bool alive(Il2CppObject* o) { return o && get<void*>(o, kCachedPtr) != nullptr; }
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }
Il2CppObject* keep(Il2CppObject* o) {
    if (o) g_keep.push_back(il2::il2cpp_gchandle_new(o, false));
    return o;
}
Il2CppString* str(const char* s) { return s ? il2::il2cpp_string_new(s) : nullptr; }  // (null: none; il2cpp_string_new(nullptr) faults)
std::string name_of(Il2CppObject* o) {
    return alive(o) ? il2::to_utf8(reinterpret_cast<Il2CppString*>(il2::invoke(api.obj_name, o))) : std::string();
}
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> split(const char* s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = s; p && *p; ++p) {
        if (*p == sep) out.push_back(cur), cur.clear();
        else cur.push_back(*p);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

Il2CppObject* component(Il2CppObject* go, Il2CppObject* type) {
    if (!alive(go) || !type) return nullptr;
    void* a[] = {type};
    Il2CppObject* c = il2::invoke(api.go_get, go, a);
    return alive(c) ? c : nullptr;
}
Il2CppObject* add_component(Il2CppObject* go, Il2CppObject* type) {
    if (!alive(go) || !type) return nullptr;
    if (Il2CppObject* have = component(go, type)) return have;
    void* a[] = {type};
    Il2CppObject* c = il2::invoke(api.go_add, go, a);
    return alive(c) ? c : nullptr;
}
Il2CppObject* go_of(Il2CppObject* c) { return alive(c) ? il2::invoke(api.comp_go, c) : nullptr; }

int64_t enum_of(Il2CppClass* e, const char* name, int64_t fallback) {
    int64_t v = fallback;
    if (e && name && *name && !il2::enum_value(e, name, v)) logx::warn("clothing: no %s in its enum", name);
    return v;
}

Il2CppObject* new_object(Il2CppClass* k, const MethodInfo* ctor, const char* arg) {
    Il2CppObject* o = k ? il2::il2cpp_object_new(k) : nullptr;
    if (!o || !ctor) return o;
    // (no string made for no argument: il2cpp_string_new(nullptr) read address 0 inside GameAssembly, the
    // GameAssembly+0x3cd25a fault of every ClothingExpanded crash: LocalizedString's ctor has none)
    void* a[] = {arg ? str(arg) : nullptr};
    il2::invoke(ctor, o, arg ? a : nullptr);
    return o;
}

Il2CppObject* localized(const char* id) {
    Il2CppObject* s = new_object(api.lstring_class, api.lstring_ctor, nullptr);
    if (s) put_ref(s, api.lstring_id, str(id));
    return s;
}

// TLD's prefab for a name (Addressables, through ours too once the catalog is in).
Il2CppObject* prefab(const char* key) {
    void* a[] = {str(key)};
    Il2CppObject* p = il2::invoke(api.prefab_by_name, nullptr, a);
    return alive(p) ? p : nullptr;
}
Il2CppObject* prefab_component(const char* key, Il2CppObject* type) { return component(prefab(key), type); }

// Every instance field of `klass` (and its parents up to MonoBehaviour) from src to dst, but GearItem back-references
// (m_GearItem), which become `gear`. A component's serialized settings: copied from a prefab, nothing ran on it yet.
void copy_fields(Il2CppObject* dst, Il2CppObject* src, Il2CppClass* klass, Il2CppObject* gear) {
    if (!dst || !src || !il2::il2cpp_field_get_value || !il2::il2cpp_field_set_value) return;
    for (Il2CppClass* k = klass; k && k != api.mono_class; k = il2::il2cpp_class_get_parent(k)) {
        void* it = nullptr;
        while (FieldInfo* f = il2::il2cpp_class_get_fields(k, &it)) {
            if (il2::il2cpp_field_get_flags(f) & (il2::kFieldStatic | il2::kFieldLiteral)) continue;
            const Il2CppType* t = il2::il2cpp_field_get_type(f);
            Il2CppClass* fk = t && il2::il2cpp_class_from_type ? il2::il2cpp_class_from_type(t) : nullptr;
            if (!fk) continue;
            if (!strcmp(il2::il2cpp_field_get_name(f), "m_GearItem")) {
                il2::il2cpp_field_set_value(dst, f, gear);
                continue;
            }
            alignas(16) unsigned char buf[512] = {};
            il2::il2cpp_field_get_value(src, f, buf);
            if (il2::il2cpp_class_is_valuetype(fk)) il2::il2cpp_field_set_value(dst, f, buf);
            else il2::il2cpp_field_set_value(dst, f, *reinterpret_cast<void**>(buf));
        }
    }
}

// A managed array of prefab components (GearItem[] / ToolsItem[]) for names.
Il2CppObject* prefab_array(Il2CppClass* element, Il2CppObject* type, const std::vector<std::string>& names) {
    std::vector<Il2CppObject*> found;
    for (const std::string& n : names) {
        if (Il2CppObject* c = prefab_component(n.c_str(), type)) found.push_back(c);
        else logx::warn("clothing: no %s in TLD for a recipe", n.c_str());
    }
    Il2CppObject* arr = il2::il2cpp_array_new ? il2::il2cpp_array_new(element, found.size()) : nullptr;
    for (size_t i = 0; arr && i < found.size(); ++i) il2::store_ref(arr, static_cast<ptrdiff_t>(0x20 + i * 8), found[i]);
    return arr;
}
Il2CppObject* int_array(const std::vector<std::string>& counts) {
    Il2CppObject* arr = il2::il2cpp_array_new ? il2::il2cpp_array_new(api.int_class, counts.size()) : nullptr;
    for (size_t i = 0; arr && i < counts.size(); ++i) {
        const int32_t v = atoi(counts[i].c_str());
        memcpy(reinterpret_cast<char*>(arr) + 0x20 + i * 4, &v, 4);
    }
    return arr;
}

Il2CppObject* find_child(Il2CppObject* tf, const std::string& name, int depth = 0) {
    if (!tf || name.empty() || depth > 6) return nullptr;
    int32_t n = 0;
    il2::invoke_value(api.tf_count, tf, n);
    for (int32_t i = 0; i < n; ++i) {
        void* a[] = {&i};
        Il2CppObject* c = il2::invoke(api.tf_child, tf, a);
        if (!c) continue;
        if (lower(name_of(c)) == lower(name)) return c;
        if (Il2CppObject* deeper = find_child(c, name, depth + 1)) return deeper;
    }
    return nullptr;
}

// --- the pack's files ----------------------------------------------------------------------------------------------

HMODULE self_module() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&self_module), &h);
    return h;
}

bool resource(int id, const char*& data, size_t& size) {
    HMODULE h = self_module();
    HRSRC r = h ? FindResourceW(h, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */) : nullptr;
    HGLOBAL g = r ? LoadResource(h, r) : nullptr;
    data = g ? static_cast<const char*>(LockResource(g)) : nullptr;
    size = r ? SizeofResource(h, r) : 0;
    return data && size;
}

bool write_file(const std::filesystem::path& file, const char* data, size_t size) {
    std::error_code ec;
    if (std::filesystem::file_size(file, ec) == size && !ec) {
        std::ifstream in(file, std::ios::binary);  // same size: the same unless the start differs
        char head[4096] = {};
        const size_t n = std::min(size, sizeof(head));
        in.read(head, static_cast<std::streamsize>(n));
        if (in && !memcmp(head, data, n)) return true;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(data, static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

// The bundles and the catalog (its ".\AssetBundles\" made this folder) into TLDCoop\clothing.
bool write_pack() {
    g_dir = paths::mod_dir() / L"clothing";
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    const char* data = nullptr;
    size_t size = 0;
    if (!resource(kResBundle, data, size) || !write_file(g_dir / L"clothingexpanded_assets_all.bundle", data, size)) return false;
    if (!resource(kResShaders, data, size) ||
        !write_file(g_dir / L"clothingexpanded_unitybuiltinshaders_c1d995b77ba295f65f21e50ed586f479.bundle", data, size))
        return false;
    if (!resource(kResCatalog, data, size)) return false;
    std::string catalog(data, size);
    std::string dir = g_dir.generic_string() + "/";  // forward slashes: nothing to escape in the JSON
    const std::string from = ".\\\\AssetBundles\\\\";
    for (size_t p = catalog.find(from); p != std::string::npos; p = catalog.find(from, p + dir.size()))
        catalog.replace(p, from.size(), dir);
    // Its locator id is TLD's own main catalog's ("AddressablesMainContentCatalog"): loaded as is it took the game's
    // place and TLD crashed (06.11, Shatsca: GameAssembly+0x3cd25a, a null). Its own name, as ModComponent does.
    const std::string id_key = "\"m_LocatorId\":\"";
    if (const size_t p = catalog.find(id_key); p != std::string::npos) {
        const size_t v = p + id_key.size(), end = catalog.find('"', v);
        if (end != std::string::npos) catalog.replace(v, end - v, "catalog_clothingexpanded");
    } else {
        logx::warn("clothing: the pack's catalog has no locator id to rename; not loading it");
        return false;
    }
    return write_file(g_dir / L"catalog_clothingexpanded.json", catalog.data(), catalog.size());
}

// --- the catalog -----------------------------------------------------------------------------------------------------

bool start_catalog() {
    const std::string path = (g_dir / L"catalog_clothingexpanded.json").string();
    logx::info("clothing: asking Addressables to load the pack's catalog");
    bool auto_release = false;
    void* a[] = {str(path.c_str()), &auto_release, nullptr};
    Il2CppObject* boxed = il2::invoke(api.load_catalog, nullptr, a);
    Il2CppObject* op = boxed ? *static_cast<Il2CppObject**>(il2::il2cpp_object_unbox(boxed)) : nullptr;  // m_InternalOp
    if (!op) {
        logx::warn("clothing: Addressables didn't start loading the pack's catalog");
        return false;
    }
    g_catalog_op = il2::il2cpp_gchandle_new(op, false);
    logx::info("clothing: the pack's catalog is loading (%s)", path.c_str());
    return true;
}

// --- making an item ---------------------------------------------------------------------------------------------------

Il2CppObject* pack_bundle() {
    Il2CppObject* arr = il2::invoke(api.bundles, nullptr);
    for (size_t i = 0; arr && i < il2::array_length(arr); ++i) {
        Il2CppObject* b = il2::array_object_at(arr, i);
        if (lower(name_of(b)).find(kBundleName) != std::string::npos) return b;
    }
    return nullptr;
}

Il2CppObject* pack_texture(const std::string& path) {
    Il2CppObject* b = pack_bundle();
    if (!b) return nullptr;
    void* a[] = {str(path.c_str()), api.t_texture};
    Il2CppObject* t = il2::invoke(api.bundle_load, b, a);
    return alive(t) ? t : nullptr;
}

std::string short_name(const char* key) { return std::string(key).substr(5); }  // "GEAR_X" -> "X"

// The icon's texture: the pack names a few with another case (DeerSkinToolbelt).
// TLDCoop_DLC (08.11: every item "no icon in the pack"): what the pack holds, once, to see how it names its icons
void list_icons_once() {
    static bool done = false;
    if (done) return;
    done = true;
    // (20:10 logs: no line at all; which bundles are loaded, and is ours among them?)
    Il2CppObject* all = il2::invoke(api.bundles, nullptr);
    std::string loaded;
    for (size_t i = 0; all && i < il2::array_length(all) && i < 40; ++i)
        loaded += (loaded.empty() ? "" : ", ") + name_of(il2::array_object_at(all, i));
    Il2CppObject* b = pack_bundle();
    logx::info("clothing: icons: %zu bundle(s) loaded [%s]; ours %s; asset names %s", all ? il2::array_length(all) : 0,
               loaded.c_str(), b ? "found" : "NOT found", api.bundle_names ? "can be listed" : "can't be listed");
    if (!api.bundle_names) return;
    Il2CppObject* arr = b ? il2::invoke(api.bundle_names, b) : nullptr;
    size_t n = arr ? il2::array_length(arr) : 0, shown = 0;
    std::string some;
    for (size_t i = 0; i < n; ++i) {
        Il2CppString* sn = reinterpret_cast<Il2CppString*>(il2::array_object_at(arr, i));
        const std::string name = sn ? il2::to_utf8(sn) : std::string();
        if (lower(name).find("ico") == std::string::npos) continue;
        if (shown++ < 12) some += (some.empty() ? "" : ", ") + name;
    }
    logx::info("clothing: the pack holds %zu asset(s), %zu with 'ico' in the name: %s", n, shown, some.c_str());
}

Il2CppObject* icon_texture(const Item& it) {
    list_icons_once();
    const std::string n = short_name(it.key);
    // TLDCoop_DLC (20:44 log: "ours NOT found" among the loaded bundles): the pack's catalog keys its icons
    // "ico_GearItem__<Name>"; TLD's own icon loader goes through Addressables by such a name
    static int way = -1;  // which spelling TLD's loader takes (-1 not known yet, -2 none: not asked again)
    if (api.grid_icon && way != -2) {
        const std::string keys[] = {"ico_GearItem__" + n, n, std::string("GEAR_") + n};
        for (int i = way >= 0 ? way : 0; i < 3; ++i) {
            void* a[] = {str(keys[i].c_str())};
            Il2CppObject* t = il2::invoke(api.grid_icon, nullptr, a);
            if (alive(t)) {
                if (way < 0) logx::info("clothing: icons from TLD's loader as '%s'", keys[i].c_str());
                way = i;
                return t;
            }
            if (way >= 0) break;
        }
        if (way < 0) {
            way = -2;
            logx::warn("clothing: TLD's icon loader has none of ours either");
        }
    }
    for (const std::string& path : {"Assets/InventoryGridIcons/ico_GearItem__" + n + ".png",
                                     lower("Assets/InventoryGridIcons/ico_GearItem__" + n + ".png"),
                                     "ico_GearItem__" + n, lower("ico_GearItem__" + n)})
        if (Il2CppObject* t = pack_texture(path)) return t;
    for (const std::string& v : {n, std::string("DeerSkinToolbelt")}) {
        if (v != n && lower(v) != lower(n)) continue;
        if (Il2CppObject* t = pack_texture("Assets/InventoryGridIcons/ico_GearItem__" + v + ".png")) return t;
    }
    return nullptr;
}

// GearItemData, in steps: each one guarded on its own (07.11: Shatsca's log, "a FAULT at the step 'data'" at
// GameAssembly+0x3cd25a, address 0). The prefab reference is a must; without the others the item still works.
void data_reference(Il2CppObject* data, const Item& it) {
    put_ref(data, api.d_ref, new_object(api.gear_ref_class, api.gear_ref_ctor, it.key));
}
void data_tag(Il2CppObject* data, const Item&) {
    if (api.d_tag >= 0) memset(reinterpret_cast<char*>(data) + api.d_tag, 0, 16);  // no TLD tag (ModComponent: none)
}
void data_weight(Il2CppObject* data, const Item& it) {
    int64_t kg = 0;
    float w = it.weight;
    void* a[] = {&w};
    if (api.from_kg && il2::invoke_value(api.from_kg, nullptr, kg, a)) put<int64_t>(data, api.d_weight, kg);
}
void data_numbers(Il2CppObject* data, const Item& it) {
    put<float>(data, api.d_max_hp, it.max_hp);
    put<float>(data, api.d_decay, it.days_decay > 0 ? it.max_hp / it.days_decay : 0.0f);
}
void data_name(Il2CppObject* data, const Item& it) {
    put_ref(data, api.d_name, localized(it.name_id));
    put_ref(data, api.d_desc, localized(it.desc_id));
}
void data_icon(Il2CppObject* data, const Item& it) {
    Il2CppObject* tex = icon_texture(it);
    if (!tex) {
        logx::warn("clothing: no icon for %s in the pack", it.key);
        return;
    }
    g_step = "data: icon object";
    void* ta[] = {api.t_icon_data};
    Il2CppObject* icon_data = keep(il2::invoke(api.so_create, nullptr, ta));
    if (!icon_data) return;
    put_ref(icon_data, api.icon_tex, tex);
    int flags = kDontUnloadUnusedAsset;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, icon_data, fa);
    put_ref(data, api.d_icon, icon_data);
}
void data_cover(Il2CppObject* data, const Item& it) {
    const std::string icon = "ico_GearItem__" + short_name(it.key);
    for (ptrdiff_t off : api.d_cover) put_ref(data, off, new_object(api.tex_ref_class, api.tex_ref_ctor, icon.c_str()));
}

using DataStep = void (*)(Il2CppObject*, const Item&);
bool data_step(const char* step, DataStep fn, Il2CppObject* data, const Item& it) {
    g_step = step;
    __try {
        fn(data, it);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool set_data(Il2CppObject* data, const Item& it) {
    if (!data_step("data: prefab reference", data_reference, data, it)) return false;
    const struct {
        const char* step;
        DataStep fn;
    } rest[] = {{"data: tag", data_tag},     {"data: weight", data_weight}, {"data: numbers", data_numbers},
                {"data: name", data_name},   {"data: icon", data_icon},     {"data: cover pictures", data_cover}};
    for (const auto& s : rest) {
        if (!data_step(s.step, s.fn, data, it))
            logx::warn("clothing: %s: a FAULT at the step '%s'; the item goes on without it", it.key, g_step);
    }
    return true;
}

void set_clothing(Il2CppObject* c, const Item& it) {
    put<int32_t>(c, api.c_region, static_cast<int32_t>(enum_of(api.region_enum, it.region, 0)));
    put<int32_t>(c, api.c_min, static_cast<int32_t>(enum_of(api.layer_enum, it.min_layer, 0)));
    put<int32_t>(c, api.c_max, static_cast<int32_t>(enum_of(api.layer_enum, it.max_layer, 0)));
    put<float>(c, api.c_decay_out, it.days_out > 0 ? it.max_hp / it.days_out : 0.0f);
    put<float>(c, api.c_decay_in, it.days_in > 0 ? it.max_hp / it.days_in : 0.0f);
    put<float>(c, api.c_warmth, it.warmth);
    put<float>(c, api.c_warmth_wet, it.warmth_wet);
    put<float>(c, api.c_windproof, it.windproof);
    put<float>(c, api.c_toughness, it.toughness);
    put<float>(c, api.c_sprint, it.sprint);
    put<float>(c, api.c_waterproof, it.waterproof / 100.0f);
    put<float>(c, api.c_dry, it.dry_fire > 0 ? 100.0f / it.dry_fire : 0.0f);
    put<float>(c, api.c_dry_nofire, it.dry_nofire > 0 ? 100.0f / it.dry_nofire : 0.0f);
    put<float>(c, api.c_freeze, it.freeze > 0 ? 100.0f / it.freeze : 0.0f);
    put<float>(c, api.c_dry_bonus, 1.5f);
    if (*it.main_tex) put_ref(c, api.c_tex, str(it.main_tex));  // else the base item's (the satchel: none)
    put_ref(c, api.c_blend, *it.blend_tex ? str(it.blend_tex) : nullptr);
}

void set_repair(Il2CppObject* go, Il2CppObject* gear, const Item& it) {
    if (!it.repairable) return;
    Il2CppObject* r = add_component(go, api.t_repair);
    if (!r) return;
    put_ref(r, api.r_gear, gear);
    put_ref(r, api.r_items, prefab_array(api.gear_class, api.t_gear, split(it.repair_mats, '|')));
    put_ref(r, api.r_units, int_array(split(it.repair_counts, '|')));
    put<float>(r, api.r_cond, static_cast<float>(it.repair_cond));
    put<int32_t>(r, api.r_minutes, it.repair_min);
    put_ref(r, api.r_audio, str(it.repair_audio));
    Il2CppObject* tools = prefab_array(api.tools_class, api.t_tools, split(it.repair_tools, '|'));
    put_ref(r, api.r_tools, tools);
    put<bool>(r, api.r_needs_tool, tools && il2::array_length(tools) > 0);
}

void set_harvest(Il2CppObject* go, const Item& it) {
    if (!*it.harvest) return;
    const std::vector<std::string> parts = split(it.harvest, ';');
    if (parts.size() < 3) return;
    Il2CppObject* h = add_component(go, api.t_harvest);
    if (!h) return;
    put<int32_t>(h, api.h_skill, 0);  // SkillType.None
    put<int32_t>(h, api.h_minutes, atoi(parts[0].c_str()));
    put_ref(h, api.h_audio, str(it.harvest_audio));
    put_ref(h, api.h_tools, prefab_array(api.tools_class, api.t_tools, {}));
    put_ref(h, api.h_yield, prefab_array(api.gear_class, api.t_gear, split(parts[1].c_str(), '|')));
    put_ref(h, api.h_units, int_array(split(parts[2].c_str(), '|')));
}

void set_inspect(Il2CppObject* go, const Item& it) {
    Il2CppObject* in = add_component(go, api.t_inspect);
    if (!in) return;
    put<float>(in, api.i_dist, it.inspect_dist);
    put<V3>(in, api.i_angles, it.angles);
    put<V3>(in, api.i_offset, it.offset);
    put<V3>(in, api.i_scale, it.scale);
    Il2CppObject* tf = il2::invoke(api.comp_tf, in);
    Il2CppObject* normal = go_of(find_child(tf, it.normal_model));
    Il2CppObject* inspect = go_of(find_child(tf, it.inspect_model));
    if (normal && inspect) {
        put_ref(in, api.i_normal_mesh, normal);
        put_ref(in, api.i_inspect_mesh, inspect);
        bool on = true, off = false;
        static const MethodInfo* set_active =
            il2::find_method(il2::find_class(kCore, "UnityEngine", "GameObject"), "SetActive", 1);
        void* a1[] = {&on};
        void* a0[] = {&off};
        il2::invoke(set_active, normal, a1);
        il2::invoke(set_active, inspect, a0);
    }
}

// The accessory's buff, as TLD's own item has it; the pack's numbers where it sets them.
void set_buffs(Il2CppObject* go, Il2CppObject* gear, const Item& it) {
    const std::string extra = lower(it.extra);
    auto copy = [&](const char* from, Il2CppObject* type, Il2CppClass* klass) -> Il2CppObject* {
        Il2CppObject* src = prefab_component(from, type);
        if (!src) {
            logx::warn("clothing: %s has no buff to copy for %s", from, it.key);
            return nullptr;
        }
        Il2CppObject* dst = add_component(go, type);
        copy_fields(dst, src, klass, gear);
        return dst;
    };
    if (extra == "gear_toolbelt") {
        if (Il2CppObject* b = copy(it.extra, api.t_weight_buff, api.weight_buff_class)) {
            put<int32_t>(b, api.wb_items, 3);
            put<float>(b, api.wb_reduction, 0.5f);
        }
    } else if (extra == "gear_crampons" || extra == "gear_improvisedcrampons") {
        copy(it.extra, api.t_climb_buff, api.climb_buff_class);
    } else if (extra == "gear_riflescabbarda") {
        if (Il2CppObject* b = copy(it.extra, api.t_weight_buff, api.weight_buff_class)) {
            put<int32_t>(b, api.wb_items, 1);
            put<float>(b, api.wb_reduction, 0.5f);
        }
        copy(it.base, api.t_carry_buff, api.carry_buff_class);  // the satchel's own carrying capacity
    }
}

// The pack's materials use Unity's "Standard" shader, which TLD doesn't draw: TLD's own (a coffee cup's), as
// ModComponent does.
void fix_shaders(Il2CppObject* go) {
    static uintptr_t shader_h = 0, keywords_h = 0;
    if (!target(shader_h)) {
        Il2CppObject* cup = prefab("GEAR_CoffeeCup");
        bool all = true;
        void* a[] = {api.t_mesh_renderer, &all};
        Il2CppObject* rs = cup ? il2::invoke(api.go_in_children_all, cup, a) : nullptr;
        Il2CppObject* r = rs && il2::array_length(rs) ? il2::array_object_at(rs, 0) : nullptr;
        Il2CppObject* m = r ? il2::invoke(api.shared_material, r) : nullptr;
        Il2CppObject* s = m ? il2::invoke(api.mat_shader, m) : nullptr;
        Il2CppObject* k = m ? il2::invoke(api.mat_keywords, m) : nullptr;
        if (!s) {
            logx::warn("clothing: no shader from TLD's coffee cup; the models may not show");
            return;
        }
        shader_h = il2::il2cpp_gchandle_new(s, false);
        keywords_h = k ? il2::il2cpp_gchandle_new(k, false) : 0;
    }
    Il2CppObject* shader = target(shader_h);
    Il2CppObject* keywords = target(keywords_h);
    bool all = true;
    void* a[] = {api.t_mesh_renderer, &all};
    Il2CppObject* rs = il2::invoke(api.go_in_children_all, go, a);
    int fixed = 0;
    for (size_t i = 0; rs && i < il2::array_length(rs); ++i) {
        Il2CppObject* mats = il2::invoke(api.shared_materials, il2::array_object_at(rs, i));
        for (size_t j = 0; mats && j < il2::array_length(mats); ++j) {
            Il2CppObject* m = il2::array_object_at(mats, j);
            if (!alive(m) || name_of(il2::invoke(api.mat_shader, m)) != "Standard") continue;
            void* sa[] = {shader};
            il2::invoke(api.mat_set_shader, m, sa);
            if (keywords) {
                void* ka[] = {keywords};
                il2::invoke(api.mat_set_keywords, m, ka);
            }
            ++fixed;
        }
    }
    static int told = 0;
    if (fixed && told++ < 3) logx::info("clothing: %s: %d materials on TLD's shader", name_of(go).c_str(), fixed);
}

bool map_item(const Item& it) {
    g_step = "prefab";
    Il2CppObject* go = prefab(it.key);
    if (!go) {
        logx::warn("clothing: %s isn't in the pack's catalog", it.key);
        return false;
    }
    if (component(go, api.t_gear)) return true;  // made already
    g_step = "base item";
    Il2CppObject* base = prefab_component(it.base, api.t_gear);
    Il2CppObject* base_data = base ? get<Il2CppObject*>(base, api.gear_data) : nullptr;
    if (!base_data) {
        logx::warn("clothing: TLD has no %s (needed for %s)", it.base, it.key);
        return false;
    }
    // GearItemData: the base item's, ours
    void* ia[] = {base_data};
    g_step = "copy of the data";
    Il2CppObject* data = keep(il2::invoke(api.instantiate, nullptr, ia));
    if (!data) return false;
    void* na[] = {str(it.key)};
    il2::invoke(api.obj_set_name, data, na);
    int flags = kDontUnloadUnusedAsset;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, data, fa);
    il2::invoke(api.hide_flags, go, fa);
    keep(go);
    if (!set_data(data, it)) {
        g_fault_inside = true;  // the prefab reference: the item can't be made (map_all: the pack off)
        return false;
    }
    // the components
    g_step = "ClothingItem";
    Il2CppObject* clothing = add_component(go, api.t_clothing);
    if (Il2CppObject* base_clothing = prefab_component(it.base, api.t_clothing)) {
        g_step = "ClothingItem copy";
        copy_fields(clothing, base_clothing, api.clothing_class, nullptr);
    }
    g_step = "ClothingItem numbers";
    if (clothing) set_clothing(clothing, it);
    g_step = "hover icons";
    if (Il2CppObject* base_hover = prefab_component(it.base, api.t_hover)) {
        copy_fields(add_component(go, api.t_hover), base_hover, api.hover_class, nullptr);
    }
    g_step = "GearItem";
    Il2CppObject* gear = add_component(go, api.t_gear);
    if (!gear || !clothing) {
        logx::warn("clothing: %s: Unity didn't add the gear components", it.key);
        return false;
    }
    put_ref(gear, api.gear_data, data);
    put<int32_t>(gear, api.gear_start, static_cast<int32_t>(enum_of(api.start_enum, it.start, 1)));
    g_step = "Repairable";
    set_repair(go, gear, it);
    g_step = "Harvest";
    set_harvest(go, it);
    g_step = "Inspect";
    set_inspect(go, it);
    g_step = "buffs";
    set_buffs(go, gear, it);
    int32_t layer = static_cast<int32_t>(api.layer_gear);
    void* la[] = {&layer};
    il2::invoke(api.go_layer, go, la);
    g_step = "shaders";
    fix_shaders(go);
    g_step = "GearItem.Awake";
    il2::invoke(api.gear_awake, gear);  // as ModComponent: the prefab's component caches (m_ClothingItem...)
    return true;
}

// A fault in TLD's code while an item is made (07.11: an access violation, the game gone) caught: that item's step in
// the log, ClothingExpanded off for this run, the game goes on.
bool map_guarded(const Item& it, bool* faulted) {
    __try {
        return map_item(it);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return false;
    }
}

void map_all(const char* why) {
    int ok = 0, bad = 0;
    static int told = 0;
    for (const Item& it : kItems) {
        if (told < 40) ++told, logx::info("clothing: making %s", it.key);  // (where it stops, if it does)
        bool faulted = false;
        g_fault_inside = false;
        (map_guarded(it, &faulted) ? ok : bad)++;
        faulted = faulted || g_fault_inside;
        if (faulted) {
            logx::error("clothing: %s: a FAULT at the step '%s'; ClothingExpanded is off for this run", it.key, g_step);
            g_state = State::Failed;
            g_failed = bad;
            return;
        }
    }
    g_mapped = ok;
    g_failed = bad;
    logx::info("clothing: %d of %zu items ready%s (%s)", ok, std::size(kItems), bad ? ", some FAILED" : "", why);
}

// A prefab Addressables let go and loaded again is bare: made again.
bool any_bare() {
    for (const Item& it : kItems) {
        Il2CppObject* go = prefab(it.key);
        if (go && !component(go, api.t_gear)) return true;
    }
    return false;
}


// --- attach / detach (the inventory's item page) ----------------------------------------------------------------------
// One more button on the item page, a copy of EQUIP (its click runs ItemDescriptionPage.OnEquip, which tells ours
// apart by UIButton.current), after DROP. It says ATTACH BELT / CRAMPONS / SCABBARD on a base item, DETACH ... on one
// of ours; a second's progress bar, then the base item (its condition) and the accessory become the made item, or back.

struct UiApi {
    ptrdiff_t page_equip = -1, page_examine = -1, page_drop = -1;  // ItemDescriptionPage.m_MouseButton*
    FieldInfo* button_current = nullptr;                           // static UIButton.current
    const MethodInfo *instantiate_under = nullptr, *go_tf = nullptr, *go_active = nullptr, *go_active_self = nullptr;
    const MethodInfo *tf_local = nullptr, *tf_set_local = nullptr, *in_children = nullptr, *set_enabled = nullptr;
    const MethodInfo *label_set = nullptr, *get_inventory = nullptr, *get_pm = nullptr, *add_item = nullptr;
    const MethodInfo *destroy_gear = nullptr, *best_with_name = nullptr, *is_wearing = nullptr, *hud = nullptr;
    const MethodInfo *load_panel = nullptr, *bar_launch = nullptr;
    const MethodInfo *add_to_pack = nullptr, *set_hp = nullptr;  // PlayerManager.AddItemToPlayerInventory; GearItem.SetNormalizedHP
    Il2CppClass* bar_class = nullptr;
    ptrdiff_t inv_items = -1, gear_hp = -1, gear_in_pack = -1, gear_clothing = -1;
    Il2CppObject *t_label = nullptr, *t_localize = nullptr, *t_button = nullptr;
} ui;

UpdateButtonsFn g_update_buttons = nullptr;
VoidFn g_on_equip = nullptr;

struct PageButton {
    uintptr_t page = 0, button = 0;  // the page; our copy of its EQUIP (a GameObject)
    uintptr_t gear = 0;              // the item it shows
};
std::vector<PageButton> g_pages;

enum class Act { None, Attach, Detach };
struct Pending {
    Act act = Act::None;
    uintptr_t gear = 0, extra = 0;
    const Item* item = nullptr;
    double started = 0;
} g_pending;

struct Vec3 {
    float x, y, z;
};

std::string prefab_name(Il2CppObject* gear) {
    std::string n = name_of(go_of(gear));
    if (const size_t p = n.find('('); p != std::string::npos) n.resize(p);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n;
}

// The accessory exists in this game (the tool belt and the rifle scabbard come with a DLC; Shatsca 06.11: "nie mam
// dlc, te przedmioty możesz zapauzować"): no ATTACH for it otherwise. Asked once per accessory.
bool accessory_here(const char* key) {
    static std::vector<std::pair<std::string, bool>> known;
    const std::string k = lower(key);
    for (const auto& [name, here] : known)
        if (name == k) return here;
    const bool here = prefab(key) != nullptr;
    known.emplace_back(k, here);
    logx::info("clothing: %s %s", key, here ? "is in this game" : "isn't in this game (a DLC's?): no ATTACH for it");
    return here;
}

// What our button does for this item: its made item (attach: one with this base; detach: this one), or none.
Act act_for(const std::string& name, const Item*& out) {
    const std::string n = lower(name);
    for (const Item& it : kItems)
        if (lower(it.key) == n) {
            // TLDCoop_DLC (Shatsca 10.11: DETACH greyed out / "take it off first" for the belt and the scabbard): without
            // the DLC there's no tool belt or scabbard to get back, so no DETACH for them (crampons always come off)
            const std::string e = lower(it.extra);
            if (e != "gear_crampons" && e != "gear_improvisedcrampons" && !accessory_here(it.extra)) break;
            return out = &it, Act::Detach;
        }
    for (const Item& it : kItems)  // attach: the first made item of this base (crampons: the real ones' first)
        if (lower(it.base) == n && lower(it.extra) != "gear_improvisedcrampons") {
            // crampons: either kind will do
            const bool crampons = lower(it.extra) == "gear_crampons";
            if (!accessory_here(it.extra) && !(crampons && accessory_here("GEAR_ImprovisedCrampons"))) break;
            return out = &it, Act::Attach;
        }
    out = nullptr;
    return Act::None;
}

const char* label_for(Act a, const Item& it) {
    const std::string e = lower(it.extra);
    const bool belt = e == "gear_toolbelt", scabbard = e == "gear_riflescabbarda";
    if (a == Act::Attach) return belt ? "TLDCOOP_CE_AttachBelt" : scabbard ? "TLDCOOP_CE_AttachScabbard" : "TLDCOOP_CE_AttachCrampons";
    return belt ? "TLDCOOP_CE_DetachBelt" : scabbard ? "TLDCOOP_CE_DetachScabbard" : "TLDCOOP_CE_DetachCrampons";
}

void tell(const char* key) {
    const std::string t = custom_text::text(key);
    bool high = false, overlay = true;
    void* a[] = {str(t.c_str()), &high, &overlay};
    if (ui.hud) il2::invoke(ui.hud, nullptr, a);
}

void set_label(Il2CppObject* button_go, const std::string& text) {
    Il2CppObject* tf = il2::invoke(ui.go_tf, button_go);
    bool all = true;
    void* la[] = {ui.t_localize, &all};
    if (Il2CppObject* loc = tf ? il2::invoke(ui.in_children, tf, la) : nullptr) {
        bool off = false;
        void* ea[] = {&off};
        il2::invoke(ui.set_enabled, loc, ea);  // its UILocalize would put "Equip" back
    }
    void* ta[] = {ui.t_label, &all};
    if (Il2CppObject* label = tf ? il2::invoke(ui.in_children, tf, ta) : nullptr) {
        void* sa[] = {str(text.c_str())};
        il2::invoke(ui.label_set, label, sa);
    }
}

void set_active_go(Il2CppObject* go, bool on) {
    if (!alive(go)) return;
    void* a[] = {&on};
    il2::invoke(ui.go_active, go, a);
}

Vec3 local_of(Il2CppObject* go) {
    Vec3 v{};
    if (alive(go)) il2::invoke_value(ui.tf_local, il2::invoke(ui.go_tf, go), v);
    return v;
}

PageButton* page_button(Il2CppObject* page) {
    for (PageButton& p : g_pages)
        if (target(p.page) == page) {
            if (alive(target(p.button))) return &p;
            if (p.button) il2::il2cpp_gchandle_free(p.button), p.button = 0;
        }
    Il2CppObject* equip = get<Il2CppObject*>(page, ui.page_equip);
    Il2CppObject* examine = get<Il2CppObject*>(page, ui.page_examine);
    Il2CppObject* drop = get<Il2CppObject*>(page, ui.page_drop);
    if (!alive(equip) || !alive(drop)) return nullptr;
    Il2CppObject* parent = nullptr;
    if (Il2CppObject* tf = il2::invoke(ui.go_tf, equip)) parent = il2::invoke(il2::find_method(il2::find_class(kCore, "UnityEngine", "Transform"), "get_parent", 0), tf);
    void* ia[] = {equip, parent};
    Il2CppObject* copy = parent ? il2::invoke(ui.instantiate_under, nullptr, ia) : nullptr;
    if (!alive(copy)) return nullptr;
    void* na[] = {str("TLDCoop_ClothingButton")};
    il2::invoke(api.obj_set_name, copy, na);
    // TLDCoop_DLC (Shatsca 10.11: "przycisk ODEPNIJ wychodzi poza ekran, umieść go z lewej strony"): before the first
    // button, one step back as EXAMINE -> DROP goes (a row or a column, whichever TLD has); it was after DROP.
    const Vec3 a = local_of(alive(examine) ? examine : equip), b = local_of(drop), first = local_of(equip);
    Vec3 at{first.x - (b.x - a.x), first.y - (b.y - a.y), first.z};
    if (!alive(examine)) at = {first.x, first.y + 40, first.z};
    void* pa[] = {&at};
    il2::invoke(ui.tf_set_local, il2::invoke(ui.go_tf, copy), pa);
    PageButton* slot = nullptr;
    for (PageButton& p : g_pages)
        if (target(p.page) == page) slot = &p;
    if (!slot) {
        g_pages.push_back({});
        slot = &g_pages.back();
        slot->page = il2::il2cpp_gchandle_new(page, false);
    }
    slot->button = il2::il2cpp_gchandle_new(copy, false);
    logx::info("clothing: the item page's ATTACH/DETACH button is made");
    return slot;
}

void update_buttons_detour(Il2CppObject* self, Il2CppObject* gi, bool allow, const MethodInfo* m) {
    g_update_buttons(self, gi, allow, m);
    if (!g_ready || g_state != State::Ready || !alive(self)) return;
    const Item* it = nullptr;
    const Act act = alive(gi) && get<bool>(gi, ui.gear_in_pack) ? act_for(prefab_name(gi), it) : Act::None;
    PageButton* pb = act != Act::None ? page_button(self) : nullptr;
    if (!pb) {  // none here (or not for this item): ours off
        for (PageButton& p : g_pages)
            if (target(p.page) == self) set_active_go(target(p.button), false);
        return;
    }
    if (target(pb->gear) != gi) {
        if (pb->gear) il2::il2cpp_gchandle_free(pb->gear);
        pb->gear = il2::il2cpp_gchandle_new(gi, false);
    }
    Il2CppObject* button = target(pb->button);
    set_active_go(button, allow);
    std::string text = custom_text::text(label_for(act, *it));
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    set_label(button, text);
}

Il2CppObject* inventory_find(const char* name) {
    Il2CppObject* inv = il2::invoke(ui.get_inventory, nullptr);
    Il2CppObject* items = inv ? get<Il2CppObject*>(inv, ui.inv_items) : nullptr;
    if (!items) return nullptr;
    void* a[] = {items, str(name)};
    Il2CppObject* g = il2::invoke(ui.best_with_name, nullptr, a);
    return alive(g) ? g : nullptr;
}

float condition_of(Il2CppObject* gear) {
    Il2CppObject* data = get<Il2CppObject*>(gear, api.gear_data);
    const float max = data ? get<float>(data, api.d_max_hp) : 0.0f;
    return max > 0 ? std::clamp(get<float>(gear, ui.gear_hp) / max, 0.0f, 1.0f) : 1.0f;
}

void finish(bool success, bool cancel, float progress);

void start(Act act, const Item& it, Il2CppObject* gear) {
    if (g_pending.act != Act::None) return;
    Il2CppObject* clothing = get<Il2CppObject*>(gear, ui.gear_clothing);
    bool worn = false;
    if (alive(clothing) && il2::invoke_value(ui.is_wearing, clothing, worn) && worn) {
        tell("TLDCOOP_CE_TakeOff");
        return;
    }
    const Item* use = &it;
    Il2CppObject* extra = nullptr;
    if (act == Act::Attach) {
        // crampons: the real ones, else improvised (and the made item that goes with them)
        extra = inventory_find(it.extra);
        if (!extra && lower(it.extra) == "gear_crampons") {
            for (const Item& o : kItems)
                if (lower(o.base) == lower(it.base) && lower(o.extra) == "gear_improvisedcrampons") use = &o;
            extra = inventory_find("GEAR_ImprovisedCrampons");
        }
        if (!extra) {
            const std::string e = lower(it.extra);
            tell(e == "gear_toolbelt" ? "TLDCOOP_CE_NoBelt" : e == "gear_riflescabbarda" ? "TLDCOOP_CE_NoScabbard"
                                                                                        : "TLDCOOP_CE_NoCrampons");
            return;
        }
    }
    g_pending = {act, il2::il2cpp_gchandle_new(gear, false), extra ? il2::il2cpp_gchandle_new(extra, false) : 0, use,
                 net::now_ms()};
    void* la[] = {il2::type_object(ui.bar_class)};
    Il2CppObject* bar = ui.load_panel ? il2::invoke(ui.load_panel, nullptr, la) : nullptr;
    if (!bar || !ui.bar_launch || !coop_lapse::on_bar_end(&finish)) return finish(true, false, 1.0f);
    const std::string text = custom_text::text(act == Act::Attach ? "TLDCOOP_CE_Attaching" : "TLDCOOP_CE_Detaching");
    float seconds = 1, minutes = 0, failure = 0;
    bool skip_restore = false;
    void* a[] = {str(text.c_str()), &seconds, &minutes, &failure, &skip_restore, nullptr};
    il2::invoke(ui.bar_launch, bar, a);
}

// TLDCoop_DLC (Shatsca 10.11: "add GEAR_CargoToolbelt" -> "0 added", none of the 25 usable): TLD's AddItemCONSOLE
// gives nothing for our items (its lookup doesn't reach the pack's prefabs). One of ours: a copy of the prefab we made
// (RadialSpawnManager.GetPrefabFromName finds it), switched on, at that condition.
Il2CppObject* instance_of(const char* key, float cond) {
    Il2CppObject* go = prefab(key);
    if (!go) return nullptr;
    void* a[] = {go};
    Il2CppObject* copy = il2::invoke(api.instantiate, nullptr, a);
    if (!alive(copy)) return nullptr;
    void* na[] = {str(key)};
    il2::invoke(api.obj_set_name, copy, na);
    set_active_go(copy, true);
    Il2CppObject* gi = component(copy, api.t_gear);
    if (!gi) return nullptr;
    if (ui.set_hp) {
        bool round = false;
        void* ha[] = {&cond, &round};
        il2::invoke(ui.set_hp, gi, ha);
    }
    return gi;
}

// One of ours into our pack: TLD's console way first, our copy if that gives nothing.
bool give(const char* key, float cond) {
    Il2CppObject* pm = il2::invoke(ui.get_pm, nullptr);
    if (!pm) return false;
    int32_t n = 1;
    void* a[] = {str(key), &n, &cond};
    if (alive(il2::invoke(ui.add_item, pm, a))) return true;
    Il2CppObject* gi = ui.add_to_pack ? instance_of(key, cond) : nullptr;
    if (!gi) {
        logx::warn("clothing: %s: no copy of it to give", key);
        return false;
    }
    bool looted = false, notify = true;
    void* b[] = {gi, &looted, &notify};
    il2::invoke(ui.add_to_pack, pm, b);
    static int logged = 0;
    if (logged++ < 3) logx::info("clothing: %s given from our own copy (TLD's console way gave nothing)", key);
    return true;
}

void finish(bool success, bool cancel, float progress) {
    (void)progress;
    Pending p = g_pending;
    g_pending = {};
    Il2CppObject* gear = target(p.gear);
    Il2CppObject* extra = target(p.extra);
    if (p.gear) il2::il2cpp_gchandle_free(p.gear);
    if (p.extra) il2::il2cpp_gchandle_free(p.extra);
    if (!success || cancel || !p.item || !alive(gear) || net::now_ms() - p.started > 60000) return;
    Il2CppObject* inv = il2::invoke(ui.get_inventory, nullptr);
    Il2CppObject* pm = il2::invoke(ui.get_pm, nullptr);
    if (!inv || !pm) return;
    const float cond = condition_of(gear);
    auto add = [&](const char* name, float c) { return give(name, c); };
    auto destroy = [&](Il2CppObject* g) {
        void* a[] = {g};
        il2::invoke(ui.destroy_gear, inv, a);
    };
    if (p.act == Act::Attach) {
        if (!alive(extra) || !add(p.item->key, cond)) return;
        destroy(extra);
        destroy(gear);
        logx::info("clothing: %s + %s -> %s (condition %.0f%%)", p.item->base, p.item->extra, p.item->key, cond * 100);
    } else {
        if (!add(p.item->base, cond)) return;
        add(p.item->extra, cond);  // (the pack doesn't keep the accessory's own condition: the item's)
        destroy(gear);
        logx::info("clothing: %s -> %s + %s (condition %.0f%%)", p.item->key, p.item->base, p.item->extra, cond * 100);
    }
}

void on_equip_detour(Il2CppObject* self, const MethodInfo* m) {
    Il2CppObject* button = nullptr;
    if (ui.button_current) il2::il2cpp_field_static_get_value(ui.button_current, &button);
    Il2CppObject* go = alive(button) ? go_of(button) : nullptr;
    for (PageButton& p : g_pages) {
        if (!go || target(p.button) != go) continue;
        Il2CppObject* gear = target(p.gear);
        const Item* it = nullptr;
        const Act act = alive(gear) ? act_for(prefab_name(gear), it) : Act::None;
        if (act != Act::None && it) start(act, *it, gear);
        return;
    }
    g_on_equip(self, m);
}

bool init_ui() {
    Il2CppClass* page = il2::find_class(kGame, "", "ItemDescriptionPage");
    Il2CppClass* button = il2::find_class(kGame, "", "UIButton");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* comp = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* pm = il2::find_class(kGame, "", "PlayerManager");
    Il2CppClass* inv = il2::find_class(kGame, "", "Inventory");
    Il2CppClass* label = il2::find_class(kGame, "", "UILabel");
    if (!page || !button || !object || !go || !tf || !comp || !gm || !pm || !inv || !label) return false;
    ui.page_equip = il2::field_offset(page, "m_MouseButtonEquip");
    ui.page_examine = il2::field_offset(page, "m_MouseButtonExamine");
    ui.page_drop = il2::field_offset(page, "m_MouseButtonDrop");
    ui.button_current = il2::il2cpp_class_get_field_from_name(button, "current");
    ui.instantiate_under = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"});
    ui.go_tf = il2::find_method(go, "get_transform", 0);
    ui.go_active = il2::find_method(go, "SetActive", 1);
    ui.tf_local = il2::find_method(tf, "get_localPosition", 0);
    ui.tf_set_local = il2::find_method(tf, "set_localPosition", 1);
    ui.in_children = il2::find_method_params(comp, "GetComponentInChildren", {"System.Type", "System.Boolean"});
    ui.set_enabled = il2::find_method(il2::find_class(kCore, "UnityEngine", "Behaviour"), "set_enabled", 1);
    ui.label_set = il2::find_method(label, "set_text", 1);
    ui.get_inventory = il2::find_method(gm, "GetInventoryComponent", 0);
    ui.get_pm = il2::find_method(gm, "GetPlayerManagerComponent", 0);
    ui.add_item = il2::find_method_params(pm, "AddItemCONSOLE", {"System.String", "System.Int32", "System.Single"});
    ui.add_to_pack = il2::find_method_params_quiet(pm, "AddItemToPlayerInventory", {"GearItem", "System.Boolean", "System.Boolean"});
    if (Il2CppClass* g = il2::find_class(kGame, "", "GearItem")) ui.set_hp = il2::find_method_quiet(g, "SetNormalizedHP", 2);
    ui.destroy_gear = il2::find_method_params(inv, "DestroyGear", {"GearItem"});
    ui.inv_items = il2::field_offset(inv, "m_Items");
    if (Il2CppClass* ext = il2::find_class(kGame, "TLD.Gear", "GearItemObjectExtensions"))
        ui.best_with_name = il2::find_method(ext, "GetBestGearItemWithName", 2);
    ui.is_wearing = il2::find_method(api.clothing_class, "IsWearing", 0);
    if (Il2CppClass* hud = il2::find_class(kGame, "", "HUDMessage"))
        ui.hud = il2::find_method_params(hud, "AddMessage", {"System.String", "System.Boolean", "System.Boolean"});
    ui.bar_class = il2::find_class(kGame, "", "Panel_GenericProgressBar");
    ui.load_panel = il2::find_method_params(il2::find_class(kGame, "", "InterfaceManager"), "LoadPanel", {"System.Type"});
    ui.bar_launch = ui.bar_class ? il2::find_method_params(ui.bar_class, "Launch",
                                                           {"System.String", "System.Single", "System.Single", "System.Single",
                                                            "System.Boolean", "OnExitDelegate"})
                                 : nullptr;
    ui.gear_hp = il2::field_offset(api.gear_class, "m_CurrentHP");
    ui.gear_in_pack = il2::field_offset(api.gear_class, "m_InPlayerInventory");
    ui.gear_clothing = il2::field_offset(api.gear_class, "m_ClothingItem");
    auto type = [](Il2CppClass* k) {
        Il2CppObject* t = k ? il2::type_object(k) : nullptr;
        if (t) il2::il2cpp_gchandle_new(t, false);
        return t;
    };
    ui.t_label = type(label);
    ui.t_localize = type(il2::find_class(kGame, "", "UILocalize"));
    const MethodInfo* update = il2::find_method_params(page, "UpdateButtons", {"GearItem", "System.Boolean"});
    const MethodInfo* on_equip = il2::find_method(page, "OnEquip", 0);
    const bool ok = ui.page_equip >= 0 && ui.page_drop >= 0 && ui.button_current && ui.instantiate_under && ui.go_tf &&
                    ui.go_active && ui.tf_local && ui.tf_set_local && ui.in_children && ui.set_enabled && ui.label_set &&
                    ui.get_inventory && ui.get_pm && ui.add_item && ui.destroy_gear && ui.inv_items >= 0 &&
                    ui.best_with_name && ui.is_wearing && ui.gear_hp >= 0 && ui.gear_in_pack >= 0 &&
                    ui.gear_clothing >= 0 && ui.t_label && ui.t_localize && update && on_equip;
    if (!ok) return false;
    return hooks::install_method("ItemDescriptionPage.UpdateButtons (clothing)", update,
                                 reinterpret_cast<void*>(&update_buttons_detour), reinterpret_cast<void**>(&g_update_buttons)) &&
           hooks::install_method("ItemDescriptionPage.OnEquip (clothing)", on_equip, reinterpret_cast<void*>(&on_equip_detour),
                                 reinterpret_cast<void**>(&g_on_equip));
}

// TLDCoop_DLC (Shatsca 10.11: "na mapie się nie respią w strukturach - daj im konkretny % szans (rzadkość)"): when TLD
// first fills a container with random gear (Container.PopulateWithRandomGear), [clothing] spawn_chance % of them get
// one of ours too, the rarer ones less often (like Item Rarities' tiers). Solo or the host only: a friend's containers
// come from the host's world.
using PopulateFn = void (*)(Il2CppObject*, const MethodInfo*);
PopulateFn g_populate = nullptr;
const MethodInfo* g_container_add = nullptr;  // Container.AddGear(GearItem)
int g_spawn_pct = 3;
uint64_t g_rng = 0;

uint32_t next_random() {
    if (!g_rng) g_rng = GetTickCount64() * 6364136223846793005ull + 1442695040888963407ull;
    g_rng ^= g_rng << 13, g_rng ^= g_rng >> 7, g_rng ^= g_rng << 17;
    return static_cast<uint32_t>(g_rng >> 16);
}

// How common one of ours is (higher: more often): plain tool belts most, the moose bag with the scabbard least.
int rarity_weight(const std::string& key) {
    // Shatsca 10.11: deerskin boots with crampons aren't to be found in buildings (made or attached only)
    if (key.find("DeerskinICrampons") != std::string::npos || key.find("DeerskinNCrampons") != std::string::npos) return 0;
    if (key.find("MooseBag") != std::string::npos) return 1;
    if (key.find("Combat") != std::string::npos) return 2;
    if (key.find("ICrampons") != std::string::npos) return 3;
    if (key.find("NCrampons") != std::string::npos) return 5;
    if (key.find("Cargo") != std::string::npos || key.find("Jeans") != std::string::npos || key.find("Work") != std::string::npos) return 8;
    return 4;  // the other tool belts
}

void populate_detour(Il2CppObject* self, const MethodInfo* m) {
    g_populate(self, m);
    if (g_state != State::Ready || g_spawn_pct <= 0 || !alive(self) || !g_container_add) return;
    if (const auto role = netrt::role(); role && *role != net::Role::Host) return;
    if (static_cast<int>(next_random() % 1000) >= g_spawn_pct * 10) return;
    int total = 0;
    for (const Item& it : kItems) total += rarity_weight(it.key);
    if (total <= 0) return;
    int pick = static_cast<int>(next_random() % static_cast<uint32_t>(total));
    const Item* chosen = nullptr;
    for (const Item& it : kItems)
        if ((pick -= rarity_weight(it.key)) < 0) {
            chosen = &it;
            break;
        }
    if (!chosen) return;
    const float cond = 0.35f + static_cast<float>(next_random() % 600) / 1000.0f;  // worn: 35-95 %
    Il2CppObject* gi = instance_of(chosen->key, cond);
    if (!gi) return;
    void* a[] = {gi};
    il2::invoke(g_container_add, self, a);
    logx::info("clothing: %s (%.0f%%) put in a container (%d%% of containers get one)", chosen->key, cond * 100, g_spawn_pct);
}

// TLDCoop_DLC (Shatsca 10.11: the moose bag with the scabbard on, the rifle still weighs its full 4 kg): without the
// DLC there's no scabbard to copy its weight buff from, so the worn GEAR_MooseBagPlusScabbard halves one rifle here
// (GearItem.GetItemWeightKG, unless TLD is asked without the clothing bonus).
using WeightFn = int64_t (*)(Il2CppObject*, float, bool, const MethodInfo*);
WeightFn g_weight = nullptr;
ptrdiff_t g_gun_item = -1, g_gun_type = -1;
bool g_scabbard_worn = false;
double g_scabbard_checked = -1e9;

bool scabbard_worn() {
    const double now = net::now_ms();
    if (now - g_scabbard_checked < 500) return g_scabbard_worn;
    g_scabbard_checked = now;
    g_scabbard_worn = false;
    if (g_state != State::Ready || accessory_here("GEAR_RifleScabbardA")) return false;  // the DLC's own buff does it
    Il2CppObject* bag = inventory_find("GEAR_MooseBagPlusScabbard");
    Il2CppObject* clothing = bag ? get<Il2CppObject*>(bag, ui.gear_clothing) : nullptr;
    bool worn = false;
    g_scabbard_worn = alive(clothing) && il2::invoke_value(ui.is_wearing, clothing, worn) && worn;
    return g_scabbard_worn;
}

// TLDCoop_DLC (Shatsca 11.11 18:35: "cargo pants with the tool belt (found in a drawer) don't lighten anything"):
// without the DLC there's no WeightReductionBuff to copy from GEAR_ToolBelt either. Ours, as the item says: while one
// of our belted pants is worn, the three heaviest tools in the pack weigh half. (Checked every 0.5 s.)
ptrdiff_t g_tools_item = -1;  // GearItem.m_ToolsItem
std::vector<Il2CppObject*> g_light_tools;
double g_belt_checked = -1e9;

void check_belt() {
    const double now = net::now_ms();
    if (now - g_belt_checked < 500) return;
    g_belt_checked = now;
    g_light_tools.clear();
    if (g_state != State::Ready || g_tools_item < 0 || accessory_here("GEAR_ToolBelt")) return;
    bool belt = false;
    for (const Item& it : kItems) {
        if (lower(it.extra) != "gear_toolbelt") continue;
        Il2CppObject* pants = inventory_find(it.key);
        Il2CppObject* clothing = pants ? get<Il2CppObject*>(pants, ui.gear_clothing) : nullptr;
        bool worn = false;
        if (alive(clothing) && il2::invoke_value(ui.is_wearing, clothing, worn) && worn) {
            belt = true;
            break;
        }
    }
    if (!belt) return;
    Il2CppObject* inv = il2::invoke(ui.get_inventory, nullptr);
    Il2CppObject* list = inv ? get<Il2CppObject*>(inv, ui.inv_items) : nullptr;
    Il2CppObject* arr = list ? get<Il2CppObject*>(list, 0x10) : nullptr;
    const int32_t n = list ? get<int32_t>(list, 0x18) : 0;
    std::vector<std::pair<int64_t, Il2CppObject*>> tools;
    for (int32_t i = 0; arr && i < n && static_cast<size_t>(i) < il2::array_length(arr); ++i) {
        // List<GearItemObject>: 16-byte structs (GearItem, name) from 0x20
        Il2CppObject* gi = get<Il2CppObject*>(arr, 0x20 + static_cast<ptrdiff_t>(i) * 16);
        if (!alive(gi) || !alive(get<Il2CppObject*>(gi, g_tools_item))) continue;
        tools.emplace_back(g_weight(gi, 1.0f, true, nullptr), gi);
    }
    std::sort(tools.begin(), tools.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < tools.size() && i < 3; ++i) g_light_tools.push_back(tools[i].second);
}

int64_t weight_detour(Il2CppObject* self, float mult, bool ignore_bonus, const MethodInfo* m) {
    const int64_t w = g_weight(self, mult, ignore_bonus, m);
    static bool busy = false;
    if (ignore_bonus || busy || !alive(self)) return w;
    busy = true;
    bool half = false;
    if (g_tools_item >= 0 && alive(get<Il2CppObject*>(self, g_tools_item))) {
        check_belt();
        half = std::find(g_light_tools.begin(), g_light_tools.end(), self) != g_light_tools.end();
    } else if (g_gun_item >= 0 && g_gun_type >= 0) {
        Il2CppObject* gun = get<Il2CppObject*>(self, g_gun_item);
        half = alive(gun) && get<int32_t>(gun, g_gun_type) == 1 /* GunType.Rifle */ && scabbard_worn();
    }
    busy = false;
    return half ? w / 2 : w;
}

bool hook_scabbard() {
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* gun = il2::find_class(kGame, "", "GunItem");
    const MethodInfo* weight = gear ? il2::find_method_params_quiet(gear, "GetItemWeightKG", {"System.Single", "System.Boolean"}) : nullptr;
    g_gun_item = gear ? il2::field_offset_quiet(gear, "m_GunItem") : -1;
    g_tools_item = gear ? il2::field_offset_quiet(gear, "m_ToolsItem") : -1;
    g_gun_type = gun ? il2::field_offset_quiet(gun, "m_GunType") : -1;
    if (!weight || g_gun_item < 0 || g_gun_type < 0) return false;
    return hooks::install_method("GearItem.GetItemWeightKG (clothing scabbard)", weight, reinterpret_cast<void*>(&weight_detour),
                                 reinterpret_cast<void**>(&g_weight));
}

bool hook_spawns() {
    Il2CppClass* container = il2::find_class(kGame, "", "Container");
    const MethodInfo* populate = container ? il2::find_method_quiet(container, "PopulateWithRandomGear", 0) : nullptr;
    g_container_add = container ? il2::find_method_params_quiet(container, "AddGear", {"GearItem"}) : nullptr;
    g_spawn_pct = config::load().clothing_spawn;
    if (!populate || !g_container_add) return false;
    return hooks::install_method("Container.PopulateWithRandomGear (clothing)", populate, reinterpret_cast<void*>(&populate_detour),
                                 reinterpret_cast<void**>(&g_populate));
}

}  // namespace

bool init() {
    for (const Text& t : kTexts) custom_text::add(t.key, t.en, t.pl);
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* comp = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* so = il2::find_class(kCore, "UnityEngine", "ScriptableObject");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* bundle = il2::find_class("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle");
    Il2CppClass* addressables = il2::find_class(kAddressables, "UnityEngine.AddressableAssets", "Addressables");
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* data = il2::find_class(kGame, "TLD.Gear", "GearItemData");
    Il2CppClass* clothing = il2::find_class(kGame, "", "ClothingItem");
    Il2CppClass* repair = il2::find_class(kGame, "", "Repairable");
    Il2CppClass* harvest = il2::find_class(kGame, "", "Harvest");
    Il2CppClass* harvest_base = il2::find_class(kGame, "", "HarvestBase");
    Il2CppClass* inspect = il2::find_class(kGame, "", "Inspect");
    Il2CppClass* weight_buff = il2::find_class(kGame, "", "WeightReductionBuff");
    Il2CppClass* icon_data = il2::find_class(kGame, "TLD.Gear", "GearItemInventoryIconSimpleData");
    Il2CppClass* radial = il2::find_class(kGame, "", "RadialSpawnManager");
    Il2CppClass* lstring = il2::find_class(kGame, "", "LocalizedString");
    if (!object || !go || !comp || !tf || !so || !renderer || !material || !bundle || !addressables || !gear || !data ||
        !clothing || !repair || !harvest || !harvest_base || !inspect || !weight_buff || !icon_data || !radial || !lstring) {
        logx::warn("clothing: UNAVAILABLE (TLD classes missing)");
        return false;
    }
    auto type = [](Il2CppClass* k) {
        Il2CppObject* t = k ? il2::type_object(k) : nullptr;
        if (t) il2::il2cpp_gchandle_new(t, false);
        return t;
    };
    api.mono_class = il2::find_class(kCore, "UnityEngine", "MonoBehaviour");
    api.obj_name = il2::find_method(object, "get_name", 0);
    api.obj_set_name = il2::find_method(object, "set_name", 1);
    api.instantiate = il2::find_method_params(object, "Instantiate", {"UnityEngine.Object"});
    api.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    api.go_add = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_get = il2::find_method_params(go, "GetComponent", {"System.Type"});
    api.go_layer = il2::find_method(go, "set_layer", 1);
    api.go_in_children_all = il2::find_method_params(go, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    api.comp_go = il2::find_method(comp, "get_gameObject", 0);
    api.comp_tf = il2::find_method(comp, "get_transform", 0);
    api.tf_count = il2::find_method(tf, "get_childCount", 0);
    api.tf_child = il2::find_method(tf, "GetChild", 1);
    api.so_create = il2::find_method_params(so, "CreateInstance", {"System.Type"});
    api.shared_materials = il2::find_method(renderer, "get_sharedMaterials", 0);
    api.shared_material = il2::find_method(renderer, "get_sharedMaterial", 0);
    api.mat_shader = il2::find_method(material, "get_shader", 0);
    api.mat_set_shader = il2::find_method(material, "set_shader", 1);
    api.mat_keywords = il2::find_method(material, "get_shaderKeywords", 0);
    api.mat_set_keywords = il2::find_method(material, "set_shaderKeywords", 1);
    api.bundles = il2::find_method(bundle, "GetAllLoadedAssetBundles_Native", 0);
    api.bundle_load = il2::find_method_params(bundle, "LoadAsset", {"System.String", "System.Type"});
    api.bundle_names = il2::find_method_quiet(bundle, "GetAllAssetNames", 0);
    if (Il2CppClass* utils = il2::find_class(kGame, "", "Utils"))
        api.grid_icon = il2::find_method_params_quiet(utils, "GetInventoryGridIconTexture", {"System.String"});
    api.load_catalog = il2::find_method_params(addressables, "LoadContentCatalogAsync",
                                               {"System.String", "System.Boolean", "System.String"});
    api.prefab_by_name = il2::find_method_params(radial, "GetPrefabFromName", {"System.String"});
    api.gear_ref_class = il2::find_class(kGame, "", "AssetReferenceGearItem");
    api.gear_ref_ctor = api.gear_ref_class ? il2::find_method_params(api.gear_ref_class, ".ctor", {"System.String"}) : nullptr;
    api.tex_ref_class = il2::find_class(kAddressables, "UnityEngine.AddressableAssets", "AssetReferenceTexture2D");
    api.tex_ref_ctor = api.tex_ref_class ? il2::find_method_params(api.tex_ref_class, ".ctor", {"System.String"}) : nullptr;
    api.lstring_class = lstring;
    api.lstring_ctor = il2::find_method(lstring, ".ctor", 0);
    api.lstring_id = il2::field_offset(lstring, "m_LocalizationID");
    if (Il2CppClass* weight = il2::find_class("TLD.IntBackedUnit.dll", "TLD.IntBackedUnit", "ItemWeight"))
        api.from_kg = il2::find_method_quiet(weight, "FromKilograms", 1);
    api.gear_awake = il2::find_method(gear, "Awake", 0);
    api.gear_class = gear;
    api.tools_class = il2::find_class(kGame, "", "ToolsItem");
    api.int_class = il2::find_class("mscorlib.dll", "System", "Int32");
    api.clothing_class = clothing;
    api.weight_buff_class = weight_buff;
    api.climb_buff_class = il2::find_class(kGame, "", "ClimbingBuff");
    api.carry_buff_class = il2::find_class(kGame, "", "CarryingCapacityBuff");
    api.hover_class = il2::find_class(kGame, "", "HoverIconsToShow");
    api.t_gear = type(gear);
    api.t_clothing = type(clothing);
    api.t_repair = type(repair);
    api.t_harvest = type(harvest);
    api.t_inspect = type(inspect);
    api.t_tools = type(api.tools_class);
    api.t_hover = type(api.hover_class);
    api.t_weight_buff = type(weight_buff);
    api.t_climb_buff = type(api.climb_buff_class);
    api.t_carry_buff = type(api.carry_buff_class);
    api.t_mesh_renderer = type(il2::find_class(kCore, "UnityEngine", "MeshRenderer"));
    api.t_texture = type(il2::find_class(kCore, "UnityEngine", "Texture2D"));
    api.t_icon_data = type(icon_data);
    api.gear_data = il2::field_offset(gear, "m_GearItemData");
    api.gear_start = il2::field_offset(gear, "m_StartCondition");
    api.d_ref = il2::field_offset(data, "m_PrefabReference");
    api.d_tag = il2::field_offset(data, "m_GameplayTag");
    api.d_weight = il2::field_offset(data, "m_BaseWeight");
    api.d_max_hp = il2::field_offset(data, "m_MaxHP");
    api.d_decay = il2::field_offset(data, "m_DailyHPDecay");
    api.d_name = il2::field_offset(data, "m_LocalizedName");
    api.d_desc = il2::field_offset(data, "m_LocalizedDescription");
    api.d_icon = il2::field_offset(data, "m_IconData");
    api.d_cover[0] = il2::field_offset(data, "m_CoverFlowMainTexture");
    api.d_cover[1] = il2::field_offset(data, "m_CoverFlowDamageTexture");
    api.d_cover[2] = il2::field_offset(data, "m_CoverFlowBlendTexture");
    api.d_cover[3] = il2::field_offset(data, "m_CoverFlowOpenedTexture");
    api.icon_tex = il2::field_offset(icon_data, "m_Icon");
    api.c_region = il2::field_offset(clothing, "m_Region");
    api.c_min = il2::field_offset(clothing, "m_MinLayer");
    api.c_max = il2::field_offset(clothing, "m_MaxLayer");
    api.c_decay_out = il2::field_offset(clothing, "m_DailyHPDecayWhenWornOutside");
    api.c_decay_in = il2::field_offset(clothing, "m_DailyHPDecayWhenWornInside");
    api.c_warmth = il2::field_offset(clothing, "m_Warmth");
    api.c_warmth_wet = il2::field_offset(clothing, "m_WarmthWhenWet");
    api.c_windproof = il2::field_offset(clothing, "m_Windproof");
    api.c_toughness = il2::field_offset(clothing, "m_Toughness");
    api.c_sprint = il2::field_offset(clothing, "m_SprintBarReductionPercent");
    api.c_waterproof = il2::field_offset(clothing, "m_Waterproofness");
    api.c_dry = il2::field_offset(clothing, "m_DryPercentPerHour");
    api.c_dry_nofire = il2::field_offset(clothing, "m_DryPercentPerHourNoFire");
    api.c_freeze = il2::field_offset(clothing, "m_FreezePercentPerHour");
    api.c_dry_bonus = il2::field_offset(clothing, "m_DryBonusWhenNotWorn");
    api.c_tex = il2::field_offset(clothing, "m_PaperDollTextureName");
    api.c_blend = il2::field_offset(clothing, "m_PaperDollBlendmapName");
    api.r_gear = il2::field_offset(repair, "m_GearItem");
    api.r_items = il2::field_offset(repair, "m_RequiredGear");
    api.r_units = il2::field_offset(repair, "m_RequiredGearUnits");
    api.r_cond = il2::field_offset(repair, "m_ConditionIncrease");
    api.r_minutes = il2::field_offset(repair, "m_DurationMinutes");
    api.r_audio = il2::field_offset(repair, "m_RepairAudio");
    api.r_tools = il2::field_offset(repair, "m_RepairToolChoices");
    api.r_needs_tool = il2::field_offset(repair, "m_RequiresToolToRepair");
    api.h_skill = il2::field_offset(harvest_base, "m_AppliedSkillType");
    api.h_minutes = il2::field_offset(harvest_base, "m_DurationMinutes");
    api.h_audio = il2::field_offset(harvest_base, "m_Audio");
    api.h_tools = il2::field_offset(harvest_base, "m_RequiredTools");
    api.h_yield = il2::field_offset(harvest, "m_YieldGear");
    api.h_units = il2::field_offset(harvest, "m_YieldGearUnits");
    api.i_inspect_mesh = il2::field_offset(inspect, "m_InspectModeMesh");
    api.i_normal_mesh = il2::field_offset(inspect, "m_NormalMesh");
    api.i_dist = il2::field_offset(inspect, "m_DistanceFromCamera");
    api.i_offset = il2::field_offset(inspect, "m_Offset");
    api.i_angles = il2::field_offset(inspect, "m_Angles");
    api.i_scale = il2::field_offset(inspect, "m_Scale");
    api.wb_items = il2::field_offset(weight_buff, "m_AffectedItems");
    api.wb_reduction = il2::field_offset(weight_buff, "m_WeightReduction");
    api.region_enum = il2::find_class(kGame, "", "ClothingRegion");
    api.layer_enum = il2::find_class(kGame, "", "ClothingLayer");
    api.start_enum = il2::find_class(kGame, "", "GearStartCondition");
    if (Il2CppClass* layers = il2::find_class(kGame, "", "vp_Layer")) {
        FieldInfo* f = il2::il2cpp_class_get_field_from_name(layers, "Gear");
        if (!f || !il2::literal_value(f, api.layer_gear)) api.layer_gear = 0;
    }
    const bool ok =
        api.obj_name && api.obj_set_name && api.instantiate && api.hide_flags && api.go_add && api.go_get && api.go_layer &&
        api.go_in_children_all && api.comp_go && api.comp_tf && api.tf_count && api.tf_child && api.so_create &&
        api.shared_materials && api.shared_material && api.mat_shader && api.mat_set_shader && api.mat_keywords &&
        api.mat_set_keywords && api.bundles && api.bundle_load && api.load_catalog && api.prefab_by_name &&
        api.gear_ref_ctor && api.tex_ref_ctor && api.lstring_ctor && api.lstring_id >= 0 && api.gear_awake &&
        api.tools_class && api.int_class && api.climb_buff_class && api.carry_buff_class && api.hover_class &&
        api.mono_class && api.gear_data >= 0 && api.gear_start >= 0 && api.d_ref >= 0 && api.d_weight >= 0 &&
        api.d_max_hp >= 0 && api.d_decay >= 0 && api.d_name >= 0 && api.d_desc >= 0 && api.d_icon >= 0 &&
        api.icon_tex >= 0 && api.c_region >= 0 && api.c_tex >= 0 && api.c_blend >= 0 && api.r_items >= 0 &&
        api.h_yield >= 0 && api.i_scale >= 0 && api.wb_items >= 0 && api.wb_reduction >= 0 && api.region_enum &&
        api.layer_enum && api.start_enum && il2::il2cpp_field_get_value && il2::il2cpp_field_set_value &&
        il2::il2cpp_array_new && il2::il2cpp_class_from_type;
    if (!ok) {
        logx::warn("clothing: UNAVAILABLE (TLD API missing; see warnings above)");
        return false;
    }
    if (!write_pack()) {
        logx::warn("clothing: UNAVAILABLE (couldn't write the pack to %s)", g_dir.string().c_str());
        return false;
    }
    g_ready = true;
    if (!init_ui()) logx::warn("clothing: no ATTACH/DETACH button on the item page (TLD API missing or hooks failed)");
    if (!hook_spawns()) logx::warn("clothing: its items won't turn up in containers (TLD API missing or the hook failed)");
    else logx::info("clothing: %d%% of containers get one of its items when the game first fills them", g_spawn_pct);
    if (!hook_scabbard()) logx::warn("clothing: the moose bag's scabbard won't lighten a rifle (TLD API missing or the hook failed)");
    logx::info("clothing: ready (the pack in %s; its catalog loads with the game)", g_dir.string().c_str());
    return true;
}

void on_frame(const std::string& scene, bool in_game) {
    (void)in_game;
    if (!g_ready) return;
    const double now = net::now_ms();
    if (g_first_frame == 0) g_first_frame = now;
    switch (g_state) {
        case State::Waiting:
            if (now - g_first_frame < kStartDelayMs) return;  // TLD's own Addressables first
            g_state = start_catalog() ? State::Loading : State::Failed;
            g_load_started = now;
            return;
        case State::Loading: {
            // The load's state isn't asked (06.11-07.11: TLD crashed 0.3 s after the load started, at our first look
            // at the operation, twice, with or without the locator id fixed): after a few seconds the catalog is in
            // when its first prefab can be found.
            static double last_probe = 0;
            if (now - g_load_started < 8000 || now - last_probe < 4000) return;
            last_probe = now;
            if (now - g_load_started > kCatalogTimeoutMs) {
                logx::warn("clothing: the pack's catalog didn't come in (timed out); no ClothingExpanded items");
                g_state = State::Failed;
                return;
            }
            logx::info("clothing: is the catalog in? (looking for %s)", kItems[0].key);
            if (!prefab(kItems[0].key)) return;
            logx::info("clothing: the catalog is in; making the items");
            map_all("catalog in");
            if (g_state == State::Failed) return;
            g_state = State::Ready;
            g_scene = scene;
            return;
        }
        case State::Ready:
            if (scene != g_scene) {
                g_scene = scene;
                if (any_bare()) map_all("made again after a scene change");
            }
            return;
        case State::Failed:
            return;
    }
}

// TLDCoop_DLC (Shatsca 08.11: "add GEAR_CargoToolbelt" in the F1 console added nothing): TLD's `add` looks the name
// up in ConsoleManager's list of the game's own gear, which ours aren't in. `add` / `gear_add` with one of ours is
// answered here (PlayerManager.AddItemCONSOLE, as the attach does).
bool console(const std::string& line) {
    std::istringstream in(line);
    std::string cmd, name;
    int count = 1;
    in >> cmd >> name;
    if (!(in >> count)) count = 1;
    for (char& c : cmd) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if ((cmd != "add" && cmd != "gear_add") || name.empty() || g_state != State::Ready || !ui.add_item || !ui.get_pm) return false;
    const Item* found = nullptr;
    for (const Item& it : kItems)
        if (lower(it.key) == lower(name) || lower(short_name(it.key)) == lower(name)) found = &it;
    if (!found) return false;
    int added = 0;
    for (int i = 0; i < std::clamp(count, 1, 20); ++i)
        if (give(found->key, 1.0f)) ++added;
    logx::info("clothing: console: add %s x%d: %d added", found->key, count, added);
    return true;
}

void describe() {
    static const char* kStates[] = {"waiting", "loading the catalog", "ready", "FAILED"};
    logx::info("dev: clothing: %s, %d items made, %d failed, pack in %s", kStates[static_cast<int>(g_state)], g_mapped,
               g_failed, g_dir.string().c_str());
    for (const Item& it : kItems) {
        Il2CppObject* go = g_state == State::Ready ? prefab(it.key) : nullptr;
        logx::info("dev: clothing:   %s (%s + %s): %s", it.key, it.base, it.extra,
                   !go ? "-" : component(go, api.t_gear) ? "gear" : "BARE");
    }
}

bool ready() { return g_ready && g_state == State::Ready; }

void* gear_prefab(const char* name, bool tool) {
    if (!g_ready || !name) return nullptr;
    if (is_ours(name) && g_state != State::Ready) return nullptr;
    return component(prefab(name), tool ? api.t_tools : api.t_gear);
}

void* make_copy(const char* key) { return ready() && is_ours(key) ? instance_of(key, 1.0f) : nullptr; }

bool give_item(const char* key, float condition) { return ready() && ui.get_pm && give(key, condition); }

bool is_ours(const char* name) {
    if (!name) return false;
    for (const Item& it : kItems)
        if (lower(it.key) == lower(name)) return true;
    return false;
}

}  // namespace clothing_expanded
