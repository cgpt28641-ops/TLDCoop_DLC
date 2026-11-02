#include "game_state.h"

#include <iterator>

#include "net/afflictions.h"

#include "il2cpp_api.h"
#include "log.h"
#include "net/held.h"

namespace game {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";

struct Unity {
    float x, y, z;
};

// Resolved once in init(). A missing entry disables only the reads that need it.
struct Api {
    FieldInfo* active_scene = nullptr;  // static string GameManager.m_ActiveScene
    FieldInfo* since_level_load = nullptr;  // static float GameManager.m_SecondsSinceLevelLoad
    FieldInfo* save_name = nullptr;         // static string SaveGameSystem.m_CurrentSaveName
    FieldInfo* game_id = nullptr;           // static uint SaveGameSystem.m_CurrentGameId
    FieldInfo* transition = nullptr;        // static GameManager.m_SceneTransitionData
    ptrdiff_t random_seed = -1;             // SceneTransitionData.m_GameRandomSeed

    const MethodInfo* get_player_transform = nullptr;  // static GameManager.GetPlayerTransform()
    const MethodInfo* get_fps_camera = nullptr;        // static GameManager.GetVpFPSCamera()
    const MethodInfo* get_time_of_day = nullptr;       // static GameManager.GetTimeOfDayComponent()
    const MethodInfo* get_weather = nullptr;           // static GameManager.GetWeatherComponent()
    const MethodInfo* get_wind = nullptr;              // static GameManager.GetWindComponent()

    const MethodInfo* transform_position = nullptr;     // Transform.get_position()
    const MethodInfo* transform_euler_angles = nullptr; // Transform.get_eulerAngles()

    ptrdiff_t camera_yaw = -1;    // vp_FPSCamera.m_Yaw
    ptrdiff_t camera_pitch = -1;  // vp_FPSCamera.m_Pitch

    const MethodInfo* tod_day = nullptr;
    const MethodInfo* tod_hour = nullptr;
    const MethodInfo* tod_minutes = nullptr;
    const MethodInfo* tod_normalized = nullptr;

    const MethodInfo* weather_stage = nullptr;
    const MethodInfo* weather_temperature = nullptr;
    const MethodInfo* weather_indoor = nullptr;
    Il2CppClass* weather_stage_enum = nullptr;

    const MethodInfo* wind_speed = nullptr;

