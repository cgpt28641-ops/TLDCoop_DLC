#include "clothing_expanded.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "custom_text.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/session.h"
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
std::vector<uintptr_t> g_keep;  // made objects held for good (GearItemData copies, icon data, the prefabs)

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
Il2CppString* str(const char* s) { return il2::il2cpp_string_new(s); }
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
    void* a[] = {str(arg)};
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
    return write_file(g_dir / L"catalog_clothingexpanded.json", catalog.data(), catalog.size());
}

// --- the catalog -----------------------------------------------------------------------------------------------------

bool start_catalog() {
    const std::string path = (g_dir / L"catalog_clothingexpanded.json").string();
    bool auto_release = false;
    void* a[] = {str(path.c_str()), &auto_release, nullptr};
    Il2CppObject* boxed = il2::invoke(api.load_catalog, nullptr, a);
    Il2CppObject* op = boxed ? *static_cast<Il2CppObject**>(il2::il2cpp_object_unbox(boxed)) : nullptr;  // m_InternalOp
    if (!op) {
        logx::warn("clothing: Addressables didn't start loading the pack's catalog");
        return false;
    }
    g_catalog_op = il2::il2cpp_gchandle_new(op, false);
    logx::info("clothing: loading the pack's catalog (%s)", path.c_str());
    return true;
}

const MethodInfo* method_up(Il2CppObject* o, const char* name) {
    for (Il2CppClass* k = o ? il2::il2cpp_object_get_class(o) : nullptr; k; k = il2::il2cpp_class_get_parent(k))
        if (const MethodInfo* m = il2::il2cpp_class_get_method_from_name(k, name, 0)) return m;
    return nullptr;
}

// 0: still loading, 1: in, -1: failed
int catalog_status() {
    Il2CppObject* op = target(g_catalog_op);
    if (!op) return -1;
    static const MethodInfo *is_done = nullptr, *status = nullptr;
    if (!is_done) is_done = method_up(op, "get_IsDone");
    if (!status) status = method_up(op, "get_Status");
    bool done = false;
    if (!is_done || !il2::invoke_value(is_done, op, done)) return -1;
    if (!done) return 0;
    int32_t st = 0;  // AsyncOperationStatus: None 0, Succeeded 1, Failed 2
    if (status) il2::invoke_value(status, op, st);
    return st == 1 ? 1 : -1;
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
Il2CppObject* icon_texture(const Item& it) {
    const std::string n = short_name(it.key);
    for (const std::string& v : {n, std::string("DeerSkinToolbelt")}) {
        if (v != n && lower(v) != lower(n)) continue;
        if (Il2CppObject* t = pack_texture("Assets/InventoryGridIcons/ico_GearItem__" + v + ".png")) return t;
    }
    return nullptr;
}

void set_data(Il2CppObject* data, const Item& it) {
    put_ref(data, api.d_ref, new_object(api.gear_ref_class, api.gear_ref_ctor, it.key));
    if (api.d_tag >= 0) memset(reinterpret_cast<char*>(data) + api.d_tag, 0, 16);  // no TLD tag (ModComponent: none)
    int64_t kg = 0;
    float w = it.weight;
    void* a[] = {&w};
    if (api.from_kg && il2::invoke_value(api.from_kg, nullptr, kg, a)) put<int64_t>(data, api.d_weight, kg);
    put<float>(data, api.d_max_hp, it.max_hp);
    put<float>(data, api.d_decay, it.days_decay > 0 ? it.max_hp / it.days_decay : 0.0f);
    put_ref(data, api.d_name, localized(it.name_id));
    put_ref(data, api.d_desc, localized(it.desc_id));
    const std::string icon = "ico_GearItem__" + short_name(it.key);
    if (Il2CppObject* tex = icon_texture(it)) {
        void* ta[] = {api.t_icon_data};
        Il2CppObject* icon_data = keep(il2::invoke(api.so_create, nullptr, ta));
        if (icon_data) {
            put_ref(icon_data, api.icon_tex, tex);
            int flags = kDontUnloadUnusedAsset;
            void* fa[] = {&flags};
            il2::invoke(api.hide_flags, icon_data, fa);
            put_ref(data, api.d_icon, icon_data);
        }
    } else {
        logx::warn("clothing: no icon for %s in the pack", it.key);
    }
    for (ptrdiff_t off : api.d_cover) put_ref(data, off, new_object(api.tex_ref_class, api.tex_ref_ctor, icon.c_str()));
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
    Il2CppObject* go = prefab(it.key);
    if (!go) {
        logx::warn("clothing: %s isn't in the pack's catalog", it.key);
        return false;
    }
    if (component(go, api.t_gear)) return true;  // made already
    Il2CppObject* base = prefab_component(it.base, api.t_gear);
    Il2CppObject* base_data = base ? get<Il2CppObject*>(base, api.gear_data) : nullptr;
    if (!base_data) {
        logx::warn("clothing: TLD has no %s (needed for %s)", it.base, it.key);
        return false;
    }
    // GearItemData: the base item's, ours
    void* ia[] = {base_data};
    Il2CppObject* data = keep(il2::invoke(api.instantiate, nullptr, ia));
    if (!data) return false;
    void* na[] = {str(it.key)};
    il2::invoke(api.obj_set_name, data, na);
    int flags = kDontUnloadUnusedAsset;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, data, fa);
    il2::invoke(api.hide_flags, go, fa);
    keep(go);
    set_data(data, it);
    // the components
    Il2CppObject* clothing = add_component(go, api.t_clothing);
    if (Il2CppObject* base_clothing = prefab_component(it.base, api.t_clothing)) {
        copy_fields(clothing, base_clothing, api.clothing_class, nullptr);
    }
    if (clothing) set_clothing(clothing, it);
    if (Il2CppObject* base_hover = prefab_component(it.base, api.t_hover)) {
        copy_fields(add_component(go, api.t_hover), base_hover, api.hover_class, nullptr);
    }
    Il2CppObject* gear = add_component(go, api.t_gear);
    if (!gear || !clothing) {
        logx::warn("clothing: %s: Unity didn't add the gear components", it.key);
        return false;
    }
    put_ref(gear, api.gear_data, data);
    put<int32_t>(gear, api.gear_start, static_cast<int32_t>(enum_of(api.start_enum, it.start, 1)));
    set_repair(go, gear, it);
    set_harvest(go, it);
    set_inspect(go, it);
    set_buffs(go, gear, it);
    int32_t layer = static_cast<int32_t>(api.layer_gear);
    void* la[] = {&layer};
    il2::invoke(api.go_layer, go, la);
    fix_shaders(go);
    il2::invoke(api.gear_awake, gear);  // as ModComponent: the prefab's component caches (m_ClothingItem...)
    return true;
}

void map_all(const char* why) {
    int ok = 0, bad = 0;
    for (const Item& it : kItems) (map_item(it) ? ok : bad)++;
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
            const int st = catalog_status();
            if (st == 0 && now - g_load_started < kCatalogTimeoutMs) return;
            if (st != 1) {
                logx::warn("clothing: the pack's catalog didn't load (%s); no ClothingExpanded items",
                           st == 0 ? "timed out" : "failed");
                g_state = State::Failed;
                return;
            }
            map_all("catalog in");
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

}  // namespace clothing_expanded
