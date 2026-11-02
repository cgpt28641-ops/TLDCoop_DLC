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
// Milestone 2 (seamless_coop.inc): co-op. A building's room is a place of its own in the region scene ("place": the
// building's id, "" outdoors), carried in each player's State. The host's game is the authority on the rooms: it
// builds a room when any player comes near, lists its ready rooms to the friends (Rooms), and its world lists (items,
// containers, objects, fires, water) carry the rooms' contents under building-scoped ids. A friend builds only the
// rooms the host has, rolls their template with the host's seed, never fills a room from its own files and never
// writes room files (its own player.inside record still puts its body back in a room after a load).
//
// Building list: src/seamless_configs.inc (tools/seamless/gen_configs.py from SI's own list).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

// Stage 2 (seamless_windows.inc): the private layer a building's room is drawn on while it is held for the views into
// it from outside (-1: not held). A friend standing in that room is drawn on it too (avatar.cpp), so the views show them.
int view_layer(const std::string& place);

// --- co-op (milestone 2) ---------------------------------------------------------------------------------------------

// The building the local player is inside ("" outdoors, or the feature off): State's place.
std::string place();

// A6: where a building's door is in its region (where a player inside its room is marked on the map, radar and
// HUD). A sub-interior: its parent building's. False for an unknown id.
bool door_position(const std::string& place, float& x, float& y, float& z);

// The interior scene of a building ("CampOffice"), for TLD's name of it; "" for an unknown id.
std::string place_scene(const std::string& id);

// For the world modules (instead of is_clone_object): true for what isn't shared, i.e. an object of a room that isn't
// ready yet (still building, or a template). A ready room's objects are shared like the region's. False with no rooms.
bool sync_skips(Il2CppObject* component_or_game_object);

// A building-scoped id for an object of a ready room ("si:<building>|<name>|<x>,<y>,<z>", room-local cm): the same on
// every machine, where the game's own ids of a room's objects aren't. "" for anything else.
std::string room_key(Il2CppObject* component);

// Changes whenever a room comes up (ready) in our game: the world modules take its contents in as both games' (not as
// changes of ours) and wait for the host's lists.
uint32_t rooms_generation();

// A GameObject in a ready room that is closed in our game (the room switched off as a whole while no one of ours is in
// it): active as far as the room goes. The world modules count it as there (a friend in that room sees it).
bool active_in_closed_room(Il2CppObject* game_object);

// An item another player put down (made or moved by the world modules) where a ready room of ours stands closed: it
// becomes part of that room (switched off with it), instead of falling 80 m through a room that isn't there. True
// when it stands in a ready room.
bool settle_in_room(Il2CppObject* gear_item);

// A world position inside the parked spot of a room that isn't built in our game: what the other game puts there
// can't stand here yet (it comes with the host's lists once the room is up).
bool unbuilt_room_at(float x, float y, float z);

// Host: the item (by its co-op id) is in the saved file of a room that isn't built in our game yet: a friend's change
// to it waits for the room (it'd be missing here until then).
bool unbuilt_room_has_item(const std::string& pdid);

// A room of this region scene isn't built in our game yet (not built, or building; a building left at the game's door
// doesn't count; a region we haven't entered: all of its rooms): a change of another game's to something not here may
// be to one of its objects (they get their co-op ids, made from the building and their spot in the room, once it's
// up). False with the feature off.
bool rooms_pending_build(const std::string& scene);

// Friend: a room object of ours whose room the host hasn't said is up in its game: the host's lists don't have it yet
// (it isn't removed for missing from them).
bool host_lacks_room_of(Il2CppObject* component_or_game_object);

// The building whose ready room holds this object ("" for anything else).
std::string room_of(Il2CppObject* component_or_game_object);

// Host: rooms whose items go to a friend as full records once the lists go to them again (player 0: every friend).
std::vector<std::pair<uint8_t, std::vector<std::string>>> take_room_puts();

// Before a co-op teleport next to another player (a join, a respawn): their place. Leaves the room we're in when they
// are elsewhere; takes us into theirs when it is built here (true, `indoors` set: teleport to their spot), or puts the
// spot at its door when it isn't (true, x/y/z changed, it is built first). False: nothing to do with rooms.
bool prepare_teleport(const std::string& place, float& x, float& y, float& z, bool& indoors);

}  // namespace seamless_interiors