    const MethodInfo* get_struggle = nullptr;   // static GameManager.GetPlayerStruggleComponent()
    const MethodInfo* in_struggle = nullptr;    // PlayerStruggle.InStruggle()
    const MethodInfo* crouched = nullptr;       // PlayerManager.PlayerIsCrouched()
} g;

// What's in the player's hands (read_held).
struct HeldApi {
    const MethodInfo* player_manager = nullptr;    // static GameManager.GetPlayerManagerComponent()
    const MethodInfo* player_animation = nullptr;  // static GameManager.GetPlayerAnimationComponent()
    ptrdiff_t in_hands = -1;                       // PlayerManager.m_ItemInHandsInternal
    ptrdiff_t anim_state = -1;                     // PlayerAnimation.m_State
    const MethodInfo* object_name = nullptr;       // Object.get_name()
    const MethodInfo* sanitize = nullptr;          // static Utils.SanitizePrefabName(string)
    // GearItem's parts, which say what kind of item it is.
    ptrdiff_t torch = -1, flare = -1, lamp = -1, flashlight = -1, gun = -1, bow = -1, spear = -1, stone = -1,
              noise_maker = -1, matches = -1;
    const MethodInfo* torch_burning = nullptr;  // TorchItem.IsBurning()
    const MethodInfo* flare_burning = nullptr;  // FlareItem.IsBurning()
    ptrdiff_t flare_type = -1;                  // FlareItem.m_Type (FlareType)
    const MethodInfo* lamp_on = nullptr;        // KeroseneLampItem.IsOn()
    ptrdiff_t flashlight_state = -1;            // FlashlightItem.m_State (Off, Low, High, Paused)
    ptrdiff_t gun_type = -1;                    // GunItem.m_GunType (Revolver, Rifle, FlareGun, Camera)
    ptrdiff_t spear_state = -1;                 // BearSpearItem.m_CurrentSpearState (None, Setting, Raised, ...)
    ptrdiff_t bow_state = -1;                   // BowItem.m_BowState (Unloaded, Ready, ReadyToAim, Aim, ...)
    // TLDCoop_DLC: being lit (m_IgniteAfterDelay: TLD's ignite delay running) and a match's flame
    ptrdiff_t flare_igniting = -1, torch_igniting = -1, match_igniting = -1;
    ptrdiff_t match_ignited = -1;               // MatchesItem.m_Ignited
} h;

constexpr ptrdiff_t kCachedPtr = 0x10;  // UnityEngine.Object.m_CachedPtr: null once the native object is destroyed
// PlayerAnimation.State
constexpr int32_t kAnimHidden = 0, kAnimToAiming = 1, kAnimAiming = 2, kAnimFromAiming = 3, kAnimFiring = 15;
constexpr int32_t kFlashlightLow = 1, kFlashlightHigh = 2;  // FlashlightItem.State
constexpr int32_t kSpearRaised = 2;                         // BearSpearItem.SpearState
constexpr int32_t kGunRevolver = 0, kGunRifle = 1, kGunFlareGun = 2, kGunCamera = 3;  // GunType
constexpr int32_t kBowReadyToAim = 2, kBowAim = 3, kBowFiring = 5;                     // BowState

void init_held(Il2CppClass* game_manager) {
    h.player_manager = il2::find_method(game_manager, "GetPlayerManagerComponent", 0);
    h.player_animation = il2::find_method(game_manager, "GetPlayerAnimationComponent", 0);
    h.in_hands = il2::field_offset(il2::find_class(kGame, "", "PlayerManager"), "m_ItemInHandsInternal");
    h.anim_state = il2::field_offset(il2::find_class(kGame, "", "PlayerAnimation"), "m_State");
    h.object_name = il2::find_method(il2::find_class(kCore, "UnityEngine", "Object"), "get_name", 0);
    h.sanitize = il2::find_method_params(il2::find_class(kGame, "", "Utils"), "SanitizePrefabName", {"System.String"});
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    h.torch = il2::field_offset(gear, "m_TorchItem");
    h.flare = il2::field_offset(gear, "m_FlareItem");
    h.lamp = il2::field_offset(gear, "m_KeroseneLampItem");
    h.flashlight = il2::field_offset(gear, "m_FlashlightItem");
    h.gun = il2::field_offset(gear, "m_GunItem");
    h.bow = il2::field_offset(gear, "m_BowItem");
    h.spear = il2::field_offset(gear, "m_BearSpearItem");
    h.stone = il2::field_offset(gear, "m_StoneItem");
    h.noise_maker = il2::field_offset(gear, "m_NoiseMakerItem");
    h.matches = il2::field_offset(gear, "m_MatchesItem");
    if (Il2CppClass* matches = il2::find_class(kGame, "", "MatchesItem")) {
        h.match_ignited = il2::field_offset(matches, "m_Ignited");
        h.match_igniting = il2::field_offset(matches, "m_IgniteAfterDelay");
    }
    h.flare_igniting = il2::field_offset(il2::find_class(kGame, "", "FlareItem"), "m_IgniteAfterDelay");
    h.torch_igniting = il2::field_offset(il2::find_class(kGame, "", "TorchItem"), "m_IgniteAfterDelay");
    Il2CppClass* flare = il2::find_class(kGame, "", "FlareItem");
    h.torch_burning = il2::find_method(il2::find_class(kGame, "", "TorchItem"), "IsBurning", 0);
    h.flare_burning = il2::find_method(flare, "IsBurning", 0);
    h.flare_type = il2::field_offset(flare, "m_Type");
    h.lamp_on = il2::find_method(il2::find_class(kGame, "TLD.Gear", "KeroseneLampItem"), "IsOn", 0);
    h.flashlight_state = il2::field_offset(il2::find_class(kGame, "", "FlashlightItem"), "m_State");
    h.gun_type = il2::field_offset(il2::find_class(kGame, "", "GunItem"), "m_GunType");
    h.spear_state = il2::field_offset(il2::find_class(kGame, "", "BearSpearItem"), "m_CurrentSpearState");
    h.bow_state = il2::field_offset(il2::find_class(kGame, "", "BowItem"), "m_BowState");
    if (!h.player_manager || h.in_hands < 0 || !h.object_name)
        logx::warn("game state: the item in the player's hands can't be read; the others won't see it");
}

bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }

Il2CppObject* part(Il2CppObject* gear, ptrdiff_t field) {
    Il2CppObject* p = field >= 0 ? il2::read_field<Il2CppObject*>(gear, field) : nullptr;
    return alive(p) ? p : nullptr;
}

