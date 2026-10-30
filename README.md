# TLDCoop_DLC: co-op do The Long Dark z obsługą DLC

**Tryb Survival w The Long Dark we 2–4 osoby, w jednym świecie, przez Steam, z kodami dołączania.**

To prywatna przeróbka moda [TLDCoop](https://github.com/ScarredVeins/TLDCo-Op) autorstwa **ScarredVeins**. Cała
zasługa za sam mod należy do niego (zobacz [Autorzy](#autorzy)). Ta wersja dodaje:

- **Działanie na The Long Dark 2.58** (188966 S). To poprawki
  [afkfatih](https://github.com/afkfatih/TLDCo-Op/tree/support-tld-2.58) z
  [tego zgłoszenia](https://github.com/ScarredVeins/TLDCo-Op/issues/1).
- **Jeden gracz naraz w szafce.** Gdy ktoś przeszukuje szafkę, skrzynię czy zwłoki, inni nie mogą jej otworzyć,
  dopóki nie skończy. Wcześniej dwie osoby w tej samej szafce potrafiły wysypać grę hosta.
- **Wspólną wodę w toaletach i zbiornikach.** Co jeden gracz zaczerpnie, tego ubywa u wszystkich.
- **Obsługę DLC.** Mod pyta grę, jakie DLC ma zainstalowane, i nie wpuszcza do świata hosta gracza, któremu
  brakuje jego DLC. Gracz dostaje wtedy jasny komunikat, czego mu brakuje.

> **Wersja testowa.** Mod działa, ale mogą się zdarzać błędy. Przed grą zrób kopię zapisów (zobacz
> [Instalacja](#instalacja)).

Oryginalny opis po angielsku, ze zrzutami ekranu i pełną listą funkcji, jest w
[README.original.md](README.original.md).

---

## Spis treści

- [Co potrafi](#co-potrafi)
- [Jak działa obsługa DLC](#jak-działa-obsługa-dlc)
- [Wymagania](#wymagania)
- [Instalacja](#instalacja)
- [Jak grać](#jak-grać)
- [Ustawienia](#ustawienia)
- [Inne mody (MelonLoader)](#inne-mody-melonloader)
- [Wyłączanie i odinstalowanie](#wyłączanie-i-odinstalowanie)
- [Czy to bezpieczne?](#czy-to-bezpieczne)
- [Rozwiązywanie problemów](#rozwiązywanie-problemów)
- [Czego jeszcze nie ma](#czego-jeszcze-nie-ma)
- [Budowanie samemu](#budowanie-samemu)
- [Autorzy](#autorzy)

---

## Co potrafi

[![Znajomy w Twoim świecie, obok wilka upolowanego razem](screenshots/05-friend-in-your-world.jpg)](screenshots/05-friend-in-your-world.jpg)

**Start z menu gry**
- Po wybraniu NEW, RESUME albo LOAD w Survival pojawia się wybór **SOLO albo CO-OP**.
- Host dostaje krótki **kod**, a znajomi go wpisują. Nie trzeba podawać IP ani przekierowywać portów, bo wszystko
  idzie przez Steam.
- Przed startem jest **poczekalnia** (lobby). Host wpuszcza graczy (LET IN / TURN AWAY), oni wciskają READY, a host
  START.
- Mogą grać **maksymalnie 4 osoby**. Do trwającej gry też można dołączyć: host wciska **F9**, żeby wpuścić gracza,
  albo **F10**, żeby go odrzucić.
- Gra toczy się na poziomie trudności hosta, w jego świecie, z jego porą dnia i pogodą.

**Widzicie się nawzajem**
- Znajomi wyglądają jak jedna z **23 postaci z fabuły** (Mackenzie, Astrid, Molly, Jane…).
- Widać, co trzymają w rękach. Latarnia, pochodnia czy raca naprawdę oświetlają otoczenie.
- Postacie jedzą, śpią w łóżku, kucają i podają przedmioty.
- **Krew:** ślady krwi krwawiącego gracza (krople, plamy po walce ze zwierzęciem) widzą wszyscy.
- **Stan znajomego:** patrząc na znajomego z bliska (do 5 m) widzisz pod jego nazwą ogólny stan i dolegliwości
  (np. hipotermia, zatrucie, skręcenie, krwawienie).
- **Wspólne oznaczenia:** oznaczenia sprayem (a więc i znaczniki na mapie, które z nich powstają) widzą wszyscy.
- **Kopia zapasowa:** przed każdą grą w co-op zapisy są kopiowane do `TLDCoop\backups` (5 ostatnich kopii,
  ustawienie `backups=` w `coop.ini`).
- **Gesty:** machanie (F5), wskazywanie (F6), „chodź tu” (F7), „stój” (F8), kiwanie głową na tak (F3) i na nie (F4).
  Klawisze można zmienić w `coop.ini`, sekcja `[emotes]`.
- **Kolory graczy:** każdy gracz ma swój kolor na radarze, mapie i nad głową. Kolor znajomego można zmienić w
  `coop.ini`, sekcja `[colors]` (np. `Shatsca=blue`).
- Nad głową widać nazwę gracza, a na krawędzi ekranu strzałkę. Jest też mały radar, a gracze są widoczni na mapie
  gry.

**Wspólny świat**
- **Zwierzęta są wspólne.** Wszyscy widzą tego samego wilka, a strzały trafiają w to samo zwierzę.
- Wspólne są też **tusze** (mięso, skóra), **przedmioty** (co jeden podniesie, znika u innych), **szafki i sejfy**,
  **ogniska i gotowanie**, rozbite meble i zebrane rośliny.
- **Unikalne przedmioty** (notatki, klucze, plecak techniczny) każdy ma swoje.

**Przetrwanie razem**
- Każdy przedmiot można **dać znajomemu**.
- Ze **spaniem i czekaniem** jest tak, że czas przeskakuje dopiero, gdy wszyscy się położą. Każdy głosuje, jak długo.
- **Śmierć:** ciało zostaje z plecakiem, a Ty wracasz jako nowy ocalały obok znajomych.
- **Zrywanie połączenia:** po powrocie zmiany obu stron są łączone. Zapis hosta przechowuje świat, a zapis każdego
  gracza jego postać.

## Jak działa obsługa DLC

Świat należy do hosta, więc może w nim być wszystko z DLC, które host ma zainstalowane: regiony, budynki,
przedmioty. Gracz bez tego DLC nie miałby czego wczytać.

- Przy dołączaniu każdy gracz wysyła hostowi listę swoich zainstalowanych DLC. Lista pochodzi **z samej gry**
  (`TLD.OptionalContent.OptionalContentManager`), a nie ze sztywnej listy w modzie. Dzięki temu *Tales from the Far
  Territory*, *Iron Road* i przyszłe dodatki działają tak samo.
- Gracz, któremu **brakuje któregoś DLC hosta**, nie zostanie wpuszczony. Zobaczy komunikat **MISSING DLC** z nazwami
  brakujących dodatków.
- Gracz, który ma **więcej DLC niż host**, wchodzi bez problemu.
- Host może wyłączyć sprawdzanie: `dlc_check=0` w `coop.ini`. Robi to na własne ryzyko, bo regiony i przedmioty z
  brakującego DLC nie zadziałają u tego gracza.
- W `TLDCoop\coop.log` po uruchomieniu co-opa jest linia, która pokazuje, co widzi gra:
  ```
  dlc: installed optional content: ... (owned: ...)
  ```

## Wymagania

- **The Long Dark 2.59** (189140 S, sprawdzone też na 2.58) na Steam, Windows. Wszyscy gracze muszą mieć tę samą wersję gry. Mod to
  sprawdza.
- **Włączony i zalogowany Steam.** Każdy gracz potrzebuje własnej kopii gry.
- **Ten sam zip u wszystkich.** Ta wersja używa protokołu **v34**, więc nie zagra z oryginalnym TLDCoop v0.13.1
  (v27).
- **Gracze dołączający muszą mieć wszystkie DLC hosta** (zobacz wyżej).

## Instalacja

1. **Pobierz zip.** Wejdź w zakładkę **Actions** tego repozytorium, otwórz najnowszy udany build (zielony) i na dole
   strony, w sekcji **Artifacts**, kliknij **TLDCoop_DLC**. Musisz być zalogowany na GitHubie.
2. **Zrób kopię zapisów.** Wklej w pasek adresu Eksploratora `%LOCALAPPDATA%\Hinterland\TheLongDark` i skopiuj ten
   folder w bezpieczne miejsce.
3. **Zamknij The Long Dark.**
4. **Otwórz folder gry.** W Steamie kliknij prawym na **The Long Dark**, potem **Zarządzaj** → **Przeglądaj pliki
   lokalne**. To folder z plikiem `tld.exe`.
5. **Sprawdź, czy nie ma tam już `winhttp.dll`.** Zwykle go nie ma. Jeśli jest, kliknij go prawym → **Właściwości**
   → **Szczegóły**. Jeśli nazwa produktu to **MelonLoader.Bootstrap**, zmień nazwę pliku na `winmm.dll` (zobacz
   [Inne mody](#inne-mody-melonloader)).
6. **Wypakuj zip do folderu gry.** `winhttp.dll` ma leżeć **bezpośrednio obok `tld.exe`**, a folder `TLDCoop` obok
   niego.

Tak ma wyglądać folder gry:

```
TheLongDark\
├── tld.exe                       (gra, już tam jest)
├── winhttp.dll                   ← TLDCoop_DLC
└── TLDCoop\                      ← TLDCoop_DLC
    ├── tldcoop_bodies.bundle       postacie, jako które widać znajomych
    ├── tldcoop_eating.bundle       animacje
    ├── tldcoop_handover.bundle
    └── tldcoop_sleeping.bundle
```

7. **Uruchom grę ze Steama.** W Survival wybierz NEW albo RESUME. Jeśli pojawi się ekran **SOLO OR CO-OP?**, mod
   działa. Dziennik moda jest w `TLDCoop\coop.log`.

Każdy gracz instaluje ten sam zip.

## Jak grać

1. **Wszyscy** uruchamiają The Long Dark z włączonym Steamem.
2. **Host** wybiera **Survival → NEW** (albo RESUME lub LOAD), potem **CO-OP**. Na ekranie ustawień wybiera MODE
   **HOST**, postać (**CHARACTER**) i **CONFIRM**. W poczekalni widzi **swój kod** i wysyła go znajomym.
3. **Każdy znajomy** wybiera **Survival → NEW** (albo RESUME lub LOAD), potem **CO-OP**, MODE **JOIN**, wpisuje
   **kod** hosta, a na koniec **CONFIRM** i **READY**.
4. **Host** wciska **LET IN** przy każdym znajomym, a gdy wszyscy są READY, **START**. Wszyscy ładują się razem do
   świata hosta.

**Tryb i mapę wybiera tylko host.** U znajomego, który zaczyna przez NEW, ekrany wyboru trybu (Pielgrzym,
Prześladowca…) i mapy **przeklikują się same**: gra i tak toczy się w trybie hosta, a przy pierwszym wejściu znajomy
pojawia się obok hosta. Jeśli któryś ekran nie zareaguje w ciągu kilku sekund, zostaje na ekranie i można wybrać
ręcznie (w `coop.log` jest wtedy linia `joining: ... it's yours to choose`).

Do gry, która już trwa, też da się dołączyć. Znajomy robi krok 3, a host dostaje komunikat w grze: **F9** wpuszcza,
**F10** odrzuca.

Znajomi widzą Twoją **nazwę ze Steama**. Kod hosta się nie zmienia, więc grupa może go używać za każdym razem.

## Ustawienia

Ekran ustawień co-opa obejmuje to, czego zwykle potrzeba: hostowanie lub dołączanie, postać, kod oraz przełączniki
NAMES, RADAR i AUTO MAP. Kilka dodatkowych opcji jest w `TLDCoop\coop.ini` w folderze gry. Ten plik tworzy się przy
pierwszym uruchomieniu i ma komentarz przy każdej linii. Po zmianie trzeba zrestartować grę.

| Ustawienie | Co robi |
|---|---|
| `dlc_check=1` | **Nowe.** Gracz musi mieć wszystkie DLC hosta. `0` wpuszcza każdego (na własne ryzyko). |
| `ask_to_join=1` | Każdy znajomy prosi o wejście, a host go wpuszcza. `0` wpuszcza od razu każdego z kodem. |
| `body=` | Twoja postać (ustawia ją ekran co-opa): mackenzie, mackenzie_indoor, astrid, astrid_outdoor albo molly — tylko te, na których dobrze leżą ubrania. |
| `voice=male` / `female` | Głos Twojej postaci. |
| `[colors] Nick=kolor` | **Nowe.** Kolor znajomego na radarze, mapie i nad głową: `red`, `orange`, `yellow`, `green`, `teal`, `blue`, `purple`, `pink`, `white`, `grey` albo `#RRGGBB`. |
| `[emotes] wave=F5 ...` | **Nowe.** Klawisze gestów: `wave`, `point`, `come`, `stop`, `yes`, `no` (F1..F12, A..Z, 0..9, NUM0..NUM9, `off`). |
| `[hud] names / radar / chart` | Nazwy nad głowami, radar i automatyczne rysowanie mapy (`1` włączone, `0` wyłączone). |
| `[struggle] picker_scale=0.5` | Jak długo widać wybór broni, gdy rzuci się na Ciebie zwierzę (`1` jak w grze). |
| `transport=local` | Gra w sieci lokalnej po IP zamiast przez kody Steam (z `bind_ip`, `host_ip`, `port`). |

## Inne mody (MelonLoader)

TLDCoop nie potrzebuje MelonLoadera i nie przeszkadza mu. Nie zmienia plików gry, działa tylko w pamięci, a w
trybie solo nic nie robi. Gra wczytuje go jako `winhttp.dll`, więc MelonLoader musi mieć **inną nazwę**:

| Plik MelonLoadera w folderze gry | Co zrobić |
|---|---|
| `version.dll` | Nic, jeśli Twoje mody się ładują. Jeśli nie (zdarza się po nowszych aktualizacjach Windows 11), zmień nazwę na `winmm.dll`. |
| `winhttp.dll` | **Zmień nazwę na `winmm.dll`**, potem zainstaluj TLDCoop. |
| `winmm.dll` | Nic. |

Jak rozpoznać, czyj jest `winhttp.dll`? Kliknij prawym → **Właściwości** → **Szczegóły**. Plik MelonLoadera ma
nazwę **MelonLoader.Bootstrap** (ok. 10 MB), a plik TLDCoop nie ma nazwy produktu (ok. 2 MB).

**Pamiętaj:** wszyscy w grupie powinni mieć te same mody zmieniające rozgrywkę, np. dodające przedmioty albo
zmieniające loot. Mody graficzne i od interfejsu mogą się różnić.

## Wyłączanie i odinstalowanie

- **Odinstalowanie:** przy zamkniętej grze usuń `winhttp.dll` i folder `TLDCoop` z folderu gry. Jeśli chcesz
  zachować ustawienia, zachowaj `TLDCoop\coop.ini`.
- **Wyłączenie bez usuwania:** utwórz w folderze `TLDCoop` pusty plik o nazwie `disabled` (bez rozszerzenia). Mod
  wtedy nic nie robi. Usuń ten plik, żeby go włączyć z powrotem.

Twoje zapisy są w `%LOCALAPPDATA%\Hinterland\TheLongDark`. Odinstalowanie moda ich nie rusza.

## Czy to bezpieczne?

- **Cały kod jest w tym repozytorium**, a `winhttp.dll` buduje automatycznie GitHub Actions
  (`.github/workflows/build.yml`).
- **Sprawdź pobrany plik.** Przy każdym buildzie GitHub wypisuje sumę SHA-256 każdego pliku. W PowerShellu
  `Get-FileHash winhttp.dll` powinno pokazać tę samą wartość.
- **Dlaczego `winhttp.dll`?** Gra szuka tej standardowej biblioteki Windows najpierw we własnym folderze. Dzięki temu
  mod ładuje się bez instalatora. Wszystkie prawdziwe wywołania przekazuje do oryginalnego `winhttp.dll` z
  `C:\Windows\System32`.
- **Antywirus** może oznaczyć plik jako podejrzany, bo mod wpina się w kod gry. To fałszywy alarm.
- Mod czyta i zapisuje tylko swój folder `TLDCoop\` (ustawienia, dziennik) oraz zwykłe zapisy gry. Ze Steamem łączy
  się tylko po wybraniu CO-OP.

## Rozwiązywanie problemów

- **Nie ma ekranu SOLO OR CO-OP?** Sprawdź, czy `winhttp.dll` leży bezpośrednio obok `tld.exe` i czy w folderze
  `TLDCoop` nie ma pliku `disabled`. Potem zajrzyj do `TLDCoop\coop.log`.
- **MISSING DLC:** brakuje Ci DLC, które ma host. Zainstaluj je na Steamie albo poproś hosta o `dlc_check=0`.
- **„mod protocol version differs”:** ktoś ma inną wersję moda, np. oryginalny TLDCoop. Wszyscy muszą mieć ten sam
  zip.
- **„game version differs”:** ktoś ma inną aktualizację gry. Niech Steam zaktualizuje grę u wszystkich.
- **Gra zamyka się w menu głównym przy modach MelonLoadera:** masz `winhttp.dll` ze starego instalatora
  `TLDCoop-Setup.exe` (z 18.10). Podmień go na ten z zipa.
- **Mody MelonLoadera przestały się ładować po instalacji:** MelonLoader miał nazwę `winhttp.dll`. Zainstaluj go
  ponownie i zmień nazwę jego pliku na `winmm.dll`.
- **Znajomi wyglądają jak kolorowe kapsuły:** u osoby, która widzi kapsuły, brakuje
  `TLDCoop\tldcoop_bodies.bundle`. Skopiuj go z zipa.
- **Zgłaszanie błędu:** dołącz `TLDCoop\coop.log` od **każdego** gracza. Przy starcie gry poprzedni log
  zostaje jako `coop-prev.log` (a jeszcze wcześniejszy jako `coop-prev2.log`), więc po crashu i ponownym
  uruchomieniu log z crashem to `coop-prev.log`. Linie `[FAULT]` pokazują, w którym miejscu gra się wywaliła.

## Czego jeszcze nie ma

- **Pociąg z Iron Road** (`TLD.SteamTrain.*`) nie jest jeszcze synchronizowany między graczami.
- **Unikalne przedmioty z Iron Road** (np. woskowe cylindry) nie są jeszcze na liście unikalnych. Może się zdarzyć,
  że tylko jeden gracz podniesie dany przedmiot.
- **Gesty** są na razie liczone w kodzie (ruch ręki i głowy). Ładne, gotowe animacje wymagają zbudowania ich w Unity.

## Budowanie samemu

Najprościej: każde wypchnięcie zmian do repozytorium uruchamia build w GitHub Actions. Gotowy zip jest w
**Artifacts**.

Lokalnie potrzebujesz Windowsa,
[Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/) z „Desktop development with C++”,
[CMake](https://cmake.org/) i [Ninja](https://ninja-build.org/):

```
git clone --recursive https://github.com/cgpt28641-ops/TLDCoop_DLC.git
cd TLDCoop_DLC
build.cmd
```

Powstanie `build\winhttp.dll`. Skopiuj go obok `tld.exe`, a pliki `.bundle` z zipa do `TLDCoop\`. Pliki
`build\*_test.exe` to testy działające bez gry, w tym `content_test.exe` do obsługi DLC.

| Folder / plik | Zawartość |
|---|---|
| `src/` | Mod. `dllmain.cpp` go ładuje, a `src/net/` to protokół i połączenie (bez kodu gry, z testami). |
| `src/net/content.h` | **Nowe.** Porównywanie list DLC graczy. |
| `src/optional_content.cpp` | **Nowe.** Odczyt zainstalowanych DLC z gry. |
| `tests/` | Testy bez gry, w tym `content_test.cpp`. |
| `bodypack/` | Jak budowane są postacie (Unity 6000.0.60f1) i animacje. |
| `third_party/minhook` | [MinHook](https://github.com/TsudaKageyu/minhook) do przechwytywania funkcji. |
| `tools/` | Narzędzia pomocnicze. |

## Autorzy

- Mod **TLDCoop**: **ScarredVeins**, zbudowany z pomocą agentów AI (Claude Code).
  [Oryginalne repozytorium](https://github.com/ScarredVeins/TLDCo-Op).
- Poprawki pod 2.58: **[afkfatih](https://github.com/afkfatih)**.
- Obsługa DLC (TLDCoop_DLC): **cgpt28641-ops** z Claude.
- [MinHook](https://github.com/TsudaKageyu/minhook): Tsuda Kageyu (licencja BSD 2-Clause).
- **The Long Dark** i jego postacie należą do **Hinterland Studio**. To nieoficjalny mod fanowski, niewykonany ani
  niepopierany przez Hinterland. Do gry potrzebna jest własna kopia gry.
