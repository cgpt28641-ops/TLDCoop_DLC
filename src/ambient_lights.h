#pragma once
// TLDCoop_DLC: Ambient Lights, built in. A port of the MelonLoader mod AmbientLights (Xpazeman, maintained by
// Thekillergreece; MIT, third_party/ambient-lights/LICENSE): daylight through each interior's windows that follows
// the time of day and the weather, TLD's own indoor ambience and fill lights toned to match, light shafts by the
// weather. No MelonLoader and no extra files: the mod's light data is compiled in (src/ambient_data.inc).
//
// On or off: playing alone, coop.ini [ambient] enabled; in co-op the host's choice (the CO-OP setup screen's MODS
// page), sent to every friend (net Features). Its levels are each player's own ([ambient]).
// Left out from the mod: its experimental "true sun" (a shadow-casting sun through windows), debug keys and markers.

#include "config.h"

namespace ambient_lights {

void init();                                    // hooks TLD's lighting; once, at startup
void set_settings(const config::Settings& s);   // coop.ini / the setup screen's choice and levels
void on_frame(const std::string& scene, bool in_game);

}  // namespace ambient_lights
