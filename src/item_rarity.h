#pragma once
// TLDCoop_DLC (Shatsca 10.11, Karol agreed: our own version, not the GPL Item Rarities mod): every item gets a rarity
// (Common .. Mythic, like CS skins), and the inventory's and containers' squares are tinted with its colour.
// Our own list (keywords on the item's name). Main thread only.

#include <string>

namespace item_rarity {

enum class Rarity { None, Common, Uncommon, Rare, Epic, Legendary, Mythic };

bool init();
// An item's rarity by its prefab name (GEAR_...).
Rarity of(const std::string& gear_name);
const char* name(Rarity r);

}  // namespace item_rarity
