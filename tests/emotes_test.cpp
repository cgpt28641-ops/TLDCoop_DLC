// TLDCoop_DLC: gestures (net/emotes.h) and their place in State. Exit code 0 when all pass.

#include <cmath>
#include <cstdio>
#include <string>

#include "net/emotes.h"
#include "net/protocol.h"

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

}  // namespace

int main() {
    using net::Emote;
    expect(net::virtual_key("F5") == 0x74 && net::virtual_key(" f10 ") == 0x79 && net::virtual_key("F12") == 0x7B,
           "function keys");
    expect(net::virtual_key("F13") == 0 && net::virtual_key("F0") == 0 && net::virtual_key("F05") == 0,
           "no such function key");
    expect(net::virtual_key("g") == 'G' && net::virtual_key("7") == '7' && net::virtual_key("NUM3") == 0x63,
           "letters, digits, the number pad");
    expect(net::virtual_key("") == 0 && net::virtual_key("off") == 0 && net::virtual_key("Ctrl") == 0, "none or unknown");
    for (int i = 1; i < static_cast<int>(Emote::Count); ++i) {
        const Emote e = static_cast<Emote>(i);
        const std::string what = std::string("the ") + net::emote_key_name(e) + " gesture: a default key, a time, a notice";
        expect(net::virtual_key(net::emote_default_key(e)) != 0 && net::emote_seconds(e) > 1 &&
                   *net::emote_notice(e) != 0,
               what.c_str());
    }
    expect(net::emote_of(200) == Emote::None && net::emote_of(1) == Emote::Wave, "an unknown gesture is none");
    expect(net::emote_weight(Emote::Wave, 0) == 0 && net::emote_weight(Emote::Wave, 1.0f) == 1 &&
               net::emote_weight(Emote::Wave, 10) == 0 && net::emote_weight(Emote::Wave, 0.125f) > 0 &&
               net::emote_weight(Emote::Wave, 0.125f) < 1,
           "eased in, full, over");
    {
        net::StateMsg s;
        s.valid = true;
        s.scene = "LakeRegion";
        s.emote = static_cast<uint8_t>(Emote::Point);
        s.emote_seq = 7;
        net::Writer w(net::Msg::State);
        net::write_state(w, s);
        net::Packet p;
        p.type = net::Msg::State;
        p.data.assign(w.data(), w.data() + w.size());
        net::Reader r = net::packet_body(p);
        net::StateMsg got;
        expect(w.ok() && net::read_state(r, got) && got.emote == s.emote && got.emote_seq == 7 && got.scene == s.scene,
               "State carries the gesture");
    }
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
