// TLDCoop_DLC: players' blood (net::BloodMarkMsg) reads back as it was sent. Exit code 0 when all pass.

#include <cmath>
#include <cstdio>

#include "net/protocol.h"

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
bool round_trip(const net::BloodMarkMsg& m, net::BloodMarkMsg& got) {
    net::Writer w(net::Msg::BloodMark);
    net::write_blood_mark(w, m);
    net::Packet p;
    p.type = net::Msg::BloodMark;
    p.data.assign(w.data(), w.data() + w.size());
    net::Reader r = net::packet_body(p);
    return w.ok() && net::read_blood_mark(r, got);
}
}  // namespace

int main() {
    net::BloodMarkMsg m;
    m.scene = "MountainTownRegion";
    m.kind = net::BloodKind::Stain;
    m.x = 12.5f, m.y = 300.25f, m.z = -40;
    m.a = 0.6f, m.b = 1.1f;
    net::BloodMarkMsg got;
    expect(round_trip(m, got) && got.scene == m.scene && got.kind == m.kind && got.x == m.x && got.y == m.y &&
               got.z == m.z && got.a == m.a && got.b == m.b,
           "a stain reads back the same");
    m.kind = net::BloodKind::Cluster;
    m.count = 5;
    expect(round_trip(m, got) && got.kind == net::BloodKind::Cluster && got.count == 5, "a cluster keeps its drops");
    m.count = 1000;
    expect(!round_trip(m, got), "an absurd cluster is refused");
    m.count = 0;
    m.x = NAN;
    expect(!round_trip(m, got), "a spot that isn't a number is refused");
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
