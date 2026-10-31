#pragma once
// TLDCoop_DLC: JustZoom, built in. A port of the MelonLoader mod JustZoom (WillWunderhorn / LittleWolfStorm;
// https://github.com/WillWunderhorn/JustZoom, "Just zoom from Absolute Cinema mod"), included with its author's
// permission: hold a key (Z) to zoom in, the mouse wheel (while holding it) zooms more or less (1.1x..20x); the view
// turns slower and smoothly while zoomed. No MelonLoader or ModSettings needed: coop.ini [zoom] enabled, key,
// wheel_step (5..50), speed (1..30), smooth (1..40). Each player's own (nothing of it goes over the network).

namespace just_zoom {

void init();
void late_update(bool in_game);  // GameManager.LateUpdate (JustZoom's OnLateUpdate)
void reset();                    // a scene loaded

}  // namespace just_zoom
