#pragma once
// TLDCoop_DLC: a copy of the player's saves before each co-op game. When a world loads with co-op on, TLD's survival
// save folder (PersistentDataPath.m_PathSurvival: every slot, the host's world and each friend's survivor alike) is
// copied to TLDCoop\backups\<date time>; the newest `backups` (coop.ini [net], 5 by default; 0: none) are kept. To go
// back to one: copy its files over TLD's save folder with the game closed. Main thread only (the copy runs on its own).

#include <string>

namespace coop_backup {

bool init();
void set_count(int keep);
void on_frame(const std::string& scene, bool coop_on);

}  // namespace coop_backup
