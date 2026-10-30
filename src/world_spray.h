#pragma once
// TLDCoop_DLC: map markings are everyone's. In TLD 2.x a player marks the map by spray painting a wall (the marking is
// on TLD's map too, where it was painted). A marking put down (DynamicDecalsManager.PlaceDownSprayPaintDecal) or
// wiped (RemoveDynamicDecal) goes to the others as SprayMark; their games paint (CreateCustomDecal, placed down as
// TLD does, under the same id) or wipe the same. The host passes a friend's on, keeps what comes for scenes it isn't
// in until it gets there, and gives a friend who comes into its scene every marking it has there. Main thread only.

#include <string>

namespace world_spray {

bool init();
void on_frame(bool in_game);

}  // namespace world_spray
