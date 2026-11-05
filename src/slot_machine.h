#pragma once
// TLDCoop_DLC: the slot machine (Karol's "automat", the spec in HANDOFF.md, 05.11.2026). Build 1, for testing only
// (coop.ini [automat] test=1): a machine placed with a command (command.txt: "automat postaw"), Karol's model built
// in game from embedded data (src/slot_machine_model.inc), reels, crank and bulb that move, its condition on hover, a
// spin on click. The stake, items, recipes and co-op come in the next builds.

#include <string>

namespace slot_machine {

void init();
void on_frame(const std::string& scene, bool in_game);
// command.txt "automat ...": postaw | usun | stan N | wynik jackpot|trojka|para|zwrot|czaszka|zaciecie | lista
void command(const std::string& args);

}  // namespace slot_machine
