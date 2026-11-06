#pragma once
// TLDCoop_DLC: fuel from a kerosene lamp back into a canister (after Better Fuel Management by WulfMarius / ds5678;
// Karol 06.11: only this one function for now). The lamp's examine screen gets DRAIN FUEL: TLD's progress bar for 3 s,
// then the lamp's kerosene goes into the kerosene containers in the pack (as much as they hold). Each player's own:
// coop.ini [fuel] drain (on by default). Main thread only.

#include "config.h"

namespace fuel_transfer {

bool init();
void set_settings(const config::Settings& s);

}  // namespace fuel_transfer