bool says_yes(const MethodInfo* m, Il2CppObject* obj) {
    bool yes = false;
    return m && obj && il2::invoke_value(m, obj, yes) && yes;
}

bool g_ready = false;


// --- TLDCoop_DLC: health, for the others --------------------------------------------------------------------------

struct AfflictionCheck {
    const char* getter;     // static GameManager.Get<X>Component()
    const char* klass;      // its class
    const char* check;      // a bool method saying the player has it
    net::Affliction what;
    const MethodInfo* get = nullptr;
    const MethodInfo* has = nullptr;
    ptrdiff_t display = -1;  // LocalizedString m_LocalizedDisplayName, when it has one
};
AfflictionCheck g_checks[] = {
    {"GetHypothermiaComponent", "Hypothermia", "HasHypothermia", net::Affliction::Hypothermia},
    {"GetFrostbiteComponent", "Frostbite", "HasFrostbite", net::Affliction::Frostbite},
    {"GetFoodPoisoningComponent", "FoodPoisoning", "HasFoodPoisoning", net::Affliction::FoodPoisoning},
    {"GetDysenteryComponent", "Dysentery", "HasDysentery", net::Affliction::Dysentery},
    {"GetSprainedAnkleComponent", "SprainedAnkle", "HasSprainedAnkle", net::Affliction::SprainedAnkle},
    {"GetSprainedWristComponent", "SprainedWrist", "HasSprainedWrist", net::Affliction::SprainedWrist},
    {"GetBloodLossComponent", "BloodLoss", "HasBloodLoss", net::Affliction::BloodLoss},
    {"GetInfectionComponent", "Infection", "HasInfection", net::Affliction::Infection},
    {"GetInfectionRiskComponent", "InfectionRisk", "HasInfectionRisk", net::Affliction::InfectionRisk},
    {"GetBrokenRibComponent", "BrokenRib", "HasBrokenRib", net::Affliction::BrokenRib},
    {"GetBurnsComponent", "Burns", "HasBurns", net::Affliction::Burns},
    {"GetIntestinalParasitesComponent", "IntestinalParasites", "HasIntestinalParasites",
     net::Affliction::IntestinalParasites},
    {"GetCabinFeverComponent", "CabinFever", "HasCabinFever", net::Affliction::CabinFever},
    {"GetHungerComponent", "Hunger", "IsStarving", net::Affliction::Starving},
    {"GetThirstComponent", "Thirst", "IsDehydrated", net::Affliction::Dehydrated},
    {"GetFatigueComponent", "Fatigue", "IsExhausted", net::Affliction::Exhausted},
    {"GetFreezingComponent", "Freezing", "IsFreezing", net::Affliction::Freezing},
};
const MethodInfo* g_get_condition = nullptr;
ptrdiff_t g_hp = -1, g_max_hp = -1;
const MethodInfo* g_localized_text = nullptr;  // LocalizedString.Text()

void init_health(Il2CppClass* game_manager) {
    int found = 0;
    for (AfflictionCheck& c : g_checks) {
        Il2CppClass* k = il2::find_class(kGame, "", c.klass);
        c.get = il2::find_method_quiet(game_manager, c.getter, 0);
        c.has = k ? il2::find_method_quiet(k, c.check, 0) : nullptr;
        c.display = k ? il2::field_offset(k, "m_LocalizedDisplayName") : -1;
        if (c.get && c.has) ++found;
    }
    Il2CppClass* condition = il2::find_class(kGame, "", "Condition");
    g_get_condition = il2::find_method(game_manager, "GetConditionComponent", 0);
    g_hp = il2::field_offset(condition, "m_CurrentHP");
    g_max_hp = il2::field_offset(condition, "m_MaxHP");
    g_localized_text = il2::find_method(il2::find_class(kGame, "", "LocalizedString"), "Text", 0);
    logx::info("game state: %d of %zu afflictions can be read (the others see them)", found, std::size(g_checks));
}

}  // namespace

