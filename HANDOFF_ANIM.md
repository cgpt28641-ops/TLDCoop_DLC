# HANDOFF: lepsze animacje postaci graczy (Unity w GitHub Actions)

Osobny wątek od głównego HANDOFF.md (09.11.2026). Cel: poprawić ruch awatarów innych graczy w TLDCoop_DLC.
Problem: postacie wyglądają u wszystkich „tak samo źle”, a nikt nie widzi wyniku przed testem w grze.
Pomysł: renderować ruch w Unity uruchamianym przez GitHub Actions, oglądać klatki, poprawiać, a potem eksportować do moda.

## Zasady Karola (obowiązują)
- Do Karola pisz po polsku, krótko i prosto.
- Po każdym buildzie moda wyślij link: https://github.com/cgpt28641-ops/polar-link/releases/download/dll-latest/winhttp.dll
- Nigdy nie wpisuj haseł. Sekrety Unity dodaje Karol w ustawieniach repo.
- Wszystko jest w jednym `winhttp.dll`, bez dzielenia na osobne DLL.

## Nowe repo animacji: PUBLICZNE
Karol ma wyczerpane limity na prywatne repo.
- Nazwa nie może kojarzyć się z TLD. Propozycja: `mocap-gait-lab` (albo `humanoid-motion-lab`). Karol zakłada repo i podaje nazwę.
- Sekrety repo (Settings → Secrets and variables → Actions): `UNITY_EMAIL`, `UNITY_PASSWORD` i opcjonalnie `UNITY_SERIAL` (Unity Personal).
- **Wolno** wrzucić szkielet z gry (Karol się zgodził 09.11), ale z neutralnymi nazwami kości (`hips`, `spine1`,
  `upperarm_l`…). Mapowanie na nazwy z gry (`Hips_jnt`, `LeftUpArm_jnt`…) zostaje w repo moda.
- **Nie wolno** wrzucać innych plików z gry: mesh postaci, tekstur, klipów TLD. Tylko darmowy mocap CMU
  (free for any use) i darmowy lub prosty model ludzika (kapsuły na kościach albo model CC0).
- W README i nazwach plików nie może być słów „TLD”, „Long Dark” ani „Hinterland”.

## Co już istnieje (repo moda: cgpt28641-ops/polar-link, dawniej TLDCoop_DLC)
- `tools/mocap/` (Python, numpy):
  - `bvh.py`: czytnik BVH;
  - `rig.py`: FK, kwaterniony;
  - `retarget.py`: `retarget`, `humanize` (odsunięcie ramion, zgięcie łokci), `smooth`;
  - `gait.py`: wycina jeden pełny krok z chodu CMU (heel strike → heel strike), mierzy prędkość, domyka pętlę;
  - `gen_clips.py`: buduje `src/avatar_mocap.inc`;
  - `preview.py`: paski podglądu.
  - Uruchomienie: `python3 tools/mocap/gen_clips.py <checkout github.com/una-dinosauria/cmu-mocap> [preview_dir]`.
- `tools/mocap/rig_local.json`: **szkielety z gry**, dwa rigi, `Body_mackenzie_indoor` (M) i `Body_astrid` (F).
  Lista kości z polami `name`, `parent`, `pos`, `rot` (kwaternion xyzw), `scale` w przestrzeni lokalnej.
  To jest szkielet do nowego repo (po zmianie nazw).
- Lista klipów (`gen_clips.py`, `CLIPS` i `GAIT`):
  - gait: `g_walk` 07_01, `g_run` 35_17, `g_creep` 77_14, `g_idle` 82_08;
  - akcje: climb, kneel, fire, chop (tylko górna połowa ciała), search, work, sit, air, fall, carry.
  - `HUMAN` = korekty ramion i łokci dla poszczególnych klipów.
