#pragma once
// TLDCoop_DLC: Ambient Lights' arithmetic, without the game: the light sets of AmbientLights (the MelonLoader mod by
// Xpazeman, maintained by Thekillergreece; MIT licence, third_party/ambient-lights/LICENSE), ported from its
// LightConfig.cs / ALUtils.cs. ambient_lights.cpp feeds it the game's time, weather and colours.

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ambient {

struct Rgb {
    float r = 0, g = 0, b = 0, a = 1;
};
struct Hsv {
    float h = 0, s = 0, v = 0, a = 1;  // h in degrees, as TLD's ColorHSV
};

inline float lerp(float a, float b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return a + (b - a) * t;
}
inline Rgb lerp(const Rgb& a, const Rgb& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

inline Hsv to_hsv(const Rgb& c) {
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
    Hsv o{0, 0, mx, c.a};
    if (mx <= 0) return o;
    o.s = d / mx;
    if (d <= 0) return o;
    if (mx == c.r) o.h = 60.0f * std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g) o.h = 60.0f * ((c.b - c.r) / d + 2.0f);
    else o.h = 60.0f * ((c.r - c.g) / d + 4.0f);
    if (o.h < 0) o.h += 360.0f;
    return o;
}

inline Rgb to_rgb(const Hsv& c) {
    const float s = std::max(c.s, 0.0f), v = c.v;
    if (s <= 0) return {v, v, v, c.a};
    float h = std::fmod(c.h, 360.0f);
    if (h < 0) h += 360.0f;
    h /= 60.0f;
    const int i = static_cast<int>(std::floor(h)) % 6;
    const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) {
        case 0: return {v, t, p, c.a};
        case 1: return {q, v, p, c.a};
        case 2: return {p, v, t, c.a};
        case 3: return {p, q, v, c.a};
        case 4: return {t, p, v, c.a};
        default: return {v, p, q, c.a};
    }
}

// Unity's Color -> Color32 -> Color (what assigning a Color32 field does to it).
inline Rgb to_bytes(const Rgb& c) {
    auto q = [](float x) { return std::round(std::clamp(x, 0.0f, 1.0f) * 255.0f) / 255.0f; };
    return {q(c.r), q(c.g), q(c.b), q(c.a)};
}

// --- the data (src/ambient_data.inc, made from the mod's JSON by tools/ambient/gen_data.py) ---------------------

enum Orientation { kNorth = 0, kSouth = 1, kEast = 2, kWest = 3, kDefault = 4 };

struct OrientSet {
    float sun[2];  // how much of the sun's colour (vs the fog's) at the period's start and end
    float hue[2];  // hue shift, degrees
    bool has_hue;
};
struct PeriodSet {
    const char* name;
    OrientSet orient[4];
    float intensity[2], range[2], sun_strength[2];
};
struct WeatherSet {
    const char* name;
    float s, v, r, g, b, intensity, range;
};
struct Emitter {
    float x, y, z;
    int orientation;  // Orientation
    float size, cover;
};
struct SceneLights {
    const char* scene;
    int first, count;  // kEmitters[first .. first+count)
    float intensity, range, ambience;  // the scene's options: intensity_, range_, ambient_intensity_multiplier
};

#include "ambient_data.inc"

inline const PeriodSet* find_period(const char* name) {
    for (const PeriodSet& p : kPeriods)
        if (std::strcmp(p.name, name) == 0) return &p;
    for (const PeriodSet& p : kPeriods)
        if (std::strcmp(p.name, "default") == 0) return &p;
    return nullptr;
}
inline WeatherSet find_weather(const char* name) {
    for (const WeatherSet& w : kWeathers)
        if (std::strcmp(w.name, name) == 0) return w;
    for (const WeatherSet& w : kWeathers)
        if (std::strcmp(w.name, "default") == 0) return w;
    return {"default", 1, 1, 1, 1, 1, 1, 1};
}
inline const SceneLights* find_scene(const char* scene) {
    for (const SceneLights& s : kScenes)
        if (std::strcmp(s.scene, scene) == 0) return &s;
    return nullptr;
}

// TLD's TODBlendState -> the mod's period names.
inline const char* period_of_blend(int tod_blend_state) {
    switch (tod_blend_state) {
        case 0: return "early_dawn";  // NightEndToDawn
        case 1: return "dawn";        // DawnToMorning
        case 2: return "morning";     // MorningToMidday
        case 3: return "afternoon";   // MiddayToAfternoon
        case 4: return "dusk";        // AfternoonToDusk
        case 5: return "after_dusk";  // DuskToNightStart
        case 6: return "night";       // NightStartToNightEnd
        default: return "default";
    }
}

// TLD's WeatherStage -> the mod's weather names.
inline const char* weather_of_stage(int stage) {
    switch (stage) {
        case 8: return "aurora";  // ClearAurora
        case 4: return "clear";
        case 3: return "partlycloudy";
        case 5: return "cloudy";
        case 6: return "lightfog";
        case 0: return "densefog";
        case 1: return "lightsnow";
        case 2: return "heavysnow";
        case 7: return "blizzard";
        default: return "default";
    }
}

