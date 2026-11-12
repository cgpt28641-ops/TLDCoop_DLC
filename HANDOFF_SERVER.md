# HANDOFF: TLDCoop_DLC dedicated server (separate chat, from 12.11)

Read this first, then `SERVER_PLAN.md` (same project, also in the repo root).

## Project in short
- TLDCoop_DLC: a C++ IL2CPP co-op mod for The Long Dark, everything in ONE `winhttp.dll` (proxy DLL in the game
  folder). Repo **cgpt28641-ops/polar-link** (old name tldcoop_dlc redirects), cloned at `/home/claude/tldcoop_dlc`.
- CI `.github/workflows/build.yml` (windows-latest, MSVC, Ninja) builds it and runs `tests/*_test.exe`; every push to
  **main** updates the release **dll-latest**:
  https://github.com/cgpt28641-ops/polar-link/releases/download/dll-latest/winhttp.dll
  Pushes to other branches build and test only (no release): use a branch for WIP.
- Players: Karol (= "Kerel" on Discord), Shatsca (a guy), Szymon (szymongrzyb2017). The main changelog for the mod
  is `HANDOFF.md`; this file is only for the server.

## Goal
A dedicated server **without the game** (like SkyCoop Reborn's), on their own empty Windows PC, 24/7. Everyone is a
client (no host). The current world is kept. Joining by the code as now, **no second Steam account**.

## Decisions (Karol 12.11)
- Windows 10/11 on their PC (the network code is Windows-only now: `net/session.cpp`, `udp_transport.cpp`,
  `steam_transport.cpp` use windows.h). The same PC runs the Discord bot.
- Keep the world they play now (import, see SERVER_PLAN).
- Joining by code without a second Steam account: try Steam's **anonymous game server** first (SERVER_PLAN
  "Decided"); fallback UDP IP:port over Tailscale.
- The old host mode stays (playing without the server).
- Karol 12.11 10:44: "do it all today"; until he says go, prepare only — **no changes to the mod's code**.

## Where things are
- `src/net/` = library **tldnet** (CMake `add_library(tldnet ...)`): protocol (`protocol.h`, message list `enum class
  Msg`, protocol version `kProtocolVersion` = 51 now), session (handshake, version check, states 20 Hz, reliable
  messages, chunks, timeouts), transports (UDP, Steam lobby + P2P through the game's steam_api64.dll flat API),
  game-independent logic with tests: `claims.h` (who took an item), `container_locks.h`, `wildlife_own.h` (animal
  owner decision), `sleep_vote.h`, `harvests.h`, `handover.h`, `uniques.h`, `content.h` (DLC check).
- `src/bot/main.cpp` = **tldbot**, a standalone exe that already speaks the protocol as host or client: the server
  starts from it.
- `src/world_keep.*`: what the host keeps for scenes it isn't in, written as protocol messages to
  `<instance>/worlds/<save_id>.bin` (tags: ItemChange, ItemList, ItemRecord, ObjectChange, ObjectGone, FireChange,
  ContainerTheirs/Ours, WaterChange, Wildlife, WildRegions). The server's store = this, for all scenes.
- Host-side logic to move to the server: `world_items.cpp` (lists, pending, claims, g_scene_items / records),
  `world_containers.cpp` (store, locks), `world_objects.cpp` (g_store, g_gone, send_away_changes), `world_fires.cpp`,
  `world_water.cpp`, `coop_sleep.cpp` (votes, decide), `wildlife_owner.inc` (registry, host_arbitrate), `coop_train.cpp`
  (boat box, train), `coop_death.cpp` (bodies), clock/weather (`World` msg from the host).
- TLD type dump (fields, methods, RVAs): `/mnt/user-data/uploads/TLDCoop/tld_types.txt` (if the new chat has it).

## Working rules (Karol's, always)
- Talk to Karol in Polish, short and simple.
- After every build: the dll link; an entry in HANDOFF (this one for the server); post on Discord #updates what was
  wrong / what is now / what is untested; answer whoever asked.
- Read logs very carefully (the "REPORT #...: what happened" lines first, then the whole run; the build stamp is the
  first line of coop.log).
- Never type passwords. Hinterland's assets are never committed. Everything in the mod stays in one winhttp.dll.
- Discord bridge: repo `cgpt28641-ops/tldcoop-discord` (public; messages only encrypted in `vault/`, key in the
  original session's container `/home/claude/.secrets/tldcoop_vault.key` — a new chat doesn't have it: ask Karol
  before relying on reading Discord there).

## Status
- 12.11: plan written (`SERVER_PLAN.md`), nothing built yet. Stage 0 next: server skeleton from tldbot + prove the
  anonymous Steam game server (code join) on their PC.
- Waiting for: the PC with Windows installed (CPU / RAM / GPU), Karol's go.
