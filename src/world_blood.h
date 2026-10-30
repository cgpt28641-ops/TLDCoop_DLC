#pragma once
// TLDCoop_DLC: a bleeding player's blood is seen by everyone. TLD leaves it through the player's BloodTrail (drops as
// they walk, a stain where they fought an animal, clusters): hooked, what our own trail leaves goes to the others as
// BloodMark, and their games leave the same with their own player's trail (its own state kept as it was), so it lies
// in their world (and their save) as it does in ours. The host passes a friend's on to the other friends. Animals'
// blood isn't touched (it's theirs, already shared). Main thread only.

namespace world_blood {

bool init();
void on_frame(bool in_game);

}  // namespace world_blood
