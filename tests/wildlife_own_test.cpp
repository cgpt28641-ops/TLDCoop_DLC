// TLDCoop_DLC (v44): wildlife ownership's decisions (net/wildlife_own.h): who runs an animal, the hysteresis, the
// rate limit and the puppets' prediction. Exit code 0 when everything holds.

#include <cmath>
#include <cstdio>
#include <vector>

#include "net/wildlife_own.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

using namespace net::wild;

Seat seat(uint8_t player, float metres, bool looking = false, double rtt = 0) {
    Seat s;
    s.player = player;
    s.metres = metres;
    s.looking = looking;
    s.rtt_ms = rtt;
    return s;
}

}  // namespace

int main() {
    // Closer players win, after the hold, and only by the margin.
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeWander;
        const std::vector<Seat> seats = {seat(0, 100), seat(1, 30)};
        Why why;
        check(decide(t, f, seats, 0, why) == kNobody, "a better player waits for the hold");
        check(decide(t, f, seats, kHoldMs - 1, why) == kNobody, "...still waiting just before it");
        check(decide(t, f, seats, kHoldMs + 1, why) == 1 && why == Why::Closer, "...then takes over (closer)");
        f.owner = 1;
        const std::vector<Seat> back = {seat(0, 10), seat(1, 60)};
        check(decide(t, f, back, kHoldMs + 2000, why) == kNobody, "no switch back within the minimum time");
        check(decide(t, f, back, kHoldMs + 1 + kMinSwitchMs + kHoldMs, why) == 0, "switches back later");
    }
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeWander;
        const std::vector<Seat> seats = {seat(0, 40), seat(1, 35)};
        Why why;
        decide(t, f, seats, 0, why);
        check(decide(t, f, seats, 10000, why) == kNobody, "a slightly closer player doesn't take over (margin)");
    }
    // 08.11: three players together: one looking at the wolf doesn't take it from an owner as near (looking bonus <
    // margin), however long.
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeWander;
        const std::vector<Seat> seats = {seat(0, 30), seat(1, 28, true), seat(2, 32)};
        Why why;
        decide(t, f, seats, 0, why);
        check(decide(t, f, seats, 60000, why) == kNobody, "a friend standing by and looking doesn't take it");
    }
    // Engagement: an animal after a player goes to them quickly, whoever is closer.
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeStalking, f.target = 2;
        const std::vector<Seat> seats = {seat(0, 20), seat(2, 60)};
        Why why;
        decide(t, f, seats, 0, why);
        check(decide(t, f, seats, kEngagedHoldMs + 1, why) == 2 && why == Why::Engaged, "the hunted player's game takes it");
    }
    // v45: soon after a switch, still to the one it hunts (not the full kMinSwitchMs).
    {
        Track t;
        t.last_switch_ms = 0;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeAttack, f.target = 3;
        const std::vector<Seat> seats = {seat(0, 10), seat(3, 30)};
        Why why;
        decide(t, f, seats, 100, why);
        check(decide(t, f, seats, kEngagedMinSwitchMs + 1, why) == 3, "the hunted player gets it 1.5 s after a switch");
    }
    // Never during a fight, never for the dead (while the owner is there).
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = true, f.mode = kModeStruggle, f.target = 1;
        const std::vector<Seat> seats = {seat(0, 200), seat(1, 1)};
        Why why;
        decide(t, f, seats, 0, why);
        check(decide(t, f, seats, 60000, why) == kNobody, "no handover in a fight");
        f.mode = kModeDead, f.dead = true;
        check(decide(t, f, seats, 120000, why) == kNobody, "no handover of a dead animal");
        f.pending = true, f.dead = false, f.mode = kModeWander;
        check(decide(t, f, seats, 180000, why) == kNobody, "no decision while a handover is under way");
    }
    // The owner left: the best one at once.
    {
        Track t;
        Facts f;
        f.owner = 0, f.owner_present = false, f.mode = kModeWander;
        const std::vector<Seat> seats = {seat(1, 300), seat(2, 50)};
        Why why;
        check(decide(t, f, seats, 0, why) == 2 && why == Why::OwnerGone, "an owner who left is replaced at once");
        check(decide(t, f, {}, 0, why) == kNobody, "nobody there: nobody takes it (frozen)");
    }
    // Looking and the link count.
    {
        check(score(seat(1, 50, true), kNobody, 0) < score(seat(2, 60, false), kNobody, 0), "looking at it counts");
        check(score(seat(1, 50, false, 400), kNobody, 0) > score(seat(2, 50, false, 20), kNobody, 0), "a slow link counts");
        check(looking_at(0, 0, 10, 10) && !looking_at(0, 0, -10, 10) && !looking_at(0, 0, 200, 200),
              "looking: in front, not behind, not too far");
        check(looking_at(90, 10, 0, 10) && !looking_at(90, 0, 10, 10), "looking: facing +x");
    }
    // The rate limit.
    {
        RateLimit r(4, 4);
        int n = 0;
        for (int i = 0; i < 20; ++i) n += r.take(0) ? 1 : 0;
        check(n == 4, "a burst of 4 at once");
        check(!r.take(100) && r.take(260), "then 4 a second");
    }
    // Prediction: a steady walk is drawn ahead of the last record, a jump snaps.
    {
        Predictor p;
        Predictor::Pose pose;
        for (int i = 0; i <= 5; ++i) {
            pose.x = static_cast<float>(i) * 0.2f;  // 2 m/s along x
            p.add(pose, i * 100.0, i * 100.0 + 50);
        }
        Predictor::Pose out;
        bool ok = p.sample(600 + 50, out);  // 100 ms after the last record (on its clock), less the jitter allowance
        check(ok && out.x > 1.0f && out.x < 1.3f, "drawn ahead of the last record at its speed");
        check(std::fabs(p.speed() - 2.0f) < 0.01f, "its speed");
        for (int i = 0; i < 30; ++i) p.sample(650 + i * 16.0, out);
        check(out.x <= 1.0f + 2.0f * 0.4f + 0.01f, "never more than 400 ms ahead");
        pose.x = 50;
        p.add(pose, 700, 750);
        p.sample(760, out);
        check(std::fabs(out.x - 50) < 0.5f, "a jump is drawn at once");
        Predictor::Pose late = pose;
        late.x = 0;
        p.add(late, 650, 800);  // older than what it has: ignored
        p.sample(820, out);
        check(out.x > 40, "a late record is ignored");
    }
    printf(failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