bool init() {
    Il2CppClass* game_manager = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* fps_camera = il2::find_class(kGame, "", "vp_FPSCamera");
    Il2CppClass* time_of_day = il2::find_class(kGame, "", "TimeOfDay");
    Il2CppClass* weather = il2::find_class(kGame, "", "Weather");
    Il2CppClass* wind = il2::find_class(kGame, "", "Wind");
    g.weather_stage_enum = il2::find_class(kGame, "", "WeatherStage");

    if (game_manager) g.active_scene = il2::il2cpp_class_get_field_from_name(game_manager, "m_ActiveScene");
    if (game_manager) g.transition = il2::il2cpp_class_get_field_from_name(game_manager, "m_SceneTransitionData");
    g.random_seed = il2::field_offset(il2::find_class(kGame, "", "SceneTransitionData"), "m_GameRandomSeed");
    if (Il2CppClass* saves = il2::find_class(kGame, "", "SaveGameSystem")) {
        g.save_name = il2::il2cpp_class_get_field_from_name(saves, "m_CurrentSaveName");
        g.game_id = il2::il2cpp_class_get_field_from_name(saves, "m_CurrentGameId");
    }
    if (game_manager)
        g.since_level_load = il2::il2cpp_class_get_field_from_name(game_manager, "m_SecondsSinceLevelLoad");
    g.get_player_transform = il2::find_method(game_manager, "GetPlayerTransform", 0);
    g.crouched = il2::find_method(il2::find_class(kGame, "", "PlayerManager"), "PlayerIsCrouched", 0);
    g.get_fps_camera = il2::find_method(game_manager, "GetVpFPSCamera", 0);
    g.get_time_of_day = il2::find_method(game_manager, "GetTimeOfDayComponent", 0);
    g.get_weather = il2::find_method(game_manager, "GetWeatherComponent", 0);
    g.get_wind = il2::find_method(game_manager, "GetWindComponent", 0);

    g.transform_position = il2::find_method(transform, "get_position", 0);
    g.transform_euler_angles = il2::find_method(transform, "get_eulerAngles", 0);

    g.camera_yaw = il2::field_offset(fps_camera, "m_Yaw");
    g.camera_pitch = il2::field_offset(fps_camera, "m_Pitch");

    g.tod_day = il2::find_method(time_of_day, "GetDayNumber", 0);
    g.tod_hour = il2::find_method(time_of_day, "GetHour", 0);
    g.tod_minutes = il2::find_method(time_of_day, "GetMinutes", 0);
    g.tod_normalized = il2::find_method(time_of_day, "GetNormalizedTime", 0);

    g.weather_stage = il2::find_method(weather, "GetWeatherStage", 0);
    g.weather_temperature = il2::find_method(weather, "GetCurrentTemperature", 0);
    g.weather_indoor = il2::find_method(weather, "IsIndoorEnvironment", 0);

    g.wind_speed = il2::find_method(wind, "GetSpeedMPH", 0);

    g.get_struggle = il2::find_method(game_manager, "GetPlayerStruggleComponent", 0);
    g.in_struggle = il2::find_method(il2::find_class(kGame, "", "PlayerStruggle"), "InStruggle", 0);
    init_held(game_manager);
    init_health(game_manager);

    g_ready = g.active_scene && g.get_player_transform && g.transform_position;
    logx::info("game state: %s", g_ready ? "ready" : "INCOMPLETE (see warnings above)");
    return g_ready;
}

std::string active_scene() {
    if (!g.active_scene) return {};
    Il2CppString* s = nullptr;
    il2::il2cpp_field_static_get_value(g.active_scene, &s);
    return il2::to_utf8(s);
}

std::string save_id() {
    if (!g.save_name || !g.game_id || !g.transition || g.random_seed < 0) return {};
    Il2CppString* name = nullptr;
    il2::il2cpp_field_static_get_value(g.save_name, &name);
    uint32_t id = 0;
    il2::il2cpp_field_static_get_value(g.game_id, &id);
    Il2CppObject* transition = nullptr;
    il2::il2cpp_field_static_get_value(g.transition, &transition);
    const std::string slot = name ? il2::to_utf8(name) : std::string();
    if (slot.empty() || !transition) return {};
    return slot + ":" + std::to_string(id) + ":" + std::to_string(il2::read_field<int32_t>(transition, g.random_seed));
}

float seconds_since_level_load() {
    if (!g.since_level_load) return -1;
    float seconds = -1;
    il2::il2cpp_field_static_get_value(g.since_level_load, &seconds);
    return seconds;
}

bool in_struggle() {
    Il2CppObject* struggle = g.get_struggle ? il2::invoke(g.get_struggle, nullptr) : nullptr;
    bool fighting = false;
    return struggle && il2::invoke_value(g.in_struggle, struggle, fighting) && fighting;
}

