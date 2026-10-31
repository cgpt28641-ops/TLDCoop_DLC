#pragma once
// TLDCoop_DLC, FOR TESTING (to be removed later): TLD's own developer console in game, as the MelonLoader mod
// Developer Console (DigitalzombieTLD; https://github.com/DigitalzombieTLD/TLD-Developer-Console, MIT, built in with
// the author's permission) does it: TLD's uConsole prefab (an addressable, "uConsole") is made, and a key opens it
// (coop.ini [devconsole] key=F1; enabled=0 switches it off). TLD's own commands only (help, gear_add, god, ...).
//
// To remove it: delete src/dev_console.cpp/.h and third_party/developer-console, the line in CMakeLists.txt, and the
// TLDCOOP_DEV_CONSOLE blocks in src/core.cpp (search for that word).

#include <string>

namespace dev_console {

void on_frame(const std::string& scene, bool in_game);

}  // namespace dev_console