- Format `src/avatar_mocap.inc`:
  - 19 kości (`kMocapBoneNames`: Hips_jnt, Spine1–3, Neck, Left/Right Shoulder, UpArm, Elbow, Hand, nogi…);
  - 30 fps;
  - obroty lokalne jako int16 ×4 (kwaternion × 32767) w tablicy `kMocap_<clip>_<M|F>_rot`;
  - przesunięcie bioder od spoczynku w mm w tablicy `kMocap_<clip>_<M|F>_hips`;
  - pętle są przenikane przez ostatnie 0,4 s.
- `src/avatar.cpp`: steruje awatarem.
  - Body pack ma legacy Animation ze stanami idle/walk/run (`bodypack/Editor/BuildBodies.cs`).
  - Na to mod nakłada klipy z mocapu: gait z fazą kroku i skalą prędkości, a akcje na warstwie 1, całe ciało albo `_up` od Spine1.
  - `g_mocap_gait`: komenda dev `pose gait 0` przełącza na klipy z body packa.
- Body pack (`bodypack/`) Karol buduje u siebie w Unity (`build_bodypack.cmd`). Pliki gry nie są w repo.

## Plan
1. **Nowe repo i workflow GameCI.** `game-ci/unity-builder` (albo `unity-test-runner`) z `-executeMethod`.
   Renderowanie wymaga bez `-nographics` + xvfb (obrazy GameCI to umożliwiają). Najpierw minimalny test:
   1 kapsuła → 1 PNG w artefakcie. Pierwsze ustawienie licencji i renderu może wymagać kilku podejść.
2. **Skrypt edytora** `Assets/Editor/MotionLab.cs`:
   - buduje szkielet z JSON-a (neutralne nazwy) i mesh z kapsuł albo prostego ludzika na kościach;
   - wczytuje klipy (BVH → retarget w Pythonie → JSON z obrotami albo AnimationClip);
   - odtwarza je i renderuje z kilku kamer (przód, bok, 3/4, z góry na stopy);
   - zapisuje PNG co kilka klatek, arkusz kontaktowy i MP4 (ffmpeg w akcji). Wynik idzie do artefaktu.
3. **Ocena i poprawki** (Claude ogląda klatki narzędziem Read). Typowe problemy do sprawdzenia:
   - ślizganie się stóp (foot locking / IK stóp);
   - przejścia stanie ↔ chód ↔ bieg bez szarpnięć (blend po fazie kroku);
   - obrót w miejscu (przestępowanie zamiast obrotu jak na talerzu);
   - idle: oddech, kołysanie, zmiana nogi;
   - sztywne ręce i ramiona za blisko ciała;
   - wysokość bioder i zgięcie kolan.
4. **Eksport do moda.** Nowe dane klipów w formacie `avatar_mocap.inc` (albo rozszerzenie formatu) z powrotem w
   polar-link, przez `tools/mocap/gen_clips.py` albo nowy eksporter. Potem build na CI, link dla Karola,
   wpis na #updates (dokładnie, co zmienione) i test w grze.
5. Opcjonalnie później: komenda F1 `avatar_shots` w modzie robi zrzuty prawdziwej postaci w grze
   (stanie, chód, bieg, obrót z kilku stron) do zipa z logami, żeby porównać z podglądem z Unity.

## Discord (od 09.11, 19:42)
- **Bez automatycznego sprawdzania** (za dużo tokenów). Sprawdzaj Discorda tylko wtedy, gdy Karol napisze
  „sprawdź Discorda”. Nie ustawiaj zaplanowanych sprawdzeń.
- Działaj tylko, gdy ktoś oznaczył bota (`inbox/mentions.lst`).
- Most: repo cgpt28641-ops/tldcoop-discord. `gh workflow run discord.yml -R cgpt28641-ops/polar-link --ref main`,
  potem `git pull --rebase`, potem czytaj `inbox/mentions.lst`.
- Pliki `outbox/*.txt` muszą mieć w pierwszej linii `#kanał` (z krzyżykiem), np. `#updates`.

## Stan do sprawdzenia przez graczy
Zrobione 08.11 wieczorem, czeka na test (szczegóły w HANDOFF.md):
- wilki przy trzech graczach;
- świeża tusza wilka;
- ikony ClothingExpanded;
- sadzonki brzozy po spaniu;
- szafki „no answer”.