HeldState read_held() {
    HeldState out;
    Il2CppObject* pm = h.player_manager && h.in_hands >= 0 ? il2::invoke(h.player_manager, nullptr) : nullptr;
    Il2CppObject* gi = pm ? il2::read_field<Il2CppObject*>(pm, h.in_hands) : nullptr;
    if (!alive(gi)) return out;
    Il2CppObject* animation = h.player_animation ? il2::invoke(h.player_animation, nullptr) : nullptr;
    const int32_t shown = animation && h.anim_state >= 0 ? il2::read_field<int32_t>(animation, h.anim_state) : -1;
    if (shown == kAnimHidden) return out;  // put away for an interaction: their hands look empty

    // The prefab name, from the item's GameObject name ("GEAR_Torch(Clone)" -> "GEAR_Torch"). Asked again only when
    // another item comes into the hands.
    // TLDCoop_DLC: the item is held (a GC handle) while it's the one named, so another item can't come to its address
    // and be taken for it.
    static Il2CppObject* last_item = nullptr;
    static uintptr_t last_handle = 0;
    static std::string last_name;
    if (gi != last_item || !last_handle || il2::il2cpp_gchandle_get_target(last_handle) != gi) {
        if (last_handle) il2::il2cpp_gchandle_free(last_handle);
        last_handle = il2::il2cpp_gchandle_new(gi, false);
        last_item = gi;
        Il2CppObject* name = il2::invoke(h.object_name, gi);
        void* args[] = {name};
        Il2CppObject* clean = name && h.sanitize ? il2::invoke(h.sanitize, nullptr, args) : nullptr;
        last_name = il2::to_utf8(reinterpret_cast<Il2CppString*>(clean ? clean : name));
    }
    out.gear = last_name;

    using net::HeldKind;
    HeldKind kind = HeldKind::Other;
    if (Il2CppObject* torch = part(gi, h.torch)) {
        kind = HeldKind::Torch;
        if (says_yes(h.torch_burning, torch)) out.flags |= net::kHeldLit;
        else if (il2::read_field<bool>(torch, h.torch_igniting)) out.flags |= net::kHeldIgniting;
    } else if (Il2CppObject* flare = part(gi, h.flare)) {
        kind = il2::read_field<int32_t>(flare, h.flare_type) == net::kFlareBlue ? HeldKind::FlareBlue : HeldKind::FlareRed;
        if (says_yes(h.flare_burning, flare)) out.flags |= net::kHeldLit;
        else if (il2::read_field<bool>(flare, h.flare_igniting)) out.flags |= net::kHeldIgniting;
    } else if (Il2CppObject* match = part(gi, h.matches)) {  // TLDCoop_DLC
        kind = HeldKind::Match;
        if (il2::read_field<bool>(match, h.match_ignited)) out.flags |= net::kHeldLit;
        else if (il2::read_field<bool>(match, h.match_igniting)) out.flags |= net::kHeldIgniting;
    } else if (Il2CppObject* lamp = part(gi, h.lamp)) {
        kind = HeldKind::Lantern;
        if (says_yes(h.lamp_on, lamp)) out.flags |= net::kHeldLit;
    } else if (Il2CppObject* flashlight = part(gi, h.flashlight)) {
        kind = HeldKind::Flashlight;
        const int32_t state = il2::read_field<int32_t>(flashlight, h.flashlight_state);
        if (state == kFlashlightLow || state == kFlashlightHigh) out.flags |= net::kHeldLit;
        if (state == kFlashlightHigh) out.flags |= net::kHeldHigh;
    } else if (Il2CppObject* gun = part(gi, h.gun)) {
        const int32_t type = il2::read_field<int32_t>(gun, h.gun_type);
        kind = type == kGunRifle      ? HeldKind::Rifle
               : type == kGunRevolver ? HeldKind::Revolver
               : type == kGunFlareGun ? HeldKind::FlareGun
               : type == kGunCamera   ? HeldKind::Camera
                                      : HeldKind::Other;
    } else if (Il2CppObject* bow = part(gi, h.bow)) {  // a bow is drawn by its own states, not the player's aim
        kind = HeldKind::Bow;
        const int32_t state = h.bow_state >= 0 ? il2::read_field<int32_t>(bow, h.bow_state) : -1;
        if (state == kBowReadyToAim || state == kBowAim || state == kBowFiring) out.flags |= net::kHeldAiming;
    } else if (Il2CppObject* spear = part(gi, h.spear)) {
        kind = HeldKind::Spear;
        if (il2::read_field<int32_t>(spear, h.spear_state) == kSpearRaised) out.flags |= net::kHeldRaised;
    } else if (part(gi, h.stone)) {
        kind = HeldKind::Stone;
    } else if (part(gi, h.noise_maker)) {
        kind = HeldKind::NoiseMaker;
    }
    if (shown == kAnimToAiming || shown == kAnimAiming || shown == kAnimFromAiming || shown == kAnimFiring)
        out.flags |= net::kHeldAiming;
    out.kind = static_cast<uint8_t>(kind);
    return out;
}

