// TLDCoop_DLC: the DLC check (net/content.h). First the list helpers, then real host + client sessions over UDP on
// 127.0.0.1: a friend lacking one of the host's DLC is refused with MissingContent naming it; one with the same DLC,
// or more, gets in; a host without DLC lets anyone in; dlc_check off lets anyone in; a friend whose list is unknown
// is refused by a host with DLC; a host whose own list is unknown doesn't check. Exit code 0 when all pass.

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "net/content.h"
#include "net/session.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

std::string joined(const std::vector<std::string>& v) { return net::content_join(v); }

void helpers() {
    expect(joined(net::content_split(" b, a,,a ")) == "a,b", "split trims, sorts and drops duplicates and blanks");
    expect(net::content_split("?").empty() && !net::content_known("?") && !net::content_known(" ? "),
           "? is unknown, not a list");
    expect(net::content_known("") && net::content_split("").empty(), "\"\" is a known empty list");
    expect(net::content_join({"z", "a", " a ", "", "x,y"}) == "a,z", "join sorts, trims, drops blanks and commas");
    expect(joined(net::content_missing("a,b,c", "b")) == "a,c", "missing: the host's ids the friend lacks");
    expect(net::content_missing("a,b", "a,b,c").empty(), "missing: a friend with more DLC lacks nothing");
    expect(net::content_missing("", "x").empty(), "missing: a host without DLC needs nothing");
    expect(net::content_missing("?", "").empty() && net::content_missing("a", "?").empty(),
           "missing: nothing to compare when either side is unknown");
    const std::string d = net::content_detail({std::string(40, 'a'), std::string(40, 'b')}, 63);
    expect(d.size() == 63 && d.compare(60, 3, "...") == 0, "detail is cut to fit, ending in ...");
    expect(net::content_detail({"x"}, 63) == "x", "a short detail is kept whole");
}

void pump(net::Session& a, net::Session& b, double ms) {
    const double until = net::now_ms() + ms;
    while (net::now_ms() < until) {
        a.update();
        b.update();
        Sleep(1);
    }
}

struct Outcome {
    bool connected = false;
    bool rejected = false;
    uint8_t reason = 0;
    std::string detail;
};

Outcome try_join(uint16_t port, const std::string& host_content, const std::string& client_content, bool check = true) {
    auto quiet = [](const std::string& line) { printf("    net: %s\n", line.c_str()); };
    net::Config hc, cc;
    hc.role = net::Role::Host;
    cc.role = net::Role::Client;
    hc.port = cc.port = port;
    hc.name = "host";
    cc.name = "friend";
    hc.game_version = cc.game_version = "test";
    hc.content = host_content;
    cc.content = client_content;
    hc.content_check = check;
    net::Session host(quiet), client(quiet);
    Outcome o;
    if (!host.start(hc) || !client.start(cc)) {
        printf("FAIL: sessions didn't start\n");
        ++failures;
        return o;
    }
    for (int i = 0; i < 200; ++i) {
        pump(host, client, 10);
        const net::Status st = client.status();
        if (st.connected || st.rejected) break;
    }
    const net::Status st = client.status();
    o.connected = st.connected && host.status().connected;
    o.rejected = st.rejected;
    o.reason = st.reject_reason;
    o.detail = st.reject_detail;
    host.stop();
    client.stop();
    return o;
}

const uint8_t kMissing = static_cast<uint8_t>(net::RejectReason::MissingContent);

void sessions() {
    Outcome o = try_join(27993, "dlc_far_territory,dlc_iron_road", "dlc_far_territory");
    expect(o.rejected && !o.connected && o.reason == kMissing && o.detail == "dlc_iron_road",
           "a friend lacking one of the host's DLC is refused, naming it");

    o = try_join(27994, "dlc_far_territory,dlc_iron_road", "dlc_iron_road,dlc_far_territory");
    expect(o.connected && !o.rejected, "a friend with the same DLC (listed in another order) gets in");

    o = try_join(27995, "dlc_far_territory", "dlc_far_territory,dlc_iron_road");
    expect(o.connected && !o.rejected, "a friend with more DLC than the host gets in");

    o = try_join(27996, "", "");
    expect(o.connected && !o.rejected, "no DLC on either side: in");

    o = try_join(27997, "dlc_iron_road", "", false);
    expect(o.connected && !o.rejected, "dlc_check=0: a friend without the host's DLC gets in");

    o = try_join(27998, "dlc_iron_road", "?");
    expect(o.rejected && o.reason == kMissing && o.detail == net::kContentUnknown,
           "a friend whose DLC is unknown is refused by a host with DLC");

    o = try_join(27999, "?", "");
    expect(o.connected && !o.rejected, "a host that can't tell its own DLC doesn't check");
}

}  // namespace

int main() {
    helpers();
    sessions();
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
