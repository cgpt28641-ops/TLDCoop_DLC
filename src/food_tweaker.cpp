#include "food_tweaker.h"

#include <cstring>
#include <string>
#include <unordered_map>

#include "coop_features.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"

// TLDCoop_DLC: Food Tweaker (built in, after the MelonLoader mod by GruffCassquatch, Phaedrus and Romain; Karol 06.11:
// "zróbmy Food Tweaker", with its author's permission). Its "realistic" values: calories of meat (per kg) and of the
// packaged foods, drinks and dishes, and how long meat takes to cook. Off (the game's own values) unless the host turns
// it on for everyone (the MODS page: FOOD TWEAKER). Fish keeps the game's values (the mod's fish weights are a separate
// system).
namespace food_tweaker {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";

struct Api {
    ptrdiff_t food = -1;            // GearItem.m_FoodItem
    ptrdiff_t cal_left = -1;        // FoodItem.m_CaloriesRemaining
    ptrdiff_t cal_total = -1;       // FoodItem.m_CaloriesTotal
    ptrdiff_t cooking = -1;         // CookingPotItem.m_GearItemBeingCooked
    ptrdiff_t warm_only = -1;       // CookingPotItem.m_CanOnlyWarmUpFood
    ptrdiff_t warm_mult = -1;       // CookingPotItem.m_NearFireWarmUpCookingTimeMultiplier
    const MethodInfo* cook_mult = nullptr;    // CookingPotItem.GetTotalCookMultiplier()
    const MethodInfo* object_name = nullptr;  // Object.get_name()
} api;
bool g_ready = false;

using AwakeFn = void (*)(Il2CppObject*, const MethodInfo*);
using CookTimeFn = float (*)(Il2CppObject*, const MethodInfo*);
AwakeFn g_awake = nullptr;
CookTimeFn g_cook_time = nullptr;

bool active() { return g_ready && coop_features::on(net::kFeatureFoodTweaker); }

std::string prefab_name(Il2CppObject* o) {
    std::string n = o ? il2::to_utf8(reinterpret_cast<Il2CppString*>(il2::invoke(api.object_name, o))) : std::string();
    if (const size_t p = n.find('('); p != std::string::npos) n.resize(p);  // "GEAR_X(Clone)"
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n;
}

// The mod's realistic calories: a whole item (packaged food, drinks, dishes) or a kilo of meat, cooked.
const std::unordered_map<std::string, float>& realistic() {
    static const std::unordered_map<std::string, float> m = {
        // meat (per kg cooked; the raw piece the same)
        {"GEAR_RawMeatBear", 1305}, {"GEAR_CookedMeatBear", 1305}, {"GEAR_RawMeatDeer", 1172}, {"GEAR_CookedMeatDeer", 1172},
        {"GEAR_RawMeatMoose", 1040}, {"GEAR_CookedMeatMoose", 1040}, {"GEAR_RawMeatPtarmigan", 980},
        {"GEAR_CookedMeatPtarmigan", 980}, {"GEAR_RawMeatRabbit", 932}, {"GEAR_CookedMeatRabbit", 932},
        {"GEAR_RawMeatWolf", 875}, {"GEAR_CookedMeatWolf", 875},
        // drinks
        {"GEAR_AcornCoffeeCup", 5}, {"GEAR_BirchbarkTea", 5}, {"GEAR_BurdockTea", 5}, {"GEAR_CoffeeCup", 5},
        {"GEAR_GreenTeaCup", 5}, {"GEAR_SodaOrange", 160}, {"GEAR_SodaEnergy", 115}, {"GEAR_ReishiTea", 5},
        {"GEAR_RoseHipTea", 5}, {"GEAR_SodaGrape", 170}, {"GEAR_Soda", 120},
        // other food
        {"GEAR_AcornCooked", 150}, {"GEAR_AcornCookedBig", 600}, {"GEAR_AirlineFoodChick", 620}, {"GEAR_AirlineFoodVeg", 560},
        {"GEAR_BeefJerky", 410}, {"GEAR_BurdockPrepared", 108}, {"GEAR_CandyBar", 585}, {"GEAR_CattailStalk", 15},
        {"GEAR_CondensedMilk", 815}, {"GEAR_DogFood", 425}, {"GEAR_EnergyBar", 500}, {"GEAR_GranolaBar", 300},
        {"GEAR_KetchupChips", 540}, {"GEAR_MapleSyrup", 920}, {"GEAR_MRE", 1200}, {"GEAR_PeanutButter", 3060},
        {"GEAR_PinnacleCanPeaches", 245}, {"GEAR_CannedBeans", 265}, {"GEAR_Crackers", 515}, {"GEAR_CannedSardines", 230},
        {"GEAR_TomatoSoupCan", 150},
        // dishes
        {"GEAR_CookedBannockAcorn", 327}, {"GEAR_CookedPancakeAcorn", 808}, {"GEAR_CookedBannock", 427}, {"GEAR_Broth", 48},
        {"GEAR_CookedPorridgeFruit", 560}, {"GEAR_CannedCorn", 273}, {"GEAR_CannedHam", 480}, {"GEAR_Carrot", 41},
        {"GEAR_CookedFishcakes", 230}, {"GEAR_PotatoCooked", 131}, {"GEAR_CookedPieFishermans", 1420},
        {"GEAR_CookedPancakePeach", 1053}, {"GEAR_CookedPancake", 708}, {"GEAR_CookedPiePeach", 367},
        {"GEAR_CookedPorridge", 96}, {"GEAR_CookedPieForagers", 1205}, {"GEAR_CookedPieRoseHip", 328},
        {"GEAR_CookedPiePredator", 1745}, {"GEAR_CookedStewVegetables", 994},
    };
    return m;
}

// Raw meat: minutes per kg (the mod's realistic cooking times)
float meat_minutes(const std::string& n) {
    if (n == "GEAR_RawMeatBear" || n == "GEAR_RawMeatWolf") return 35;
    if (n == "GEAR_RawMeatDeer") return 20;
    if (n == "GEAR_RawMeatMoose") return 25;
    if (n == "GEAR_RawMeatPtarmigan" || n == "GEAR_RawMeatRabbit") return 28;
    return 0;
}

// An item's food, its calories as the mod has them: the whole (or a kilo of meat) at the mod's value, what's left in
// the same share as before.
void awake_detour(Il2CppObject* self, const MethodInfo* m) {
    g_awake(self, m);
    if (!active() || !self) return;
    Il2CppObject* food = il2::read_field<Il2CppObject*>(self, api.food);
    if (!food) return;
    const auto& table = realistic();
    const auto it = table.find(prefab_name(self));
    if (it == table.end()) return;
    float* left = reinterpret_cast<float*>(reinterpret_cast<char*>(food) + api.cal_left);
    float* total = reinterpret_cast<float*>(reinterpret_cast<char*>(food) + api.cal_total);
    if (!(*total > 0)) return;
    const float share = *left / *total;
    *total = it->second;
    *left = share * it->second;
    static int told = 0;
    if (told++ < 5) logx::info("food tweaker: %s now %.0f calories", it->first.c_str(), it->second);
}

// Raw meat on a fire: the mod's minutes per kg for what's left of the piece, times the fire's (or the warm-up's) rate.
float cook_time_detour(Il2CppObject* self, const MethodInfo* m) {
    const float game = g_cook_time(self, m);
    if (!active() || !self) return game;
    Il2CppObject* gear = il2::read_field<Il2CppObject*>(self, api.cooking);
    if (!gear) return game;
    const float per_kg = meat_minutes(prefab_name(gear));
    Il2CppObject* food = per_kg > 0 ? il2::read_field<Il2CppObject*>(gear, api.food) : nullptr;
    if (!food) return game;
    const float total = il2::read_field<float>(food, api.cal_total);
    const float share = total > 0 ? il2::read_field<float>(food, api.cal_left) / total : 1.0f;
    float mult = 1;
    if (il2::read_field<bool>(self, api.warm_only)) mult = il2::read_field<float>(self, api.warm_mult);
    else il2::invoke_value(api.cook_mult, self, mult);
    return per_kg * share * mult;
}

}  // namespace

