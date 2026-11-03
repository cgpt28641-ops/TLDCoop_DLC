#pragma once
// TLDCoop_DLC: the other players' footprints in the snow (TLD's own trails, one per avatar shown outdoors).

namespace avatar_tracks {
bool init();
void update(bool in_game);  // every frame, after avatar::update
}  // namespace avatar_tracks
