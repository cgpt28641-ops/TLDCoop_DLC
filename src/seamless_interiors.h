#pragma once
// TLDCoop_DLC: Seamless Interiors, built in. A C++ port of the MelonLoader mod Seamless Interiors by karsontr13
// (https://github.com/karsontr13/TLD-Seamless-Interiors), made with the author's permission: walking through a
// building's door needs no loading screen. The building's interior scene is loaded into the region next to it (its
// "clone", parked 80 m above the building, as SI does) and the door moves the player there and back behind a short
// fade to black. The game keeps the region as its scene the whole time.
//
// Milestone 1a: rooms built near the player, doors without a loading screen, the game's region save kept free of the
// rooms' objects. Milestone 1b (seamless_persist.inc): each room keeps its state with the save in our own per-save
// folder (furniture, the player's placed objects, loose items, containers, broken/opened/harvested objects, fires),
// what the player leaves in a room is adopted by it, a save made inside puts the player back in the room, and a closed
// room's fires, pots, lamps and curing go on with the game's time. Off by default: coop.ini [interiors] enabled, in
// co-op the host's choice (the MODS page). Off, every hook calls straight through to the game.
//
// Building list: src/seamless_configs.inc (tools/seamless/gen_configs.py from SI's own list).

#include <string>

#include "config.h"

struct Il2CppObject;

namespace seamless_interiors {

void init();
void set_settings(const config::Settings& s);
void on_frame(const std::string& scene, bool in_game);  // GameManager.Update
void on_late_frame();                                   // GameManager.LateUpdate

// GameManager.Awake of an interior scene we are loading as a template, or of a GameManager inside a room: the game
// must not wake a second GameManager (it would take over the scene, the clock and the player). True: destroyed, skip
// TLD's Awake (and ours). Called first thing in core's GameManager.Awake detour.
bool skip_game_manager_awake(Il2CppObject* game_manager);

// An interior scene is being loaded additively for a room right now: what appears in the world meanwhile isn't the
// region's (other modules' scans skip a round).
bool loading_template();

// A component or GameObject that is part of one of our rooms (or of an interior scene loading for one). Cheap false
// while the feature has no rooms.
bool is_clone_object(Il2CppObject* component_or_game_object);

// The player stands in one of our rooms (the door took them in).
bool player_inside();

}  // namespace seamless_interiors
