#pragma once
// TLDCoop_DLC: House Lights, built in. A C++ port of the MelonLoader mod House Lights by DemonBunnyBon (after
// Xpazeman's original; MIT, third_party/house-lights), made with the author's permission: the houses' light switches
// work again and the "aurora" lights (TLD's electrolizers) come on, as if the power were back. The switch models are the
// mod's own asset bundle, embedded in the DLL (src/house_lights_bundle.inc) and written next to it on first use.
//
// ON / OFF for everyone is the host's (the MODS page, net::kFeatureHouseLights); the levels are each player's own
// (coop.ini [houselights]). A switch flipped in co-op goes to everyone in that scene (net Msg::HouseLights, v49).

#include <string>

#include "config.h"

namespace house_lights {

void init();
void set_settings(const config::Settings& s);
void on_frame(const std::string& scene, bool in_game);

}  // namespace house_lights
