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
- **30.10 later: Karol limited it further to the indoor variants: mackenzie_indoor, astrid, molly** (default body mackenzie_indoor). Outdoor variants have their own hats/coats baked in.
- **DONE (commit afbbb33):** the co-op setup and death screen offer only these 5 (`avatar::choosable_body_ids`); coop.ini default `body=mackenzie`; an unchoosable ini body falls back to mackenzie. Bodies from older builds are still accepted from the network.
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

## Clothes status (30.10, ~11:50)
- clothes.json: characters mackenzie_indoor/astrid/molly; collars brought down (Collar()); wool hat, toque and improvised hat are generated fitted beanies (Beanie(); catalog only has their folded ground models); balaclava + ear warmer re-oriented (reshape); hair hidden under full hats (hide_rules); rules.txt format: one line per rule "hide <items> : <words>".
- Previews (previews/sheet_<character>.jpg, columns = outfits, rows front/side/back) look good on all three. Karol approved cap and rabbit hat as OK.
- Next: mod integration (v35) — load bundle, send worn clothes, dress avatars, apply rules.txt.

## Standing rule (Karol, 30.10 15:16)
**After every change, append a dated entry to this file** (what changed, why, status: tested on preview or not), and commit it with the change.

## Clothes log (30.10 afternoon, newest last)
Pipeline recap: base body = character's own clothes hidden (base_hide regions) + base layer (TLDCoop_BaseTop from FleeceSweater, LongUnderwear, WoolSocks); every other piece wraps over BaseSurface by layer (`layers` min/max in clothes.json). Previews: `previews/close_<char>.jpg` (head+shoulders) and `sheet_<char>.jpg`, 21 outfit columns (see `preview_outfits`), rows front/side/back, cell 400×600.

- **v32–v33** – balaclava = SkinMask (head skin offset 6 mm, eyes/mouth holes) + skull shell + neck tube; vest SliceFit. Tested: OK.
- **v34** – scarves snug round the neck; knitted pieces (balaclava/scarves/beanies) get only a light baked AO (0.8–1.0) – dark blotches came from hair the game hides. Tested.
- **v35** – scarves = flat knitted bands (2 turns, 4.8 cm tall × 2.2 cm), over the coat collar (layer min 0.028), hanging end on top. Balaclava shell fitted per head from BodyCut (eyes height/front, head extents incl. hair), closed over the forehead, neck tube raised. Fixed CS0136 (`thick`→`depth`). Tested: balaclava covers the whole head on all 3, scarves round the neck – Karol OK.
- **v36** – white patches = inner layers poking through outer ones. Added `ClampLayer` (last per-vertex pass, no smoothing: push ≤3 cm, pull ≤5 cm below neck−0.025). Base top made charcoal (`GreyCopy` → `_thermal` texture, 0.2+0.42·lum). Tested: Astrid/Molly clean.
- **v37** – Mackenzie still had white on shoulders: his own shirt sits in the body's **neck** region (x ±0.1, down to 1.56 m). `base_hide` now supports `region@materialword`; added `neck@Mackenzie_Ep5_Body`. **Mod integration must honour this syntax too.** Tested (v38 build): fixed.
- **v38** – depth bias by layer in TLDCoop/Cloth (`Offset [_OffsetFactor],[_OffsetUnits]`, set from the piece's layer min: −wrap·100, −wrap·2000); tops max 0.009, vests min 0.02. Hair hidden under beanies (BasicWoolHat, Toque, ImprovisedHat). Tested: hair OK (Molly shows a bit of pale nape under the beanie – could lower the beanie at the back). Vests still bad.
- **v39** – vest cause: raw vests (yoke + standing collar) were left inside the torso above the armpits, so the shirt covered them → shredded look. SliceFit now also fits front/back depth above the armpits (cap ×1.45) and keeps the width factor of the last slice below the armpits. **Not yet seen on preview.**
- **v40** – EarMuffs is really a ring-shaped ear warmer band; it covered the face. Reshape got `tilt`, `back_only`, `cut_front`: rotate_x 90, scale 0.85, centre (0, 0.075, −0.012), tilt 0.35 (back lower), front part beyond centre z+0.025 removed → on the ears and round the back of the head. **Not yet seen on preview.**

Open items: check v39 vests and v40 ear warmer; Molly's nape under beanies; bear coat / wolf cape grey gaps between fur pieces; scarf hanging end is straight/stiff (could angle + fold); later mod integration (v35 protocol) – Karol has not approved it yet.
- **v40 result (15:21)** – ear warmer: on the ears and round the back – OK. Vests: better on Astrid, still shredded yoke/collar on Mackenzie and yellow shards on InsulatedVest. Hiding all hair under beanies showed holes in the scalp (eye backs visible from behind).
- **v41** – BodyCut splits hair-material triangles of the head at a hat rim (2.5 cm above the eyes at the front → 0.5 cm under them at the back) into a new part region `hairtop`; beanie hide rule now hides only `hairtop` (fringe, nape, ponytail stay). Vests taken out of `collar_items` (the collar squeeze crumpled their standing collar/yoke) – experiment. **Not yet seen on preview.** Mod integration: the `hairtop` part comes from BodyCut, so the runtime must use the same split.
- **v41 result (15:36)** – hair split at the beanie rim works (nape/ponytail stay, no scalp holes). Vests still bad above the armpits.
- **v42** – offline sim (`scratchpad/ana/vsim.py`: base fleece + tee + vest, slice fit + wrap) reproduced the vest problem: the nearest-normal push scatters the yoke at the shoulders. New `Radial()` (after SliceFit, slice-fit items only): above armpit−4 cm each vertex moves out along the ray from a spine point 10 cm under the armpits to `clearance` outside the farthest crossing of the base torso (triangles |x|<0.2, recorded in `BaseTris` when the base is built), faded in over 4 cm. Sim: yokes/collars now lie over the shirt (Astrid clean, Mackenzie much better). **Not yet seen on preview.**
- **v42 result (15:50)** – vests fixed on Astrid and Molly (yoke + collar over the shirt, BallisticVest clean on all). Left: Mackenzie DownVest dark collar shards, Mackenzie InsulatedVest grey tee patch on the shoulder (side), small dark points at the DownVest armholes from behind; Mackenzie's beanies look squat with hair bulging under the rim.
- **15:51 Karol:** balaclava (Mackenzie: knitted beard shape) and beanies (bowl/mushroom from the side) don't suit him; asked about trousers, boots, socks. Checked legs: trousers OK; base socks poke through boots at the toes; sock-only outfits look like flat grey/red paws (the catalog sock is a single flat ground model).
- **v43** –
  - `HeadFit(character)`: skull ellipsoid from BodyCut (eye height/front, skin extents above the eyes; hair added only when the scalp isn't modelled). Used by the balaclava shell (+6–8 mm) and now by **beanies** (+1.4 cm over the skull, crown +3 cm slack, rim eyes+3.5 cm front → eyes−2 cm back); hairtop split rim moved to the same line.
  - Balaclava: under the mouth the mask is flattened from the chin to the throat (beard stays inside, hidden).
  - **Socks** (`sock_items`, `sock_from: GEAR_BasicShoes`): made in the shape of the character's low shoes, 4 mm inside, one knitted material in the sock's colour (`Convert(..., sockColourFrom)`), no mirroring. `boot_items`: under any footwear no sock is drawn (preview does this; **the mod must too**).
  - **Not yet seen on preview.**
