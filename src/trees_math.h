#pragma once
// TLDCoop_DLC: Dynamic Trees' snow arithmetic, without the game. Ported from DynamicTrees (Fuar, The Illusion,
// Jods-Its; https://github.com/Fuar11/DynamicTrees), built in with its authors' permission:
// DynamicTreeData.CalculateAccumulationAmountPerHour / SetState and TextureHelper.GetTextureBasedOnWeather.

#include <algorithm>

namespace trees {

// TLD's WeatherStage and WindStrength values.
enum Weather { kDenseFog = 0, kLightSnow = 1, kHeavySnow = 2, kPartlyCloudy = 3, kClear = 4, kCloudy = 5,
               kLightFog = 6, kBlizzard = 7, kClearAurora = 8, kToxicFog = 9, kElectrostaticFog = 10 };
enum Wind { kCalm = 0, kSlightlyWindy = 1, kWindy = 2, kVeryWindy = 3, kWindBlizzard = 4 };

// The snow thresholds (0-100) between the textures' nine states.
constexpr float kThresholds[] = {0, 5, 15, 40, 65, 80, 85, 95, 100};

// The trees' snow per game hour: snowy weather adds (more with wind), dry weather takes it off with wind (and
// above 80 by itself).
inline float rate_per_hour(int weather, int wind, float current) {
    switch (weather) {
        case kLightSnow: {
            static const float add[] = {0, 1, 3, 5, 7};
            return 3 + (wind >= 0 && wind <= 4 ? add[wind] : 0);
        }
        case kHeavySnow: {
            static const float add[] = {0, 3, 6, 8, 10};
            return 7 + (wind >= 0 && wind <= 4 ? add[wind] : 0);
        }
        case kBlizzard: return 30;
        case kDenseFog: case kPartlyCloudy: case kClear: case kCloudy: case kLightFog: case kClearAurora:
        case kElectrostaticFog: {
            static const float take[] = {0, 2, 5, 10, 20};
            return (current >= 80 ? -10.0f : 0.0f) - (wind >= 0 && wind <= 4 ? take[wind] : 0);
        }
        default: return 0;  // ToxicFog and the like: as the mod (no change)
    }
}

inline float accumulate(float current, int weather, int wind, float hours) {
    return std::clamp(current + rate_per_hour(weather, wind, current) * hours, 0.0f, 100.0f);
}

// A new game's (or an unknown save's) trees: as the weather it starts in.
inline float starting(int weather) {
    switch (weather) {
        case kLightSnow: return 15;
        case kHeavySnow: return 65;
        case kBlizzard: return 85;
        default: return 0;
    }
}

// 0 clear ... 8 full.
inline int state_of(float snow) {
    int s = 0;
    for (int i = 1; i < 9; ++i)
        if (snow >= kThresholds[i]) s = i;
    return s;
}

// The mod's texture sets, mildest first (index 8 is TLD's own all-white texture, which a material may start with).
constexpr const char* kPine[] = {"TRN_TreeBarkPine_Clear", "TRN_TreeBarkPine_Lowest", "TRN_TreeBarkPine_Low",
                                 "TRN_TreeBarkPine_LowMedium", "TRN_TreeBarkPine_Medium",
                                 "TRN_TreeBarkPine_HighMedium", "TRN_TreeBarkPine_High",
                                 "TRN_TreeBarkPine_Highest", "TRN_TreeBarkPine_Snow_A"};
constexpr const char* kCedar[] = {"TRN_TreeBarkCedar_Clear", "TRN_TreeBarkCedar_Low", "TRN_TreeBarkCedar_LowMedium",
                                  "TRN_TreeBarkCedar_Medium", "TRN_TreeBarkCedar_HighMedium",
                                  "TRN_TreeBarkCedar_High", "TRN_TreeBarkCedar_Highest", "TRN_TreeBarkCedar_Highest",
                                  "TRN_TreeCedarBark_Snow_B"};

// Which of a set (0-7) for this much snow; `r` in [0, 1) picks the mod's random neighbour, so the trees of one
// region don't all look alike.
inline int texture_index(float snow, float r) {
    r = std::clamp(r, 0.0f, 0.9999f);
    const int s = state_of(snow);
    if (s == 0) return static_cast<int>(r * 2);            // 0 or 1
    if (s >= 8) return 7;
    if (s == 7) return 7 + static_cast<int>(r * 2) - 2;    // 5 or 6
    return s + static_cast<int>(r * 3) - 1;                // s-1 .. s+1
}

// TLDCoop_DLC: every snow set shown again (04.11 the snowiest were capped at Medium while the textures were flat
// white; the bundle's own are realistic now: snow in clumps, green and bark between, tools/trees/realistic.py).
inline int shown_index(float snow, float r) { return texture_index(snow, r); }

}  // namespace trees
