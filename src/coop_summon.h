#pragma once
// TLDCoop_DLC (Shatsca 07.11: "komenda teleportplayer pod F1 (konsola hosta), żeby przeteleportować szymongrzyb2017 na
// swoją pozycję"): the host's F1 console takes `teleportplayer NAME` (part of the name is enough; `all`: everyone):
// that friend's game puts them beside the host. Only within the same scene (another region: the host is told so).
// uConsole.RunCommand is hooked: our command is answered there, any other goes on to TLD. Main thread only.

#include <string>

namespace coop_summon {

bool init();
void on_frame(const std::string& scene, bool in_game);

}  // namespace coop_summon
