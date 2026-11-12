#pragma once
// TLDCoop_DLC: playing on a dedicated server (tldserver: the polar-link-server repo). The server sits in the host's
// seat (player 0) but has no game: no clock, weather, experience mode or feature switches of its own. It says so
// (Msg::Server, about every 2 s) and what it wants of our game:
//   world source   - our clock and weather are the world's now: world_sync publishes them (to the server, which gives
//                    them to the others as a host would). One game at a time, the server picks; a game that comes in
//                    follows the world for a few seconds first, then may be picked.
//   wants mode     - nobody gave it an experience mode and seed yet: ours become the world's (world_mode).
//   wants features - likewise the feature switches (coop_features).
// With a host that is a game none of this is ever set. Main thread only.

#include "net/protocol.h"

namespace coop_server {

void on_frame();           // each frame, before the world modules
bool linked();             // our host is a dedicated server
bool world_source();       // ...and our game's clock and weather are the world's
bool wants_mode();
bool wants_features();
void publish_world(const net::WorldMsg& world);  // world source: our world, to the server (as one datagram)

}  // namespace coop_server
