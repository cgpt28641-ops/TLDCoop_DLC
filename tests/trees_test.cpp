// TLDCoop_DLC: Dynamic Trees' snow arithmetic (src/trees_math.h). Exit code 0 when all pass.

#include <cmath>
#include <cstdio>

#include "trees_math.h"

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using namespace trees;
    expect(rate_per_hour(kLightSnow, kCalm, 0) == 3 && rate_per_hour(kLightSnow, kWindBlizzard, 0) == 10,
           "light snow: 3 an hour, 10 with a blizzard wind");
    expect(rate_per_hour(kHeavySnow, kWindy, 0) == 13, "heavy snow, windy: 7 + 6");
    expect(rate_per_hour(kBlizzard, kCalm, 0) == 30, "blizzard: 30");
    expect(rate_per_hour(kClear, kCalm, 50) == 0, "clear and calm: it stays");
    expect(rate_per_hour(kClear, kVeryWindy, 50) == -10, "clear, very windy: blown off");
    expect(rate_per_hour(kCloudy, kCalm, 90) == -10, "above 80 it slides off by itself");
    expect(accumulate(98, kBlizzard, kCalm, 1) == 100 && accumulate(2, kClear, kWindBlizzard, 1) == 0,
           "kept between 0 and 100");
    expect(std::fabs(accumulate(10, kLightSnow, kCalm, 0.5f) - 11.5f) < 1e-4f, "half an hour of light snow: +1.5");
    expect(starting(kHeavySnow) == 65 && starting(kClear) == 0, "a new save starts with its weather's snow");
    expect(state_of(0) == 0 && state_of(4.9f) == 0 && state_of(5) == 1 && state_of(64) == 3 && state_of(99) == 7 &&
               state_of(100) == 8,
           "states by the thresholds");
    bool ok = true;
    for (float snow = 0; snow <= 100; snow += 0.5f)
        for (float r = 0; r < 1; r += 0.1f) {
            const int i = texture_index(snow, r);
            if (i < 0 || i > 7) ok = false;
        }
    expect(ok, "a texture from the set always (0-7)");
    expect(texture_index(0, 0) == 0 && texture_index(0, 0.9f) == 1, "clear: the clear one or the next");
    expect(texture_index(97, 0) == 5 && texture_index(97, 0.9f) == 6, "nearly full: one of the two before the last");
    expect(texture_index(100, 0.5f) == 7, "full: the whitest");
    printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
