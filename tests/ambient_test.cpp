// TLDCoop_DLC: Ambient Lights' arithmetic (src/ambient_math.h) and its data, and the Features message. Exit code 0
// when all pass.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ambient_math.h"
#include "net/protocol.h"

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }
bool near(const ambient::Rgb& a, const ambient::Rgb& b, float eps = 0.002f) {
    return near(a.r, b.r, eps) && near(a.g, b.g, eps) && near(a.b, b.b, eps);
}
}  // namespace

int main() {
    using namespace ambient;

    // HSV both ways, hue in degrees like TLD's ColorHSV
    const Rgb orange{1.0f, 0.5f, 0.0f, 1};
    const Hsv h = to_hsv(orange);
    expect(near(h.h, 30) && near(h.s, 1) && near(h.v, 1), "orange is 30 degrees, full saturation and value");
    expect(near(to_rgb(h), orange), "and back");
    Hsv shifted = h;
    shifted.h -= 40;  // past 0: wraps round to 350
    expect(near(to_rgb(shifted), to_rgb(Hsv{350, 1, 1, 1})), "a hue shift below 0 wraps round");
    expect(near(to_rgb(Hsv{0, 0, 0.4f, 1}), Rgb{0.4f, 0.4f, 0.4f, 1}), "no saturation is grey");

    // the data compiled in
    const SceneLights* camp = find_scene("CampOffice");
    expect(camp && camp->count == 8, "CampOffice has its 8 window lights");
    expect(camp && near(camp->range, 1.1f), "and its range multiplier");
    expect(find_scene("NoSuchScene") == nullptr, "a scene without data has none");
    expect(find_period("night") && std::strcmp(find_period("night")->name, "night") == 0, "the night's set");
    expect(find_period("midday") && std::strcmp(find_period("midday")->name, "default") == 0,
           "a period without a set falls back to default");
    expect(near(find_weather("cloudy").b, 1.2f), "cloudy weather is bluer");
    expect(std::strcmp(period_of_blend(2), "morning") == 0 && std::strcmp(period_of_blend(6), "night") == 0,
           "TLD's blend states name the mod's periods");
    expect(std::strcmp(weather_of_stage(8), "aurora") == 0 && std::strcmp(weather_of_stage(11), "default") == 0,
           "TLD's weather stages name the mod's weathers");

    // a clear morning: the east windows bright and warm, light shafts full
    Now n;
    n.blend_state = 2;  // MorningToMidday
    n.blend_pct = 0.0f;
    n.weather = n.previous_weather = 4;  // Clear
    n.sun = {1.0f, 0.9f, 0.7f, 1};
    n.fog = {0.5f, 0.6f, 0.7f, 1};
    Levels lv;
    LightSet ls = light_set(n, lv);
    expect(std::strcmp(ls.period, "morning") == 0, "the period");
    expect(near(ls.lightshaft, 1) && near(ls.shadow, 1), "clear: full light shafts");
    expect(near(ls.intensity[kEast], 1) && near(ls.range[kEast], 9), "morning's start: intensity 1, range 9");
    expect(ls.colour[kEast].r > ls.colour[kNorth].r, "the east (sun 0.9) warmer than the north (0.4)");
    expect(near(ls.colour[kDefault], to_bytes(n.fog)), "lights without an orientation get the fog's colour");

    // the same morning in a blizzard: no shafts, dimmer windows
    n.weather = n.previous_weather = 7;  // Blizzard
    LightSet storm = light_set(n, lv);
    expect(near(storm.lightshaft, 0), "blizzard: no light shafts");
    expect(to_hsv(storm.colour[kEast]).v < to_hsv(ls.colour[kEast]).v, "blizzard: the windows' light darker");

    // a change of weather blends: halfway from clear to cloudy
    n.weather = 5;  // Cloudy
    n.previous_weather = 4;
    n.weather_pct = 0.5f;
    expect(near(light_set(n, lv).lightshaft, 0.55f), "halfway to cloudy: shafts at (1 + 0.1) / 2");

    // one window light: cover dims it, size widens it
    n.weather = n.previous_weather = 4;
    n.weather_pct = 1;
    ls = light_set(n, lv);
    SceneLights scene{"Test", 0, 1, 1.0f, 1.0f, 1.0f};
    Emitter open{0, 0, 0, kEast, 1.0f, 0.0f}, covered{0, 0, 0, kEast, 2.0f, 0.5f};
    const WindowLight a = window_light(ls, lv, scene, open, n.blend_state, n.blend_pct);
    const WindowLight b = window_light(ls, lv, scene, covered, n.blend_state, n.blend_pct);
    expect(a.on && near(a.intensity, 1.0f * 1.1f * 0.9f), "intensity = set x levels (1.1) x the mod's 0.9");
    expect(near(b.intensity, a.intensity * 0.5f), "half covered: half as bright");
    expect(near(b.range, a.range * 2), "twice the size: twice the reach");
    expect(to_hsv(a.colour).s <= 0.8f + 1e-4f, "saturation held at 0.8 at most");
    lv.intensity = 0;
    expect(!window_light(ls, lv, scene, open, n.blend_state, n.blend_pct).on, "intensity 0: the lights go out");
    lv = Levels{};

    // nights: game default (0) dark, endless day (3) bright as they come in
    lv.night = 0;
    expect(near(night_intensity_mod(lv, 5, 1.0f), 0), "game-default nights: dark by night start");
    lv.night = 3;
    expect(near(night_intensity_mod(lv, 5, 1.0f), 1.8f), "endless day: brighter by night start");
    expect(near(night_intensity_mod(lv, 2, 0.5f), 1), "daytime: no night change");
    lv = Levels{};

    // TLD's fill lights: less of their colour, dimmer
    const Rgb fill = fill_colour(Rgb{0, 1, 0, 1}, lv);
    expect(near(to_hsv(fill).s, 0.5f) && near(to_hsv(fill).v, 0.7f), "fill: saturation x0.5, value x0.7");

    // the Features message
    net::Writer w(net::Msg::Features, 64);
    net::write_features(w, net::kFeatureAmbientLights | net::kFeatureDynamicTrees, 42.5f);
    net::Packet p;
    p.type = net::Msg::Features;
    p.data.assign(w.data(), w.data() + w.size());
    net::Reader r = net::packet_body(p);
    uint32_t bits = 0;
    float snow = 0;
    expect(w.ok() && net::read_features(r, bits, snow) &&
               bits == (net::kFeatureAmbientLights | net::kFeatureDynamicTrees) && snow == 42.5f,
           "Features reads back");
    {  // TLDCoop_DLC: Seamless Interiors' bit (8) comes through with the others
        net::Writer w2(net::Msg::Features, 64);
        const uint32_t all = net::kFeatureAmbientLights | net::kFeatureDynamicTrees | net::kFeatureSeamlessInteriors;
        net::write_features(w2, all, 0.0f);
        net::Packet p2;
        p2.type = net::Msg::Features;
        p2.data.assign(w2.data(), w2.data() + w2.size());
        net::Reader r2 = net::packet_body(p2);
        uint32_t bits2 = 0;
        float snow2 = -1;
        expect(net::kFeatureSeamlessInteriors == 8 && w2.ok() && net::read_features(r2, bits2, snow2) &&
                   bits2 == all && (bits2 & net::kFeatureSeamlessInteriors) && snow2 == 0.0f,
               "Features: Seamless Interiors' bit 8 reads back");
    }

    printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
