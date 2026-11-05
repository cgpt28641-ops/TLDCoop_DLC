#pragma once
// TLDCoop_DLC: what our body is busy with (net::Activity): on a rope, kneeling over a carcass or a fire, chopping,
// rummaging in a container, at a workbench, sitting, in the air, carrying a body. Read from TLD's player control mode,
// its open panels and the player's footing; sent in State, shown on our avatar with motion capture (avatar.cpp).
// Main thread only.

#include <cstdint>
#include <string>

namespace coop_activity {

bool init();
// For State, at its push rate: the activity right now. `scene` and `y` (the player's height) to tell a fall.
uint8_t current(const std::string& scene, float y, double now_ms);

}  // namespace coop_activity
