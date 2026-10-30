#pragma once
// TLDCoop_DLC: our own pictures in TLD's picture frames. Each one (src/pictures_data.inc, from assets/pictures via
// tools/pictures/gen_data.py) is painted over its place in one of TLD's picture atlases: the atlas is copied on the GPU
// into a texture of ours (TLD's own file stays as it is, nothing of Hinterland's is shipped), our picture goes in, and
// every material showing that atlas shows ours instead. Always on, for everyone with the DLL.

#include <string>

namespace custom_pictures {

void init();
void on_frame(const std::string& scene, bool in_game);

}  // namespace custom_pictures
