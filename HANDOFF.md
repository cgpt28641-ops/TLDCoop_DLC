# TLDCoop_DLC — HANDOFF

## >>> START TUTAJ: stan na 04.11.2026 09:25 (czytaj najpierw to; niżej starsza historia, góra pliku od "The user" jest NIEAKTUALNA tam, gdzie się kłóci z tym) <<<

### Karol i jak z nim pracować
- Karol (Polak, nie programista; w grze "Kerel_007_" / na Discordzie "kerel123"). Pisz po polsku, krótko i prosto. Gra z hostem **Shatsca** i z **szymongrzyb2017** (Discord "szymongrzyb.").
- Zasada: **najpierw napisz przyczynę/plan, zanim coś zmienisz**, gdy pyta "przeanalizuj" / "napisz zanim coś zrobisz". Gdy mówi "rób wszystko" / "działaj" / "popraw odrazu" — działaj sam.
- **Po każdym zbudowanym pliku:**
  1. link do pobrania: https://github.com/cgpt28641-ops/TLDCoop_DLC/releases/download/dll-latest/winhttp.dll (CI wrzuca tam winhttp.dll przy każdym pushu na main); Karol 04.11: NIE robić nowego zipa, jeśli nie trzeba — tylko ten link (zip, ręczne uruchomienie workflow, tylko gdy zmienia się coś poza dll, np. tekstury drzew);
  2. wpis w tym HANDOFF + kopia do /mnt/user-data/outputs/HANDOFF.md;
  3. post na Discordzie, grupa **ICARUS**: co zrobione + link, z dopiskiem **~CLAUDE~** (zgoda Karola).
- Za każdym razem czytaj ICARUS i pobieraj **najnowsze** logi (zipy F11) + logi Karola z jego komputera.
- Karol sam loguje się wszędzie; nigdy nie wpisuj haseł/kodów.
- Karol chce mieć dostęp do Ciebie z telefonu; komputer zostaje włączony (uśpienie: nigdy, blokada dynamiczna wyłączona, aplikacja Claude otwarta, ekran wygaszony). **Gry za niego nie obsługujemy** ("tego nie robimy") — 04.11 rozważał, żebyś sam testował grę (nowy zapis co-op jako host bez graczy, NIE ruszać zapisu nr 28 = `sandbox28`, jego zapis klienta; świat hosta to `sandbox25` u Shatscy), ale **odłożył to** ("narazie odpuszczamy").
- Twarde zasady projektu: assetów Hinterland nie commitujemy/publikujemy (.bodycut lokalnie); paczki DynamicTrees nie rozsyłamy osobno (repo prywatne); self-updater i Adaptive Arsenal usunięte; JustZoom zawsze włączony; bez powrotu do starego formatu plików pokoi; **"pokoje w miejscu" (bez offsetu 80 m) tylko po wyraźnym "tak" Karola**; ruchu drzew/liści **nie ruszać** (Karol: "tego nie ruszaj").

### Repo, build, narzędzia
- Repo prywatne **cgpt28641-ops/TLDCoop_DLC**, gałąź main, lokalnie /home/claude/tldcoop_dlc (sklonuj, jeśli nowej sesji brak). C++ IL2CPP, winhttp.dll proxy, MinHook (hooks::install_method; hook na już zahookowanej metodzie idzie "na wierzch").
- CI: GitHub Actions (windows, MSVC). Status: `gh api repos/cgpt28641-ops/TLDCoop_DLC/actions/runs?per_page=5 --jq '.workflow_runs[]|"\(.head_sha[0:7]) \(.status) \(.conclusion)"'`; błąd: `gh run view <id> --log-failed | grep error`. Pełny zip (z ciałami, ~220 MB) tylko przez workflow_dispatch.
- Uwaga MSVC/Windows: `near`, `far`, `min`, `max` itp. to makra — nie używaj jako nazw zmiennych.
- **Protokół: v48** (`kProtocolVersion` w src/net/protocol.h). Wszyscy muszą mieć ten sam dll. v48 dodało: ObjectKind::Door=8 (drzwi/szafy OpenClose), Msg::LogsWant=54, Msg::LogsFile=55; WorldMsg ma na końcu opcjonalny float `aurora`.
- **Most do komputera Karola** (narzędzia mcp__remote-devices__*): foldery `D:\SteamLibrary\steamapps\common\TheLongDark\TLDCoop` (logi coop.log/coop-prev*.log, coop.ini, reports\, backups\, tldcoop_trees.bundle) i `C:\Users\Karol Dusza\Downloads`. Pliki: device_stage_files (do /mnt/user-data/uploads/...), zapis: device_commit_files ze stagedPath w /mnt/user-data/outputs/.
- **Discord (wbudowana przeglądarka, karta "seed")** = ICARUS: https://discord.com/channels/@me/1397990296977145877
  - Czytanie wiadomości/załączników: javascript_tool na `li[id^="chat-messages"]` (autor, czas, treść, linki cdn.discordapp.com).
  - Pobieranie załącznika: nowa karta (tabs_create) + navigate na URL cdn → plik ląduje w Pobranych Karola (lub obrazek się wyświetla — wtedy screenshot/zoom karty), potem device_stage_files.
  - Pisanie: find "Napisz na ICARUS" → klik w textbox → type; nowe linie shift+Enter; sprawdź `document.querySelector('[role="textbox"]').innerText`; Enter.
  - Wysłanie obrazka: przez przeglądarkę się nie da; zrobione przez computer use na aplikacji Discord Karola (zgody: Discord app-1.0.9265\discord.exe + Eksplorator plików w trybie click; plik najpierw do Pobranych przez device_commit_files; "+" → "Prześlij plik" → dwuklik pliku → podpis → Enter; na końcu computer_release_lock).
- Zipy F11 zawierają `./opis.txt` (ścieżka z "./") — czytaj pythonem zipfile.
- Analiza logów: tools/logs/ana.py (lista sesji, błędy/ostrzeżenia zbiorczo; wejście: plik json z listą ścieżek logów). Lokalne kopie logów z 03.11 były w /root/logs (znikną z sesją).
- Tekstury drzew: tools/trees/tone_snow.py (+ sf.py, lz4d): rozpakowuje paczkę UnityFS, miesza zaśnieżone zestawy z Clear (K=0.55), własny koder DXT5, zapisuje paczkę (1 blok LZMA). pip z PyPI zablokowany w tej sesji (proxy) — UnityPy niedostępne, stąd własne narzędzia.
- Typy gry: zrzut tld_types.txt (w folderze TLDCoop Karola i w uploads tej sesji), global-metadata.dat w uploads (strings).

### Stan na 04.11 rano (wszystko zbudowane, NIC z tego nie testowane w grze)
- 31e88e2 + wcześniejsze dziś:
  1. **Widoki z pokoju na zewnątrz oświetlane światłem dworu** (było: drzewa kremowe/ziemia czarna patrząc z domku przez drzwi o zmierzchu). seamless_windows.inc: g_out_cams, view_sky_on/off w Camera.FireOnPreCull/PostRender (ambient dworu g_sky_out + światła pokoju wyłączone na czas tej kamery).
  2. **Dynamic Trees mniej białe**: nowa third_party/dynamictrees/tldcoop_trees.bundle (11 MB, zestawy zaśnieżone zmieszane z Clear). Wgrana Karolowi do TLDCoop\ (stara w TLDCoop\backups\tldcoop_trees_stary.bundle). Ryzyko: gra mogła nie wczytać przepakowanej paczki — sprawdź w logu "trees: textures loaded" / brak "could not load". Znajomi dostaną ją w pełnej paczce.
  3. **Diagnostyka płasko-kremowych drzew** (screen Karola: pnie i korony jednolicie kremowe bez tekstury, ziemia czarna, skały normalne — NIE rozwiązane): co minutę "trees: a tree material shows '<tekstura>'" i "interiors: sky's light r g b ... from ...". Następne logi to rozstrzygną (biała/brak tekstury vs przepalone światło). Test proponowany: host wyłącza Dynamic Trees w MODS.
  4. **v48: drzwi/szafy/szafki w zwykłych interiorach (OpenClose) zsynchronizowane** (world_objects.cpp Kind::Door, klucz "o/", odpytywane w obie strony, Open/Close(false,false)).
  5. **Skrzydło drzwi SI** szukane też u rodzica/dziadka LoadScene (raport Shatscy 22:32: "buguje się tekstura przy drzwiach i skrzydła drzwi nie działają", log "no door leaf found"). Gdy nadal brak — log z pełną ścieżką obiektu.
  6. **ESC → "POBIERZ LOGI"** (tylko w co-op, coop_save.cpp + coop_report.cpp): pakuje nasze logi, prosi resztę; ich zipy przychodzą w częściach do TLDCoop\reports\<czas>_logi_wszystkich\.
  7. **Okna przesunięte** (rysunek Karola: widok przez okno przesunięty wzdłuż i w poprzek ściany, nic nie zasłania): niesparowane okna → najbliższa szyba budynku w tę samą stronę / średnia korekta sparowanych; w obie strony.
  8. **Szyba i szprosy na widoku**: kolejka widoków 2450 (przed szybą gry), z zewnątrz płaszczyzna 3 cm za szybą (kOutsideInBehindGlass). Jeśli z zewnątrz okna zrobią się ciemne/puste → cofnąć to.
- **Półki/szafki "druga osoba musi przeszukać jeszcze raz"**: logi z problemem (03.11 22:31–22:40) były z dll 21:20 u hosta; przyczyna (meble z budującego się pokoju wysyłane pod tymczasowym id → "container X isn't in our CoastalRegion") naprawiona 23:20 (przekluczanie). Czekamy na logi z nowej wersji. Pomysł na dalej: zmiana z nieznanym id nie przepada, tylko czeka na zbudowanie pokoju.

### Otwarte / do zrobienia
- Płasko-kremowe drzewa (pkt 3) — przeanalizować nowe logi.
- Wilk-pacynka: niezgodny prefab (88 vs 93 bajty parametrów animatora).
- Mapa: odkrywanie przez innych i gracze z innych regionów na mapie (wymaga zmiany protokołu).
- Teleport przy wchodzeniu do domku (wymaga "pokoi w miejscu" — tylko po "tak").
- Drzwi, przez które nie dało się wejść ponownie — w logu są linie "door used"/"ignored"; przeanalizować.
- Ślady stóp innych graczy (avatar_tracks, kEnabled=false) — można włączyć, jeśli czarna ziemia okaże się niezwiązana.


---
# (starsze) TLDCoop_DLC: handoff for the next assistant (30.10.2026, updated 17:35)

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
- 13:30 Karol: chat doesn't work (no details, no log; his PC not reachable). Added logs: why ENTER doesn't open it (not in game / not connected / window not in front / not hooked / which TLD screen holds the input), when the line opens, the first WM_CHAR, sent/dropped, the chat box widgets made, and a warning when the line is open while TLD's HUD is hidden.

## 2026-11-01 (17:30) — report 17:14 (host): chat works; bear vanished when the host went into a cabin
- Log: chat sends/receives fine. Two faults: (1) the ENTER that opened the line often also sent it empty (its WM_CHAR
  '\r' came after the open): now the opening ENTER's character is ignored until its key-up; (2) "chat line open but
  TLD's HUD is hidden": TLD hides its NonEssentialHud now and then; the chat box now has its own root under the HUD
  panel itself (TLDCoop_Chat), drawn before the names/radar's visibility check.
