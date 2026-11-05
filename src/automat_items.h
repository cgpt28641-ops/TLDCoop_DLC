#pragma once
// TLDCoop_DLC: the automat's items (Karol's spec, HANDOFF 05.11.2026; build 3a, 06.11.2026): the circuit board
// (GEAR_TLDCOOP_Plytka), the reel mechanism (GEAR_TLDCOOP_Mechanizm) and the automat itself (GEAR_TLDCOOP_Automat).
// TLD has no way to add gear, so each is a game item made by its name (GearItem.InstantiateGearItem and the
// InstantiateAndDeserializeGearItem overloads, hooked): asked for one of ours, TLD makes its base item (scrap metal),
// which is then renamed and given its own GearItemData copy (name, description, weight). Saves keep the name, so a
// loaded game asks for ours again. Our names and descriptions come from Localization.Get (hooked: "TLDCOOP_..." keys).
// The hooks are always on (a save with our items must load even with the automat's test off); the boards found by
// breaking down electronics only with [automat] test=1 for now.

#include <string>

namespace automat_items {

constexpr const char* kBoard = "GEAR_TLDCOOP_Plytka";
constexpr const char* kMechanism = "GEAR_TLDCOOP_Mechanizm";
constexpr const char* kAutomat = "GEAR_TLDCOOP_Automat";

void init(bool drops);
bool ready();
// "plytka" | "mechanizm" | "automat" (or a GEAR_ name) -> ours, or "" if it isn't one
std::string resolve_name(const std::string& which);
// n of ours into the pack; false if it couldn't be made
bool give(const std::string& which, int n);

}  // namespace automat_items
