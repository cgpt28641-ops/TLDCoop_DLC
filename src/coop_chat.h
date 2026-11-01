#pragma once
// TLDCoop_DLC: chat in the game, and a text box for a bug report's description.
//   - ENTER (in a co-op game, no TLD menu open) opens the line: type (any letters, Polish ones too: the window's own
//     character messages), ENTER sends it to everyone, ESC drops it. While typing, TLD's input is ignored (the player
//     doesn't walk off). Messages show in the chat box (bottom left, TLD's look) for a while, the last few lines.
//   - ask(): the same box with a prompt, for one line of text that goes to a callback (F11's bug report).
// Main thread only (the window's messages come on it too).

#include <functional>
#include <string>

namespace coop_chat {

void init();
void on_frame(bool in_game);
// Ask the player for one line (prompt shown before it); `done` gets it ("" when they pressed ESC or after a while).
void ask(const std::string& prompt, std::function<void(const std::string&)> done);
bool typing();

}  // namespace coop_chat
