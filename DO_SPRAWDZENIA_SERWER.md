# Serwer dedykowany: co trzeba sprawdzić w grze

Stan na 14.11.2026. Wszystko poniżej przeszło testy automatyczne, ale **nie było jeszcze sprawdzone w prawdziwej
grze** (oprócz tego, co jest oznaczone jako sprawdzone). Bot Discorda jest osobno i go tu nie ma.

Zaznaczaj `[x]`, co działa. Przy błędzie zapisz godzinę i co się stało.

## Przed testem

- [ ] Serwer: najnowszy `tldserver.exe` z
      https://github.com/cgpt28641-ops/polar-link-server/releases/download/server-latest/tldserver.exe
- [ ] Każdy gracz: `winhttp.dll` z wersji **dll-server** (część poprawek nie jest jeszcze w głównej wersji):
      https://github.com/cgpt28641-ops/polar-link/releases/download/dll-server/winhttp.dll
- [ ] Świeży świat: host bierze plik ze swojego `TheLongDark\TLDCoop\worlds\` (ten z nazwą swojego zapisu, `.bin`),
      serwer zatrzymany (`stop`), plik wrzucony do `C:\tldserver` jako `world.bin`.
- [ ] Pierwszy na serwer wchodzi **host świata**, dopiero potem reszta.
- [ ] Po grze: `tldserver.log` z serwera i paczka F11 od każdego gracza.

## Już sprawdzone (13.11, host sam)

- [x] Wejście przez Tailscale (adres 100.x)
- [x] Tryb gry, ziarno i ustawienia modów brane od pierwszego gracza
- [x] Zegar i pogoda idą z gry gracza
- [x] Wspólne przeczekanie czasu (jedna osoba)
- [x] Nowe miejsca: gra oddaje serwerowi listę przedmiotów, przy powrocie lista się zgadza
- [x] Szafka w pociągu: serwer oddaje zawartość
- [x] Wyjście z co-op: serwer od razu widzi, że gracz wyszedł

## Do sprawdzenia: menu i łączenie

- [ ] W menu CO-OP → DOŁĄCZ → wiersz KOD → **CTRL+V** z adresem serwera (`100.x.x.x`) → wiersz pokazuje
      „SERWER 100.x.x.x" → ZATWIERDŹ łączy
- [ ] **BACKSPACE** na tym wierszu wraca do kodów Steam
- [ ] Zwykła gra z hostem (bez serwera) dalej działa jak wcześniej

## Do sprawdzenia: zwierzęta (najważniejsze)

- [ ] **Nie dublują się po powrocie do regionu**: wejść do regionu, wyjść, wrócić (pieszo, pociągiem i komendą
      teleportu). Ma być tyle samo wilków / królików co przed wyjściem, nie dwa razy więcej.
- [ ] Jeden gracz na serwerze: liczba zwierząt jak w grze solo, nie o połowę więcej
- [ ] Dwóch graczy w jednym regionie: obaj widzą te same zwierzęta w tych samych miejscach
- [ ] Zwierzę przechodzi od jednego gracza do drugiego (gdy podejdzie bliżej drugiego) bez znikania i skakania
- [ ] Strzał w zwierzę, które „prowadzi" drugi gracz, trafia i rani
- [ ] Wilk atakuje drugiego gracza (nie tylko tego, kto go prowadzi)
- [ ] Zabite zwierzę leży u obu graczy w tym samym miejscu
- [ ] **Oprawianie**: jeden zabija, **obaj** próbują oprawić. Zapisać, co widzi każdy z nich (czy mięso ubywa u
      drugiego, czy można oprawić dwa razy). To jest jeszcze nieprzerobione, potrzebne logi.
- [ ] Po wyjściu wszystkich i powrocie po kilku godzinach w grze zwierzęta są przesunięte, ranne wykrwawione

## Do sprawdzenia: przedmioty i szafki

- [ ] Przedmiot położony przez jednego gracza widzi drugi
- [ ] Przedmiot podniesiony przez jednego znika u drugiego
- [ ] Dwóch graczy łapie ten sam przedmiot naraz: dostaje go tylko jeden
- [ ] Szafkę otwartą przez jednego drugi widzi jako zajętą
- [ ] Zawartość szafki po przeszukaniu jest taka sama u obu
- [ ] **Więcej łupu przy dwóch graczach** (szafki i przedmioty w budynkach przeszukiwane pierwszy raz)
- [ ] Przekazanie przedmiotu drugiemu graczowi
- [ ] Po restarcie serwera wszystko leży tam, gdzie zostało

## Do sprawdzenia: sen i czas

- [ ] Dwóch graczy kładzie się spać: sen startuje dopiero, gdy obaj leżą
- [ ] Wygrywa głos większości, przy remisie krótszy
- [ ] Gdy jeden gracz jest w menu albo się ładuje, sen drugiego **nie** jest przerywany
- [ ] Wilk budzi jednego: budzą się obaj
- [ ] **„Do świtu" i „do zmierzchu"** kończą się o tej godzinie, którą pokazuje gra
- [ ] Odpoczynek (REST) nie przewija czasu
- [ ] Gdy wychodzi gracz, którego gra prowadzi zegar, czas przejmuje gra drugiego gracza (bez skoków)
- [ ] Gdy nikogo nie ma, czas na serwerze stoi

## Do sprawdzenia: Iron Road

- [ ] Pociąg: jeden prowadzi, drugi jedzie: obaj widzą ten sam pociąg
- [ ] Pociąg zostawiony na torach: gracz, który wejdzie później do regionu, widzi go w tym samym miejscu
- [ ] Stan części pociągu taki sam u obu
- [ ] Zwrotnice i obrotnice: przestawione przez jednego są przestawione u drugiego, także po restarcie serwera
- [ ] Skrzynka łodzi: to, co włożył jeden, widzi drugi, także po restarcie serwera
- [ ] Szafka w pociągu wspólna dla obu

## Do sprawdzenia: reszta

- [ ] Ogniska, woda w garnkach: wspólne
- [ ] Krew, ślady sprayu: drugi gracz je widzi
- [ ] Włącznik świateł w domach: drugi gracz widzi zmianę, także gdy wejdzie później
- [ ] Czat w grze dochodzi do pozostałych
- [ ] **F11 (raport)** i **POBIERZ LOGI** działają między graczami na serwerze
- [ ] Serwer zabity z zewnątrz (np. przez antywirus) sam wstaje
- [ ] Po każdym starcie serwera jest nowa kopia świata w `C:\tldserver\backups`

## Jeszcze nie zrobione (nie sprawdzać)

- Automat do gier na serwerze
- 4. gracz
- Wspólne szafki (kilku graczy w jednej szafce naraz)
- Oprawianie tuszy między graczami (najpierw logi z testu wyżej)