bool is_gameplay_scene(const std::string& scene) {
    return !scene.empty() && scene != "MainMenu" && scene != "Boot" && scene != "Empty";
}

PlayerState read_player() {
    PlayerState p;
    if (!g_ready) return p;

    Il2CppObject* transform = il2::invoke(g.get_player_transform, nullptr);
    if (!transform) return p;
    Unity pos{};
    if (!il2::invoke_value(g.transform_position, transform, pos)) return p;
    p.position = {pos.x, pos.y, pos.z};

    Unity euler{};
    if (il2::invoke_value(g.transform_euler_angles, transform, euler)) p.body_yaw = euler.y;

    if (Il2CppObject* camera = il2::invoke(g.get_fps_camera, nullptr)) {
        p.yaw = il2::read_field<float>(camera, g.camera_yaw);
        p.pitch = il2::read_field<float>(camera, g.camera_pitch);
    }
    if (Il2CppObject* pm = h.player_manager ? il2::invoke(h.player_manager, nullptr) : nullptr)
        p.crouched = says_yes(g.crouched, pm);
    p.valid = true;
    return p;
}

WorldState read_world() {
    WorldState w;
    if (!g_ready) return w;

    if (Il2CppObject* tod = il2::invoke(g.get_time_of_day, nullptr)) {
        il2::invoke_value(g.tod_day, tod, w.day);
        il2::invoke_value(g.tod_hour, tod, w.hour);
        il2::invoke_value(g.tod_minutes, tod, w.minute);
        il2::invoke_value(g.tod_normalized, tod, w.normalized_time);
        w.valid = true;
    }
    if (Il2CppObject* weather = il2::invoke(g.get_weather, nullptr)) {
        int32_t stage = -1;
        if (il2::invoke_value(g.weather_stage, weather, stage)) {
            w.weather_stage = stage;
            w.weather_name = il2::enum_name(g.weather_stage_enum, stage);
        }
        il2::invoke_value(g.weather_temperature, weather, w.air_temp_c);
        il2::invoke_value(g.weather_indoor, weather, w.indoors);
    }
    if (Il2CppObject* wind = il2::invoke(g.get_wind, nullptr)) il2::invoke_value(g.wind_speed, wind, w.wind_mph);
    return w;
}

void read_health(uint8_t& condition, uint32_t& afflictions) {
    afflictions = 0;
    for (const AfflictionCheck& c : g_checks) {
        if (!c.get || !c.has) continue;
        Il2CppObject* comp = il2::invoke(c.get, nullptr);
        bool yes = false;
        if (comp && il2::invoke_value(c.has, comp, yes) && yes) afflictions |= net::affliction_bit(c.what);
    }
    Il2CppObject* cond = g_get_condition ? il2::invoke(g_get_condition, nullptr) : nullptr;
    const float hp = cond ? il2::read_field<float>(cond, g_hp) : 100.0f;
    const float max = cond ? il2::read_field<float>(cond, g_max_hp) : 100.0f;
    const float pct = max > 0 ? hp / max * 100.0f : 100.0f;
    condition = static_cast<uint8_t>(pct < 0 ? 0 : pct > 100 ? 100 : pct + 0.5f);
}

std::string affliction_display_name(int affliction) {
    for (const AfflictionCheck& c : g_checks) {
        if (static_cast<int>(c.what) != affliction || !c.get || c.display < 0 || !g_localized_text) continue;
        Il2CppObject* comp = il2::invoke(c.get, nullptr);
        Il2CppObject* ls = comp ? il2::read_field<Il2CppObject*>(comp, c.display) : nullptr;
        Il2CppObject* text = ls ? il2::invoke(g_localized_text, ls) : nullptr;
        return text ? il2::to_utf8(reinterpret_cast<Il2CppString*>(text)) : std::string();
    }
    return {};
}

}  // namespace game
