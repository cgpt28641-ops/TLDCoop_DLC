#pragma once
// TLDCoop_DLC: Karol's start screen (05.11.2026): when the game starts, TLD's boot screens (Panel_Boot: the warnings /
// disclaimers, then its loading) show Karol's picture instead (src/loading_screen_data.inc, from
// assets/loading/loading_screen.jpg, built into the DLL so everyone has the same). It fills the screen keeping its
// proportions (the edges cut, not stretched); the warnings' texts, logos and loading animations are hidden, only the
// "press to continue" labels stay, working as before. Later loads are TLD's own. Anything missing: TLD's screens stay
// as they are, and the log says why.

namespace loading_screen {

void init();

}  // namespace loading_screen