// ALUtils.GetShadowStrength: how much direct sun a weather lets through.
inline float shadow_strength(const char* w) {
    if (!std::strcmp(w, "partlycloudy")) return 0.6f;
    if (!std::strcmp(w, "cloudy") || !std::strcmp(w, "lightfog")) return 0.1f;
    if (!std::strcmp(w, "densefog") || !std::strcmp(w, "lightsnow") || !std::strcmp(w, "heavysnow") ||
        !std::strcmp(w, "blizzard"))
        return 0.0f;
    return 1.0f;
}

// --- the levels (AmbLitSettings; coop.ini [ambient]) -----------------------------------------------------------
struct Levels {
    float intensity = 1.1f, range = 1.0f;
    int night = 1;
    float ambience = 0.8f, fill = 0.7f, fill_color = 0.5f, aurora_intensity = 0.8f, aurora_saturation = 1.4f;
};

// What the game says now.
struct Now {
    int blend_state = 6;      // UniStormWeatherSystem.GetTODBlendState()
    float blend_pct = 1;      // GetTODBlendPercent(that)
    int weather = 11;         // Weather.GetWeatherStage()
    int previous_weather = 11;  // UniStormWeatherSystem.m_PreviousWeatherStage
    float weather_pct = 1;    // m_SecondsSinceLastWeatherChange / m_WeatherTransitionTime, clamped
    Rgb sun, fog;             // the active TODStateData's m_SunLight, m_FogColor
    float aurora_fade = 0;    // AuroraManager.GetNormalizedAlphaSquare()
    Rgb aurora_colour;        // GetAuroraColour()
    bool aurora_cinematic = false;  // IsUsingCinematicColours()
    float aurora_level = 0;   // m_NormalizedActive / m_FullyActiveValue, clamped
    float flicker = 0;        // the cloud flicker (Flicker below)
};

// A light set (LightConfig.LightSet): what every window light and TLD's own lights are given now.
struct LightSet {
    const char* period = "default";
    float period_pct = 1;
    float shadow = 1, lightshaft = 1, sun = 1, window_mod = 1, intensity_mod = 1, range_mod = 1;
    Rgb ambient_day, ambient_night, window, lightshaft_colour{1, 1, 1, 1};
    Rgb colour[5];  // by Orientation (kDefault: the fog's colour)
    float intensity[5], range[5];
};

struct WeatherMix {
    float s, v, r, g, b, intensity, range;
};
inline WeatherMix weather_mix(const char* cur, const char* prev, float pct) {
    const WeatherSet c = find_weather(cur), p = find_weather(prev);
    if (pct >= 1) return {c.s, c.v, c.r, c.g, c.b, c.intensity, c.range};
    return {lerp(p.s, c.s, pct), lerp(p.v, c.v, pct), lerp(p.r, c.r, pct), lerp(p.g, c.g, pct),
            lerp(p.b, c.b, pct), lerp(p.intensity, c.intensity, pct), lerp(p.intensity, c.intensity, pct)};
}

// LightConfig.ApplyWeatherMod.
inline Rgb apply_weather(Rgb c, const WeatherMix& w) {
    c.r *= w.r;
    c.g *= w.g;
    c.b *= w.b;
    Hsv h = to_hsv(c);
    h.s *= w.s;
    h.v *= w.v;
    Rgb o = to_rgb(h);
    o.a = 1;
    return o;
}

