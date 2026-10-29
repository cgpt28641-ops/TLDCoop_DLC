#pragma once
// TLDCoop_DLC: water sources are the same for every player. A toilet tank (or any of TLD's WaterSource objects: tanks,
// barrels...) one player takes water from has that much less for the others.
//   both   - each game checks its scene's water sources twice a second (WaterSourceManager.m_WaterSources), comparing
//            each one's save text (WaterSource.Serialize(): the litres in it and their quality) with what all games
//            last had. One that changed goes to the others as WaterPut; their game loads it the way loading a save
//            does (WaterSource.Deserialize). The host passes a friend's change on to the other friends.
//   host   - when a friend comes into the host's scene (or the host into theirs), the host sends every water source
//            it has there (WaterList) and the friend loads them all: the host's world wins. A friend's change in a
//            scene the host isn't in is kept and loaded when the host gets there (for this session).
// Sources are named by TLD's persistent id (ObjectGuid, what WaterSourceManager saves them by), or by their position
// when they have none (both games load the same scene). Main thread only.

namespace world_water {

bool init();
void on_frame(bool in_game);

}  // namespace world_water
