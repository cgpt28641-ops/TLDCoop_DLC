#pragma once
// TLDCoop_DLC: Better Night Sky (built in, MIT): finer stars and a moon with its phases in place of TLD's. Each player's
// own look: coop.ini [sky] enabled (on by default). Main thread only.

#include "config.h"

namespace night_sky {

bool init();
void set_settings(const config::Settings& s);
void on_frame();

}  // namespace night_sky
