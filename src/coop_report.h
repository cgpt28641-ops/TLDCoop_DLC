#pragma once
// TLDCoop_DLC: a bug report with one key. F11 (coop.ini [report] key) marks the moment in the log ("REPORT #id")
// and packs TLDCoop\coop.log, coop-prev.log, coop-prev2.log and coop.ini into TLDCoop\reports\<time>_<name>.zip (by
// Windows' own tar.exe, off the main thread; a folder with the files if that fails). In co-op ([report] all=1) every
// other player's game makes its own report at the same moment, with the same id, so the logs line up. Nothing is
// sent anywhere: the files stay on each PC.

#include <string>

namespace coop_report {

void init();
void on_frame(bool in_game);  // in_game: the description box can show (TLD's HUD is up)
// TLDCoop_DLC: ESC's "POBIERZ LOGI": our logs packed, and every other player's game packs its own and sends the zip
// here, into TLDCoop\reports\<time>_logi_wszystkich\.
void collect_all();
// TLDCoop_DLC: our name now (the co-op screen's; it was read once at start)
void set_name(const std::string& name);

}  // namespace coop_report
