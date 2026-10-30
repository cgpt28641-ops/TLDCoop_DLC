# TLDCoop_DLC: handoff for the next assistant (30.10.2026)

## The user
- Karol, Polish (in-game name "Kerel_007_"). Write to him in Polish, short and simple. If he writes "krótko", answer in a few lines.
- He plays co-op with the host "Shatsca" and with "szymongrzyb2017".
- He is not a programmer. He runs whatever you give him and replies "gotowe" or "zrobione", sometimes sending logs or screenshots.
- He wants ready-made zips from GitHub Actions artifacts. Give him a link, not build instructions.
- Implement items from a list only when he tells you to. Once he says "rób wszystko", you can work on your own.
- When you send him a list of changes, mark each item: `[ZROBIONE - do testu]`, `[CZESCIOWO]` or `[NIE ZROBIONE]`.

## Project
- The repo is private: **cgpt28641-ops/TLDCoop_DLC**, branch `main`.
- It is a co-op mod for The Long Dark with all DLC, **game version 2.59 189140 S** (`kTestedGameVersion` in `src/core.cpp`).
- **Design:**
  - A C++ DLL loaded as a winhttp proxy, using IL2CPP and MinHook.
  - The network protocol is in `src/net/protocol.h`, **currently v34**. Every player must run the same build.
  - CI runs on windows-latest with MSVC. Compile errors come back as annotations, and the artifact is the zip.
- **Checking it locally (Linux):**
  - Syntax: `clang++ -fsyntax-only -I<stub dir with windows.h>`.
  - Pure tests: `g++ -std=c++20 -Isrc tests/*_test.cpp`.
- **Logs:**
  - `coop.log`, rotated to `coop-prev.log` and `coop-prev2.log`.
  - `[FAULT]` lines give the crash location as module+offset.
- **Disabling the mod:** create an empty file named `disabled` in the TLDCoop folder.
- **Config:** `coop.ini`, with sections [net] (includes `backups=`), [colors] (e.g. `Shatsca=blue`) and [emotes] (keys).

## Done so far (most of it NOT yet tested in co-op)
- **v32:**
  - Match and flare light in hand.
  - The striking-a-light animation.
  - Flare burn time synced.
  - A thrown flare is sent while still in flight.
  - Cooking cans and melting snow are synced: opened state, host sync every 5 s.
  - Player colours.
  - The map updates live.
  - Animals within 250 m of any player are driven by the AI (they no longer freeze).
  - Locking a decoration while someone moves it.
  - Gestures F5–F8, F3 and F4.
- **v33:** other players see your blood trails (Msg BloodMark = 41, `src/world_blood.*`).
- **v34:**
  - Save backups (`TLDCoop\backups\<date>`, keeps 5).
  - Shared spray-paint markers (Msg SprayMark = 42).
  - A friend's condition and afflictions shown under their name tag within 5 m (State.condition, afflictions).
- **Fixes:**
  - Host crash when searching a backpack or car (`m_GearToInstantiate` is a `List<string>`). Confirmed fixed.
  - Crash when quitting (Steam DLLs are now pinned).
  - The breakdown ring was missing (hook on the time-acceleration HUD in `src/coop_lapse.cpp`).
- **Full list for the user:** `TLDCoop_DLC_lista_poprawek.txt` (items 1–19). Not done:
  - 10 and 11: Iron Road train and Iron Road unique items, postponed by agreement.
  - 17: no ring when breaking things down.
  - Minimap and players drifting out of place: cause unknown. Needs screenshots plus coop.log.

## Current work: clothes on the avatars
**Goal:** the other players see your hat and coat.

**Input:**
- Karol's catalog `TLD_Odziez_Pelny_Katalog`: 23 characters × 70 glTF items.
- Each item is skinned to the NPC rig of a given character (`*_jnt` bones, root `Body_<char>`).

**Assets:** these are Hinterland's assets. Give them only to friends who own the game, never publish them, and do not put them in the repo.

**Tool:** `clothespack/` in the repo, copied to `D:\TLDCoop_Ubrania` on Karol's PC.
- `build_clothes.cmd` runs Unity 6000.0.60f1 (`C:\Program Files\Unity\Hub\Editor\6000.0.60f1\Editor\Unity.exe`) in batchmode.
  - `TLDCoop.BuildClothes.Build` produces the `tldcoop_clothes` bundle (about 82 MB).
  - `TLDCoop.BuildClothes.Preview` produces preview PNGs and `preview.log`.
