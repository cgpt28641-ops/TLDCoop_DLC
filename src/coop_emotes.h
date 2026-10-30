#pragma once
// TLDCoop_DLC: gestures (net/emotes.h). A key each (coop.ini [emotes]: wave, point, come, stop, yes, no); the one we
// make goes to the others in State (emote, emote_seq) while it plays, and their games show it on our body (avatar).
// We see a line on the HUD ("You wave"): in first person our own body isn't seen. Main thread only.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace coop_emotes {

// coop.ini [emotes] lines (name=key); a gesture not named keeps its default key.
void set_keys(const std::vector<std::pair<std::string, std::string>>& from_ini);
// Every frame: the keys, while we play and the game's window is in front.
void on_frame(bool in_game, bool can_gesture);
// The gesture we make right now (net::Emote, 0 none) and the counter of gestures started, for State.
void current(uint8_t& emote, uint8_t& seq);
// Test helper (dev command): start one by its name. False if there's no such gesture.
bool start(const std::string& name);

}  // namespace coop_emotes
