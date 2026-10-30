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
