# polar-link

Co-op do trybu Survival w **The Long Dark**: 2–4 osoby w jednym świecie, przez Steam, z kodem dołączania.
Prywatna przeróbka moda TLDCoop (ScarredVeins) z obsługą DLC i kilkoma wbudowanymi dodatkami.

> **Wersja testowa.** Przed grą zrób kopię zapisów (niżej).

---

## Pobieranie

Najnowszy plik jest zawsze tutaj (każda zmiana kodu buduje się sama):

**https://github.com/cgpt28641-ops/polar-link/releases/download/dll-latest/winhttp.dll**

Przy aktualizacji wystarczy podmienić ten jeden plik. Pełna paczka z folderem `TLDCoop` (postacie, animacje, ok.
220 MB) robi się tylko na żądanie: **Actions → build → Run workflow → Artifacts**.

## Instalacja

1. **Kopia zapisów:** skopiuj folder `%LOCALAPPDATA%\Hinterland\TheLongDark` w bezpieczne miejsce.
2. **Zamknij grę.**
3. **Folder gry:** Steam → prawy na The Long Dark → **Zarządzaj → Przeglądaj pliki lokalne** (tam jest `tld.exe`).
4. Wrzuć `winhttp.dll` **obok `tld.exe`**, a folder `TLDCoop` (z pełnej paczki) obok niego.
5. Uruchom grę. Jeśli w Survival po NEW / RESUME pojawi się **SOLO OR CO-OP?** — działa.

```
TheLongDark\
├── tld.exe
├── winhttp.dll
└── TLDCoop\        (ustawienia coop.ini, log coop.log, postacie i tekstury)
```

Jeśli masz MelonLoadera jako `winhttp.dll`, zmień nazwę jego pliku na `winmm.dll`.

## Jak grać

1. Wszyscy uruchamiają grę ze Steama.
2. **Host:** Survival → NEW / RESUME / LOAD → **CO-OP** → MODE **HOST** → **CONFIRM**. W poczekalni widzi kod i
   wysyła go znajomym.
3. **Znajomi:** Survival → NEW / RESUME / LOAD → **CO-OP** → MODE **JOIN** → kod → **CONFIRM** → **READY**.
4. Host wpuszcza każdego (**LET IN**) i daje **START**.

Do trwającej gry też da się dołączyć: host wciska **F9** (wpuść) albo **F10** (odrzuć). Tryb, mapę, pogodę i porę
dnia ustala host. Gracz dołączający musi mieć wszystkie DLC hosta (inaczej zobaczy **MISSING DLC**).

## Ustawienia

Plik `TLDCoop\coop.ini` tworzy się przy pierwszym uruchomieniu i ma opis przy każdej opcji. Po zmianie zrestartuj
grę. Najczęściej używane:

| Opcja | Co robi |
|---|---|
| `[clothing] enabled=0` | Ubrania z dodatkami (pas na narzędzia, raki, pochwa na karabin). **W testach**, `1` włącza. |
| `[sky] enabled=1` | Ładniejsze nocne niebo (gwiazdy, księżyc z fazami). |
| `[fuel] drain=1` | Przycisk DRAIN FUEL: nafta z lampy z powrotem do kanistra. |
| `[food] enabled=0` | Realistyczne kalorie i czas gotowania (w co-opie decyduje host: MODS). |
| `[houselights] enabled=1` | Działające włączniki światła w domach. |
| `[ambient] enabled=1` | Oświetlenie wnętrz i nocy (w co-opie decyduje host: MODS). |
| `[trees] enabled=1` | Śnieg na drzewach. |
| `[interiors] enabled=0` | Wchodzenie do budynków bez ekranu ładowania (w budowie). |
| `[hud] names / radar / chart` | Nicki nad głowami, radar, rysowanie mapy. |
| `[death] lose_percent=70` | Ile przedmiotów przepada przy śmierci. |
| `[report] key=F11` | Klawisz zgłoszenia błędu (pakuje logi do `TLDCoop\reports`). |
| `dlc_check=1` | Gracz musi mieć DLC hosta (`0` wyłącza, na własne ryzyko). |

## Zgłaszanie błędów

- Wciśnij **F11** w chwili błędu: mod spakuje logi do `TLDCoop\reports`. W co-opie robią to wszyscy gracze naraz.
- Po crashu log z crashem to `TLDCoop\coop-prev.log` (bieżący to `coop.log`). Wrzuć oba na Discorda (#błędy).

## Wyłączanie i odinstalowanie

- **Wyłączenie:** pusty plik `disabled` w folderze `TLDCoop`.
- **Odinstalowanie:** usuń `winhttp.dll` i folder `TLDCoop`. Zapisy gry zostają nietknięte.

## Budowanie

Każde wypchnięcie do `main` buduje `winhttp.dll` w GitHub Actions i wrzuca go do wydania `dll-latest`.
Lokalnie: Windows, Visual Studio 2022 Build Tools (C++), CMake, Ninja, potem `build.cmd`.

## Autorzy i licencje

- Mod TLDCoop: **ScarredVeins** ([oryginał](https://github.com/ScarredVeins/TLDCo-Op)); poprawki pod 2.58:
  **afkfatih**.
- Ta wersja: **cgpt28641-ops** z Claude.
- Wbudowane dodatki innych autorów (za ich zgodą lub na licencji MIT) są opisane w `third_party/` i w pliku
  `THIRD_PARTY_NOTICES.txt` dołączanym do każdego builda.
- [MinHook](https://github.com/TsudaKageyu/minhook): Tsuda Kageyu (BSD 2-Clause).
- **The Long Dark** należy do **Hinterland Studio**. To nieoficjalny mod fanowski; potrzebna jest własna kopia gry.