bool init() {
    Il2CppClass* gear = il2::find_class(kGame, "", "GearItem");
    Il2CppClass* food = il2::find_class(kGame, "", "FoodItem");
    Il2CppClass* pot = il2::find_class(kGame, "", "CookingPotItem");
    api.object_name = il2::find_method(il2::find_class(kCore, "UnityEngine", "Object"), "get_name", 0);
    api.food = il2::field_offset(gear, "m_FoodItem");
    api.cal_left = il2::field_offset(food, "m_CaloriesRemaining");
    api.cal_total = il2::field_offset(food, "m_CaloriesTotal");
    api.cooking = il2::field_offset(pot, "m_GearItemBeingCooked");
    api.warm_only = il2::field_offset(pot, "m_CanOnlyWarmUpFood");
    api.warm_mult = il2::field_offset(pot, "m_NearFireWarmUpCookingTimeMultiplier");
    api.cook_mult = il2::find_method(pot, "GetTotalCookMultiplier", 0);
    const MethodInfo* awake = il2::find_method(gear, "Awake", 0);
    const MethodInfo* cook_time = il2::find_method(pot, "ModifiedCookTimeMinutes", 0);
    g_ready = api.object_name && api.food >= 0 && api.cal_left >= 0 && api.cal_total >= 0 && api.cooking >= 0 &&
              api.warm_only >= 0 && api.warm_mult >= 0 && api.cook_mult && awake && cook_time &&
              hooks::install_method("GearItem.Awake (food tweaker)", awake, reinterpret_cast<void*>(&awake_detour),
                                    reinterpret_cast<void**>(&g_awake)) &&
              hooks::install_method("CookingPotItem.ModifiedCookTimeMinutes (food tweaker)", cook_time,
                                    reinterpret_cast<void*>(&cook_time_detour), reinterpret_cast<void**>(&g_cook_time));
    logx::info("food tweaker: %s", g_ready ? "ready (off unless the host turns it on: MODS)" : "INCOMPLETE (see warnings above)");
    return g_ready;
}

void set_settings(const config::Settings& s) { coop_features::set_own(net::kFeatureFoodTweaker, s.food_tweaker); }

}  // namespace food_tweaker