- Bear: host LakeRegion -> CampOffice at 17:13:48; its game unloads the region, the friend's game stops mirroring
  ("different scenes") and showed its own animals elsewhere. Now on that stop each live host copy is taken over by one
  of our own animals of that kind (nearest), put where the copy was, facing the same, with its hp. Not done the other
  way (host back in the region: its fresh animals replace the friend's).
- 17:35 Karol: Polish letters small among capitals ("LEśNICZóWKA" for a friend elsewhere): upper() was ASCII-only in coop_hud/coop_menu/coop_death; now text_upper.h utf8_upper (CharUpperBuffW).
- 17:45 Host back in a scene a friend stayed in: the friend (wildlife_sync on_frame, g_took_over_scene set when its
  animals took the host's places) sends Msg::AnimalHandover (46; scene, kind/pos/yaw/hp of its live animals within
  400 m), protocol v40; the host (apply_handover, at each capture, up to 15 s as its animals spawn) moves its own of
  the same kind (nearest, each once) to those places. Logs: "the host is back in ...", "player N ran ... while we
  were away", "N of our animal(s) took the places of the friend's", summary. Karol: no auto-update (asked).

## 2026-11-01 (18:00) — pink bullet wedge
- Karol's video: a huge magenta wedge behind revolver/rifle shots = Adaptive Arsenal's trail (LineRenderer with
  Unity's "Sprites/Default": draws as the error shader in TLD, 0.4 m wide). Trails now off unless coop.ini
  [arsenal] trail=1, and 2 cm -> 5 mm wide when on.
- Self-update with a token built into the DLL (private repo) was written but NOT shipped: a credential inside a file
  handed to friends can be dug out. Asked Karol; alternative: the host's game hands its winhttp.dll to friends over
  the co-op link (no token, no public repo).
- 18:10 Karol: animals take no damage when shot. Adaptive Arsenal flew the bullet and restarted TLD's vp_Bullet
  just before the hit point; that dealt nothing (no "hit" logs at all). Now wildlife_hits::arsenal_impact deals it
  from our own raycast hit, as the original mod does (LocalizedDamage scale/bleed, kill chance, SetupDamageForAnim,
  ApplyDamage, MaybeFleeOrAttackFromProjectileHit, skill point): host/alone on the BaseAi above the collider; a
  friend on its copy of the host's animal sends the Hit as before. TLD's bullet is then restarted with Damage 0 for
  the impact only (spawn_impact_detour skips it). Log: "wildlife: our rifle hit WILDLIFE_x (collider): N damage".
- 18:10 Self-update shipped at Karol's explicit request ("only trusted people will have it"): src/coop_update.cpp
  as described above (main menu, 3 s in, background thread; GET releases/tags/dll-latest with the built-in
  read-only token; build id from the notes vs kBuild; download asset, MZ check, .new -> swap, .old deleted next
  start; message on the co-op screen and HUD). CI step "Update token and build id" writes src/update_secret.inc
  (gitignored) from secret UPDATE_TOKEN. Until Karol adds that secret the updater stays off ("no update token").

## 2026-11-01 (wieczór) – raporty F11 z gry na 3 osoby (build 8235358)
- Walka: gdy ktoś inny zabije zwierzę, które nas trzyma, nasza walka kończy się od razu (PlayerStruggle.MakePartnerFlee) – raport Kerela.
- Walka z łosiem „zawieszona” (Szymon: „widzę niebo i nic więcej”): po 12 s OnMooseButtHeadAttackComplete, po 35 s MakePartnerFlee, po 40 s BreakStruggle.
- Host: walka zwierzęcia z graczem, który wyszedł z gry, kończy się od razu (wcześniej niedźwiedź stał 45 s i nie reagował). Zwierzę zastrzelone przez kogoś w trakcie walki ginie od razu.
- Adaptive Arsenal (host): pomija warstwę CharacterControllerCollideOnly; trafienie w zwierzę bez strefy trafień = zwykłe obrażenia; log „a bullet of ours hit 'X'” dla pudeł.
- SkyCoop przejrzany (lista mechanik dla Karola w czacie).
- Do sprawdzenia: czat – w logach dużo pustych linii (ENTER bez tekstu).
- Czat nie otwiera się ENTER-em, gdy otwarta jest konsola F1 (uConsole.m_On) ani w klatce, w której się zamknęła.
- „Zablokowana kamera i zniknął HUD” u hosta 18:49:16–18:49:33 = okno opisu F11 (wtedy wejście gry jest wstrzymane) – nie wyjście Szymona.

## 2026-11-01 19:40 – auto-aktualizacja usunięta (prośba Karola)
- Usunięte src/coop_update.*, krok CI z tokenem (UPDATE_TOKEN), wininet, komunikat w menu co-op. dll-latest zostaje jako link do pobrania.
- Sekret UPDATE_TOKEN i sam token na GitHubie Karol może usunąć (nie są już używane).

## 2026-11-01 20:40 – raporty z gry 19:46–20:26 + usunięcie Adaptive Arsenal
- Adaptive Arsenal usunięty całkowicie (prośba Karola: psuł strzelanie): pliki, test, NOTICE, menu MODS, coop.ini [arsenal], README. Bit kFeatureAdaptiveArsenal zostaje nieużywany.
- Host wchodzi do budynku: „host loading” traktowane jak wyjście hosta (nasze zwierzęta zajmują miejsca jego kopii; po powrocie wracają do hosta). Tusze zwierząt zostają u znajomych, póki host jest w innej scenie (wildlife_sync::host_away_here).
- Host ładuje scenę: przez 4 s po zmianie sceny host nie wysyła zegara/pogody (był błysk 15:36 / „Clear 0%” → siwy ekran / rozjaśnienie u innych).
- Krew zwierząt: host wysyła krople krwi swoich zwierząt (BloodTrail.m_BaseAi) innym graczom.
- Światła trzymane (lampa naftowa, flara) wszędzie „off”: keep_lit pokazuje ukryte obiekty nadrzędne światła; describe_lights loguje, dlaczego światło jest wyłączone.
- Nadal otwarte: gdy host jest w budynku, każdy znajomy ma swoje zwierzęta (pełne rozwiązanie = „zastępca” hosta, jak w SkyCoop); „łoś stoi u jednych, u innych nie”; więcej zwierząt przy większej liczbie graczy (prośba Kerela).

## 2026-11-01 21:20 – zwierzęta zorzy; start portu Seamless Interiors
- Host wysyła flagę kAnimalAurora (BaseAi.m_WildlifeMode == 1); kopia u znajomych bierze model zorzy (region_asset(region, 1) albo zwierzę zorzy na miejscu).
- Raporty 20:48–21:04: Kerel miał stary build (z Arsenalem); u hosta i Szymona światło lampy/flary działa („sat under hidden 'FX'”).
- Karol chce Seamless Interiors (karsontr13/TLD-Seamless-Interiors, zgoda autora) przeniesione BEZ MelonLoadera, jak najdokładniej, na 2.59. ~60k linii C#. Plan etapami: rdzeń (wejście bez ładowania, host zostaje w regionie) → widok przez drzwi/okna → reszta. Klon w scratchpad/si, plan w scratchpad/si_port_plan.md.
- Lampy: leżąca lampa przyjmuje stan z rekordu drugiego gracza (KeroseneLampItem.Deserialize: paliwo, zapalona). Logi przedmiotów pokazują stan światła: [condition %, fuel %, lit / burnt x of y min, burning] – do diagnozy „flara zniszczona po wyrzuceniu” i „paliwo z pełnej lampy znika”.

## 2026-11-01 (noc) – Seamless Interiors, etap 1a (domyślnie WYŁĄCZONE)
- Nowy moduł src/seamless_interiors.cpp/.h (port SI karsontr13, za zgodą autora; NOTICE w third_party/seamless-interiors). Lista 126 budynków: tools/seamless/gen_configs.py → src/seamless_configs.inc (z kopii Configs.cs w third_party). Namalowane tła okien z ObjectsToDisable zostają włączone (brak widoku przez okna).
- Włącznik: coop.ini `[interiors] enabled=0` (domyślnie 0) i wiersz „SEAMLESS INTERIORS” na stronie MODS; w co-opie decyduje host (bit kFeatureSeamlessInteriors = 8 w Features). Wyłączone: wszystkie hooki od razu wołają oryginał.
- Co robi 1a: gdy gracz podejdzie na ~80 m do budynku, wczytuje (addytywnie) scenę wnętrza (+_SANDBOX/_DLC01), skleja pod Master_<id>_Interior 80 m nad budynkiem, przygotowuje jak SI (tryb oświetlenia „z zewnątrz”), losuje łupy deterministycznie (ziarno = zapis + budynek). Drzwi (hook LoadScene.Activate(bool) – wspólne miejsce dla kliknięcia, przytrzymania i wejścia w strefę) przenoszą gracza przez 0,25 s czerni. Wewnątrz: przestrzeń wewnętrzna gry, brak wiatru/wychłodzenia, wyciszenie Mild, wewnętrzna pętla wiatru, kapsuła i near clip, śnieg/mgła/plany śniegu, piwnica farmy, zabezpieczenia (wyjście bez drzwi, zablokowana flaga, brak obrażeń od upadku 2 s po teleporcie).
- Zapis gry czysty: przedmioty, szafki, meble i ogniska pokoi wyjmowane z list gry na czas zapisu; zapis pozycji gracza = punkt przed drzwiami.
- Drzwi zostają zwykłe (ekran ładowania), gdy pokój nie jest gotowy albo gdy zapis gry ma już dane wnętrza tego budynku.
- NIE ma jeszcze (1b i dalej): zapisu zawartości pokoi (pokój wraca do szablonu przy każdej wizycie w regionie; rzeczy włożone do szafek w pokoju PRZEPADAJĄ po wczytaniu; rzeczy upuszczone na podłogę zostają, bo zapisuje je gra), upływu czasu w zamkniętym pokoju (ogień/garnek stoją), co-opu (pokoje w miejscu, awatary 80 m w górze), okien/portali, trybu ciemnego, importu starych zapisów.

## 2026-11-01 (noc, później) – Seamless Interiors, etap 1b: pokoje pamiętają swój stan (nadal domyślnie WYŁĄCZONE)
- Nowy plik src/seamless_persist.inc (włączany do seamless_interiors.cpp), każda część z nazwą funkcji SI.
- Dane w naszym folderze zapisu: `<instance_dir>/worlds/<save_id>.interiors/` (ta sama tożsamość co plik world_keep: slot + id gry + ziarno, więc nowa gra w tym samym slocie ma nowy folder – zamiast znacznika tożsamości i CheckNewGameLootLock SI). Pliki tekstowe na budynek: `.placeables`, `.spawned`, `.gear`, `.containers`, `.state` (+ ogniska), `player.inside`, `loot.rolled`; pozycje lokalne pokoju.
- Etapowanie: zapis do `.pending` (SaveGameSystem.SaveSceneData), zatwierdzenie przy OnSaveCompleted bieżącego zapisu, odrzucenie przy RestoreGame, usunięcie przy DeleteSaveFiles. Nieczytelny plik kopiowany obok (`.unreadable_<czas>`) i nigdy nadpisany; niedopasowane rekordy zapisywane z powrotem bez zmian; budynek nie odtworzony w tej sesji nie jest zapisywany.
- Budowa: stałe id (StabilizeRuntimeGuids, GenerateDeterministicPDIDs w naszej mapie id), decyzja o łupach (flaga + plik → zawartość z plików, bez losowania), SpatialDeduplication, DestroyDuplicateGuidGear; potem nowy etap Hydrating (krok na klatkę): meble gracza (Placeable.FindOrCreateAndDeserialize; PlaceableSaveData czytane bez generyka: SerializationUtils.s_UseJsonNet ? JsonConvert.DeserializeObject(text, type, settings) : FastJson.Deserialize(text, type)), pozycje mebli, przedmioty (InstantiateAndDeserializeGearItem), szafki (1 na klatkę), stany (BreakDown, Harvestable, SmashableItem, OpenClose, Lock, WaterSource), ogniska (najpierw z pliku budynku, potem z zapisu regionu – FireManager.Deserialize przechwycony), PruneDuplicateFires, PurgeLeakedCloneGearDuplicates.
- Adopcja (zamiast „ukryte z pokojem”): przy zamknięciu pokoju i przed zapisem gry rzeczy pozostawione w pokoju (objętość + promienie podłoga/sufit) stają się dziećmi pokoju i zapisują się z nim; rzecz leżąca pod budynkiem jest „przenoszona” do pokoju (TryCarryStrayToParkedRoom).
- Zapis w pokoju: rekord gracza (pokój, pozycja lokalna, punkt drzwi). Po wczytaniu gracz czeka przy drzwiach w czerni, ten budynek buduje się pierwszy (krótsze czekania), potem gracz wraca na swoje miejsce. Rekord sprawdzany z pozycją zapisu gry (nieaktualny ignorowany); naprawa z pozycji zapisu jak w SI.
- Drzwi budynku z zapisanymi danymi, którego pokój jeszcze się nie zbudował, czekają (nie otwierają zwykłego wnętrza gry – to zablokowałoby pokój na zawsze).
- Czas w zamkniętym pokoju (TickClosedInteriorTime: ogień, żar, garnki, lampy, pochodnie, flary, suszenie), straż dekoracji w plecaku, zamrażanie fizyki przedmiotów z zapisu gry.
- Pominięte: RepairSpawnedPlaceables, przenoszenie przedmiotu „spadłego pod pokój” do drzwi, zapasowe ładowanie prefabów z Addressables (rekord bez tekstu gry zostaje jako sierota), junk/okna/import starych zapisów (etap 3), co-op (etap 2).

## 2026-11-01 (noc, etap 2) – Seamless Interiors w co-opie (nadal domyślnie WYŁĄCZONE)
- „Miejsce” gracza: State ma `place` (id budynku, w którego pokoju jest; "" na zewnątrz), protokół v41; nowa wiadomość Rooms (47): które pokoje są gotowe w grze nadawcy. Nowy plik src/seamless_coop.inc.
- Host jest autorytetem: buduje pokój, gdy KTOKOLWIEK podejdzie na 80 m (też znajomy wewnątrz pokoju, którego host nie ma), wysyła listę gotowych pokoi. Znajomy buduje tylko pokoje hosta, losuje szablon ziarnem świata hosta (WorldMsg.world_id), NIE czyta i NIE zapisuje plików pokoi (tylko własny player.inside dla ciała; hold po wczytaniu puszczany po 10 s, jeśli host nie ma pokoju), pomija has_vanilla_data. Drzwi znajomego czekają na pokój, który host ma.
- Wspólne id: po zbudowaniu pokoju (co-op) przedmioty dostają PDID = md5("coop|gear|"+id SI), ogniska md5(budynek+nazwa+miejsce) (stary PDID ogniska zostaje jako nasze id → klucze 1b bez zmian); skrzynie, obiekty (breakdown/harvest/zamki) i źródła wody mają klucz `si:<budynek>|<nazwa>|<cm lokalnie>` (room_key). Moduły world_* zamiast is_clone_object używają sync_skips (pomija tylko pokoje w budowie). Przedmioty w pokoju zamkniętym lokalnie liczą się jako leżące (active_in_closed_room); przedmiot od innego gracza w zamkniętym pokoju jest do niego wkładany (settle_in_room); ItemPut w miejscu niezbudowanego pokoju jest pomijany (przyjdzie z listą).
- Gdy pokój staje się gotowy: items/fires przyjmują jego zawartość jako wspólną (bez wysyłania), host po 2,5 s wysyła znajomym wszystkie listy jeszcze raz (netrt::resync, Together); pokój gotowy u znajomego → jego Rooms → host wysyła listy temu znajomemu.
- Awatary: gracz w pokoju, w którym nas nie ma, jest ukryty (i nie jest celem zwierząt); gracz na zewnątrz zostaje dla gracza w pokoju (zasłaniają go ściany; zwierzęta hosta dalej go widzą), ale HUD/radar go nie pokazuje – lista „gdzie indziej” podaje nazwę budynku. Interpolacja skacze przy zmianie miejsca.
- Zwierzęta: host lub znajomy w pokoju nie jest kandydatem na cel; host w pokoju dalej symuluje region (scena się nie zmienia, więc ścieżki „host loading / different scenes” się nie włączają).
- Dołączenie/odrodzenie obok gracza w pokoju: do jego pokoju, jeśli zbudowany u nas, inaczej pod drzwi (pokój budowany pilnie); obok gracza na zewnątrz – najpierw wyjście z naszego pokoju.
- Pominięte: przesuwanie mebli w pokojach (pokojowe placeables są poza PlaceableManager, więc world_objects ich nie widzi), sen (bez zmian: działa w regionie), znacznik przy drzwiach dla ukrytego awatara, okna. Wyłączone SI = wszystko jak dotąd (place pusty, brak Rooms).

## 2026-11-01 23:20 – crash przy starcie (Seamless Interiors)
- Log Kerela 23:12: 0xC0000005 w UnityPlayer.dll zaraz po „DynamicGI…” → mod wyłączony na całą sesję (dlatego „co-op się nie wyświetla”). Przyczyna: LayerMask.NameToLayer wołane w resolve() przy starcie moda (Unity jeszcze nie działa). Teraz resolve_layers() w on_frame, gdy jesteśmy w grze. Build 82b3d1e.

## 2026-11-01 23:55 – znikające przedmioty i duplikaty (raporty 23:33–23:39)
- Przyczyna „zniknęła cała woda”: przy przejściu przez drzwi TLD zabiera przedmioty sceny chwilę przed zmianą sceny; world_items u Szymona wysłał ItemGone (Destroyed) dla 14 przedmiotów, host (w tym domu) je usunął. Działo się też wcześniej (Kerel 23:21:57, 156 szt.).
  Teraz: przez 6 s po LoadScene.Activate (world_items::door_used, wołane z hooka seamless) i gdy naraz znika ≥4 i ≥ połowa udostępnionych przedmiotów sceny – nie wysyłamy „zniszczony”.
- Duplikaty (Kerel): przedmiot hosta, który u nas istnieje, ale jest wyłączony (nasza gra wylosowała go „precz”), tworzono drugi raz z nowym id; teraz włączamy nasz i stawiamy tam, gdzie host (poza plecakiem i pojemnikami).
- „ZAPISZ GRĘ” w menu ESC (src/coop_save.cpp): Panel_PauseMenu.ConfigureMenu postfix dodaje wiersz BasicMenu.AddItem (bez Action); BasicMenu.OnItemClicked/OnItemDoubleClicked/OnConfirm przechwytują nasz wiersz → Panel_PauseMenu.OnDone, a po 0,3 s GameManager.SaveGameAndDisplayHUDMessage.

## 2026-11-02 – Seamless Interiors: import starych zapisów (import_old) + poprawki 1b „nic nie znika”
- Nowy src/seamless_import.inc (SI LegacyImport): budynek, którego wnętrze jest w slocie zapisu gry, dostaje pokój zbudowany RAZ z tego zapisu: przedmioty z podłogi (m_GearManagerSerialized), szafki (m_ContainerManagerSerialized, dopasowanie po miejscu po przestawieniu mebli), stany BreakDown/Harvestable/OpenClose/WaterSource, płonące ogniska, meble przesunięte/zabrane i postawione przez gracza (m_PlacementListSerialized), skrzynki do suszenia (m_CuringStorageSerialized), wybory RandomSpawnObject.
- Odczyt bez generyków: SaveGameSlots.GetSaveSlotFromName(m_CurrentSaveName) → BaseState.m_Dict.TryGetValue(klucz) → EncryptString.DecompressBytesToString → deserialize_object(tekst, Type) (to samo co TryGetData gry). Klucz tylko pewny: „<scena>_<guid drzwi>”, guid drzwi albo nazwa sceny, gdy żaden inny budynek regionu jej nie używa; inaczej zwykłe drzwi.
- Bezpieczeństwo: slot tylko czytany. Pliki 1b robione w pamięci (g_overlay), pokój budowany z nich; zapis (staged, na dysk z najbliższym zapisem gry) + znacznik `<id>.imported` (klucz, hash rekordu, liczby) TYLKO gdy wszystko wróciło (każdy przedmiot, szafka z przedmiotami, mebel gracza, skrzynka). Inaczej pokój wyłączony, budynek przy zwykłych drzwiach, log „importing its interior from the old save stopped: …”, ponowna próba przy następnym wejściu do regionu. Wczytanie gry bez zapisu = import od nowa.
- Po imporcie drzwi czekają na pokój (są pliki), więc stare wnętrze gry się nie wczytuje (brak duplikatów). Jeśli rekord gry zmieni się po imporcie (SI wyłączone i wnętrze odwiedzone po staremu) – hash się nie zgadza → zwykłe drzwi, nasze pliki nietknięte. UWAGA dla Karola: wyłączenie SI po imporcie pokaże stan wnętrza z chwili importu.
- coop.ini `[interiors] import_old=1` (domyślnie). W co-opie importuje tylko host. Logi: „the save holds its interior from before the rooms…”, „IMPORTED from the old save: N item(s), N container(s) with M item(s)…”.
- Poprawki 1b: (1) SerializationUtils/FastJson szukane w Assembly-CSharp, a są w TLD.Serialization.dll → meble postawione przez gracza w pokojach NIGDY nie wracały (zostawały w pliku jako sieroty); teraz oba serializatory, drugi jako zapas. (2) begin_gear kasował każdy przedmiot regionu leżący w pokoju – teraz tylko kopię rekordu z pliku (ta sama nazwa ≤12 cm), resztę bierze do pokoju. (3) Zawartość skrzynek do suszenia zapisywana z meblem (3. pole .spawned); gdy nie da się jej przywrócić – stary rekord zostaje. (4) Hook LoadScene.Activate instalowany nawet, gdy reszta SI się nie rozwiąże (door_used z e53e011).
- Sprawdzone: adopcja przed każdym zapisem (adopt_before_save), przedmioty w zamkniętym pokoju liczone jako obecne (active_in_closed_room), door_used wołane dla każdych drzwi, też bezszwowych.
- Pominięte: junk (m_JunkManagedSerialized), zepsute dekoracje, zdjęcia w ramkach, klucz „ostatni nieprzypisany” SI dla budynków dzielących scenę (takie zostają przy zwykłych drzwiach).

## 2026-11-02 – Seamless Interiors: poprawki z trzech przeglądów (rdzeń, zapis/import, co-op)
Każde zgłoszenie sprawdzone w kodzie przed zmianą. Wyłączone SI = jak dotąd (przepuszczanie), poza: drzwi gry czekają, gdy ich wnętrze właśnie się wczytuje (C1), a dane usuniętego zapisu są przenoszone, nie kasowane (P2).
Rdzeń (seamless_interiors.cpp):
- C1 crash: zwykłe drzwi do wnętrza, którego scena wczytuje się dla pokoju (g_loading_base), spóźnionego wczytania (sierota) albo którego budynek jest w budowie – zablokowane („[PORTAL-BLOCK] … isn't ready yet”), inaczej skip_game_manager_awake niszczył prawdziwy GameManager.
- C2: wczytania porzucone (zmiana regionu, limit 45 s, nieudana budowa) trafiają do g_orphan_loads (przeżywa forget_region): ich scena dalej poza grą (GameManager, listy zapisu), po dojściu UnloadSceneAsync; poll_orphans co klatkę, także bez pokoi. Budynek z sierotą swojej sceny nie jest budowany; gdy gra sama wczytuje tę scenę (LoadSceneWithLoadingScreen) – sierota puszczona.
- C3: wyjmowanie z GearManager/ContainerManager i przesunięcie gracza w PlayerManager.Serialize cofane w destruktorach (wyjątek IL2CPP też je uruchamia); lista czytana ponownie przy oddawaniu.
- C4: referencje zarządzane (m_IndoorSpaceTrigger, m_TriggerID, m_HeatSource, m_Guid, sloty tablic lightmap/materiałów) przez il2cpp_gc_wbarrier_set_field (eksport opcjonalny, il2::store_ref).
- C5 SnowPlaneManager szukany najwyżej co 2 s; C6 kopie materiałów niszczone z regionem; C9 Scene.GetRootGameObjectsInternal + List<GameObject> (stara metoda tylko awaryjnie); C10 listy po indeksie; K11 korzenie pokoi w pamięci na klatkę, szybkie „nie” bez pokoi.
- C7: world_items::door_used tylko dla drzwi gry (ścieżka g_activate) i w GameManager.LoadSceneWithLoadingScreen (dołączenie, krawędź regionu, odrodzenie); drzwi bezszwowe i czekające już go nie wołają.
- C8: bez zmian poza P2 (commit/discard własnego folderu przy wyłączonym SI jest w porządku).
Zapis/import (seamless_persist.inc, seamless_import.inc):
- P1 WAŻNE: purge_leaked usuwa tylko przedmioty regionu obecne (i nieruszone) od początku hydracji (hydration_gear); hide_room: najpierw adopcja, potem purge – patyk położony obok identycznego nie znika.
- P2 WAŻNE: DeleteSaveFiles nie rusza danych aktualnie granego zapisu; inne foldery przenoszone do worlds/deleted/<folder>_<data>.
- P3: RestoreGame czeka na trwający zapis tego slotu (IsAsyncSavePending → WaitAsyncSaveComplete); jeśli dalej trwa – pliki .pending zostają.
- P4: OnSaveCompleted: commit tylko przy ASYNC(0)/SUCCESS(4); BUSY/ERROR/ERROR_SILENT – zostają staged (log).
- P5: zapis CreateFileW/WriteFile/FlushFileBuffers + MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH) z ponowieniami; nowe pliki „TLDCOOP-SI 2 <rodzaj>” ze stopką „#end <liczba>”; brak/niezgodna stopka = nieczytelny (kopia, blokada, nigdy nadpisany). Pliki „TLDCOOP-SI 1” (bez stopki) czytane jak dotąd i przepisywane do wersji 2 przy zapisie. UWAGA: starszy build moda uzna pliki v2 za nieczytelne (zablokuje, niczego nie straci).
- P6: save_* zwracają wynik; znacznik .imported dopiero gdy wszystkie pliki zapisane (inaczej czeka w Instance::import_marker na następny udany zapis); nieudany commit zostawia commit.failed, a wczytanie wtedy commituje zamiast wyrzucać.
- P7: rodzaj z nieczytelnym plikiem nie jest adoptowany (przedmioty/meble), .spawned zapisywany mimo zablokowanego .placeables; settle_in_room nie bierze do pokoju z zablokowanym .gear.
- P8: bliźniaki regionu i przedmioty gracza znalezione w pokoju przez begin_gear – usuwane/brane do pokoju dopiero na końcu hydracji (po import_complete); przy porzuceniu importu zostają w regionie.
- P9: noszona dekoracja po obiekcie (handle), nie po guid (budynki ze wspólną sceną); zapas „cała scena” w restore_placeables pomija inne pokoje; rekordy ognisk z zapisu regionu tylko gdy plik budynku nie ma żadnych FI i scena nie jest wspólna.
- P10 deterministic_ids z „#n” dla identycznych w tym samym miejscu; P11 adopcja przed zapisem tylko dla pokoi z folderem tego zapisu; P12 recursive_mutex na save_rooms i commit_import; P13 f2 przez std::to_chars.
Co-op (seamless_coop.inc, world_items.cpp, world_containers.cpp, coop_save.cpp, wildlife_targets.cpp):
- K1 WAŻNE: host: ItemPut znajomego do niezbudowanego pokoju i zabranie przedmiotu z pliku niezbudowanego pokoju czekają w g_pending (apply_pending ponownie przy rooms_generation); zmiany szafek „si:” czekają na pokój; znajomy: apply_list nie usuwa przedmiotów z pokoi, których host nie zgłosił. Nie zrobione: opóźnianie list hosta (zastąpione powyższym), ogniska.
- K2: Rooms wysyłane także bez pokoi (ids={}), pakiety Rooms zawsze odbierane; host_has wymaga hosta w tym regionie.
- K3: przez drzwi/„wszystko naraz”: Taken nadal wysyłany (tylko gdy przedmiot faktycznie w plecaku), wstrzymane tylko Destroyed.
- K4: wyłączony przedmiot hosta użyty tylko gdy po włączeniu jest w świecie (inaczej kopia), stan (sidła, puszka, światło, garnek) wczytany.
- K5 pokoje znajomego zapominane przy nowym połączeniu; K6 po pojawieniu się pokoju host wysyła pełne ItemPut (kItemAnswer) jego przedmiotów; klucz szafek/obiektów pokoju po guid mebla (gdy jest); K7 znajomy w pokoju = zniknął dla zwierząt; K8 teleport do nieznanego pokoju – pod jego drzwi; K9 unbuilt_room_at wg bryły/promienia pokoju.
- K10 przycisk ZAPISZ GRĘ: najpierw hooki kliknięć, ConfigureMenu tylko jeśli się udały; nasz wiersz po id.

## 2026-11-02 – Seamless Interiors: domknięte ryzyka K1 (pokój bez pliku), ogniska, znacznik importu
- Pkt 3 (format plików v1 + .sum) – POMINIĘTY na prośbę Karola: zostaje format v2 („TLDCOOP-SI 2” + „#end N”). Starszy build (3e6f36e i wcześniej) dalej uzna pliki v2 za nieczytelne (blokuje, nic nie traci).
- Przedmioty (host, world_items.cpp): zabranie/usunięcie przedmiotu przez znajomego, którego u hosta nie ma, czeka w g_pending, gdy w regionie jest pokój niezbudowany albo w budowie (nowe seamless_interiors::rooms_pending_build(scena); region, do którego nie weszliśmy = wszystkie jego pokoje niezbudowane) – też gdy pokój nie ma jeszcze pliku (przedmioty z szablonu, ten sam seed i te same id). Ponownie przy każdej zmianie rooms_generation; odrzucone z logiem („waited for the Seamless Interiors rooms; they are all up here and it isn't in any”) dopiero gdy wszystkie pokoje regionu stoją. Po wyjściu z regionu NIE kasowane: zostaje jak każda zmiana dla sceny, w której nas nie ma (g_pending, zapisywane w world_keep), i przy powrocie znów czeka na pokoje – inaczej pokój z szablonu oddałby przedmiot. Nowsza zmiana tego samego przedmiotu, stosowana od razu, usuwa starszą czekającą.
- Ogniska (world_fires.cpp): host: zmiana ogniska od znajomego z m_Position w niezbudowanym pokoju (unbuilt_room_at) albo rozebranie ogniska, którego tu nie ma, przy niezbudowanych pokojach regionu → g_store (zapisywane w world_keep), ponownie przy rooms_generation (apply_stored(true)); nowsza zmiana zastępuje czekającą. Znajomy: apply_list nie usuwa ani nie nadpisuje ognisk z pokoi, których host nie zgłosił (host_lacks_room_of) – ich rekordy i id w m_DestroyedFires wycinane z listy hosta przed wczytaniem.
- Import (P6): commit_import zapisuje też `<id>.imported-from` (treść jak znacznik); commit_dir commituje znacznik `.imported` na końcu i nie commituje go, gdy plik pokoju przed nim się nie udał. old_save_gate: brak znacznika + rekord gry + nasze pliki → jeśli `.imported-from` czytelny, hash rekordu gry bez zmian i wszystkie 5 plików (placeables, spawned, gear, containers, state) całe (stopka v2 zgodna, Records::whole) → pokój z plików i znacznik zapisany (staged; przy błędzie czeka w import_marker). Inaczej jak dotąd: zwykłe drzwi z logiem.
- Ryzyko: zabranie przez znajomego przedmiotu, którego host faktycznie nie ma (np. już go zabrał), też czeka do zbudowania pokoi – tylko log, bez skutków. Plików pokoju zatwierdzonych częściowo (crash w trakcie commitu, bez commit.failed) dalej nie odzyskujemy poza przypadkiem znacznika.

## 2026-11-02 – pełny przegląd moda (5 części) i poprawki
- Przeglądy: sieć/rdzeń, świat, zwierzęta, gracz/UI, budowanie/paczka. Poprawki: zwierzęta b91a22d (W1–W10), UI/wydajność/dokumentacja 85da22a (U1–U11, D1–D6), świat+sieć 54b84ed (G1–G11, N1–N6). Szczegóły w commitach.
- Najważniejsze: skrzynia hosta mogła zostać nadpisana starą kopią znajomego (G1); dawanie przedmiotu ginęło przy spóźnionym Done / zerwanym łączu (G2/G3); utknięcie w walce przy wyjściu hosta (W1); podwójne tusze (W3/W4); przycinanie przez skan wszystkich materiałów (U1); czat zjadał puszczenie klawiszy (U3).
- Kody dołączania nie są już zapisywane w coop.log (config.cpp, coop_menu.cpp) – log trafia do raportów F11.
- Otwarte: ack-before-destroy przy dawaniu (G3), zabicia znajomego pod nieobecność hosta nie trafiają do hosta (W5).
- „Pulpit zasrany screenami”: TLD sam robi zrzuty ekranu pod F8/F9/F10 (InputManager.ProcessScreenshotInput, KeyCode 289/290/291 – sprawdzone disasemblacją); mod używa F8 (gest stop) i F9/F10 (prośby o dołączenie). Hook pomija ProcessScreenshotInput, chyba że coop.ini [game] screenshots=1.

## 2026-11-02 – Seamless Interiors: partia A (A5–A8, A10–A12), protokół v42
- A12: drzwi czekające na pokój (budowa/hydracja, PORTAL-BLOCK) pokazują na HUD „Przygotowuję budynek...” (co ≤6 s), a gdy ten pokój stanie – „Budynek gotowy” (coop_notices::tell; też w grze solo).
- A10 (seamless_import.inc, SI FindLastUnclaimedInstancedKey): budynki z jedną sceną wnętrza – najpierw klucze po guid drzwi, potem „jedyny nieprzypisany klucz”, gdy nasze drzwi nie mają guid, a każdy inny budynek tej sceny ma swój klucz (guid drzwi albo znacznik `.imported`) lub wiadomo, że nie był odwiedzany. Klucz należący też do innego budynku – żaden go nie bierze. Budynek z guid drzwi, którego klucza nie ma, a wszystkie klucze sceny są cudze = nigdy nieodwiedzony → świeży pokój (wcześniej zwykłe drzwi). Reszta niejednoznaczna → zwykłe drzwi z logiem („not guessing (ambiguous…)”).
- A6 (avatar.cpp, coop_hud.cpp): znajomy w pokoju, w którym nas nie ma (ta sama scena) – Seen.at_door, pozycja = drzwi budynku (seamless_interiors::door_position; podpokój → drzwi rodzica): kropka na radarze, etykieta „IMIĘ (budynek)” na HUD, lista „gdzie indziej” bez zmian; awatar dalej ukryty.
- Mapa (M), prośba Karola: tylko kolorowe kropki (kolory jak radar), bez podpisów na mapie; legenda w lewym górnym rogu („GRACZE”, „TY” + imiona w swoich kolorach); nasza kropka też. Pozycja: WorldPositionToMapPosition pod MapElements + przesunięcie wyliczone z ikony gracza TLD (jej miejsce w MapElements minus nasze z tej samej funkcji). Log raz na otwarcie mapy: „hud: map … opened: TLD's player icon … shift …; NAME world → map → screen”. Gracz w pokoju = kropka przy drzwiach. Do sprawdzenia w grze: czy przesunięcie ~0 i kropki tam, gdzie trzeba.
- A7 (wildlife_targets.cpp): nasz gracz w pokoju (solo i host): hooki BaseAi.ComputeDistanceForTarget (1e6 m) i BaseAi.CanSmellSource (false) dla naszego AiTarget; co 0,5 s zwierzęta z m_CurrentTarget = nasz gracz: ClearTarget, a z Attack/Stalking/HoldGround/Investigate*/PassingAttack → Wander. K7 (znajomy w pokoju) sprawdzone: nie jest kandydatem, forget_gone_friend czyści cel.
- A5 (seamless_coop.inc, nowa wiadomość RoomPlace = 48, PROTOKÓŁ v42): meble/dekoracje pokoi – co 1 s skan placeables gotowych pokoi (stałe guid), zmiana (pozycja lokalna, obrót, skala, włączony) albo „gone” (w plecaku) idzie: znajomy → host (stosuje, zapisuje z pokojem, przekazuje innym), host → znajomi; pokój nie zbudowany → czeka (g_place_waiting); pokój znajomego gotowy → host wysyła cały pokój. Nic nie jest niszczone: „gone” = wyłączenie; brakujący mebel gracza tworzony z PlaceableSaveData (albo przesuwany, jeśli ten guid stoi w scenie). Mebel w rękach (przesuwany) nie jest „gone”. Ten sam kanał niesie „junk cleared” pokoju (guid „#junk”).
- A8 (seamless_persist.inc, coop.ini `[interiors] keep_rooms=1`): przy LoadSceneWithLoadingScreen do innej sceny (nie przy wczytywaniu zapisu) gotowe, zamknięte pokoje z folderem (≤6, najbliższe) → DontDestroyOnLoad; zatrzymane tylko te, które zapis przy wyjściu zapisał, tylko z ostatnio opuszczonego regionu, solo/host. Powrót (ten sam zapis, bez wczytania): odcisk (liczba i id przedmiotów, meble, szafki, ogniska) musi się zgadzać → MoveGameObjectToScene, align_room, register_fires, GearManager.Add brakujących przedmiotów, safehouse_fix, czas zamknięty += przegrane godziny (GetHoursPlayedNotPaused). Coś nie tak → zniszczony i budowany z plików jak dotąd. Zatrzymane pokoje liczą się jako „obiekty pokoi” (poza zapisem i listami co-op innego regionu). RestoreGame niszczy wszystkie.
- A11 (seamless_persist.inc, SI Junk.cs, hooki #22/#37/#38): SafehouseManager.StartCustomizing (flaga junk = pokoju), JunkManager.ClearJunk (tylko pokój, w którym gracz dostosowuje; reszta włączana z powrotem; czyszczenie regionu nie rusza pokoi), SaveSceneData (region zapisuje swoją flagę, RAII). Stan w pliku .state („JK|cleared”), import ze starego zapisu (m_JunkManagedSerialized). Dostosowywanie/dekoracje w pokoju – istniejące hooki InCustomizableSafehouse / IsDecorationItemInsideSafehouse.
- Ryzyka/do sprawdzenia w grze: A8 (czy GearManager.Reset nie niszczy przedmiotów trzymanego pokoju – wtedy odcisk się nie zgodzi i pokój zbuduje się z plików; czy kolejność zapis/LoadSceneWithLoadingScreen daje „zapisany przy wyjściu”); A5 przy przesuwaniu mebla, który TLD odkłada poza pokój do czasu adopcji; mapa – przesunięcie względem ikony TLD.

## 2026-11-02 – Seamless Interiors: partia B (B4 budynki spoza listy SI, B3 dym z komina i poświata okien)
- B4 (seamless_auto.inc, coop.ini `[interiors] auto_buildings=1`): raz na wczytanie regionu/strefy przejściowej (≥0,5 s po wczytaniu, przed rekordem „gracz w pokoju” i przed budową) LoadScene.m_LoadScenesList → drzwi ze scen regionu do wnętrz bez skonfigurowanego budynku tej sceny w promieniu 40 m (scena nieinstancjonowana skonfigurowana w regionie = zawsze „jego”) dają wygenerowany Config: id = scena(≤22) + „_a” + FNV(guid drzwi o najmniejszym guid; scena nieinstancjonowana: scena|region) – ten sam u hosta i znajomych; skorupa = najbardziej zewnętrzny przodek STR*/STRSPAWN* rozmiaru budynku (≤40×30×40 m), inaczej korzeń drzwi; fallback = pozycja skorupy; wejście/wyjście przez punkty m_ExitPointName gry; ObjectsToDestroy jak w generowanych SI (+ „<scena>_Lighting_Prefab”), tła okien zostają. Scena instancjonowana: budynek na (scena, skorupa); nie: jeden na scenę. Konfigi żyją w g_auto_cfgs przez całą sesję (A8 trzyma wskaźniki), skorupy w g_auto_shells (czyszczone z regionem); g_instances.reserve(kConfigCount+96), więc dopisanie nie przesuwa instancji.
- Odrzucane (log z liczbami): jaskinie, kopalnie (nie „Miner…”), tama, przejścia, tunele, więzienie/„Survival”, schrony (Prepper/Cache/Bunker/Empty), wagony, fabuła, warianty scen z „_”, drzwi wyłączone / tylko poza wyzwaniami / wejściowe przez dotyk. Przy budowie (stage Idle, zanim pokój się obudzi – fail_build bezpieczny dla przedmiotów): drzwi w pokoju prowadzące gdziekolwiek poza ten region (podwnętrze, inny region) → zwykłe drzwi; pudło siatek (bez kawałków >40 m i >60 m od środka) >45×25×45 m albo >30 m od skorupy; cokolwiek z świata w miejscu zaparkowanego pokoju (5 promieni z góry) albo nakładanie na inny pokój. Odmowa zapamiętana na sesję (g_auto_refused – następna wizyta nie ładuje sceny).
- Co-op: znajomy zawsze odkrywa (niezależnie od swojego ini), buduje tylko pokoje, które host ma; host/solo z auto_buildings=0 nie odkrywa nic. Te same ścieżki zapisu/importu/co-op (config_by_id dla door_position/place_scene/prepare_teleport; door_of_unbuilt = drzwi budynku). Każdy budynek logowany raz na sesję („interiors: auto: ID: scena w regionie, shell …”).
- Katalog Addressables w uploadzie ma tylko sceny gry podstawowej (353; brak Airfield/MountainPass/Mining/DLC Iron Road – ich katalog nie jest w uploadzie). Wśród jego wnętrz spoza listy SI przez filtr przechodzą tylko BlackRockTrailerB, HouseBasementC, HouseBasementPV (+ dodatkowe kopie scen instancjonowanych: przyczepy, chatki itd. w regionach); TWM, Muskeg i Hushed River mają głównie jaskinie → oczekiwane 0–kilka budynków. Liczby na region zobaczysz w logu („auto: REGION: N building(s) found … skipped: …”).
- B3 (seamless_fx.inc, SI ExteriorFireEffects.cs, coop.ini `[interiors] fire_effects=1`): dla gotowych pokoi ze skorupą: kominy skorupy (Chimney), ogniska pokoju (Fire; komin po m_ChimneyGuid/Chimney.FindByGuid, inaczej najbliższy). Co 1 s: m_LifetimeGameMinutes komina = najdłuższy pozostały czas ognia za nim (zgaśnie → najwyżej 30 min, dym gaśnie jak w grze). Poświata: szkło STR_WindowGlassMix* (bez „Alpha”) przez MaterialPropertyBlock na slot (kolor → nocny pomarańcz TodMaterial, _EmissiveStrength w górę; szkło bez _Emissive dostaje mainTexture), tylko od zmierzchu do świtu × siła ognia (ostatnie 30 min przygasa), fade 2,5 s, zapis ≤10/s i tylko gdy świeci, migotanie do 150 m; pusty blok przywraca materiał. Bez świateł na śniegu SI (dane okien offline). Jeden budynek na klatkę przy budowie danych, ogniska od nowa co 60 s. Co-op: każda gra czyta swoją kopię pokoju – u znajomego ognie hosta (rekordy ognia po id pokoju), więc widzi dym/okna, gdy ma ten pokój zbudowany (≤80 m od gracza).
- Do sprawdzenia w grze: czy m_LoadScenesList jest pełna po 0,5 s (log „N door(s) looked at”); czy promienie terenu nie odrzucają za dużo (drzewa/ściany niewidzialne); orientacja pokoju auto = obrót skorupy (bez okien bez znaczenia); poświata – czy nazwy materiałów szkła na innych skorupach też zaczynają się od STR_WindowGlassMix.

## 2026-11-02 – Seamless Interiors: etap 2, widok przez okna (protokół v43)
- Nowy plik src/seamless_windows.inc (+ dane src/seamless_windows_data.inc z tools/seamless/gen_windows.py; źródła SI: third_party/seamless-interiors/SeamlessInteriorsMod.ExteriorFireEffects.cs i BuildingWindowOverrides.cs). coop.ini `[interiors] windows=1` (domyślnie), `window_quality=0/1/2` (domyślnie 1): low 2 widoki, skala 0,35, 1 pokój z zewnątrz do 30 m; medium 4, 0,6, 2 pokoje, 50 m; high 8, 1,0, 3 pokoje, 100 m. Ustawienie lokalne (nie hosta).
- Obrót pokoju do budynku (SI TurnParkedRoomToShell + RoomTurnDegrees z tabeli): w align_room, ZAWSZE (niezależnie od windows), deterministycznie, więc w co-op każda gra obraca tak samo; parked_point uwzględnia yaw. Zapisy są w układzie pokoju, więc stare pliki pokoi działają. Protokół 43, bo gracz w pokoju stoi gdzie indziej w świecie niż w v42.
- Okna pokoju: zwykła reguła SI (szkło STR_WindowGlassMix pod korzeniem okna z tłem GLB_GlowExt/InteriorWindow, kierunek ze śniegu/tła, płaszczyzna widoku między szronem a ramą), lokalnie w pokoju. Okna budynku: tabela s_ShellWindows (4 budynki), reszta mierzona z czytelnej siatki skorupy (podsiatka szkła, klastry jak SI). Parowanie: FitWindowPattern (pełne przeszukanie ≤100 par, inaczej zgadywanie/kroki), PairInPlaceWithinCm, PairedRoomWindows/ViewOutOffsets tylko dla wierszy, które nie zmieniają reguły szukania okien pokoju (scan_altered), podwójne okna.
- Z wewnątrz: kamery WŁĄCZONE (głębia −50−slot, Unity rysuje je sama, jak SI – ręczny Render rysuje świat bez drzew), projekcja off-axis przez widoczną część szyby, RenderTexture (RGB111110Float/ARGBHalf, potęga 2 wg rozmiaru na ekranie × skala jakości), quad Unlit/Texture kolejka 3001 na warstwie 7 (kamery okien jej nie rysują). Histereza 1,25. Pass mgły gracza (PostProcess deferredFog, CommandBuffer BeforeImageEffectsOpaque) i wspólne bufory gracza (zorza, śnieg, decale, AO). Zastępcze oko (eye − 80 m) w RenderObjectInstance.m_Camera/m_CameraComponent + hooki MainCameraFrustrum.RecalculateFrustum* i Utils.PositionIsOnscreen; pudło śniegu przesunięte w Schedule*SnowParticleJobs (z popchnięciem do najbliższej szyby). RenderSettings.fog wyłączana tylko na render kamery gracza (Camera.FireOnPreCull/FireOnPostRender). Skorupa zostaje włączona w środku (SI KeepParkedShellOn). Malowane tło okna wyłączane tylko gdy to okno (wszystkie okna dzielące to tło) ma żywy widok.
- Z zewnątrz: do N najbliższych gotowych pokoi trzymanych włączonych, wszystkie kolidery wyłączane co klatkę, renderery na prywatnej warstwie 3/4/30 (po jednej na pokój), usuniętej z maski kamery gracza; kamera w eye + delta renderuje tylko tę warstwę ręcznie (Camera.Render) do quada 1 cm przed szybą budynku (kolejka 2600), połowa rozdzielczości dalej niż 25 m. Znajomy w trzymanym pokoju jest pokazywany i przełożony na warstwę pokoju (avatar.cpp, seamless_interiors::view_layer).
- Jeden reconciler: tick_windows po vp_FPSCamera.LateUpdate; release_windows cofa wszystko (oko, maska, warstwy, kolidery, tła, mgła) przy: widokach wyłączonych, zmianie regionu, LoadSceneWithLoadingScreen (przed A8), DoExitToMainMenu/LoadMainMenu, braku ticka przez 30 klatek (watch_windows w on_frame). open_room oddaje trzymane pokoje (kolidery wracają przed teleportem). Watchdog nie zamyka trzymanego pokoju.
- NIE przeniesione: kamery ścian (WallCameras), własny szron/śnieg na szybach i nakładki szronu z zewnątrz, wnęki/listwy/zaspy, kształtowane widoki, TAA motion vectors (możliwe smużenie przy obrocie), ambient widoku (u nas oświetlenie „outdoor”, więc zbędne), pomiar ramy skorupy (widok zaczyna się 4 cm za szybą), szkło wygięte/luźne/pasma i wiersze scan_altered (Grey Mother, FarmHouse A bay – mogą nie mieć okien), WindowFit, cięcie za ramą pokoju z zewnątrz.
- Do sprawdzenia w grze (log „interiors: windows:”): „ready (stand-in eye yes, snow box yes, sheet fog yes…)”, „N window(s) found in the room”, „room turned X deg… lined up”, „N view(s) out of the room ready… the player's camera draws layer 7” (jeśli DOES NOT DRAW – quady niewidoczne), „the views get the player's fog”, „room switched on to be seen… / given back”, „stand-in eye removed”. Ryzyka: drzewa znikające na zewnątrz (oko niezwolnione – szukać „stand-in eye removed”), wydajność przy wielu oknach (quality 0), gracz przechodzący przez podłogę po wejściu do trzymanego pokoju (kolidery), maska kamery gracza po wyjściu z trzymania.

## 2026-11-02 – drzwi bez wyciemnienia (cefc148)
- src/seamless_doors.inc: portale w drzwiach (widok drugiej strony w kwadzie przy drzwiach, przejście TeleportPlayer z zachowaniem pozycji bocznej, kierunku patrzenia, pitch i prędkości), także przejście „na wejście” (door_walk). coop.ini [interiors] door_portals=1, door_walk=1. Gdy drzwi nie mają pary / widok nie był rysowany → stare wyciemnienie. Protokół bez zmian.

## 2026-11-02 – nowy system zwierząt, etap 1 (0b19692, protokół v44)
- Każde zwierzę ma właściciela (host = arbiter i rejestr); wybór: odległość, patrzenie, rtt, polowanie/ucieczka, pokój SI; histereza 3 s, min. 5 s między zmianami, nie w walce, nie martwe, max 4 przekazania/s. Przekazanie z pełnym stanem + BaseAi.Serialize (handover_record=1). Przewidywanie ruchu kopii. Właściciel opuszcza scenę → najlepszy gracz w scenie; nikogo → zamrożone w rejestrze (30 min). Jeden „spawnujący” na scenę.
- Wiadomości: WildFrame, WildCtl, WildSounds, WildHit. coop.ini [wildlife] ownership=1 (host decyduje, bit kFeatureWildOwnership), 0 = stary system bez zmian.
- Niedokończone: strzały w zwierzęciu nie przechodzą z przekazaniem; zwierzę prowadzone przez znajomego widzi tylko jego; zabicie pod nieobecność hosta – tusza tylko u właściciela; rejestr tylko w pamięci sesji.

## 2026-11-02 – zwierzęta, etap 2 i 3 (7b34358, protokół v45) – bez testów, na prośbę Karola
- Etap 2 (wildlife_owner.inc): rejestr zwierząt hosta zapisuje się z grą (world_keep, Tag Wildlife=9, WildCtl Keep) i po wczytaniu wszystkie są zamrożone (godziny gry zanotowane). Zamrożone przejmuje tylko ktoś w promieniu 400 m (dalej czekają). Przy przejęciu „w tle”: krwawiące, którym minął czas wykrwawienia – padają (tusza tam, gdzie były; Kill do spawnującego); zostawione ≥1 h – nowe miejsce w ich regionie (SpawnRegion.TryGetSpawnPositionAndRotation, ≥80 m od graczy, kTakeMoved, bez starego zapisu TLD). Zapomniane po 7 dniach gry bez nikogo. Cisza właściciela (poza zasięgiem) nie kasuje już wpisu; TLD chowające zwierzę u właściciela → Gone (host zapomina).
- Tusze: znajomy oddaje tuszę hostowi z tym, co z niej wycięto (BodyHarvest.Serialize/Deserialize, kTakeBody, nakładane 1,5 s po zabiciu u hosta); gdy hosta nie ma, znajomy co 10 s wysyła stan tuszy (Keep), żeby host miał go po wyjściu znajomego.
- Etap 3 (wildlife_regions.inc): klucz regionu = pozycja regionu (0,5 m) + rodzaj (ten sam w każdej grze); AnimalRec.region idzie ze zwierzęciem. Gra spawnująca: hook SpawnRegion.GetCurrentActivePopulation liczy zwierzęta regionu żywe u innych (g_away, po id) i własne schowane (do celu, nigdy ponad); oddane zwierzę wychodzi z listy regionu (RemoveFromSpawnRegion z przywróceniem licznika – TLD nie włączy go z powrotem); każda śmierć gdziekolwiek → Kill → oczekujące odrodzenie jak w grze (m_NumRespawnsPending++, czas GetNumHoursBetweenRespawns), chyba że TLD już to policzył (kKillCounted). Stan regionów (oczekujące, czasy, złapane w sidła) co 20 s do hosta (Regions), host oddaje go następnej grze spawnującej (czasy przesunięte o upływ) i trzyma w zapisie (Tag WildRegions=10). Schowane zwierzę, które TLD włączyło po >30 s, to jego odrodzenie – udostępniane.
- Krew: przy ownership krew zwierzęcia wysyła gra, która je prowadzi (także znajomy) – world_blood.
- Luki etapu 1: strzały wbite w zwierzę spadają na ziemię przy przekazaniu (ArrowItem.DetachIntoWorld); zwierzę prowadzone przez znajomego widzi wszystkich graczy (wildlife_targets: think/eye pos/as_friends_doing u znajomego dla jego zwierząt; obrażenia/rany/walka z awatarem innego gracza nie są tam zadawane – zwierzę zostaje w Attack, host przekazuje je ofierze; przekazanie do ściganego już po 1,5 s od poprzedniej zmiany). Lokalny gracz w pick_player to kLocal (nie 0).
- Dev `owners` pokazuje linię v45 (żywe u innych, liczone przez regiony, sceny z zapisanym stanem, czekające śmierci, godziny gry).
- Ryzyka/do sprawdzenia: czy klucze regionów są takie same u wszystkich („N of M spawn region(s) here as the last game…” – N=0 oznacza różne); hook populacji („a spawn region counts …”); BodyHarvest.Deserialize na świeżo zabitym zwierzęciu hosta; przeniesienie po ≥1 h (TryGetSpawnPositionAndRotation może odmawiać blisko kamery); dalekie zwierzęta po wczytaniu save'u czekają zamrożone, a schowane początkowe zwierzęta kontrolera są liczone – daleko od graczy może być mniej zwierząt niż w solo.

## 2026-11-02 – liczba zwierząt zależna od liczby graczy (protokół v46)
- Prośba Karola. Hook SpawnRegion.CalculateTargetPopulation w grze, która spawnuje (ownership: kontroler sceny; bez ownership: host): maksimum regionu (wynik + złapane + oczekujące) × (1 + per_player% × (gracze − 1)), zaokrąglone, minus złapane i oczekujące; nigdy mniej niż w solo. Gracze = wszyscy w grze (status().players + my).
- coop.ini `[wildlife] per_player=50` (0–200), liczy się hosta: idzie w WildCtl Controllers (nowe pole u8, stąd v46). Bez ownership host używa swojego.
- Log: „wildlife: N players: a spawn region keeps X animal(s) instead of Y”. Nowe zwierzęta dochodzą przez zwykłe spawnowanie TLD (poza wzrokiem gracza), nie od razu.

## 2026-11-02 (wieczór) – poprawki z testu Shatsca (4 zgłoszenia)
- 1 „ekran co jakiś czas ciemnieje”: przy budowie pokoju (gracz ~80 m od budynku) GenericIndoors.Start z wnętrza wołał Weather.ForceIndoorEnvironment (pogoda off, indoor colour grading, -5 C, radar „indoors”) na ~4 s, aż update_global_environment wołał ForceOutdoor. Hook GenericIndoors.Start: pomijany, gdy trwa wczytywanie sceny pokoju (g_loading_base) albo obiekt jest w pokoju (under_any_room). Ustalone disasemblacją: jedyny wołający ForceIndoorEnvironment to GenericIndoors.Start.
- 2 zwierzęta z budynku: kościół – 11 okien skorupy, 0 dopasowanych do okien pokoju (zostaje malowane tło; do zbadania), niedźwiedź widoczny przez drzwi. Skakanie: gracz w pokoju stoi 80 m wyżej → TLD rzadko liczy zwierzęta. add_inside_spot(): drzwi budynku gracza w pokoju liczone jak pozycja znajomego (CanUpdateAI, regiony, AlwaysAnimate), host i ownership.
- 3 drzwi: DoorPair.open – domyślnie zamknięte (bez widoku, widać drzwi gry); klik (LoadScene.Activate → door_click) otwiera/zamyka z dźwiękiem; otwarte: widok + wejście (door_walk) przenosi; po przejściu zostają otwarte, zamykają się same, gdy gracz ≥3 m od obu stron przez 1,5 s. door_walk=0: klik w otwarte = przejście jak dawniej. Bez animacji skrzydła (widok przykrywa drzwi).
- 4 jasność: pokoje oświetlone jak dwór (oświetlenie wnętrza wyłączone, jak w SI). Nowe [interiors] indoor_light=45: w pokoju kamera gracza (i broni) rysuje z RenderSettings.ambientProbe/ambientLight/reflectionIntensity × 45% (FireOnPreCull/PostRender, przywracane; widoki okien/drzwi bez zmian). Hooki kamery instalowane zawsze (wcześniej tylko z fog_ok).
- Log „N of the player's camera's buffers shared” tylko przy zmianie liczby (był co 0,3 s – gra odtwarza bufor).

## 2026-11-03 (noc) – poprawki z testu 02.11 wieczór (protokół v47)
- Crashe 0xC00000FD (przepełnienie stosu, 2× host, 1× Szymon; 2 z 3 tuż po przekazaniu zwierzęcia): przyczyna niepewna. il2::invoke ma teraz licznik zagnieżdżenia (wątek): >48 → wywołanie odrzucone i logx::error z łańcuchem ostatnich 12 metod („calls into the game nested 48 deep”). Fault logger przy stack overflow wypisuje najczęstsze adresy powrotu na stosie (winhttp/GameAssembly/UnityPlayer + offset, „on the stack xN”). Link z /MAP; winhttp.map wrzucany do dll-latest (do odczytu offsetów winhttp.dll). Obejście: [wildlife] ownership=0.
- Szafki w pokojach SI („no answer from the host”): id szafki pokoju („si:<pokój>|g:<guid>|<nazwa>|<miejsce>”) > 64 bajtów → put_blob wysyłał pusty id (ContainerLock i ContainerPut). kPdidMax 64 → 200 (v47). Host odpowiada też na ponowne Take od właściciela.
- Meble znikające/pojawiające: zastępcze oko (80 m niżej) cięło też RenderObjectInstance pokoju (wspólny frustum). Instancje pod pokojem: kamera gracza zostaje, m_FrustumType = DotProduct na czas oka (przywracany). PositionIsOnscreen bez przesunięcia dla punktów w pokoju gracza.
- Śnieg w pokojach bez okien: przesunięcie pudła śniegu także gdy gracz w pokoju bez oka.
- Jasność/miganie: przyciemnianie przez hook Utils.SetAmbientLightScaled (raz na klatkę, zamiast per kamera, które mogło migać); domyślnie indoor_light=70 („strasznie ciemno”).
- Okna: scan_openings dzieli szyby pod wspólnym korzeniem na okna (grupy ≤25 cm), śnieg/tło/rama brane z części ≤0,6 m od okna (wcześniej prawie każdy pokój: „0 window(s) found (1 not windows)”).
- FarmHouseA na zwykłych drzwiach: zapis gry zmienił się po imporcie. [interiors] reimport_changed=1: nasze pliki do replaced/, import od nowa z zapisu gry.
- Raca z pistoletu: FlareShot (v47) – nasz wystrzał (FlareGunRoundItem.Fire) wysyłany; u innych SpawnAndFire z prefabu rakiety z własnego vp_FPSShooter (Resources.FindObjectsOfTypeAll); InflictDamage takich rakiet nic nie robi.
- [loot] per_player=50 (host): Container.PopulateWithRandomGear powtarzane z solą ziarna (world_mode::set_seed_salt), dodatkowe nazwy do m_GearToInstantiate; część ułamkowa losowo per przedmiot (deterministycznie od pozycji).
- Lampa Szymona: w logu hosta przychodzi z 0% paliwa, zgaszona – potrzebny log Szymona.
- Zwierzęta przy budynku: add_inside_spot z poprzedniej wersji bez zmian.

## 2026-11-03 – wnętrza: oświetlenie z gry, wchodzenie w drzwi
- Shatsca: „za jasno” dotyczyło wyglądu z modem (bez przyciemnienia); chcą wyglądu wnętrz z gry. [interiors] indoor_light=0 (nowy domyślny): w pokoju, w którym jest gracz, obiekty kDarkOnly (InteriorLightingManager_Prefab, Daytime/Daylight/Midday/AmbientLight/Nighttime) włączane (wyłączane po wyjściu); hook InteriorLightingManager.Update liczy głębokość, a Utils.SetAmbientLightScaled przepuszcza tylko wywołania z menedżera pokoju (TodAmbientLight: kolory wnętrza dzień/noc), wywołania „z zewnątrz” pomijane. Pokój bez menedżera: 45%. 5–100 = stare procenty. Lightmapy pokoju dalej wyłączone (jak w trybie outdoor SI) – wygląd zbliżony, nie identyczny.
- Drzwi: przejście przez otwarte drzwi wymagało sprintu (kapsuła 0,5 m zatrzymywała gracza przed zasięgiem 0,5 m), przeciążony nie wchodził. kDoorWalkReach 0,5→0,9 m, prędkość 0,4→0,25 m/s albo „pcha w stronę drzwi” (vp_FPSController.m_MotorThrottle · normalna < 0).

### 2026-11-03 21:30 — ostatni obraz (łąka i góry) na ekranach telewizorów
- Karol: ostatnio zmieniony obraz (`misc_1`, jego zdjęcie, wersja malowana) także zamiast ekranu telewizora.
- Atlasy z bundla 06fe… (lokalnie, nie w repo): `OBJ_OldTelevision_A` (1024, kineskop: x 87..415, y 117..367) i `OBJ_InteriorHouseAppliances_A` (1024, płaski TV w prawym dolnym rogu: x 542..992, y 707..990; ten atlas ma też mikrofalę, radio, toster — zmienia się tylko ekran).
- `assets/pictures/tv_old.png` (328×250, zaokrąglone rogi kineskopu) i `tv_modern.png` (450×283); alfa ~88% z miękkim brzegiem (resztki szkła/odbić widać). `tools/pictures/gen_data.py` → `src/pictures_data.inc`; custom_pictures bez zmian.
- Do sprawdzenia w grze: czy obraz na płaskim TV nie jest obrócony/odbity (UV modelu nieznane), i czy OBJ_Television/TelevisionB/Wall faktycznie używają atlasu A (B ma monitory komputerowe i laptop — nie ruszane).

### 2026-11-03 21:45 — logi z gry 20:41–21:16 (Karol F11, Shatsca F11): przyczyna crashy, haki, szafki w pokojach
- **Crashe (stack overflow po przekazaniu zwierzęcia)**: log Shatsci 21:00:18 — strażnik pętli złapał `SpawnRegion.CalculateTargetPopulation` zagnieżdżone 48×, tuż po „#24256216 handed to player 2”. TLD-owe CalculateTargetPopulation samo woła GetCurrentActivePopulation, a nasz population_detour (aktywny dopiero gdy region liczy zwierzęta w innej grze, czyli po przekazaniu) wołał CalculateTargetPopulation → nieskończona rekursja. Teraz `thread_local g_pop_asking`: w trakcie naszego pytania population_detour zwraca liczbę TLD bez dodatków.
- **Haki na tę samą metodę**: `InteriorLightingManager.Update` (Ambient Lights + oświetlenie wnętrz SI) i `GameManager.LoadMainMenu` (world_items + okna SI) — drugi hak padał z MH_ERROR_ALREADY_CREATED, więc licznik głębokości ILM w SI nie działał (oświetlenie „jak w grze” mogło nie przepuszczać światła menedżera pokoju). hooks::install łączy teraz haki: mapa metoda→aktualny detour; drugi hak zakładany na pierwszy detour (log „ours goes on top of it”).
- **Szafki przeszukane przez gracza w pokoju SI, zanim host do niego wszedł** (Shatsca: „szafki które przeszukał szymon nie były dla mnie widoczne”): apply_stored szło raz, gdy zmieniał się licznik pokojów — na początku budowy pokoju, zanim jego szafki istniały; potem już nigdy. Teraz host co 2 s ponawia apply_stored, dopóki są czekające zmiany pokojów w tej scenie (log „wait” tylko przy zmianie liczby).
- Karol (F11): „zorza jest, a światła nie świecą wewnątrz i na zewnątrz” — u klienta pogoda ustawiona na ClearAurora z synchronizacji (21:13:48, 5%); czy AuroraManager klienta się włącza — niesprawdzone (potrzebny log hosta z tego momentu). Nie zmienione.

### 2026-11-03 22:10 — zorza, ognisko w pokoju, mokre ubrania w budynku, ślady innych graczy
- Logi Karola (F11 21:22–21:48; gra jeszcze na buildzie 15:54, więc pętla CalculateTargetPopulation i padające haki są tam jeszcze — już naprawione).
- **Zorza słaba u klienta**: TLD liczy AuroraManager.m_NormalizedActive co klatkę (UpdateAuroraValue) z własnego nieba; u klienta niebo idzie skokami za pogodą hosta (zestaw przejęty przy 5%, przewinięty czas), więc lampy/radia nie dostawały prądu. WorldMsg ma na końcu `aurora` (float, -1 nieznane; czytane tylko gdy jest — protokół dalej v47, starszy host = bez zmian). Host wysyła swoje m_NormalizedActive (GameManager.GetAuroraManager), klient: hook AuroraManager.UpdateAuroraValue nadpisuje wartością hosta (świeża < 10 s). Log `world: aurora X% here, Y% at the host's`.
- **Ognisko w kominku niewidoczne, dopóki host nie dołożył patyka**: FirePut hosta dla ogniska w pokoju SI przyszedł, zanim pokój był zbudowany u klienta → „didn't load here” i przepadał. Klient trzyma takie zmiany (`g_client_wait[scene][id]`) i stosuje je ponownie, gdy przybywa pokój (rooms_generation).
- **Ubrania na podłodze w budynku mokre**: pokój stoi w regionie (scena zewnętrzna), więc ClothingItem.MaybeGetWetOnGround moczył je jak w śniegu. Hook: przedmiot pod pokojem (under_any_room) albo w obrysie zbudowanego pokoju (`built_room_at`, seamless_coop.inc) → nie moknie.
- **Ślady innych graczy na śniegu**: nowy `src/avatar_tracks.cpp`. Każdy awatar widoczny na zewnątrz dostaje FootstepTrail(AiPersistent, HumanFootprint) dodany do FootstepTrailManager.m_Instance; co 0,75 m odcisk (lewa/prawa ±0,12 m), Physics.Raycast w dół, tylko gdy collider ma tag „Snow” (CompareTag). Skok > 5 m = nowy początek. Logi `tracks:`. Niesprawdzone w grze: czy tag to naprawdę „Snow” (log „isn't tagged snow” jeśli nie) i jak wyglądają odciski.
- **Horda wilków u hosta (Shatsca 21:48, Wybrzeże)**: ~40 wilków w 2 min. Host przekazywał świeżo zespawnowane wilki graczowi 1 (Szymon), jego gra po ~3 s je chowała („gone from player 1's game (TLD put it away)”), host zapominał → region dosypywał nowe → znów przekazanie. Kopie zostawały u hosta, aż wygasły („no word of it”). Przyczyny po stronie Szymona nie widać (brak jego logu). Obejście: Entry.owned_ms; jeśli gra gracza schowa 2 zwierzęta w < 20 s od przekazania → przez minutę nic mu nie przekazujemy (`g_keeps_nothing_until`, log „puts animals away right after taking them”). Potrzebny log Szymona z takiej chwili.

### 2026-11-03 22:35 — półki „X is searching right now”, drzwi otwarte widać
- Przegapione wcześniej opisy F11 Karola (20:47–21:10; czytać `./opis.txt` w zipie — nazwa z „./”).
- **Półki zablokowane („Shatsca is searching this right now”, cały dom, 21:05)**: may_open nadpisywało g_mine nowym pojemnikiem, gdy gracz otwierał kolejną szufladę w ciągu 1 s od poprzedniej — poprzednia nigdy nie była zwalniana, a blokady hosta nie wygasają. Teraz: inny pojemnik w g_mine → najpierw release_mine("another one opened"); dodatkowo host w lock_tick zwalnia swoje blokady, które nie są jego g_mine (log „we weren't in it any more”).
- **Drzwi „znikają” po otwarciu**: widok drugiej strony stoi 6 cm przed skrzydłem, więc otwarte drzwi wyglądały jak dziura. Teraz przy otwarciu skrzydło (renderer pod LoadScene: szer. 0,4–1,1 otworu, wys. ≥ 60%, grubość < 25 cm) obraca się o 85° wokół lewej krawędzi w stronę gracza (przed widokiem), przy zamknięciu wraca (DoorFace.leaf/leaf_pos/leaf_rot; swing_pair w door_click i tick_door_close; drop_room_doors przywraca). Log „door leaf ... swung open” / „no door leaf found”.
- Nie zrobione (wymaga więcej): minimapa („nie działa” — z logu nie widać co; radar jest tworzony i pokazuje mapę), okna „dziwnie poustawiane”, płynne wejście / pokoje w miejscu budynku zamiast 80 m wyżej, gracze z innego regionu na mapie, mapa odkryta przez innego gracza (wymaga nowej wiadomości sieciowej: dzielenie punktów mapy).

### 2026-11-03 22:50 — logi Szymona i Shatsci 22:11: przyczyna hordy wilków
- Log Szymona: „we run #… now (wolf …; one made for it)” i po 0,1–2 s „our #… is gone from our game (TLD switched off it)” — jego region spawnu brał przekazane zwierzę (z rekordem TLD) za swoje ponad własny cel (u hosta cel wyższy przez per_player) i je wyłączał; host dosypywał kolejne. Teraz take_local: gdy nie jesteśmy grą spawnującą scenę (i_control), zwierzę jest wyjmowane z listy regionu (detach_from_region, bez liczenia śmierci).
- Druga rzecz w tych logach: host wysyłał graczowi całe listy regionu (TLD-owe losowe spawny + placements ~48 kB) przy każdym zbudowanym pokoju (netrt::resync) — co kilka sekund przy przechodzeniu obok domów; klient wczytywał je od nowa (u Szymona tuż po tym 4 wilki wyłączone naraz). Together::only_resync(id): przy samym resyncu world_objects wysyła tylko stany obiektów, bez spawnów i placements (log „a room came up: the region's spawns and placements not sent again”).
- Zauważone, nie ruszone: przesunięte meble w pokojach SI („… isn't in TLD's placements here; not sent” — półki/komody/łóżka przestawione w domu nie dochodzą do innych), zacięcia 100–180 ms od seamless_interiors::on_frame i world_objects::on_frame przy budowie pokoi.
- **Minimapa ciemna** (zrzut Karola 22:37: radar prawie cały czarny, jasny tylko skrawek): radar pokazuje mapę TLD tylko tam, gdzie zmapowane, a auto-mapowanie odkrywało 80 m wokół co 50 m przy zasięgu radaru 150 m. kChartRadiusM = 150 (bez ograniczania do promienia węgla TLD) — dotyczy też mapowania śladami znajomych.

### 2026-11-03 23:05 — czarna ziemia o zmierzchu (zrzuty Karola 22:54/22:55)
- Zrzuty: Sortownia Kłód 19:24, mgła; ziemia przy graczu czarna, pnie drzew i mgła jasne. Z logu brak błędów. Jedyna nowa rzecz tego wieczoru dotykająca śniegu/odcisków: avatar_tracks (FootstepTrail dla awatarów, w logu kilka nowych śladów tworzonych ponownie). Wyłączone (`kEnabled = false` w avatar_tracks.cpp) do sprawdzenia. Jeśli dalej czarno → podejrzani Dynamic Trees / Ambient Lights (test: wyłączyć w MODS).

### 2026-11-03 23:35 — logi 22:01–22:57 (Karol, Szymon, Shatsca): duplikaty z pojemników w pokojach, szafki u hosta, id pojemników
- **Duplikat łososia z piekarnika (Karol 22:03)**: Karol (klient) wyjął łososia z piekarnika w pokoju, którego host nie miał zbudowanego → zmiana trzymana u hosta w g_store; przy kolejnym otwarciu host odpowiadał swoim nietkniętym piekarnikiem („(rolled) GEAR_CookedCohoSalmon”). answer(): najpierw zmiana gracza z g_store (zastosowana, jeśli pojemnik już jest; inaczej wysłana jako „what they last told us”).
- **Szafki kuchenne pełne u hosta mimo przeszukania przez Szymona (Shatsca 22:57)**: pojemniki pokoju pojawiają się w liście TLD dopiero, gdy się na nie patrzy; zmiany wchodziły co 2 s po jednej, już po otwarciu. may_open (host): zmiana gracza dla tego pojemnika stosowana natychmiast przed otwarciem. apply_stored buduje mapę id→pojemnik raz na przebieg (wcześniej find() przebudowywał listę przy każdym braku; 100+ czekających zmian co 2 s).
- **„the other player's container XXXXXXXX isn't in our CoastalRegion” (34–40× u klientów)**: pojemnik oznaczony jako zmieniony, gdy pokój jeszcze się budował (dodatkowy łup per_player przy budowie pokoju u hosta), miał id gry zamiast klucza pokoju (si:…), różne w każdej grze. flush: pojemnik w budowanym pokoju czeka; gdy id się zmieniło → przeniesiony pod aktualny klucz (wysłany przy następnym flush).
- **Nie mógł wejść z powrotem (Karol 22:41)**: w logu brak jakiejkolwiek linii drzwi — nieustalone. Dodane: log każdego użycia drzwi („door used: …”, co 3 s) i zabezpieczenie: fade drzwi „busy” > 5 s po końcu jest zwalniany (log WARN).
- Czarna ziemia: Karol wciąż na buildzie 21:05 UTC (ze śladami) — do sprawdzenia na nowym.

### 2026-11-04 00:00 — okna z wewnątrz, meble przy dwóch graczach
- **Z żadnego okna nie widać świata od środka (Karol)**: log „N view(s) drawn this frame of 0 visible” zawsze, „0.00 views a frame on average” — render_inside_out odrzuca widok, gdy okno nie jest „przed” graczem wzdłuż o.out; o.out ze śniegu/tła wskazywał do środka pokoju. scan_openings: kierunek na zewnątrz = od środka pokoju (InteriorTrigger: in.trig_center), gdy szyba jest > 0,5 m od środka na tej osi; log „N turned to face away from the room's middle”. To też wpływa na dopasowanie okien pokoju do okien budynku (fit_pattern używa kierunków) — „okno w innym miejscu w środku i na zewnątrz”.
- **Meble „bugują się” przy dwóch graczach w pokoju**: przesuwany mebel był wysyłany co sekundę w trakcie przesuwania (pozycje pośrednie), a gra drugiego gracza przestawiała go, nawet gdy sama go właśnie przesuwała. scan_places pomija obiekt w trybie umieszczania (PlayerManager.m_ObjectToPlace); apply_place nie rusza mebla, który my przesuwamy (log „isn't applied: we're moving it”).
- Skrzydła drzwi: odchylanie jest od buildu 21:30 UTC — Karol testował na 21:05.
- Teleport przy wejściu/wyjściu: pokoje stoją 80 m nad budynkiem, przejście przenosi gracza w tej samej pozie — usunięcie wymaga pokoi w miejscu budynku (duża przebudowa, czeka na decyzję Karola).

### 2026-11-04 00:30 — pełna analiza logów (46 sesji, 30.10–03.11) + niezależny przegląd kodu z 03.11
- Zebrane: wszystkie zipy/logi z Pobranych Karola, raporty F11 z folderu gry, bieżące coop*.log. Skrypt zbiorczy (build, rola, błędy, zacięcia na sesję).
- **Awatary bez animacji czynności** („0 action clip states”, 11× AddClip ArgumentNullException przy tworzeniu awatara, od drugiej sceny): klipy z paczek akcji, raz wczytane i trzymane w g_action_clips, były wyładowywane (UnloadUnusedAssets) — managed obiekt został, natywny nie. action_clip: martwy klip wczytywany ponownie.
- Zacięcia world_objects 100–220 ms (53× u Karola) = pełne listy regionu przy każdym budowanym pokoju — naprawione wcześniej (lekki resync), Karol grał na starym pliku.
- „Animator wilka różny od hosta (88 vs 93 bajty)” ~150× — kopie wilków z innego prefabu niż u hosta (pokazywane tylko stany). NIE naprawione (wymaga wyboru prefabu wg rozmiaru parametrów albo nazwy prefabu w protokole).
- Wyjątek przy starcie moda (mod wyłączony) — tylko build 1.11 22:01, w nowszych brak.
- Przegląd kodu (agent): naprawione:
  1. Meble: obiekt w trybie przesuwania pomijany w scan_places czytał się jako „zniknął” → u innych znikał. Teraz zostaje w liście z flagą `placing` (nie wysyłany, `seen` nietknięte).
  2. Host zwalniał co 500 ms własne blokady rzeczy trzymanych przez hold() (g_held_things) — wykluczone z czyszczenia; deklaracja g_held_things przeniesiona wyżej.
  3. Kara „gra chowa zwierzęta” zabierała też zwierzęta, które gracz już prowadził — teraz tylko nowe przekazania; licznik zeruje się po minucie.
  5. Kierunek okien od środka pokoju tylko gdy pokój ma InteriorTrigger (have_trigger).
  6. Skrzydło drzwi: nie obracamy transformu samego LoadScene.
  7. g_client_wait (ogniska) czyszczone w forget_world.
  8. Świeżość zorzy liczona od chwili przyjścia wiadomości (received_ms).
  9. Ponawianie apply_stored tylko 30 s po pojawieniu się pokoju (reszta przy otwarciu: may_open).
- Do sprawdzenia: (4) losowe spawny w pokojach przy lekkim resyncu — pokoje mają własne losowanie (seed hosta), więc prawdopodobnie bez wpływu.

## 2026-11-04 — jasne drzewa o zmierzchu (widok z pokoju)
- Przyczyna (log Kerel 22:54–22:55): Karol stał W pokoju CoastalHouse_15 (y=106) i patrzył przez otwarte drzwi ("1 door view drawn, 1 from inside"). Widoki na zewnątrz z pokoju rysowały się światłem pokoju (ambient z InteriorLightingManager + światła Daytime/Daylight/... pokoju), nie wieczornym światłem dworu → drzewa kremowe, ziemia czarna. Tekstury Dynamic Trees wykluczone (jasność jak w grze, tylko kora).
- Poprawka (seamless_windows.inc / seamless_doors.inc): g_out_cams (kamery widoków okno/drzwi z pokoju na zewnątrz); FireOnPreCull takiej kamery → ambient dworu (ostatni g_sky_out z TodAmbientLight) + światła pokoju wyłączone; FireOnPostRender → przywraca ambient pokoju i światła. Log raz: "a view out of the room drawn with the outdoors' light".
- Do sprawdzenia w grze: zmierzch, wejść do domu, patrzeć przez otwarte drzwi/okno — drzewa i ziemia jak na zewnątrz; brak migotania światła w pokoju.

## 2026-11-04 — Dynamic Trees: mniej białe drzewa
- Karol: drzewa "bardzo białe". Przy śniegu 95–100 (stan 7–8) mod daje zestaw Highest: igły prawie całe białe.
- tools/trees/tone_snow.py: każdy zaśnieżony zestaw (Pine Lowest..Highest, Cedar Low..Highest) zmieszany z Clear: clear + 0.55*(snowy-clear), mip po mipie (dekodowanie DXT5 → mieszanie → własny koder DXT5, PSNR ~53 dB), alfa bez zmian. Paczka zapisana na nowo (UnityFS, jeden blok LZMA, info nieskompresowane) — 11 MB zamiast 22 MB. Clear i Snow_A bez zmian.
- Nowy plik wgrany Karolowi do TLDCoop\tldcoop_trees.bundle; stary w TLDCoop\backups\tldcoop_trees_stary.bundle. W logu sprawdzić "trees: textures loaded" i brak "could not load" / "no texture".
- Ruch liści: czekam na opis Karola (sztywne / za mocno / drgają). Parametry w materiałach: _XZAnimScaleByH/_XZAnimScaleByR 0.15, _LocalPhaseScalars 0.1 (atlas gałęzi), _TrunkBendHeight 5; globalnie _GlobalWindInfo/_GlobalBranchPhase (UpdateShaderGlobalsForFoliage).

## 2026-11-04 08:15 — drzewa płasko kremowe (screen Karola)
- Screen: pnie i korony jednolicie kremowe, zero szczegółów tekstury, ziemia czarna, skały normalne. To nie "za dużo śniegu" w teksturze — wygląda na brak tekstury (biała) albo przepalone światło drzew.
- Dodane logi raz na minutę: "trees: a tree material shows '<tekstura>'" i "interiors: sky's light r g b ... from the outdoors/a room's lighting manager". Następne logi rozstrzygną.
- Ruch liści: z mojej strony nie widać gry; poproszony o krótkie nagranie. Parametry: _XZAnimScaleByH/R 0.15, _LocalPhaseScalars 0.1, _TrunkBendHeight 5, globalne _GlobalWindInfo/_GlobalBranchPhase.

## 2026-11-04 08:50 — protokół v48: drzwi w interiorach, skrzydła drzwi SI, POBIERZ LOGI
- Drzwi/szafy/szafki w zwykłych interiorach (OpenClose, np. szkoła): otwarcie/zamknięcie widzą wszyscy. world_objects: Kind::Door (ObjectKind 8, klucz "o/"), odpytywane jak zamki ale w obie strony (m_IsOpen), stosowane przez OpenClose.Open/Close(false,false) z animacją; lista hosta przy wejściu niesie stan każdych drzwi.
- Skrzydło drzwi SI (Shatsca, raport 22:32 "skrzydla drzwi nie dzialaja", log "no door leaf found"): szukane też u rodzica i dziadka LoadScene (tylko w obrębie futryny); przy braku log z całą ścieżką obiektu.
- ESC → "POBIERZ LOGI" (tylko w co-op): pakuje nasze logi i prosi resztę (LogsWant); każdy pakuje swoje i wysyła zip (LogsFile, części po 192 KB, host przekazuje) do TLDCoop\reports\<czas>_logi_wszystkich\<nick>_<plik>.zip.
- v48: wszyscy muszą mieć ten sam dll.

## 2026-11-04 09:10 — okna: przesunięcie i szyba
- Rysunek Karola: widok przez okno przesunięty wzdłuż i w poprzek ściany. Okna bez pary szły przesunięciem z configu SI (myli się do ~1 m). Teraz: każde niesparowane okno → najbliższa wolna szyba budynku w tę samą stronę (do 2 m po średniej korekcie sparowanych), inaczej średnia korekta sparowanych. Tak samo z zewnątrz do pokoju. Log: "N more window(s) paired to the nearest building pane" / "(N paired to the nearest room window)".
- Szyba i szprosy: widoki rysowane w kolejce 2450 (przed szybą gry STR_WindowGlassMix, przezroczystą), więc jej szron, odcień i szprosy są na wierzchu; z zewnątrz płaszczyzna widoku 3 cm za szybą. Jeśli widok z zewnątrz zrobi się pusty/zasłonięty → cofnąć kOutsideInBehindGlass.
- Półki: logi z problemem były z dll 21:20 (Shatsca), poprawka przekluczania (23:20) tego dotyczy; czekamy na nowe logi.
- `near` to makro Windows — nie używać jako nazwy.

## 2026-11-04 11:50 — szybszy przesył (prośba Karola)
- session.cpp: okno niepotwierdzonych paczek 64 → 128; ponowne wysłanie zależne od pingu łącza: 2×rtt + 40 ms, w granicach 120–250 ms (bez pomiaru: 250). Steam (SendRateMax 4 MB/s, bufor 4 MB) bez zmian.
- coop_report.cpp: „POBIERZ LOGI” — zip odsyłany po jednej części (192 KB) co 300 ms (kolejka g_paced, send_paced w on_frame), zamiast wszystkich naraz; zmiany świata idą między częściami. Log „one part every 300 ms” / „the last part of our logs sent”.
- Protokół bez zmian (v48), ale dla porządku wszyscy ten sam dll. Nieprzetestowane w grze.

## 2026-11-04 18:00 — ekran co-op i lobby: nowy wygląd, po polsku (prośba Karola, podgląd zaakceptowany)
- coop_menu.cpp: setup i lobby po polsku (TRYB/POSTAĆ/GŁOS/NAZWA/NAZWY/RADAR/AUTO MAPA, WŁ/WYŁ, ZATWIERDŹ/WSTECZ/MODY, WPUŚĆ/ODRZUĆ/WYRZUĆ, START/GOTOWY/ANULUJ; opisy, komunikaty, ekran SOLO CZY CO-OP?). Strona MODS bez zmian (tylko przycisk „MODY”).
- Wiersz kodu: „TWÓJ KOD:” / „KOD HOSTA:” + 6 kratek (kopie ramki przycisku WSTECZ, sprite menuButtonCenter_fill, bez poświaty) i KOPIUJ/WKLEJ za nimi, dzieci wiersza kodu (make_framed, make_code_cells, place_code_cells). DOŁĄCZ: migający kursor „_” w następnej kratce, pełny kod na zielono. Klik w kratkę = zaznacza wiersz kodu.
- Nagłówek: „USTAWIENIA · MOD V48 · <commit>” (CMake: TLDCOOP_BUILD z git rev-parse), w lobby „GRACZE n/4 · MOD V48 · <commit>”.
- Kolory w tekście: UILabel.set_supportEncoding na wierszach i opisie, tint() = [rrggbb]..[-] (zielony/ochra/czerwony/szary); gdy metody brak — sam tekst.
- Lobby: 4 miejsca: kropka w kolorze gracza (coop_hud::colour_hex), imię, postać, ping (rtt linked), HOST/GRA/GOTOWY/NIEGOTOWY/CHCE DOŁĄCZYĆ; puste „WOLNE MIEJSCE” na szaro, niewybieralne (lobby_row_usable).
- Ostatni host: coop.ini [net] last_host / last_host_code, zapisywane, gdy klient jest połączony; w trybie DOŁĄCZ przycisk „DOŁĄCZ: <HOST>” w miejscu MODY (wpisuje kod i zatwierdza).
- Komunikaty błędów (opis + wiersz kodu w lobby): nie ma gry z kodem (link transportu „no game with code”), inna wersja moda (host vNN, ty v48), inna wersja gry, brak DLC, gra pełna (nowe Status.full z reported_busy), nie wpuszczono.
- session.cpp: klient czyta Reject hosta także w innej wersji protokołu (wcześniej odrzucał go i łączył się w nieskończoność) → „Host ma inną wersję moda”. Działa dla przyszłych niezgodności (klient musi mieć ten build).
- Niezrobione: „łączy się ponownie…” w lobby (sieć nie zgłasza takiego stanu). Wszystko NIEPRZETESTOWANE w grze; układ kratek liczony od tekstu wiersza — mogą wymagać poprawki po zrzucie (kCellWidth/Height/Gap/Lead, kCodeButtonWidth, kLobbyRowWidth 480).

## 2026-11-04 19:50 — ekran co-op: kolumna wartości, kratki (po zrzutach Karola 19:41)
- Zrzut: kratki kodu zlewały się w jeden ciemny pas, wiersze „NAZWA: WARTOŚĆ” nierówne (strzałki raz tu, raz tam). Teraz jak w ustawieniach gry: nazwa po lewej, wartość w kolumnie (kValueColumn 150 od lewego brzegu nazwy) — kopia etykiety wiersza (Instantiate, „TLDCoop_Value”, pivot Left, resize freely), strzałki wokół wartości, pasek zaznaczenia do końca wartości / kratek. Kratki 32×40, odstęp 10, czcionka 24. Lobby i strona MODS: jedna etykieta jak dotąd (values_on=false).
- Karol: „nie działa wchodzenie w modyfikacje” — w logu 19:40:59 „menu: the MODS page”, zamknięta 19:41:07; nie wiadomo, co nie działa (zapytany).

## 2026-11-04 20:05 — crash Kerela przy drzwiach (klient, build 434594d... właściwie 18:45)
- coop.log 20:00:31: „door used: 'InteriorLoadTrigger' (to RuralStoreA)” (ThompsonStore, pokój zbudowany 3 s wcześniej i trzymany dla widoku okien) → FAULT 0xC0000005 UnityPlayer.dll+0xa77fa (adres 0x5d). Brak „swung open”/„no door leaf” → padło w swing(): szukanie skrzydła u rodzica/dziadka LoadScene (dodane 04.11 08:50; GetComponentsInChildren(Renderer) + get_bounds na dużej hierarchii). 03.11 kliknięcia drzwi działały wiele razy bez tego.
- Cofnięte (kLookAbove=false): skrzydło tylko w obiekcie samych drzwi. Pewności 100% nie ma — jeśli dalej pada przy drzwiach, podejrzane door_sound albo stan trzymanego pokoju (warstwy/kolidery).

## 2026-11-04 20:30 — Seamless Interiors WYŁ na stałe (na razie), zwierzęta bez wnętrz (decyzja Karola)
- Karol: wnętrza dają więcej błędów niż są warte; zostają w kodzie, ale WYŁĄCZONE: config.cpp wymusza seamless_interiors=false (coop.ini ignorowane), wiersz w MODS nie daje się włączyć (dźwięk błędu, opis po polsku). Hooki SI (LoadScene.Activate, LoadSceneWithLoadingScreen) dalej zakładane — door_used i niżej host_leaving_scene z nich korzystają.
- Zwierzęta (system właścicieli, ownership=1):
  1. Log Kerela 19:58:09/19:58:34: „we run #… now (… one made for it)” i 16 ms później „TLD switched off it” — region spawnu w grze, która spawnuje (i_control), liczył przekazane zwierzę ponad cel i je wyłączał → Gone → host zapominał, region dosypywał nowe (znikanie / stada). Teraz: wyłączone przez TLD w ciągu 15 s od przejęcia → detach_from_region + SetActive(true), max 2× (Owned.rescued), log „TLD switched off #… after we took it; switched back on…”.
  2. Host wchodzi do budynku (LoadSceneWithLoadingScreen do innej sceny): wildlife_sync::host_leaving_scene — wszystkie zwierzęta hosta w tej scenie od razu transfer() do najbliższego znajomego w tej scenie (z rekordem TLD, bez limitu 4/s), zanim scena się wyładuje. Wcześniej: dopiero po wczytaniu, z ostatniego rekordu rejestru, 4/s (zwierzęta stały/skakały). Log „the host leaves '…': N animal(s) handed…”.
- Nieprzetestowane. Do sprawdzenia: host wchodzi do domu, znajomi przy zwierzętach — w logu hosta „the host leaves”, u znajomych „we run #… now”, brak „switched off” bez „switched back on”.

## 2026-11-04 20:40 — drzewa dalej białe (zrzut Karola 20:22)
- Log Kerela: „a tree material shows 'TRN_TreeCedarBark_Snow_B' (ours #-1, snow 100.0)” → potem 106 materiałów na stan 8 (Highest). Światło normalne (sky's light ~0.6). Czyli to nie światło ani brak tekstury: śnieg na drzewach na maksimum (100) i najbielszy zestaw (nawet po przyciemnieniu 0.55).
- trees_math.h: shown_index = min(texture_index, kWhitestShown=4) — najbielszy pokazywany zestaw to Medium; dynamic_trees.cpp używa shown_index. Testy bez zmian (texture_index ten sam).

## 2026-11-04 20:55 — logi wszystkich 20:35 (POBIERZ LOGI zadziałało: zip Shatscy doszedł) — zwierzęta
- Shatsca (16:48 build): 2× crash przy drzwiach (20:02 RuralStoreA, 20:05 MiltonHouseA), ten sam UnityPlayer+0xa77fa co u Kerela → SI wyłączone, swing cofnięty.
- Kerel F11 20:30: wchodząc pierwszy do domku (SI) nie widział rzeczy — SI, nieaktualne po wyłączeniu.
- Zwierzęta: (1) Kerel jako „spawnujący” w RuralRegion, gdy host był w FarmHouseA: zwierzę, które TLD włączało (odrodzenie regionu), od razu znów chowane jako „początkowe” (g_initial) — pętla 51× co ~1 s, nic, co TLD stworzyło przy Kerelu, nie zostawało. Teraz respawned → g_initial.erase. (2) Host w trakcie ładowania sceny liczony już jako obecny w nowej scenie (scena zmienia nazwę przed „ownership on”) → host_arbitrate dawał hostowi ciała („its owner left; the host keeps the body”), których nie mógł przyjąć („can't take: we aren't in its scene”) — 62×. present_players pomija nas, dopóki g_own_scene != scena.
- Karol: „gdy host był w domku zające mnie nie widziały, a jak wyszedł zaczęły uciekać”. Z logów: zające w rejestrze przy nim nie były (wszystkie ~250–400 m dalej, zamrożone lub przeniesione); jego własne były ukryte (czekały na przekazania), a odrodzenia chowane w pętli (1). Po wyjściu hosta jego gra pokazała i udostępniła 3 zające (#38365654–56) — to te uciekały. Pewności brak; po (1) przy nim powinny być żywe zające jego gry, które reagują normalnie.

## 2026-11-04 21:05 — przesuwanie mebli w trybie kryjówki niewidoczne dla innych (Karol)
- Logi 20:28–20:29 FarmHouseA: Shatsca przestawiał INTERACTIVE_KingBedB (PLACED) 5×, u Kerela „the other player placed … same here now” 5× — ale łóżko zostawało. PlaceableManager.DeserializeScenePlacements jednego rekordu nie przesuwa placeable, który już stoi w scenie.
- world_objects.cpp apply_placement: po wczytaniu, dla stanu 0 (stoi): Placeable.FindOrCreateAndDeserialize(m_Guid, m_Serialized) — ten sam placeable po id (albo nowy) ustawiony jak w rekordzie (pozycja/obrót/skala). Log „objects: <id> put where the other player put it”. Nieprzetestowane.

## 2026-11-04 21:15 — suszenie skóry niewidoczne u znajomego (Karol, zrzuty: u hosta „stopień wysuszenia: 1%”, u niego nic)
- world_items.cpp: load_evolve — z rekordu przedmiotu (m_EvolveItemSerialized → m_TimeSpentEvolvingGameHours) ustawiany czas suszenia (EvolveItem), przy obu ścieżkach wczytywania (obok load_lamp). Host co 30 s wysyła rekord leżącego przedmiotu, który się suszy („is curing”). Log, jeśli TLD mówi, że u nas nie może schnąć (CanEvolve false). Nieprzetestowane.

## 2026-11-04 21:40 — House Lights wbudowany (protokół v49), FPS co minutę, legenda mapy niżej
- House Lights (DemonBunnyBon/tld-house-lights, MIT, zgoda autora wg Karola): src/house_lights.cpp/.h, third_party/house-lights (LICENSE, NOTICE, hlbundle = modele włączników, Unity 2021.3.6f1), tools/houselights/gen_bundle.py → src/house_lights_bundle.inc (paczka w DLL, przy pierwszym użyciu zapisywana do TLDCoop\tldcoop_houselights.bundle i LoadFromFile).
  Hooki: GameManager.InstantiatePlayerObject (przed: włączniki gry podmieniane na modele moda + własne z tabeli kCustom; tylko wnętrza, chyba że [houselights] outside=1, i DamTransitionZone), AuroraModularElectrolizer.Initialize (zbiera, bez radii/ekranów), AuroraManager.RegisterAuroraLightSimple, AuroraManager.Update (po: UpdateElectroLights — w 2.59 nie ma UpdateForceAurora, której używa mod), PlayerManager.UpdateHUDText („Włącz/Wyłącz światło”), PlayerManager.InteractiveObjectsProcessInteraction (klik), Weather.IsTooDarkForAction.
  Zmiany względem moda: zasięgi/kolory/cienie ustawiane raz (na zmianę ustawień), nie co klatkę; poprawiony błąd moda (dźwięk prostych świateł brał listę złożonych); stan włącznika pamiętany na scenę w sesji.
  Co-op: Msg::HouseLights=56 (u8 on, scena[64]), host przekazuje dalej; włączone światła ogłaszane co 5 s (wchodzący gracz też ma). Włącz/wyłącz dla wszystkich: host, MODS → HOUSE LIGHTS (kFeatureHouseLights=32). Poziomy: coop.ini [houselights] (intensity 2, range 1.4, white, no_flicker, cull 50, shadows, audio, reach 1, outside).
- core.cpp: co minutę „frames: N fps on average…, co-op X ms a frame: najdroższe części” (Karol: FPS spada do 54 z ~104).
- coop_hud.cpp: legenda graczy na mapie 150 px niżej (nachodziła na „MAPA”).
- Nieprzetestowane w grze.

### 2026-11-04 ~22:30 UTC — tusza jelenia (duplikat), realistyczne drzewa + pnie
- Logi 20:57–21:36 (Kerel + Shatsca): Karol F11 21:11 „mogę zduplikować rzeczy z tuszy jelenia, które zebrał Shatsca”.
  W logach: host miał martwe jelenie z rejestru (ciała „harvested in its last game”), ale u Karola żadna tusza jelenia hosta
  się nie pojawiła (tylko wilki) — Karol miał w swojej grze WŁASNĄ, całą kopię tej tuszy (martwe zwierzęta z jego zapisu
  nie są ukrywane).
- Poprawka (wildlife_carcass.cpp): klient — nasza własna tusza tego samego zwierzęcia (ten sam gatunek, do 4 m od tuszy
  hosta) jest wyłączana i usuwana z zapisu („our own … carcass … lay where the host's #… does”); host — martwa tusza
  zrobiona z zapisu liczy się od razu jako tusza (seconds_dead od pierwszego zauważenia) + co 30 s log, ilu martwych NIE
  wysłał i dlaczego (bez id / bez BodyHarvest / wyłączony). Jeśli jelenie dalej nie przyjdą — ten log powie czemu.
- Drzewa: nowy tools/trees/realistic.py (z oryginalnego bundla 34babc2): pnie przemalowane na kolory kory (sosna
  szarobrązowa z płatami, cedr czerwonobrązowy włóknisty), relief wzmocniony, śnieg na korze zostaje; gałęzie: śnieg
  kępami na igłach, zieleń między, osobno dla każdego stanu (0–55% pokrycia). Limit shown_index (max Medium) cofnięty.
  Bundle w third_party/dynamictrees + w zipie; podgląd pnie_podglad.jpg.

### 2026-11-04 ~22:10 UTC — łom zdublowany, House Lights w co-op, naprawa szafki
- Logi 21:37–21:54 (build 20:15 UTC). Kerel F11 21:54 „zduplikowałem łom”: jego własny łom 6714e09a (położony na półce
  w FarmHouseA w poprzedniej sesji) leżał w jego grze, ale nie jako „luźny” przedmiot → przyszedł łom hosta, zrobiła się
  kopia obok (ac665f21), obie podniesione, obie wyrzucone = 2 łomy. Teraz (world_items apply_put): istniejący, włączony,
  nie w plecaku/pojemniku → odpięty od rodzica, ustawiony tam gdzie u hosta, bez drugiej kopii; jeśli nadal nie jest luźny
  — usuwany i robiona kopia hosta (log z nazwą rodzica).
- Shatsca F11: (1) „włącznik House Lights zniknął po wyjściu z budynku i powrocie” — scene_in szedł tylko z hooka
  InstantiatePlayerObject (przy wejściu z drzwi scena jeszcze nie aktywna); teraz też z on_frame przy zmianie sceny.
  (2) „inni nie widzieli światła” — Msg::HouseLights nie był na liście przyjmowanych w session.cpp (pakiety wyrzucane).
  (3) „naprawione drzwiczki szafki w kuchni (narzędzia stolarskie) u drugiego nadal zniszczone” — w logach zero śladu;
  dodane logi: przy starcie czy RepairableInteractiveItem jest dzielone, i w każdej scenie ile rzeczy do naprawy (+nazwy).

### 2026-11-04 ~21:40 UTC — włączniki House Lights znikają po wyjściu/wejściu (2)
- Log 22:15–22:28 (build 21:07 UTC): światło w co-op już działa (on/off od drugiego gracza). Po wyjściu i wejściu do
  FarmHouseA nadal brak włączników. Przyczyna: prefaby z bundla Unity wyładowuje przy zmianie sceny (unused assets);
  cache zwracał martwy prefab → nasze się nie robiły, a oryginalne były już ukryte. Teraz: martwy prefab/bundle/shader
  ładowany ponownie, oryginał ukrywany dopiero gdy nasz powstał, a skan powtarzany co 1 s do 20 s po wejściu (log).
- Drzewa: screenshot Karola (mgła, korony jako płaskie szaro-niebieskie wielokąty). Bundle sprawdzony: alfa i mipy
  zgodne z oryginałem; zapytany o porównanie z wyłączonymi Dynamic Trees / przy dobrej pogodzie.

### 2026-11-04 ~22:05 UTC — House Lights: wszystkie lampy ciepły biały
- Karol: wszystkie lampy z włączników w tym samym kolorze. coop.ini [houselights] warm_white=1 (domyślnie): kolor
  (1.0, 0.84, 0.64) dla każdej lampy; 0 = kolor każdej lampy jak w grze (white=1 nadal odbarwia).
- Pytania Karola (tylko czy się da): jeden kolor — zrobione; „maszyna hazardowa” (włożyć przedmiot, wylosować mnożnik) —
  da się, większa robota (model, interakcja, losuje host, zasady); czeka na decyzję i plan.

### 2026-11-04 ~22:15 UTC — „logi wszystkich” bez logów innych; imię „Player”
- Log 22:15–23:10 (3 graczy: Shatsca, Kerel, szymongrzyb2017; build 21:07): w folderach logi_wszystkich tylko log
  Karola, a pliki i zgłoszenia podpisane „Player”. Przyczyna: zip z 22:00 rozpakowany na gry nadpisał coop.ini
  (upstreamowe TLDCoop\coop.ini z name=Player) → wszyscy mieli na starcie „Player”; coop_report czytał imię tylko raz
  przy starcie, a prośba o logi szła do „Player”, więc każda gra uznawała ją za swoją i pomijała.
- Poprawki: coop_report: imię odświeżane z ustawień (set_name), prośba/odbiór logów adresowane „imię#numer gracza”;
  build.yml: coop.ini usuwany z zipa (mod sam pisze domyślny).
- Naprawa szafki: „0 thing(s) to repair” w FarmHouseA → to nie RepairableInteractiveItem; mechanizm nadal nieznany.

### 2026-11-05 ~07:30 UTC — logi Shatscy (22:14–23:26, 3 graczy): królik, mięso na ognisku, mokre ubranie, tusze bez id
- Zgłoszone: (1) włącznik po wyjściu — na buildzie 22:08 już działa (FarmHouseA/Basement przy każdym wejściu);
  (2) „dubluje się królik po zabiciu”: królik Kerela zabity kamieniem przez Szymona → host robi z niego przedmiot
  GEAR_RabbitCarcass, a u Kerela jego martwy królik leżał jeszcze 20 s (g_dying) i dało się go podnieść → mały
  zwierz (królik) teraz znika u właściciela po 1,5 s; (3) „Szymonowi nie widać mięsa wilka na ognisku”: gra Szymona
  usunęła mięso+garnek hosta (prawdopodobnie gotował w tym samym miejscu) i host je skasował → usunięcie przez innego
  gracza do 8 s po naszej zmianie jest ignorowane, nasz garnek i jedzenie wysłane wszystkim ponownie;
  (4) „suche ubranie u drugiego całkiem przemoczone”: przy ponownym pokazaniu istniejącej kopii przedmiotu brano jej
  stary stan → load_wear: mokrość/zamarznięcie ubrania i stan (HP) z zapisu drugiego gracza (log raz przy starcie
  „wetness and condition shared”).
- Niezgłoszone: host nie wysyłał tusz bez id zwierzęcia (z zapisu sprzed co-op; „not listed: 6 without our id”) →
  stąd całe kopie u innych (tusza jelenia). Teraz dostają id kExtraIdBase|n i są dzielone (u klienta jego własna kopia
  w tym miejscu znika). Szybkie przełączanie światła co 0,2 s w logu = ktoś klikał, nie pętla.

### 2026-11-05 ~14:00 UTC — AUTOMAT (jednoręki bandyta): ustalenia, NIE ZACZYNAĆ przed „zaczynamy”
- 3 bębny (wilk, mięso, zapałka, czaszka, ognisko, rzadki złoty): 3 złote ×10, 3 takie same ×3, para ×2, bez pary i
  bez czaszki zwrot, czaszka w środku przegrana; rzadkie zacięcie zabiera wszystko. Losowo z bębnów, nie tabelka %.
- Wkładanie jak do skrzyni, po zamknięciu kręci; każdy przedmiot osobno, stosy w sztukach; wygrana zostaje w środku,
  ten sam stan/mokrość; limit 3 na gracza na dzień gry; zakazane oddawane z napisem.
- Wolno: łuski, proch, groty, drzewca; garnki/patelnie; bandaże, tabletki do wody, przeciwbólowe, antybiotyki, herbata
  z ziół; jedzenie sklepowe/puszki, napoje; drewno, węgiel, tkanina, złom, zapałki, hubka, świece, papier; nafta, olej;
  zwykłe ubrania. Nie wolno: naboje, gotowe strzały, mięso i ryby, jelita i skóry (+ materiały z nich), broń,
  narzędzia, śpiwór, apteczka, lepsze ubrania ze skór, fabularne/unikalne, płytki/mechanizm/automat.
- Przepisy: płytka elektroniczna (z rozbierania elektroniki, rzadko w szufladach) → mechanizm bębnów (frezarka: 4 złom +
  1 płytka) → automat (stół warsztatowy: mechanizm, 2 płytki, 6 złomu, 4 drewna z odzysku, 2 tkaniny; ~3 h, ~15 kg),
  stawiany/przenoszony jak przenośna skrzynia. Ikony od Karola (ChatGPT), model w kodzie (bryły, ruchome bębny).
- Co-op: każdy robi/stawia/gra (własny limit), losuje host, zajęty automat = czekasz; host włącza w MODS „AUTOMAT”.
  Bez moda nowe przedmioty → złom; kopia zapisu przed testami.
- 2026-11-05 15:16 (Karol, po porównaniu z FNV/Stardew/Rust): w automacie tylko JEDEN rodzaj przedmiotu naraz, jeden wynik
  na całą stawkę (w sztukach); limit stawki 10 szt. albo 3 kg; tabela wygranych na automacie; ochrona przed
  wczytywaniem (wynik zapisany przy dźwigni, po wczytaniu ~2 min nieczynny); ZUŻYCIE automatu zamiast limitu 3/dzień
  (kilka % na losowanie, gorszy stan = częstsze zacięcia, naprawa ELEKTRONIKĄ — płytką elektroniczną, nie złomem). Efekty jackpota i „szczęście” — nie.
- 2026-11-05 15:22 (Karol): jackpot (3 złote) ×5, nie ×10; tabela wygranych tylko w opisie automatu, po najechaniu
  widać STAN automatu. Karol chce też model 3D automatu z generatora (z ikony) — wtedy bundle Unity 2021.3 w zipie.

