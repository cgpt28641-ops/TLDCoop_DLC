#pragma once
// Milestone 7b: the host's animals, mirrored in the client's game.
//   host   - about 10 times a second, every active animal (BaseAi) within range of either player: kind, AI mode,
//            health, position, facing, and its animator's state (each layer's state and time, every parameter).
//   client - while both players are in the same scene, the client's own animals there are hidden and the host's are
//            shown instead: copies of the same animal (this region's spawn prefab for that kind) with the AI and
//            colliders removed, moved (with the body's tilt along the ground) and animated to match the host's.
//            Everything is restored when mirroring stops (different scenes, disconnect).
//   sounds - every Wwise sound the host's animals make (howls, growls, footsteps, struggle; starts and stops) is
//            caught where all of TLD's sounds go (AK::SoundEngine::PostEvent / StopPlayingID / ExecuteActionOnEvent
//            in AkSoundEngine.dll) and played again on the copy's matching sound object.
// Main thread only.

#include <cstdint>
#include <string>

#include "il2cpp_api.h"
#include "world_keep.h"

namespace wildlife_sync {

bool init();
void on_frame(const std::string& scene, bool in_game);
std::string describe_sounds();  // what the Wwise hooks see (for the 'sounds' dev command)
uint32_t host_animal_id(Il2CppObject* ai);  // host: the id an animal goes by in Animals (0 = not shared); v44: any
                                            // game, the animals it runs
Il2CppObject* host_animal(uint32_t id);     // host: the animal (BaseAi) with that id, if it's still shared (v44: ours)
float host_animal_hp(Il2CppObject* ai);     // BaseAi.m_CurrentHP
// Client: whether this Transform belongs to one of our copies of the host's animals (its hit colliders), and which;
// dead = the host's animal is dead.
bool puppet_at(Il2CppObject* transform, uint32_t& id, bool& dead);
// TLDCoop_DLC, client: the host's wolf in a fight (AiMode Struggle) within a few metres of (x, y, z): where its root is
// and which way it faces. TLD's fight clips hold the player on the wolf's root, facing back along it.
bool fight_spot(float x, float y, float z, float& out_x, float& out_y, float& out_z, float& out_yaw);
// Client, for wildlife_carcass: whether we show the host's animals now; a real carcass took the place of the copy of
// the host's animal `id` (the copy hides; our own animals' hiding leaves the carcass alone); it went again.
bool mirroring();
// TLDCoop_DLC: the host went into another scene (a house) and our animals took over this one, where we still are
bool host_away_here();
void carcass_made(uint32_t id, Il2CppObject* carcass_go);
// TLDCoop_DLC (v44), [wildlife] ownership=1 (the host's): each animal is run by one game, shown as a copy in the others
// (wildlife_owner.inc). ownership(): it's on now. owner_of(id): who runs animal `id` (us, or the host's registry;
// net::kNoPlayer unknown). owned_here(ai): one of ours that we run (host_animal / host_animal_id then work in any
// game). copies_shown(): this game shows copies others' weapons may hit (a friend; or anyone with ownership).
bool ownership();
uint8_t owner_of(uint32_t id);
bool owned_here(Il2CppObject* ai);
bool copies_shown();
std::string describe_owners();  // dev command 'owners'
// v45 (host, world_keep): the wildlife registry and the spawn regions' state go with the host's save.
void save_kept(std::vector<world_keep::Record>& out);
bool load_kept(const world_keep::Record& r);

void carcass_gone(uint32_t id, Il2CppObject* carcass_go);

}  // namespace wildlife_sync
