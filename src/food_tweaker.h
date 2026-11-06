#pragma once
// TLDCoop_DLC: Food Tweaker (built in, after the MelonLoader mod by GruffCassquatch and others): realistic calories of
// meat, packaged food, drinks and dishes, and realistic cooking times for meat. The host turns it on for everyone (the
// MODS page: FOOD TWEAKER); off it changes nothing.

#include "config.h"

namespace food_tweaker {

bool init();
void set_settings(const config::Settings& s);  // this game's own choice (coop.ini [food] enabled / the MODS page)

}  // namespace food_tweaker
