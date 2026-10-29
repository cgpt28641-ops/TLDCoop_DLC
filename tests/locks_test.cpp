// TLDCoop_DLC: one player at a time in a container (net/container_locks.h, net::ContainerLockMsg). Exit code 0 when
// all pass.

#include <cstdio>
#include <string>

#include "net/container_locks.h"
#include "net/protocol.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

}  // namespace

int main() {
    using net::kNoOwner;
    const std::string cupboard = net::lock_key("MountainTownRegion", "cupboard-1");
    const std::string locker = net::lock_key("MountainTownRegion", "locker-2");
    const std::string elsewhere = net::lock_key("CoastalRegion", "cupboard-1");

    net::LockTable t;
    expect(t.owner(cupboard) == kNoOwner, "a container nobody asked for is free");
    expect(t.take(cupboard, 1, 0) == 1, "the first to ask gets it");
    expect(t.take(cupboard, 0, 10) == 1, "the host asking a moment later doesn't: it's the friend's");
    expect(t.take(cupboard, 2, 20) == 1, "nor does another friend");
    expect(t.take(cupboard, 1, 30) == 1, "the owner asking again keeps it (a refresh)");
    expect(t.owner(elsewhere) == kNoOwner, "the same id in another scene is another container");
    expect(t.take(locker, 0, 40) == 0, "the host takes a free locker");

    expect(!t.release(cupboard, 2), "someone else can't let it go");
    expect(t.owner(cupboard) == 1, "it's still the friend's");
    expect(t.release(cupboard, 1), "the owner lets it go");
    expect(t.owner(cupboard) == kNoOwner && t.take(cupboard, 2, 50) == 2, "then it's free for the next one");

    // Gone quiet: friend 2 refreshed at 50; the host's own locker never times out.
    auto freed = t.expire(50 + net::kLockStaleMs - 1, net::kLockStaleMs, 0);
    expect(freed.empty(), "still fresh: nothing freed");
    freed = t.expire(50 + net::kLockStaleMs + 1, net::kLockStaleMs, 0);
    expect(freed.size() == 1 && freed[0] == cupboard && t.owner(cupboard) == kNoOwner,
           "an owner who stopped refreshing loses it");
    expect(t.owner(locker) == 0, "the host's own isn't timed out");

    t.take(cupboard, 3, 100);
    t.take(elsewhere, 3, 100);
    freed = t.drop_player(3);
    expect(freed.size() == 2 && t.owner(cupboard) == kNoOwner && t.owner(elsewhere) == kNoOwner,
           "a friend who left frees everything they held");

    net::LockTable client;
    client.set(cupboard, 2, 0);
    expect(client.owner(cupboard) == 2, "a client keeps what the host says");
    client.set(cupboard, kNoOwner, 0);
    expect(client.owner(cupboard) == kNoOwner, "and frees it when the host says so");

    {
        net::ContainerLockMsg m;
        m.scene = "MountainTownRegion";
        m.id = "cupboard-1";
        m.op = net::LockOp::State;
        m.owner = 2;
        net::Writer w(net::Msg::ContainerLock);
        net::write_container_lock(w, m);
        net::Packet p;
        p.type = net::Msg::ContainerLock;
        p.data.assign(w.data(), w.data() + w.size());
        net::Reader r = net::packet_body(p);
        net::ContainerLockMsg got;
        expect(w.ok() && net::read_container_lock(r, got) && got.scene == m.scene && got.id == m.id &&
                   got.op == m.op && got.owner == m.owner,
               "ContainerLock reads back the same");
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
