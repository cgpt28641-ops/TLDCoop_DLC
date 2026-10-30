#pragma once
// TLDCoop_DLC: the built-in mods' ON / OFF that the host decides for everyone (the setup screen's MODS page), and
// what of theirs every game follows (the host's snow on the trees). The host sends net Features on link, on a change
// and every few seconds; a friend follows it. Alone or hosting, each mod's own setting counts.

#include <cstdint>

namespace coop_features {

void set_own(uint32_t feature, bool on);   // this game's own choice (coop.ini / the MODS page), net::kFeature*
void set_tree_snow(float snow);            // the host: its trees' snow (0-100) to send
bool on(uint32_t feature);                 // what counts here now: a friend, the host's (off until it's heard)
bool following_host();                     // a friend linked to a host that has said
float host_tree_snow();                    // a friend: the host's trees' snow (-1 none yet)
void on_frame();

}  // namespace coop_features
