# TLDCoop_DLC: handoff for the next assistant (30.10.2026, updated 17:35)

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

## Current work: clothes on the avatars (state at 30.10, 17:35)
**Goal:** other players see what you wear, and it has to look realistic and professional ("perfekcyjnie"). Karol judges from the preview images and screenshots; he wants every item kept ("nic nie usuwamy") and made right.

**Decisions so far**
- Only 3 bodies get clothes and are choosable in the mod: **mackenzie_indoor, astrid, molly** (default mackenzie_indoor). Done in the mod: `avatar::choosable_body_ids`, coop menu + death screen, ini fallback (CI run 41043842291 OK).
- **The mod itself is NOT changed for clothes yet** (Karol chose "1": work only in the tool, integrate later when he says so).
- Base body: the character's own clothes hidden + a base layer (charcoal thermal top `TLDCoop_BaseTop` from FleeceSweater, LongUnderwear, WoolSocks). Nude bodies are impossible (no skin under the game's clothes).
- Balaclava: whole head and neck covered, only eyes and mouth visible (his reference: tube mask with eye/mouth holes).

**Assets:** Hinterland's art – only for friends who own the game; never publish, never in the repo. `.bodycut` files (game meshes) live only on Karol's PC (`D:\TLDCoop_Ubrania\bodycut`) and in the scratchpad.

**Tool:** `clothespack/` in the repo = `D:\TLDCoop_Ubrania` on Karol's PC.
- `build_clothes.cmd` → Unity 6000.0.60f1 batchmode: `TLDCoop.BuildClothes.Build` (bundle `tldcoop_clothes`, ~87 MB, 3 chars × ~67 items) and `TLDCoop.BuildClothes.Preview` (`previews/sheet_<char>.jpg` full body, `close_<char>.jpg` head+shoulders; 21 outfit columns from `preview_outfits`, rows front/side/back, cell 400×600; logs `build.log`, `preview.log`).
- Input: catalog `TLD_Odziez_Pelny_Katalog/characters/<char>/GEAR_*.gltf` (skinned to each NPC rig); Molly uses Astrid's fits (`fit_from molly:astrid`).
- `tools/bodycut/` (own UnityFS/LZ4/typetree parser) splits each body by bone region → `.bodycut`; Unity builds `<char>/BodyCut.asset` + `BodyCut.txt` (parts `sub:region`, incl. eyes, mouth, bands, and `hairtop` split at the hat rim).
- `Editor/BuildClothes.cs` (~121 KB) main steps per item: read glTF (mirror x) → reshape / SkinMask (balaclava) / Scarf / Beanie / SockSweep → collar, shorten → SliceFit + Radial (vests) → Wrap ×2 + ClampLayer (layer min/max over the base surface) → baked AO → mesh + txt (bones, materials, cut regions). Shader `Shaders/TLDCoopCloth.shader` (two-sided, normal from colour, AO, depth offset by layer).
- `clothes.json` holds all settings (layers, collar, hide_rules, base_hide incl. `neck@Mackenzie_Ep5_Body`, reshape for EarMuffs, sock_items/sock_from/boot_items, beanie_items, scarf_items, slice_fit_items, skin_mask_items…).

**Workflow**
1. Edit in `/home/claude/tldcoop_dlc`, commit + push (append to this file each time), copy changed files to `/mnt/user-data/outputs/clothes_vNN/` and write them to `D:\TLDCoop_Ubrania\…` with `device_commit_files`.
2. **Verify the write** (`device_list_dir` size/mtime; if the size is the same, stage the file back and grep it – once an old cached copy was written; a fresh staging folder fixed it).
3. Karol runs `build_clothes.cmd` and writes "gotowe" (or "błąd" → read `build.log`, look for `error CS`).
4. Stage previews/logs with `device_stage_files`, crop with PIL, look, fix.
- Git: repo remote URL is lowercase `tldcoop_dlc` (GitHub redirects; pushes occasionally fail with a server error – just retry, check `git status -sb`). Commit identity set to Claude/noreply.
- Offline helper: `scratchpad/ana/vsim.py` (base + tee + vest simulation with a software rasterizer).

**Current look (after v45/v46)**
- OK: coats, shirts, sweaters, trousers, boots, cap, rabbit hat, ear warmer (on the ears, round the back), scarves (flat bands round the neck over the collar), beanies (fitted to the skull, hair inside hidden, nape/ponytail visible), balaclava (whole head, beard flattened under it), vests on Astrid/Molly, no socks drawn under footwear, socks as clean knitted tubes.
- Open: Mackenzie DownVest dark collar shards, Mackenzie InsulatedVest tee patch on the shoulder, small dark points at DownVest armholes from behind; bear coat / wolf cape grey gaps between fur pieces; scarf hanging end straight/stiff.

