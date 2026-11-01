#pragma once
// TLDCoop_DLC: "ZAPISZ GRĘ" in ESC's menu (Panel_PauseMenu's BasicMenu): the game saves now, the way it does after
// sleeping (GameManager.SaveGameAndDisplayHUDMessage), with the save icon. In co-op the host's save is the world's;
// a friend's saves their own survivor. Seamless Interiors' room files go with it (its SaveSceneData hook).

namespace coop_save {

void init();
void on_frame(bool in_game);

}  // namespace coop_save