// LightConfig.GetCurrentLightSet.
inline LightSet light_set(const Now& n, const Levels& lv) {
    LightSet ls;
    ls.period = period_of_blend(n.blend_state);
    ls.period_pct = n.blend_pct;
    const char* cur = weather_of_stage(n.weather);
    const char* prev = weather_of_stage(n.previous_weather);
    const float wp = std::clamp(n.weather_pct, 0.0f, 1.0f);
    const WeatherMix wm = weather_mix(cur, prev, wp);
    const PeriodSet* prd = find_period(ls.period);
    const float t = n.blend_pct;

    Rgb base_sun = n.sun, base_fog = n.fog;
    base_sun.a = base_fog.a = 1;
    if (std::fabs(n.aurora_fade) > 0.0001f) {
        Hsv a = to_hsv(n.aurora_colour);
        if (!n.aurora_cinematic) {
            a.s *= lv.aurora_saturation;
            a.v *= lv.aurora_intensity;
        }
        const Rgb am = to_rgb(a);
        base_sun = lerp(n.sun, am, n.aurora_level);
        base_fog = lerp(n.fog, am, n.aurora_level);
    }

    float strength = shadow_strength(cur);
    if (wp < 1) strength = lerp(shadow_strength(prev), strength, wp);
    ls.intensity_mod = wm.intensity + n.flicker;
    ls.range_mod = wm.range;
    ls.shadow = strength * (std::strcmp(ls.period, "night") == 0 ? 0.5f : 1.0f);
    ls.lightshaft = strength;
    const float sun_strength = prd ? lerp(prd->sun_strength[0], prd->sun_strength[1], t) : 1.0f;
    ls.sun = (strength + n.flicker * 0.4f) * sun_strength;

    Hsv shaft = to_hsv(base_sun);
    shaft.v = 0.8f;
    ls.lightshaft_colour = apply_weather(to_rgb(shaft), wm);

    const Rgb b = apply_weather(base_sun, wm);
    Hsv day = to_hsv(b);
    day.s *= 0.5f;
    day.v = 0.4f;
    Hsv night = day;
    night.v = 0.01f;
    ls.ambient_day = to_rgb(day);
    ls.ambient_night = to_rgb(night);

    Hsv win = to_hsv(b);
    win.s *= std::min(wm.s - 0.2f, 0.4f);
    win.v *= (ls.intensity_mod + 0.5f) + n.flicker;
    ls.window = to_rgb(win);
    ls.window_mod = ls.intensity_mod;

    float base_int = 1, base_rng = 10;
    for (int d = 0; d < 4; ++d) {
        Rgb c = base_fog;
        if (prd) {
            const OrientSet& o = prd->orient[d];
            c = lerp(base_fog, base_sun, lerp(o.sun[0], o.sun[1], t));
            if (o.has_hue) {
                Hsv h = to_hsv(c);
                h.h += lerp(o.hue[0], o.hue[1], t);
                c = to_rgb(h);
            }
            c = apply_weather(c, wm);
            base_int = lerp(prd->intensity[0], prd->intensity[1], t);
            base_rng = lerp(prd->range[0], prd->range[1], t);
        }
        ls.colour[d] = to_bytes(c);
        ls.intensity[d] = base_int;
        ls.range[d] = base_rng;
    }
    ls.colour[kDefault] = to_bytes(base_fog);
    ls.intensity[kDefault] = base_int;
    ls.range[kDefault] = base_rng;
    return ls;
}

// ALUtils.GetIntensityNightMod: from dusk into the night (and out of it at dawn) towards the night's level.
inline float night_intensity_mod(const Levels& lv, int blend_state, float blend_pct) {
    const float night = lv.night == 0 ? 0.0f : lv.night == 2 ? 1.5f : lv.night == 3 ? 1.8f : 1.0f;
    if (blend_state == 5) return lerp(1.0f, night, blend_pct);  // DuskToNightStart
    if (blend_state == 0) return lerp(night, 1.0f, blend_pct);  // NightEndToDawn
    return 1.0f;
}
inline float night_range_mod(const Levels& lv) { return lv.night == 0 ? 0.0f : lv.night == 2 ? 1.5f : lv.night == 3 ? 2.0f : 1.0f; }

constexpr float kGlobalIntensity = 0.9f, kGlobalRange = 1.0f, kGlobalAmbience = 0.5f;  // AmbientLights.global*

// One window light's intensity / range / colour (AmbLight.SetLightParams with ALUtils.Get*Modifier).
struct WindowLight {
    bool on = false;
    float intensity = 0, range = 0;
    Rgb colour;
};
inline WindowLight window_light(const LightSet& ls, const Levels& lv, const SceneLights& scene, const Emitter& e,
                                int blend_state, float blend_pct) {
    const int o = e.orientation >= 0 && e.orientation <= 4 ? e.orientation : kDefault;
    float imod = lv.intensity * scene.intensity * ls.intensity_mod * kGlobalIntensity;
    float rmod = lv.range * scene.range * ls.range_mod * kGlobalRange;
    if (std::strcmp(ls.period, "night") == 0) {
        imod *= night_intensity_mod(lv, blend_state, blend_pct);
        rmod *= night_range_mod(lv);
    }
    WindowLight w;
    const float intensity = ls.intensity[o] * imod;
    w.on = intensity > 0;
    w.intensity = intensity * (1.0f - e.cover);
    w.range = ls.range[o] * rmod * e.size;
    Hsv h = to_hsv(ls.colour[o]);
    h.s = std::min(h.s, 0.8f);
    w.colour = to_rgb(h);
    return w;
}

// GameLights.UpdateAmbience: TLD's indoor ambient light's multiplier.
inline float ambience_multiplier(const LightSet& ls, const Levels& lv, const SceneLights& scene, int blend_state,
                                 float blend_pct) {
    return lv.ambience * ls.intensity_mod * scene.ambience * kGlobalAmbience *
           night_intensity_mod(lv, blend_state, blend_pct);
}

// GameLights.UpdateFillLights: one of TLD's fill lights, from its own colour.
inline Rgb fill_colour(const Rgb& original, const Levels& lv) {
    Hsv h = to_hsv(original);
    h.s *= lv.fill_color;
    h.v *= lv.fill;
    return to_rgb(h);
}

}  // namespace ambient
