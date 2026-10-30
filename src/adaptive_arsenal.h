#pragma once
// TLDCoop_DLC: Adaptive Arsenal, built in. A port of the MelonLoader mod AdaptiveArsenal (Deaadman;
// https://github.com/Deaadman/adaptive-arsenal), included with its author's permission:
//   - rifle and revolver bullets fly (each gun its own muzzle velocity, gravity and drag) instead of hitting at once,
//     with a trail behind them;
//   - accuracy: the skill, crouching or standing, moving and hip fire spread the shot (up to 10 degrees);
//   - damage falls off with the distance flown (all of it at the muzzle, a tenth at 500 m);
//   - the revolver can be aimed while walking;
//   - the HUD shows the spent round flying out.
// How: TLD's own bullet (vp_Bullet) is taken as it starts and flown here; where it lands, it is started again just in
// front of what it hit, so TLD's own hit code (damage, blood, sounds, skill points; the co-op hit sync) does the rest.
//
// On or off: alone, coop.ini [arsenal] enabled; in co-op the host's choice (the MODS page).

#include <string>

#include "config.h"

struct Il2CppObject;

namespace adaptive_arsenal {

void init();
void set_settings(const config::Settings& s);
void on_frame(const std::string& scene, bool in_game);

// From vp_Bullet.Start's hook (wildlife_hits): true when the bullet is ours now (TLD's Start must not run).
bool take_bullet(Il2CppObject* bullet);

}  // namespace adaptive_arsenal