## Next steps
1. The open items above (Mackenzie's vests, fur coats' grey gaps, scarf end).
2. When Karol approves: **mod integration (protocol v35)** – load `tldcoop_clothes.bundle` next to the DLL; send each player's worn items; dress the avatar on the cut body (BodyCut parts, bones by name, rootBone `Hips_jnt`); apply `rules.txt` (hide words / cut regions), `base_hide` (incl. `region@material` syntax), the `hairtop` split, and "no socks under boot_items"; tests in `tests/`, README/INSTALL_DLC.txt.
3. Give friends the bundle privately (not on GitHub).
4. Test v32–v34 features in co-op; later Iron Road, gesture animations, minimap mismatch.

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
- **v43 result (16:04)** – beanies now fitted (proper knit hats on all 3), balaclava over Mackenzie's beard smooth. Socks were the shoe model recoloured (laces, sole visible) – not good; no socks under boots works.
- **v44** – `SockFromShoe()`: shoe welded (0.2 mm), small loose parts (laces etc. <25 % of the biggest) dropped, Taubin-smoothed 25 rounds, sole thickness (1.2 cm) taken off the bottom, normals from the smoothed faces, 5 mm inside. **Not yet seen on preview.**
- **v44 result (16:10)** – smoothed-shoe socks still wrong (flat heel panel, spikes from the heel tab).
- **v45** – `SockSweep()` replaces it: per foot, a knitted tube swept along a centre line (from 5 cm over the shoe's top down the ankle, quarter circle round the heel, forward along the sole to a rounded, closed toe), sized from the shoe on that side (heel/toe/width percentiles), each ring skinned like the nearest shoe vertex; ribbed UVs. `SockFromShoe()` left in the file, unused. **Not yet seen on preview.**
- **v45 result (16:16)** – swept socks look clean (knit, heel, round toe) but the ankle tube sat behind the shin.
- **v46** – SockSweep ankle line = middle of the shoe's opening (top 3 cm of the shoe), at least 3.5 cm in front of the heel. **Not yet seen on preview.**
- **v46 result (17:39)** – sock ankle now on the shin line (good from the side); from behind the long johns' leg showed through the sock top.
- **v47** – sock ankle tube wider at the top (r 5.3 cm → 4.4 cm at the heel) so the long johns' leg sits inside it. **Not yet seen on preview.**
- **v47 result (17:43)** – dark blotches on the socks from behind/side remained: they are baked AO (rays hit the hidden body's own shoes inside the sock), not the long johns.
- **v48** – socks get the light AO like the other knitted pieces (0.8–1.0). **Not yet seen on preview.**
- **v48 result (17:47)** – socks clean on all 3 (front/side/back): knitted, on the shin line, no blotches. Socks done.
- **17:49 Karol:** from the side the long johns' leg still shows over the front of the sock at the ankle.
- **v49** – SockSweep's leg rings now go round what is already on the leg (BaseSurface = long johns, built before the socks): per ring at height y, the long johns' points on that side within ±1.2 cm give the centre and the 95th-percentile half width/depth, +6 mm; blended back to the foot's own line over the last 4 cm above the heel bend. **Not yet seen on preview.**
- **v49 result (17:53)** – fitting the sock leg round the long johns made a stepped/spiky top, and the long johns still showed on the top of the foot: they reach down to 6–10 cm (into the foot).
- **v50** – leg-fit removed again (plain round sock leg); new `ankle_cut_items` (LongUnderwear, LongUnderwearWool) + `ankle_cut` 0.05: their triangles below Foot_jnt + 5 cm are dropped – socks or footwear cover from there. **Not yet seen on preview.**
- **v50 result (17:58)** – long johns no longer on the foot, but their cut hem flared forward over the sock top (jagged edge in front).
- **v51** – after the cut, the long johns' leg is drawn in round the ankle: from Foot_jnt+13 cm down to +9 cm each vertex is held within 4.2 cm of the leg's middle (per side, per 2 cm slice), so the hem sits inside the sock (top r 5.3 cm). **Not yet seen on preview.**
- **v51 result (18:03)** – hem still in front of the sock: measured offline that the sock's ankle (shoe opening middle, z −0.085 for Astrid) sat ~3.5 cm behind the long johns' leg middle (z ≈ −0.05).
- **v52** – SockSweep centres the sock leg on the long johns' leg (BaseSurface points from the shoe top to +8 cm on that side, z mid of min/max, clamped heel+3.5…+14 cm), top radius 5.7 → 4.5 cm, and the lower leg curves 3 cm back so the heel still sits behind the shin. **Not yet seen on preview.**
- **v52 result (18:08)** – socks now right from the side and back; from the front a few small long-john specks still poked through the sock's front.
- **v53** – long johns' ankle draw-in stronger: from Foot_jnt+16 cm down to +12 cm, held within 3.7 cm of the leg's middle. **Not yet seen on preview.**
- **v53 result (18:12)** – Mackenzie clean; Astrid/Molly still a few tiny specks at the sock front (long johns' low-poly hem).
- **v54** – `ankle_cut` 0.05 → 0.085 (long johns end 8.5 cm over Foot_jnt, still ~2 cm inside the sock top). **Not yet seen on preview.**
- **v54 result (18:17)** – Astrid/Molly socks clean; Mackenzie now had a gap with a jagged hem above the sock (his Foot_jnt sits higher: cut 0.222 m above his sock top 0.213 m).
- **v55** – the long johns cut is also held at most 2 cm under the sock top (shoe `sock_from` 99th-percentile height + 3 cm). **Not yet seen on preview.**
- **v55 result (18:21)** – socks + long johns clean on all 3 from front, side and back. Feet done.

## Ambient Lights (30.10, 18:35) – request
Karol wants the MelonLoader mod **AmbientLights** (Xpazeman, maintained by Thekillergreece, v3, **MIT licence**) built into TLDCoop: no MelonLoader, no extra files, switchable ON/OFF when the game is created (first start), the switch living in our mod.
- His copy: `D:\TLDCoop_Ubrania\AmbientLights` (AmbientLights.dll 55 KB + `ambient-lights/` ~100 JSON: config, global_periods, global_sets, weather_sets, scene_<scene>.json emitters per interior).
- Source: `https://github.com/Thekillergreece/tld-ambient-lights` (cloned to scratchpad `al_src_tk`; ~3100 lines C#: AmbientLights, GameLights, LightConfig, ALUtils, AmbLight, TimeWeather, AmbLitSettings, Patches).
- What it does: per interior scene adds point lights at "window" emitters (position, orientation N/S/E/W, size, cover, priority) whose intensity/range/colour follow time-of-day periods and weather (sets interpolated over change_duration), plus fill/ambience changes; Harmony hooks on GameManager.Awake/Update, SaveGameSystem.LoadSceneData, MissionServicesManager.SceneUnloaded, TimeOfDay.Deserialize/InstantiateUniStorm, InteriorLightingManager.*, DarkLightingManager.*, LightShaftGimble/LightShaftTod, LightTOD.UpdateLights, TodAmbientLight.SetAmbientLightValue, UniStormWeatherSystem.UpdateSunTransform.
- Plan: port to C++ in TLDCoop (MinHook on the same IL2CPP methods), JSON data embedded in the DLL, settings in coop.ini, ON/OFF in the co-op setup; keep the MIT notice. Status: assessed, waiting for Karol's answer on who decides (host for all vs each player).
- **Decision (18:40):** host decides ON/OFF for everyone.
- **DONE (not tested in game yet):** Ambient Lights built into the mod, **protocol v35**.
  - Data: `third_party/ambient-lights/` (the mod's MIT JSON + LICENSE) → `tools/ambient/gen_data.py` → `src/ambient_data.inc` (92 interiors, 591 window emitters, periods, weathers; lenient parse: the mod's files miss commas / have a Latin-1 byte).
  - `src/ambient_math.h`: pure port of LightConfig.GetCurrentLightSet / ALUtils (HSV in degrees like TLD's ColorHSV, period/weather blends, flicker input, night mods, window light, ambience multiplier, fill colour). Fixed the mod's in-frame compounding of SetLightParams. `tests/ambient_test.cpp` (30 checks, pass on Linux).
  - `src/ambient_lights.cpp`: scene settles 4 s → creates point lights (GameObject "XPZ_Light_<dir>" + Light) at the scene's emitters, collects TLD's fill lights (point + non-window spot lights not in an InteriorLightingGroup) and glow-window materials; per frame reads UniStorm/Weather/Aurora/Wind → light set → our lights. Hooks: TodAmbientLight.SetAmbientLightValue (ambience multiplier + day/night colours, restored when off), InteriorLightingManager.Update (fill colours, window colour/_EmissiveStrength: original×mod, or TLD's frame value×mod for group-managed materials), ILM.UpdateLightShaft, LightShaftGimble.UpdateLight, LightShaftTod.Update (shafts × weather strength), DarkLightingManager.UpdateConstant/NonTod/ColouredLights (fill level). Left out: "true sun", debug keys/markers, LightTOD.
  - If the MelonLoader AmbientLights is installed too (lights named XPZ_Light_* exist), ours yields (log warning).
  - On/off: solo `coop.ini [ambient] enabled`; host sends `Msg::Features` (43, u32 bits, kFeatureAmbientLights) on link and on change; client follows (off until it hears). Levels `[ambient] intensity range night ambience fill fill_color aurora_*` are per player (defaults = Karol's config.json: 1.1/1/1/0.8/0.7/0.5).
  - Menu: TLD's setup grid has only 8 rows, all used → on the SETUP screen the NAME row became **AMBIENT LIGHTS: ON/OFF** (host; joiner sees HOST'S, no arrows). The lobby still lists names.
  - README / INSTALL_DLC updated (v35, credits, remove the MelonLoader copy).
  - **Next:** CI build (check MSVC warnings/errors), then Karol + friends test in an interior (e.g. CampOffice, Dam) with ON and OFF; check coop.log "ambient:" lines.
- **18:53 Karol:** keep the NAME row; there will be **more built-in mods** like this.
- **Changed:** NAME row restored. New **MODS page** in `coop_menu.cpp`: a MODS button in the setup's bottom bar (host only, where LET IN sits in the lobby) swaps the rows for one row per mod (`kMods[]` table: name + help; rows `kModRows[] = {kMode, kCharacter, kNames, kRadar, kChart}` – the rows that have arrows, so up to 5 mods per page now; more needs paging). Rows/arrows toggle ON/OFF, BACK/CONFIRM/Esc/Enter return to the setup. Values in `g.mod_on[]` (load_choices from settings, saved in close_menu when hosting).
  - **To add a mod:** append to `kMods[]`, map its `g.mod_on[i]` in `load_choices()` / `close_menu()`, a `config::Settings` field + coop.ini key, a bit in `net::kFeature*` sent by the mod's module (copy ambient_lights host_frame/client_frame), and its own module like `ambient_lights.cpp`. Consider moving the Features send/receive into one shared module once there are two mods.
- **CI 41102428734 (MODS page + Ambient Lights): success, no warnings, ambient_test passed.** Zip: https://github.com/cgpt28641-ops/TLDCoop_DLC/actions/runs/41102428734 (artifact TLDCoop_DLC). Not yet tested in game.
- **19:19 Karol tested** (Milton GreyMothersHouseA, BankA): works, no errors in coop.log. Window light came on a few seconds after entering (our fixed 4 s settle).
- **v35 fix:** set up as soon as the scene's InteriorLightingManager exists and `m_Initialized` (checked every 100 ms); the 4 s wait only for scenes without it. Log: "ambient: <scene> set up N s after it came in". **Not yet tested.**

## DynamicTrees (30.10, 19:42) – request
Karol wants https://github.com/Fuar11/DynamicTrees built in like Ambient Lights. Checked (clone in scratchpad `dyntrees`):
- **No licence file** (BuildInfo: "Copyright © 2023-2024", authors Fuar, The Illusion, Jods-Its; textures by Jods). Without a licence it's all rights reserved: we may not copy its code or textures into TLDCoop without the authors' permission.
- What it is: ~1200 lines C# (MelonLoader + ComplexLogger). Snow on trees accumulates with snowy weather and wind, melts in clear windy weather (saved to a JSON); tree bark/foliage textures swapped by accumulation level (8 levels × pine/cedar) from an embedded AssetBundle `dynamictrees` (22 MB). Hook: RenderObjectInstance.Setup (instanced trees) + scene load texture replacement.
- Status: asked Karol how to proceed (permission from the authors / each player supplies the mod's own file / skip).
- **19:44 Karol: he has the authors' permission** → building it in (recorded in third_party/dynamictrees/NOTICE.txt).
- **DONE (not tested in game), protocol v36:**
  - `src/coop_features.*`: one place for the host's MODS choices: `set_own(kFeature*, on)`, `on(feature)` (a friend: the host's; off until heard), host's tree snow. Features msg = u32 bits + f32 tree snow, sent on link/change and every 5 s. Ambient Lights now uses it (its own send/receive removed).
  - `src/trees_math.h` (+ `tests/trees_test.cpp`, 14 checks): snow per game hour by WeatherStage/WindStrength exactly as DynamicTreeData, starting snow by weather, 9 states, texture index with the mod's random neighbour.
  - `src/dynamic_trees.cpp`: snow from TimeOfDay.GetHoursPlayedNotPaused deltas; per save in `TLDCoop\dynamic_trees.ini` [snow] <save id>; friend takes the host's. Textures: scans `Resources.FindObjectsOfTypeAll(Material)` 1 s and 6 s after a scene comes in for materials whose mainTexture is a pine/cedar bark texture (TLD's `TRN_TreeBarkPine_Snow_A`, `TRN_TreeCedarBark_Snow_B` or ours) → sets the bundle texture (per-material random pick), then `TerrainData.RefreshPrototypes()` on every Terrain. Outdoors only; restores TLD's textures when off. (Generic instead of the mod's per-region object paths / RenderObjectInstance hook.)
  - Bundle: `third_party/dynamictrees/tldcoop_trees.bundle` (= the mod's `Textures/dynamictrees`, 22 MB, Unity 2021.3.8f1 build) → CI copies it to `pack\TLDCoop\`.
  - MODS page: 2nd row DYNAMIC TREES; `[trees] enabled` in coop.ini. README/INSTALL updated.
  - **Risk to check in game:** the 2021.3 bundle loading in the current game (coop.log "trees: textures loaded" / "no texture X"), and that instanced trees really change (log "trees: N tree materials found", "materials changed").
- **20:01 log check (Karol, ChurchB → MountainTown):** Dynamic Trees works with the 2021.3 bundle: "textures loaded", 186 tree materials found and changed 1.3 s after MountainTownRegion came in. Ambient Lights in ChurchB still waited the full 4 s ("no indoor lighting manager": no initialized InteriorLightingManager found there).
- **Fix:** set up also as soon as TLD's TodAmbientLight.SetAmbientLightValue runs in the new scene (every interior has one; ≥0.3 s after the change). **Not yet tested.**

### 2026-10-30 — obrazy w grze (podgląd)
- Z catalog.json: tekstury obrazów (Assets/ArtAssets/Textures/Unique/OBJ_*Picture*, WallPictures, PosterGarden, SignPosters itd.) leżą w wspólnym bundlu `06fe64a087041e406a76f4539374d751.bundle` (351 MB, Unity 6000.0.60f1), format DXT1 Crunched (28).
- Wyciągnięte narzędziem w scratchpadzie (ufs/unfs.py + sf.py + crntool/crn2rgba z Unity-Technologies/crunch, gałąź unity). 17 tekstur → arkusz `obrazy_w_grze.jpg` wysłany Karolowi. Tylko lokalnie — assety Hinterland, nie do repo.
- Większość to atlasy (kilka obrazów w jednej teksturze): podmiana jednego obrazu = zamalowanie jego prostokąta w atlasie i podmiana Texture2D w runtime (jak w DynamicTrees: materiał z mainTexture o tej nazwie → nasza tekstura z naszego bundla).
- Czeka: Karol wybiera obraz i daje PNG.

### 2026-10-30 — własny obraz w ramce (custom_pictures)
- Karol wybrał obrazek nr 6 (wspinacz) z atlasu `OBJ_PictureFrame_GM_A` (512², DXT1 crunched) i dał swoje zdjęcie; wersja „malowana” (Kuwahara 9+5, lekkie odbarwienie) zaakceptowana.
- Miejsce w atlasie (512, od góry): x 215..498, y 335..498; obraz w atlasie leży obrócony (w grze pionowo). Nasz atlas budujemy w 1024 → x 430, y 670, 568×328.
- `assets/pictures/gm_a_6.png` (już obrócony, w orientacji atlasu) → `tools/pictures/gen_data.py` → `src/pictures_data.inc` (RGB wbudowane w DLL).
- `src/custom_pictures.cpp`: przy wejściu sceny (1 s, 6 s) i co 60 s szuka materiałów z mainTexture `OBJ_PictureFrame_GM_A`; buduje raz kopię: Graphics.Blit do RenderTexture.GetTemporary(1024) → Texture2D(RGBA32, mipy).ReadPixels → GetPixels32(0) → wklejenie naszego obrazka → SetPixels32 → Apply(true,true); nazwa `..._tldcoop`, hideFlags DontUnloadUnusedAsset; podmienia mainTexture. Nic z Hinterlandu nie jest w repo ani w zipie.
- Zawsze włączone (bez przełącznika). Logi: `pictures: ...`. Do sprawdzenia w grze: gdzie wisi ta ramka (przedmiot PictureFrame_GM_*), czy obraz ma dobrą orientację i kolory.
- Następne: obrazek nr 5 (Karol da zdjęcie).

### 2026-10-30 — Adaptive Arsenal wbudowany (MODS → ADAPTIVE ARSENAL)
- Mod https://github.com/Deaadman/adaptive-arsenal (GPL-3.0, Karol ma zgodę autora) przeniesiony do C++: `src/adaptive_arsenal.cpp/.h`, liczby w `src/arsenal_math.h` (test `tests/arsenal_test.cpp`, 12 sprawdzeń). Licencja i zgoda: `third_party/adaptive-arsenal/`.
- Bit `net::kFeatureAdaptiveArsenal = 4` (Features wysyła już u32 — protokół bez zmian, nadal v36). Config `[arsenal] enabled` (domyślnie 1). Trzeci wiersz na stronie MODS.
- Jak działa: hook vp_Bullet.Start (w wildlife_hits, `bullet_start_detour` woła `adaptive_arsenal::take_bullet`). Kula gracza (karabin/rewolwer w rękach) → kopia vp_Bullet (Instantiate, SetActive(false)), oryginał zniszczony; lot liczony co klatkę (Time.deltaTime, kroki 0.02 s, grawitacja, opór 0.1, prędkość wylotowa z moda ×0.5) z Physics.Raycast po odcinkach (maska: wszystkie warstwy oprócz Ignore Raycast/UI/Player/*Trigger*/*NoCollide*/*Weapon*, triggery pomijane; lista warstw w logu `arsenal: bullets hit layers`).
- Trafienie: kopia ustawiona 1 m przed punktem, obrócona w stronę lotu, Damage × (1 → 0.1 do 500 m), Accuracy (u TLD = zasięg efektywny) = 1e4, Range = reszta zasięgu; SetActive(true) i oryginalny Start TLD (`wildlife_hits::start_bullet`) — więc obrażenia, krew, dźwięki, punkty umiejętności i synchronizacja trafień co-op idą starą drogą. Pudło (1000 m / 6 s): Start z Range 0.05.
- Rozrzut: celność moda (umiejętność karabinu / tier rewolweru + m_AccuracyRange; ×0.7 z biodra, ×0.8 stojąc, ×0.9 w ruchu) → do 10°.
- Smuga: GameObject z LineRenderer (materiał z Shader.Find: Sprites/Default…), znika 2 s po trafieniu.
- Rewolwer w ruchu: hook vp_FPSPlayer.Update (AimRevolver=18 i IsMoveInputUnblocked → InputWalk) i Panel_HUD.Update (chowa m_AimingLimitedMobility).
- Łuska na HUD: hook EquipItemPopup.UpdateAmmoStatus → kopia UISprite (m_ListAmmoSprites[1]) leci po krzywej 1 s.
- Nieprzetestowane w grze. Do sprawdzenia w logu: `arsenal: ... fired`, `arsenal: a bullet hit after ...`, lista warstw.

### 2026-10-30 — światło flary trzymanej przez znajomego
- Zgłoszenie: inni gracze nie widzą światła flary, którą trzyma gracz. Log Karola (21:55, gracz 'Shatsca'): kopia GEAR_FlareA ma LightIndoor/LightOutdoor, „holds a lit red flare” — czyli `light_up` je włącza, a światła nie widać.
- Poprawka „na pewniaka” w `set_light` (avatar.cpp): włączone światło trzymanego przedmiotu dostaje `renderMode = ForcePixel` (inaczej Unity może zrobić z niego światło per-vertex przy limicie świateł pikselowych — prawie niewidoczne) i `LightTracking`, jeśli model go nie miał.
- Diagnostyka: 2 s po zapaleniu log `avatar: GEAR_... lit, indoors/outdoors: <światło> on/off, intensity, range, mode, mask`. Jeśli dalej nie widać — z tego wpisu będzie wiadomo, czy światło jest wyłączane, ma 0 intensywności czy złą maskę.

### 2026-10-30 — walka z wilkiem u znajomego + kamera po śmierci
- Zrzut Karola: host walczy z wilkiem, u klienta awatar leży w swoim miejscu, a kopia wilka skacze obok „w powietrze”. Klipy walki TLD grają na korzeniu wilka (ofiara leży na nim, obrócona o 180°), a gracz hosta zostaje tam, gdzie stał. Poprawka: `wildlife_sync::fight_spot` (klient: wilk hosta w trybie Struggle w promieniu 6 m) → awatar leżący (`down`) przenoszony na korzeń wilka, yaw = wilk + 180 (log `fighting the host's wolf: shown under it`).
- Uwaga z logu: „the host's wolf #22 is gone” 2 s po początku walki (22:07:33) — możliwe, że TLD wyłącza wilka hosta w trakcie walki i capture go pomija (`!active(go_of(ai))`). Do sprawdzenia, jeśli wilk dalej znika.
- Kamera po śmierci (można chodzić, nie można się rozglądać): `controls_back` odblokowywał tylko flagę; walka zawęża RotationPitchLimit/RotationYawLimit vp_FPSCamera i one zostawały. Teraz: m_LockRotationLimit=false i limity wracają do DefaultRotation*Limit; `controls_back` woła się drugi raz 1.5 s po odrodzeniu. Log: `death: controls back: ...` (u gracza, który zginął).

### 2026-10-30 — śmierć zabiera 70% przedmiotów
- Prośba Karola: po śmierci losowo tracisz 70% przedmiotów, ale nie specjalnych. `leave_body` (coop_death.cpp): z przedmiotów w plecaku (bez wody) wybiera losowo `lose_percent`% zwykłych (dokładnie zaokrąglone), wyjmuje je i niszczy (Object.Destroy); reszta idzie do ciała jak dotąd. Specjalne (`special_item`): GearItem.m_CantDropItem, m_NarrativeCollectibleItem, m_MissionObject, m_BlueprintItemUnlock, GearItemData.m_CantDrop / m_RemainInInventoryOnDrop — nigdy nie giną. Woda idzie do ciała w całości.
- `coop.ini [death] lose_percent=70` (0 = nic nie ginie); ustawienie każdego gracza dla jego własnej śmierci. Log: `death: N of M item(s) lost (...): nazwy`.

### 2026-10-30 — znikające postacie, niewidoczna flara w ręce
- Znikanie: w logu Karola 46× „hidden: no updates from the other player for over a second”, zawsze obaj gracze naraz (host przestaje wysyłać na 1–2 s — przycięcie gry hosta). `kStaleMs` 1000 → 6000 ms: postać zostaje (stoi) przy krótkiej przerwie zamiast znikać.
- Flara w ręce niewidoczna (nie zapalona): kij flary (UncappedFlareMesh) jest prawdopodobnie pod m_FXGameObject, który `light_up` chowa, gdy flara nie płonie — zostawała tylko 5 cm nasadka w pięści. W `make_held`: wszystko na chwilę pokazane, największy MeshRenderer = główny model; jego rodzice zdjęci z fx_low/fx_high (zawsze widoczne), log `its model (...) was under ..., shown only while lit: shown always now`. Gdy niezapalone — `ParticleSystem.Stop()` na emiterach.
- Zapałki „średnio ułożone” (zrzut: pudełko przy nadgarstku, ręka opuszczona) — nie ruszane; potrzebny zrzut z bliska / opis, jak ma wyglądać.

### 2026-10-30 — zwierzęta tylko przy hoście (gra we 3)
- Karol: zwierzęta (zające, wilki) widać tylko tam, gdzie jest host; zając „zbugowany” (stoi). Log 22:23 (klient, 6085 linii): 0 ostrzeżeń i błędów.
- Przyczyna: TLD wyłącza regiony spawnu daleko od swojego gracza (SpawnRegionManager.m_SpawnRegionDisableDistance), więc w świecie hosta nie ma zwierząt przy znajomych daleko od niego. Poprawka (host): hook `SpawnRegion.UpdateFromManager` — region w zasięgu (m_SpawnRegionDisableDistance, min. 50, domyślnie 250 m) od znajomego dostaje na czas tej aktualizacji `m_OverrideDistanceToCamera = true`. Log: `wildlife: a spawn region near a friend kept on`. Nieprzetestowane: nie wiadomo na 100%, czy ta flaga TLD oznacza „nie wyłączaj przez odległość”.

### 2026-10-30 — naprawa mebli w trybie kryjówki dla wszystkich
- Karol: naprawiona szafka (tryb kryjówki) nie jest naprawiona u innych. TLD: `InteractiveObjects.RepairableInteractiveItem` (<IsRepaired>k__BackingField, m_FixedGameobject / m_BrokenGameobject, Deserialize(RepairableItemSaveState{m_Guid, m_IsRepaired})).
- Nowy rodzaj obiektu `net::ObjectKind::Repair = 7` (read_object_state akceptuje 1..7; wszyscy gracze muszą mieć nowy build). W world_objects traktowany jak zamek: `locks_into` co 3 s szuka ich (FindObjectsOfType), co 0.5 s patrzy na IsRepaired; naprawa → ObjectPut („repaired”); u innych `apply` woła Deserialize ze stanem naprawionym (i ręcznie: flaga + pokaż naprawiony / schowaj zepsuty, jeśli Deserialize nie zadziałało). Host ma je też na liście sceny dla wchodzących.

### 2026-10-30 — stabilność: przerwy 1–2 s u wszystkich
- Analiza logu Karola: przerwy „no updates” dla obu graczy naraz (host nic nie wysyła/nie dochodzi) i osobno tylko dla gracza 2 (jego łącze). Nie pokrywają się z niczym regularnym w logu klienta.
- Najbardziej prawdopodobne: limit wysyłania Steam (ISteamNetworkingMessages, domyślnie zaczyna od 128 KB/s). Duże wiadomości (lista obiektów sceny, ciało z zawartością, kontenery — dzielone na Chunki, okno 64 niezatwierdzonych, wysyłane ponownie co 250 ms) zapychają kolejkę i pozycje graczy czekają za nimi. Poprawka w steam_transport `begin()`: SetGlobalConfigValueInt32 SendRateMin 512 KB/s, SendRateMax 4 MB/s, SendBufferSize 4 MB (log `steam: send rate ...`).
- Druga możliwość: przycięcia gry hosta. Diagnostyka w core.cpp: każdy moduł w on_frame mierzony (`TIMED`), klatka > 400 ms od poprzedniej (lub nasze > 100 ms) → log `frame: N ms since the last frame (the game itself X ms, co-op Y ms: najdroższe moduły)`. Do sprawdzenia w logu HOSTA.

### 2026-10-30 23:03 — stan testów
- Karol z kolegami gra na razie na wersji SPRZED zmian z wieczoru (build Adaptive Arsenal, 19:48 UTC). Nieprzetestowane w grze: światło flary, walka z wilkiem u klienta, kamera po śmierci, 70% przedmiotów, znikanie postaci (6 s), flara w ręce, regiony spawnu przy znajomych, naprawa mebli, limit wysyłania Steam, log `frame:`. Najnowszy build: run 41134263322. Karol da znać, gdy zbierze więcej poprawek do zrobienia.

### 2026-10-30 23:20 — kamera po śmierci (dalej), bez NEXT SURVIVOR, zwierzęta stoją, kropki na mapie
- Log hosta (build 21:39 UTC, z naprawą mebli): śmierć hosta w walce z wilkiem 23:12:47; po RESPAWN `controls back` znalazło tylko limit pochylenia, a telemetria pokazuje yaw/pitch stałe (132.8 / 0.0) przy ruchu → kamera nie reaguje na mysz, ruch działa. Przyczyna nieznana, więc `controls_back` robi teraz dodatkowo: vp_FPSCamera włączony (Behaviour.enabled), PlayerCameraAnim.m_State → Idle (+RestoreMainCameraPos), PlayerStruggle.ResetGameSettingsToNormal(), InputManager.s_IgnoreInput=false, PopContext ekranu śmierci, ShowCursor(false); i loguje `death: input now: contexts [...], current ..., controls locked, control mode`. Jeśli dalej — ten wpis z logu gracza, który zginął, wskaże winnego.
- NEXT SURVIVOR usunięty: prawy przycisk ekranu śmierci schowany (`.../Buttons/RightButton`), `right_detour` nic nie robi; RESPAWN jako ta sama postać (config body).
- Zwierzęta stoją daleko od hosta: TLD robi z nich „imposter” daleko od kamery hosta (BaseAi.m_ImposterDistanceOnScreen/OffScreen, m_ImposterCullingMode). W capture (host), dla zwierząt w promieniu 250 m od znajomego, za każdym razem: Animator AlwaysAnimate, odległości imposter = 100000, tryb cullingu imposter = AlwaysAnimate. (Hook regionów spawnu nic nie logował — regiony przy Karolu i tak były w zasięgu hosta.)
- Mapa: nazwy znajomych są, strzałek nie ma (kopia PlayerIcon TLD jest wyłączona, gdy gra nie pokazuje gracza na mapie). Teraz kopia włączana (Behaviour.enabled), skala 1; gdy kopii brak — sprite `ico_map_directional`. Log `hud: map arrow for player N made`.

### 2026-10-30 23:40 — wilk biega w kółko; dołączanie do złego gracza
- Film + log hosta (coop_host8): po walce wilk biegał w kółko między graczami. W logu hosta `a wolf now pays attention to ...` co 1–3 s naprzemiennie między graczami 1/2/3/host (stoją blisko siebie). `pick_player` (wildlife_targets): zmiana celu teraz tylko gdy nowy gracz jest bliżej o ≥ 8 m i < 60% odległości (było 80%), i nie częściej niż co 8 s na zwierzę.
- Po wyjściu hosta: klient łączył się do „.|benek123|.” zamiast do Shatsca — Steam przekazuje lobby (z tym samym kodem) innemu członkowi, a klient brał pierwsze znalezione lobby i jego właściciela jako hosta. steam_transport: host zapisuje w lobby `tldcoop_host` = swój Steam id; klient bierze hosta z tych danych (właściciel tylko gdy ich brak), po wejściu sprawdza, czy host jest w lobby — jeśli nie, wychodzi i próbuje następnego z wyników; w stanie Joined co sekundę sprawdza, czy host dalej jest w lobby — jeśli wyszedł, klient wychodzi i szuka gry z kodem od nowa. Wszyscy (host też) potrzebują tego buildu.
- Odmowa dołączenia była na całą sesję hosta (`kept_out`): odrzucony gracz nie mógł poprosić drugi raz, a host nie widział już prośby. Teraz `kept_out` ma czas: odrzucona prośba 15 s, wyrzucenie (kick) 60 s. Tekst w menu klienta: „Press BACK, then you can ask again in a moment (15 s)…”.
- Drugi własny obraz: zdjęcie Karola (DSC_6778, łąka i góry) jako malowany w miejsce lewego górnego obrazu z `OBJ_PictureMisc_01` (atlas 2048, 4 obrazy krajobrazów; lewy górny = x 14..1058, y 24..870). Kuwahara 7+4, odbarwienie 0.72, chłodniej, faktura płótna; kanał alfa z poszarpanym brzegiem, żeby pędzlowa ramka TLD została. `assets/pictures/misc_1.png` (522×423, atlas budowany w 1024 → x 7, y 12). custom_pictures obsługuje teraz RGBA (mieszanie z atlasem).

### 2026-10-31 00:30 — po śmierci: przechylona kamera (host) i czarny ekran (śmierć we śnie, Karol)
- Log hosta coop_10 + film: po śmierci w walce z wilkiem i RESPAWN widok przechylony. `controls back` znajdował: „the camera's script (was off)” (vp_FPSCamera wyłączony — to blokowało mysz, już naprawione) i „the camera's animation” — także 1.5 s później, czyli PlayerCameraAnim wracał do animacji. Teraz: gdy PlayerCameraAnim nie jest w idle → Animator.Play(m_MainLayer_AnimState_Idle, 0) i (m_HipLayer_AnimState_Idle, 1), RestoreMainCameraPos, m_State=0; ramiona: PlayerAnimation.Trigger_Struggle_End, ResetStruggleStartEnd, FullResetAimingModes, RestoreCameraBasedHandPositioning.
- Log Karola: zginął w trakcie wspólnego snu („pogrążasz się w wiecznej ciemności”); po odrodzeniu czarny ekran (HUD widać), zegar skakał 22:53↔22:54, FPS spadał — TLD dalej „spał” (Rest.m_Sleeping, przyspieszony czas, wygaszenie). Nowe `coop_sleep::wake_after_death()` (EndSleeping(true), w razie potrzeby m_Sleeping=false ręcznie; PassTime.End; tryb co-op na Awake) wołane z `controls_back`; a gdy CameraFade.CurrentAlpha > 0.05 → FadeIn(1 s).
- `controls_back` po odrodzeniu: od razu, po 1.5 s, 4 s i 8 s. Log `death: input now:` ma teraz też obrót kamery (z = przechył), czy animacja kamery jest w idle, ile ekranu wygaszone i stan snu.
- Z logów `frame:`: przycięcia >1 s to głównie wczytywanie (paczka drzew ~1–1.8 s raz przy starcie), w grze hosta 0.4–0.5 s od samej gry, nie od moda.
- 00:53: po tej zmianie u hosta po odrodzeniu wiszące w powietrzu ręce z pierwszej osoby (kamera już w idle, a rig rąk dalej w pozie ze śmierci/walki). Zamiast Trigger_Struggle_End (usunięte): raz na odrodzenie `Animator.Rebind()` na PlayerAnimation.m_Animator (ręce) i PlayerCameraAnim.m_Animator (kamera), potem ResetStruggleStartEnd / FullResetAimingModes / RestoreCameraBasedHandPositioning. Log: „the arms' and camera's animations from their start”.
- 01:00: światło trzymanej flary/zapałki dalej niewidoczne. Nowy log (z poprzedniego buildu) pokazał: `GEAR_PackMatches lit ...: TLDCoop_Flame off`, `GEAR_FlareA lit ...: LightIndoor off ..., LightOutdoor off` — światła wyłączane po zapaleniu. Podejrzany LightTracking (TLD-owe śledzenie świateł). Teraz: LightTracking nie jest zachowywany w kopii ani dodawany (zapałka, set_light); `keep_lit` co klatkę włącza z powrotem światła, które powinny świecić (log `... was switched off; on again`, do 10 razy).
- 01:00: kucający znajomy „wisi” nad ziemią (zrzuty: zgięte nogi, stopy w powietrzu). Hipoteza: TLD podaje wyższą pozycję gracza po przykucnięciu. Przy przejściu w kucanie zapamiętujemy różnicę wysokości względem ostatniej pozycji stojąc (gdy w promieniu 0.5 m; maks. 0.8 m) i odejmujemy ją podczas kucania (log `crouched: TLD has them X m higher`). Jeśli log pokaże 0 — przyczyna inna (poza nóg w crouch_pose).

### 2026-10-31 01:10 — konsola deweloperska (na testy, łatwa do usunięcia)
- Karol: dodać https://github.com/DigitalzombieTLD/TLD-Developer-Console (MIT, zgoda autora), tak żeby dało się łatwo usunąć. `src/dev_console.cpp/.h`: raz (menu główne albo gra) tworzy prefab TLD `uConsole` przez AssetReference(GUID 5e8222cec53bee74d90412e9e11caec3).InstantiateAsync + niegeneryczne AsyncOperationHandle.WaitForCompletion (te same pola co generyczny), ustawia uConsole.m_Activate = F1 (`coop.ini [devconsole] key=F1..F15`, `enabled=0` wyłącza). Tylko wbudowane komendy TLD (dodatkowe komendy moda — tp, pos, gear_list — wymagałyby tworzenia delegatów; pominięte).
- Usuwanie: skasować src/dev_console.*, third_party/developer-console, linię w CMakeLists (source + `target_compile_definitions(... TLDCOOP_DEV_CONSOLE)`), bloki `#ifdef TLDCOOP_DEV_CONSOLE` w core.cpp, wpisy w README.
- Ten sam build: światło flary/zapałki (bez LightTracking, keep_lit), kucanie (crouch_rise), Rebind rigów po śmierci.
- 01:20: CRASH przy menu głównym u obu (FAULT GameAssembly+0x3bdd39, adres 0x135, zaraz po pierwszej klatce MainMenu) w buildzie z konsolą. Winna konsola: blokujące AsyncOperationHandle.WaitForCompletion na pierwszej klatce menu. Teraz: start 5 s po wejściu do menu, tylko InstantiateAsync (bez czekania; sprawdzenie, że operacja ruszyła), potem co klatkę patrzymy na uConsole.m_Instance (ustawia je Awake prefabu) i dopiero wtedy klawisz; 30 s limit. Wywołanie w __try/__except (SEH): błąd TLD = brak konsoli, gra działa dalej.

### 2026-10-31 01:45 — postać wstaje w trakcie walki, wilk zamarza w powietrzu, ręce po odrodzeniu
- Log hosta coop_host12: odrodzenie 01:34:57 poprawne (od razu i po 1.5 s: kamera (9,0,0), idle). Ale 01:35:00 wilk zaatakował nowego ocalałego od razu, a nasze sprawdzenia po 4 s i 8 s (`controls back: the struggle, ... screen's fade`) PRZERWAŁY tę walkę (BreakStruggle, kamera do idle, rozjaśnienie) — stąd wstawanie w trakcie walki, wilk zamrożony w powietrzu i ręce widoczne potem. Poprawka: opóźnione sprawdzenia nic nie robią, gdy gracz walczy (PlayerStruggle.InStruggle) albo tryb sterowania ≠ Normal — wtedy kończą się na dobre (log `the new survivor is busy ...`).
- Wolne klatki: wczytanie tekstur Dynamic Trees przy pierwszej scenie gry trwało 1.1–1.8 s w jednej klatce. Teraz ładowane w menu głównym (8 s po wejściu; log `trees: N textures loaded at the main menu`).
- W logach obu: brak FAULT/ERROR; konsola ładuje się poprawnie; „shown under it” działa u klienta; zwierzęta przy znajomych bez imposterów działa.

## 2026-10-31 (13:30) — bedroll in view / arms after respawn
- Host log 52f0f885: 3 deaths, camera checks clean, yet a bedroll in first person after respawn and the arms in the air again.
- Cause (likely): `Animator.Rebind()` on PlayerAnimation.m_Animator (the FP arms rig) put every animated prop of the rig back
  to its default, shown — the bedroll prop. It did not fix the arms either.
- Now (coop_death `controls_back` + `put_arms_away`): only the camera rig is rebound. With nothing in hand: at respawn
  DisableWeapon, ResetParams, m_State -> Hidden; HideHands if the hands are drawn; any other active renderer under the arms
  rig is SetActive(false) and logged ("the arms' rig's props in view, switched off: ..."). Late checks (1.5/4/8 s) do the
  hide/props part only when TLD's state is Hidden and nothing is held. Holding something: logged, left alone.
- Untested. Look in the log for "death: arms:" and "props in view".
- CI: artifact storage quota hit (20 GB of old builds). With Karol's OK all old artifacts were deleted except run 41151450359's;
  GitHub recalculates the quota only every 6-12 h. Meanwhile build.yml uploads winhttp.dll alone to the private prerelease
  `dll-latest` when the artifact upload fails (the run now ends green, with a warning). Download:
  https://github.com/cgpt28641-ops/TLDCoop_DLC/releases/download/dll-latest/winhttp.dll (build 1e9f623, run 41202336256).
  Bundles are unchanged since run 41151450359, so only winhttp.dll needs replacing.

## 2026-10-31 (14:10) — searched containers in a scene we weren't in
- Report: host in the Milton house, grzyb searched the bank; host walked in later: the bank's containers still to be searched
  (and empty once searched).
- Cause: a container's signature (what decides "changed, send it") was its items and weight only. A search that found
  nothing, or whose finds were all taken, left it equal: never sent, so the host's copy stayed unsearched.
- Now: the signature also carries m_Inspected and m_NotPopulated (the save text already carries m_InspectedProxy, so the
  other games apply it). A client also asks the host for a container's contents together with its lock request (before
  TLD's search), so a container already searched by someone opens as searched.
- CI: artifacts kept 2 days (retention-days: 2), so the storage doesn't fill again.

## 2026-10-31 (21:00) — Karol's list after the evening session (host log coop_host13, build 38ab54a: before the containers fix)
- Rabbit "killed" far from the host couldn't be picked up: it was STUNNED by the friend's stone (wildlife_hits
  apply_thrown "one stunned", 255 m from the host). A friend sees only a copy (no AI, no interaction), so a stunned
  rabbit can't be picked up there. Now the host kills it (ApplyDamage inf) -> carcass item for everyone (Stun only if
  that didn't kill it).
- Rabbits vanishing: no evidence in the host log. Added logs: host `wildlife: the X #id stops being shared: <why>`
  (TLD removed / switched off / out of range, distance to nearest player); `wildlife: the dead X stays an animal`
  (why a dead rabbit isn't made an item).
- ESC paused the world: Panel_PauseMenu.Enable hooked (class-name guarded); while it's open in a connected co-op game,
  GameManager.m_IsPaused is cleared before GameManager.Update, Time.timeScale set back to 1 if 0. Log "menu: ESC's menu
  open; co-op: the game is not paused".
- Flare light not visible: log showed LightOutdoor switched off again every frame (keep_lit turned it on, something
  turned it off). keep_lit now counts; at 3 it destroys the non-Unity scripts on the light's GameObject, at 8 all game
  scripts of the held copy; logged "keeps being switched off; N script(s) ... taken away: <classes>".
- Coffee cup on a stove (CookingPotDummy) not seen by others: a NEW pot was sent without its food, so the other game
  couldn't find the food. world_items scan now sends the food first ("went into a new pot").
- Sleep: bed list 30 MIN, 1, 2, 4, 8, 12 h, dawn, dusk, REST (renamed "REST (REAL TIME)": no skip, heals 1.5x; that
  is what "doesn't work" was). Wait list 30 MIN too. Rows smaller (pitch 16) for 9 rows. SleepChoice::M30 = 9,
  kSleepChoices 10; protocol v37.
- Host radar blank after a rope climb: the fog copy is redrawn every 10 s; `hud: radar shows ...` logged on change.
- Air temperature differences: local (indoors, height, fire), not sync. Containers searched by others: fixed in
  2715c1b (the host ran 38ab54a).
- README brought up to date.

## 2026-10-31 (21:15) — JustZoom built in
- WillWunderhorn/JustZoom (MelonLoader, permission given to Karol; repo has no license file) ported to
  src/just_zoom.cpp: hold Z (coop.ini [zoom] key) to zoom, wheel while holding 1.1x..20x, FOV via Camera.fieldOfView +
  vp_FPSCamera.m_RenderingFieldOfView, sensitivity via MouseSensitivity/m_MouseSensitivity, smoothed look by writing
  Angle/m_Yaw/m_Pitch/m_Current*/m_Target* (as the original). Runs in GameManager.LateUpdate (core), reset on scene
  change. Key read with GetAsyncKeyState (game window in front); wheel via UnityEngine.Input.mouseScrollDelta.
  Local only. third_party/justzoom/NOTICE.txt; README updated.
- CI (21:15): the account is on the free plan: 0.5 GB Actions storage (Karol's billing page; $0 budgets block any
  overage, so nothing is charged). A full zip is ~220 MB. Now: every push to main uploads winhttp.dll to the
  dll-latest release (not counted against Actions storage); the full zip artifact only on workflow_dispatch
  (retention 2 days). README Installation updated.
- 2026-11-01: README intro said 2.58; the mod runs on 2.59 189140 S (host log). Fixed.

## 2026-11-01 — one late check after a respawn
- coop_death: controls_back at respawn (0 s) plus ONE late check 8 s after (was 1.5, 4, 8 s; in every log only the
  8 s one found something: the camera's animation not idle). Still skipped while fighting / not Normal mode.

## 2026-11-01 — bug report key (F11)
- src/coop_report.cpp: F11 (coop.ini [report] key; GetAsyncKeyState, game in front, 10 s debounce) logs
  "==== REPORT #id ====" and packs coop.log, coop-prev.log, coop-prev2.log, coop.ini (copied with _SH_DENYNO: the log is
  open) into TLDCoop\reports\<stamp>_<name>.zip with System32\tar.exe -a (a background thread; a folder if tar fails).
  [report] all=1: Msg::Report (44, {u32 id, name}) to everyone; the host passes a friend's on; each game makes its
  own report with the same id. Protocol v38. Notices on the HUD. README updated.

## 2026-11-01 — gestures and grips redone (worked out on the rig offline)
- Karol: "all gestures look bad, items sit badly in the hands". Before: only a two-bone reach of the right arm to a
  point; the hand kept the clip's (limp) turn and fingers; items placed at "palm" by the shoulder frame.
- Offline preview (scratchpad pv/: extract.py reads tldcoop_bodies' Transforms, skinned meshes and legacy idle clip;
  rig.py does FK + skinning + a painter's renderer; pose_new.py is the Python twin of the new C++). Renders checked
  for every gesture over time and every hold (items as cylinders).
- avatar.cpp: Avatar.rig (fingers, clavicles, Neck, Spine2 by name). Hand turned by its knuckles (fingers = wrist ->
  Middle1, across = Pinky1 -> Index1, palm = fingers x across, mirrored for the left), orient_hand / curl /
  thumb_curl / hand_shape (Flat, Open, Point, Grip, Fist, Beckon). All clips but "down" key every finger, so the
  additive bends reset each frame; gestures skip over action clips (they might not).
- Gestures: wave = upper arm out, forearm up swinging from the elbow (22 deg, 1.9 Hz), flat hand a beat behind,
  clavicle up, spine away; point = arm along the look, index out, spine/clavicle turned; come = palm up, wrist and
  fingers drawing in; stop = flat hand pushed forward, palm out; yes/no = neck+head share, decaying. In with a little
  overshoot (ease-out-back), out eased.
- Holds: wrap per kind (Stick: through the fist index-side forward; Pistol: round a grip under the gun; Hang;
  Support: the other hand under, palm up, its palm placed on the spot; Palm: stone). Item grip = palm centre (pistol:
  5 cm above the fist). held_pose.h: rifle low = port arms, rifle aim / revolver aim / spear set re-tuned so the
  left hand reaches.
- 10:30 Karol: flare must be upright; some arm poses unnatural. held_pose.h: torch/flare/match upright (dir 0.05,0.98,0.18)
  out to the side; flashlight lower with the arm out (was up by the face); spear set at the hip, left hand forward
  (left palm now within 3 cm of the shaft on the rig; was 11 cm short). Preview: outputs/przedmioty_w_rekach_v2.png.
- 10:50 "more realistic": a raised arm lifts its clavicle (lift_shoulder: (elevation-55)*0.3, max 16 deg; reach2 =
  reach, lift, reach); half the hand's twist about the forearm goes to Forearm_jnt (relax_wrist, once a frame); rifle
  aim: right elbow out, left under, spine/neck lean 6, head 8 (Hold.elbow_out/lean/head); revolver aim lean 3/head 4;
  spear set re-tuned (left palm 3 cm from the shaft). Fixed: finger bends were applied 3-4 times a frame for the
  supporting hand (wrap_hand in the settle loop) - now once.
- 11:10 Karol: the left hand sat badly on the rifle (port arms). It held it from under, palm up, beside the barrel. Now Hold.left_fist (rifle low, spear set): the left hand closes round it (Stick, Fist); palm_centre moved into the curled fingers (fingers 0.075, palm 0.04 m: measured on the rig), so items run through the fist. Preview outputs/karabin_lewa_reka.png.

## 2026-11-01 (12:00) — chat (ENTER) and the bug report's description
- src/coop_chat.cpp: subclasses the Unity window's WndProc (EnumWindows, UnityWndClass): while typing, WM_CHAR builds
  the line (UTF-16 -> UTF-8, Polish letters), WM_KEY* are swallowed; InputManager.s_IgnoreInput held true while
  typing. ENTER opens it (in game, connected, no TLD screen: InputManager.m_CurrentContext null). Msg::Chat (45,
  {who, text}), host relays; protocol v39. Lines 15 s, 6 at once (8 while typing).
- coop_hud: chat box bottom left in TLD's look (a clone of TLD's hover-text box behind TLD's hover-text labels);
  coop_hud::set_chat each frame.
- coop_report: F11 on our side opens the box ("OPISZ BŁĄD (ENTER - zapisz, ESC - pomiń):", 120 s timeout); the
  description goes into the log and opis.txt in the zip; the others' games pack at once as before.
