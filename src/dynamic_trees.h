#pragma once
// TLDCoop_DLC: Dynamic Trees, built in. A port of the MelonLoader mod DynamicTrees (Fuar, The Illusion, Jods-Its;
// https://github.com/Fuar11/DynamicTrees), included with its authors' permission: snow on the trees builds up in
// snowy, windy weather and goes in clear, windy weather, the pines' and cedars' textures (Jods') stepping from green
// to white. Its textures: TLDCoop\tldcoop_trees.bundle (the mod's own asset bundle, shipped in our zip).
//
// On or off: alone, coop.ini [trees] enabled; in co-op the host's choice (the MODS page), and a friend's trees carry
// the host's snow (net Features). The snow is kept per save in TLDCoop\dynamic_trees.ini.

#include <string>

#include "config.h"

namespace dynamic_trees {

void init();
void set_settings(const config::Settings& s);
void on_frame(const std::string& scene, bool in_game);

}  // namespace dynamic_trees