- `Editor/BuildClothes.cs`:
  - Has its own glTF parser.
  - Converts right-handed to Unity: mirrors x, flips triangle winding, turns UV v into 1−v, bind poses become S·IBM·S.
  - Item edits: `lift`, `inflate` (along the normal), `shorten` (raises the hem).
  - Dress(): a SkinnedMeshRenderer whose bones are mapped by name to the body's bones.
  - Hiding the body's own clothes: the whole submesh gets the `TLDCoop/Hidden` material, because body meshes are not readable.
- `clothes.json`:
  - 22 items: hats, coats and vests.
  - lift/inflate/shorten values, `hide_under`, `hide_words`.
  - Preview characters: mackenzie, astrid, heller, molly, jeremiah, jane.
- **Workflow:**
  1. Change the repo and push it to Karol's PC with `device_commit_files` (from `/mnt/user-data/outputs`).
  2. **Always confirm the write by staging the file back.** It once reported success without writing anything.
  3. Karol runs the cmd and writes "gotowe".
  4. Read `preview.log`, `build.log` and the PNGs from `D:\TLDCoop_Ubrania` with `device_stage_files`.
- **Folder access granted:** `D:\TLDCoop_Ubrania` and `D:\SteamLibrary\steamapps\common\TheLongDark`.

**Status after the last run (commit c06223e):**
- **OK:** Mackenzie, Astrid and Molly. Their own sweaters and jackets are hidden, and hats sit correctly.
- Only 5 out of 23 bodies have clothes as separate submeshes: mackenzie, mackenzie_indoor, astrid, astrid_outdoor and molly. The other 18 (heller, jeremiah, jane, methuselah, convicts, patrons, millworker, terry, atwood…) are one piece, so their own clothes show through the coat.

**Problems Karol reported (latest screenshot, 30.10):**
1. **Coat collars sit too high and reach the eyes on some characters.** Very visible on the BasicWinterCoat (green) and on Heller and Jeremiah. Likely causes:
   - the item was skinned to another character's proportions (neck and head height);
   - `inflate` along the normals pushes the collar up and out.

   Ideas to try:
   - per-character lowering of the collar or lift for coats;
   - weaker inflate in the neck area (scale it by the weight of the Neck/Head bones);
   - trimming collar vertices whose height is above the neck bone.
2. Jeremiah in outfit 2 (RabbitskinHat + BearSkinCoat) renders as a white fluffy blob, so check the materials or alpha of the BearSkinCoat.
3. DownParka and BearSkinCoat sleeves are 12–15 cm too long past the wrist (Mackenzie, Molly).
4. Single-piece bodies show their own clothes through.

**My proposal to Karol (he has not chosen yet):**
- (1) Clothes only on the 5 split bodies for now. Recommended.
- (2) Stronger inflate for the other 18.
- (3) Draw the coat so it covers the body (a stencil or depth trick). Experimental.

**Karol's latest request:** "make it much better than it is now". Quality matters to him: the clothes and animations are supposed to look really good.

**DECISION (30.10, Karol: "nie wiem który wariant, chcę żeby wyglądało bardzo dobrze"):** go with variant 1, for quality.
- Visible clothes are dressed only onto the bodies with separate parts: **Mackenzie, Astrid (indoor/outdoor) and Molly**. Mackenzie and Astrid are the game's own player characters anyway.
- Other bodies keep their original outfit and no clothes are added onto them. No stronger inflate.
- Variant 3 (covering the body with a stencil) is an optional later experiment, only if they want more characters.
- Effort now goes into polishing those 3 characters: collars, sleeves, hem lengths, and checking all 22 items on each of them from several angles.

## Next steps
1. Fix the collars (point 1 above), Jeremiah's coat and the sleeves. Do another build_clothes iteration and check the previews.
2. Get Karol's decision on (1)/(2)/(3) for the single-piece bodies.
3. **Mod integration (protocol v35):**
   - Load `tldcoop_clothes.bundle` next to the DLL.
   - Send each player's worn clothing, either in State or as a separate message.
   - Dress the avatar: a SkinnedMeshRenderer with bones mapped by name, rootBone `Hips_jnt`, materials TLDCoop/Body.
   - Hide the body's submeshes listed in `rules.txt`.
   - Add tests in `tests/` and update the README and INSTALL_DLC.txt.
4. Give the friends the bundle privately (not on GitHub).
5. Test everything from v32–v34 together in co-op, then update the list of changes.
6. Later:
   - Iron Road (train and unique items).
   - Proper gesture animations (Unity clips).
   - The minimap mismatch (needs screenshots).
