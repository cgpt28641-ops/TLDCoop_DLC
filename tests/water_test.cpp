// TLDCoop_DLC: WaterPut and WaterList (world_water) survive the trip: written, read back, the same. Exit code 0 when
// all pass.

#include <cstdio>
#include <string>
#include <vector>

#include "net/protocol.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

net::Packet packet_of(const net::Writer& w, net::Msg type) {
    net::Packet p;
    p.type = type;
    p.data.assign(w.data(), w.data() + w.size());
    return p;
}

}  // namespace

int main() {
    {
        net::WaterPutMsg m;
        m.scene = "MountainTownRegion";
        m.seq = 42;
        m.id = "5c1f2a9e-toilet";
        m.text = "{\"m_CurrentLiters\":1500,\"m_CurrentLiquidQuality\":1,\"m_RolledSpawnChance\":true}";
        net::Writer w(net::Msg::WaterPut, net::kMaxMessage);
        net::write_water_put(w, m);
        expect(w.ok(), "WaterPut is written");
        const net::Packet p = packet_of(w, net::Msg::WaterPut);
        net::Reader r = net::packet_body(p);
        net::WaterPutMsg got;
        expect(net::read_water_put(r, got) && got.scene == m.scene && got.seq == m.seq && got.id == m.id &&
                   got.text == m.text,
               "WaterPut reads back the same");
    }
    {
        net::WaterListMsg m;
        m.scene = "CoastalRegion";
        for (int i = 0; i < 40; ++i)
            m.sources.emplace_back("id-" + std::to_string(i), "{\"m_CurrentLiters\":" + std::to_string(i * 250) + "}");
        m.sources.emplace_back("@12.34,5.00,-6.70", "{}");
        net::Writer w(net::Msg::WaterList, net::kMaxMessage);
        net::write_water_list(w, m);
        expect(w.ok(), "WaterList is written");
        const net::Packet p = packet_of(w, net::Msg::WaterList);
        net::Reader r = net::packet_body(p);
        net::WaterListMsg got;
        expect(net::read_water_list(r, got) && got.scene == m.scene && got.sources == m.sources,
               "WaterList reads back the same, position ids included");
    }
    {
        net::WaterListMsg empty;
        empty.scene = "LakeRegion";
        net::Writer w(net::Msg::WaterList, net::kMaxMessage);
        net::write_water_list(w, empty);
        const net::Packet p = packet_of(w, net::Msg::WaterList);
        net::Reader r = net::packet_body(p);
        net::WaterListMsg got;
        expect(net::read_water_list(r, got) && got.sources.empty(), "an empty WaterList reads back empty");
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
