# TLDCoop_DLC: a dedicated server without the game (plan, 12.11)

Prepared 12.11 morning, before the work starts. No mod code changed yet.

## Goal

A separate program (`tldserver`) holds the shared world. It runs 24/7 on a cheap VPS, needs no TLD, no GPU and no
Steam. Every game, the old host included, is a client. Anyone joins or leaves whenever they like.

## What we already have that the server can be built from

- **`src/net/` (tldnet library)** is independent of the game: protocol (`protocol.h`, ~60 message types), session
  (handshake, version, 20 Hz states, reliable messages, chunks, timeouts), `udp_transport.cpp` (IP:port),
  `claims.h` (who took an item first, with a coin toss), `container_locks.h`, `wildlife_own.h` (who runs an animal),
  `sleep_vote.h`, `harvests.h`, `handover.h`. All of it is already tested offline (`tests/*_test.cpp`).
- **`src/bot/main.cpp` (tldbot)** is a standalone program that already speaks the protocol as a host or a client.
  It is the skeleton for the server.
- **`world_keep`** already writes the host's world for the scenes it isn't in, as the protocol messages themselves
  (`ItemChange`, `ItemList`, `ItemRecord`, `ObjectChange`, `ObjectGone`, `FireChange`, `ContainerTheirs/Ours`,
  `WaterChange`, `Wildlife`, `WildRegions`) to `worlds/<save>.bin`. The server's store is that, for **every** scene,
  always.

## What the host's game decides today (and who decides it with a server)

| Today the host's game... | With the server |
|---|---|
| holds the loose items of every scene (records = TLD's own save text) | **server** stores the records per scene; never parses TLD text (it's opaque bytes, as world_keep already treats it) |
| decides who took an item first (claims) | **server** (claims.h as is) |
| holds containers' contents, locks | **server** (container store + container_locks.h) |
| fires, water, plants / broken down / doors / locks / placements, bodies, snow shelters | **server** stores the last state per object id, as today's ObjectPut / FirePut / WaterPut |
| the first game in a scene: TLD rolls the random spawns + loot | **the first client in a fresh scene** rolls (TLD does it), sends its list; the server keeps it and it's the truth from then on (today: the host's list) |
| clock, weather, wind | **server** runs the clock (day length from the save / settings); weather: the first client's TLD weather is adopted and stepped by one client chosen as "weather owner" (simplest), later the server's own |
| sleep votes / time skip | **server** (sleep_vote.h); it moves its clock |
| animals: registry + arbiter (host_arbitrate), spawning per scene by one game | **server** = registry + arbiter (wildlife_own.h decide); spawning stays in the game that controls the scene (already per scene under ownership) |
| train, track switches, boat box, house lights, automat | **server** stores the last state, a driver/owner client runs it (as now) |
| player saves (inventory, needs, skills) | each player's own TLD save as now; the server only stores where they are |

## The big change in the clients

Today `hosting()` is true in exactly one game and lots of code checks it. With a server:
- a new role **Client-of-server** for everyone; the host-only branches move to the server (the store/arbiter part)
  or to "the client that controls this scene" (the TLD-dependent part: rolling spawns, running animals, the clock's
  weather);
- `world_keep` on the server, not in a game;
- the world's TLD save (`.sav` of the host) stops being the truth for shared things: the server's store is.
  Each client keeps its own save for its own character.

## Stages (each one ships and gets tested)

0. **Server skeleton** (from tldbot): `tldserver --port 27015 --world worlds/<name>.bin`; accepts up to 4 clients
   over UDP, passes on what a host passes on today (states, chat, profiles). Console commands: save, players, kick,
   stop. Windows .exe first (the session uses windows.h), Linux build after.
1. **World store**: every world message a host keeps today goes to the server and is saved there for every scene;
   on arrival in a scene the server sends its lists (what the host does in send_list / send_away_*).
2. **Items, containers, claims**: the server answers takes (claims), locks containers, sends contents on open.
3. **Objects, fires, water, plants, doors, bodies**: last state per id from the server.
4. **Clock, weather, sleep**: the server's clock; sleep votes on the server.
5. **Animals**: registry + arbiter on the server; spawning by the scene's controller client.
6. **Iron Road** (train, switches, boat box), house lights, automat.
7. **No host**: the menu's CO-OP has "connect to server (IP:port)"; the old host mode stays for playing without
   a server.

## Things to decide with Karol

- The world to start from: an existing host save (import its world_keep `.bin` + a scan of every scene's items the
  first time someone enters) or a fresh world.
- Where it runs: a ~5 €/month Linux VPS (after the Linux build) or a Windows box first.
- Joining: IP:port (the VPS needs UDP 27015 open). Steam join codes need Steam on the server, so not at first.

## Risks

- Everything shared is re-wired: for a while more bugs than now. Each stage is tested before the next.
- Random spawns / loot rolled by "the first client" differ from what a host's save had; one import at the start.
- Protocol version bump at each stage: everyone updates together.
