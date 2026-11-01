#pragma once
// TLDCoop_DLC: the mod updates itself (Karol's call: the DLL goes only to trusted friends). At the main menu (once a
// start) a background thread asks the private repo's "dll-latest" release (GitHub's API, with a fine-grained
// read-only token CI builds in from the UPDATE_TOKEN secret) which build it holds; if it's another than ours, its
// winhttp.dll is downloaded next to ours, ours is renamed to winhttp.dll.old (Windows lets a loaded DLL be renamed)
// and the new one takes its name: the next start runs it. The player is told to restart (co-op screen, HUD).
// coop.ini [update] enabled=0 switches it off. Only winhttp.dll: the TLDCoop folder's bundles stay as they are.

#include <string>

namespace coop_update {

void init();
void on_frame(const std::string& scene, bool in_game);
// "" or what to tell the player (a new version is ready: restart).
std::string message();

}  // namespace coop_update
