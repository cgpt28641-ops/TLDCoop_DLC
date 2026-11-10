#pragma once
// TLDCoop_DLC: ClothingExpanded (built in; the mod by TonisGaming, Karol 06.11 "Dobra rób 1"): 28 items made of a
// piece of TLD's clothing and an accessory (pants + tool belt, boots + crampons or improvised crampons, the moose-hide
// satchel + rifle scabbard), with the mod's own models, icons and paper-doll pictures. Its Addressables catalog and
// bundles are inside the DLL (TLDCoop\clothing on disk); the items are made into gear the way ModComponent would.
// Attach/detach buttons on the inventory's item page. Everyone's own (same DLL: the others' items exist for us too).
// Main thread only.

#include <string>

namespace clothing_expanded {

bool init();
void on_frame(const std::string& scene, bool in_game);
// dev `clothing`: what's loaded and mapped.
void describe();
// TLDCoop_DLC: a console line (F1): `add GEAR_X [N]` with one of ours adds it. True: it was ours (answered).
bool console(const std::string& line);
// TLDCoop_DLC (workbench recipes, Shatsca 10.11): the catalog is in and our items are made.
bool ready();
// A game item's or one of ours' prefab GearItem (or ToolsItem with `tool`) by name; null if none.
void* gear_prefab(const char* name, bool tool = false);
// One of ours into the player's pack (1 = full condition). True: given.
bool give_item(const char* key, float condition);
// Is this name one of ours?
bool is_ours(const char* name);

}  // namespace clothing_expanded
