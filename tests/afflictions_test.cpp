// TLDCoop_DLC: a friend's condition and afflictions (net/afflictions.h) and their place in State.
#include <cstdio>
#include <string>

#include "net/afflictions.h"
#include "net/protocol.h"

namespace {
int failures = 0;
void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using net::Affliction;
    expect(static_cast<int>(Affliction::Count) <= 32, "every affliction has a bit");
    for (int i = 0; i < static_cast<int>(Affliction::Count); ++i)
        expect(*net::affliction_name(static_cast<Affliction>(i)) != 0, "every affliction has a name");
    expect(std::string(net::condition_word(100)) == "Healthy" && std::string(net::condition_word(50)) == "Hurt" &&
               std::string(net::condition_word(10)) == "Badly hurt",
           "condition in a word");
    net::StateMsg s;
    s.condition = 42;
    s.afflictions = net::affliction_bit(Affliction::BloodLoss) | net::affliction_bit(Affliction::Freezing);
    net::Writer w(net::Msg::State);
    net::write_state(w, s);
    net::Packet p;
    p.type = net::Msg::State;
    p.data.assign(w.data(), w.data() + w.size());
    net::Reader r = net::packet_body(p);
    net::StateMsg got;
    expect(w.ok() && net::read_state(r, got) && got.condition == 42 && got.afflictions == s.afflictions,
           "State carries condition and afflictions");
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
