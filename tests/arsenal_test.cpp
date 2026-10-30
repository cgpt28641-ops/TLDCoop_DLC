// TLDCoop_DLC: Adaptive Arsenal's numbers (src/arsenal_math.h). Exit code 0 when all pass.

#include <cmath>
#include <cstdio>

#include "arsenal_math.h"

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
bool near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) < e; }
}  // namespace

int main() {
    using namespace arsenal;
    expect(muzzle_velocity("GEAR_Rifle_Barbs(Clone)", kRifle) == 400, "Barbs' rifle: 800 m/s, halved");
    expect(muzzle_velocity("GEAR_Rifle", kRifle) == 400 && muzzle_velocity("GEAR_Rifle_Traders", kRifle) == 550,
           "the plain rifle and the trader's");
    expect(muzzle_velocity("GEAR_RevolverStubNosed", kRevolver) == 150 &&
               muzzle_velocity("GEAR_Revolver", kRevolver) == 200,
           "revolvers: the stub-nosed first, then the plain one");
    expect(muzzle_velocity("GEAR_SomethingNew", kRifle) == 400, "an unknown gun: its kind's usual");
    expect(revolver_range(0) == 40 && revolver_range(4) == 100 && revolver_range(9) == 100, "revolver ranges by tier");
    expect(near(accuracy(100, false, false, false), 120), "crouched and still: 1.2x");
    expect(near(accuracy(100, true, true, true), 100 * 1.2f * 0.7f * 0.8f * 0.9f), "hip, standing, moving");
    expect(spread_degrees(120) == 0 && near(spread_degrees(50), 5) && spread_degrees(-5) == 10, "spread 0-10 degrees");
    expect(damage_share(0) == 1 && near(damage_share(250), 0.55f) && near(damage_share(900), 0.1f), "damage drop-off");
    const V3 f{0, 0, 1};
    const V3 r = turned(f, 0, 10);
    expect(r.x > 0.17f && r.x < 0.18f && near(length(r), 1), "10 degrees right");
    const V3 d = turned(f, 10, 0);
    expect(d.y < -0.17f, "10 degrees down");
    V3 v{0, 0, 400};
    for (int i = 0; i < 60; ++i) v = step_velocity(v, 1.0f / 60);
    expect(v.y < -9.0f && v.y > -9.9f && v.z < 400 && v.z > 359, "a second of flight: falling ~9.8 m/s, a bit slower");
    printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
