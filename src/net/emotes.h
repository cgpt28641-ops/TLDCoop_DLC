#pragma once
// TLDCoop_DLC: gestures (emotes): a player waves, points, beckons, says stop, nods or shakes their head, and the
// others see their body do it. A key each (coop.ini [emotes]); State carries the gesture while it plays (emote) and a
// counter (emote_seq) that changes with every new one, so the same gesture twice plays twice. Plain code, no IL2CPP
// (emotes_test).

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace net {

enum class Emote : uint8_t { None = 0, Wave, Point, ComeHere, Stop, Yes, No, Count };

inline Emote emote_of(uint8_t wire) {
    return wire < static_cast<uint8_t>(Emote::Count) ? static_cast<Emote>(wire) : Emote::None;
}

// coop.ini's key names for them ([emotes] wave=F5 ...) and the notice we see when we do one.
inline const char* emote_key_name(Emote e) {
    constexpr const char* kNames[] = {"", "wave", "point", "come", "stop", "yes", "no"};
    const auto i = static_cast<size_t>(e);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "";
}
inline const char* emote_notice(Emote e) {
    constexpr const char* kNames[] = {"", "You wave", "You point", "You beckon", "You signal stop", "You nod",
                                      "You shake your head"};
    const auto i = static_cast<size_t>(e);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "";
}

// How long each plays (seconds), its ease in and out included.
inline float emote_seconds(Emote e) {
    switch (e) {
        case Emote::Wave: return 2.6f;
        case Emote::Point: return 2.4f;
        case Emote::ComeHere: return 2.6f;
        case Emote::Stop: return 2.0f;
        case Emote::Yes: return 1.6f;
        case Emote::No: return 1.6f;
        default: return 0;
    }
}

// The default keys: F5 to F8 for the hand's, F3 and F4 for yes and no (F9 and F10 let a friend in or turn them away).
inline const char* emote_default_key(Emote e) {
    constexpr const char* kKeys[] = {"", "F5", "F6", "F7", "F8", "F3", "F4"};
    const auto i = static_cast<size_t>(e);
    return i < sizeof(kKeys) / sizeof(kKeys[0]) ? kKeys[i] : "";
}

// A key as coop.ini names it, as a Windows virtual-key code: F1..F12, A..Z, 0..9, NUM0..NUM9; 0 for none ("", "off")
// or anything else.
inline int virtual_key(std::string name) {
    std::string k;
    for (char c : name)
        if (!std::isspace(static_cast<unsigned char>(c))) k += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (k.empty() || k == "OFF" || k == "NONE") return 0;
    if (k.size() >= 2 && k[0] == 'F' && std::isdigit(static_cast<unsigned char>(k[1]))) {
        const int n = std::atoi(k.c_str() + 1);
        return n >= 1 && n <= 12 && std::to_string(n) == k.substr(1) ? 0x70 + n - 1 : 0;
    }
    if (k.size() == 4 && k.rfind("NUM", 0) == 0 && std::isdigit(static_cast<unsigned char>(k[3]))) return 0x60 + (k[3] - '0');
    if (k.size() == 1 && ((k[0] >= 'A' && k[0] <= 'Z') || (k[0] >= '0' && k[0] <= '9'))) return k[0];
    return 0;
}

// How much of the gesture shows at `t` seconds into it (0..1): eased in over the first quarter second, out over the
// last.
inline float emote_weight(Emote e, float t) {
    const float total = emote_seconds(e), ease = 0.25f;
    if (total <= 0 || t < 0 || t >= total) return 0;
    float w = 1;
    if (t < ease) w = t / ease;
    if (total - t < ease) w = (total - t) / ease;
    return w * w * (3 - 2 * w);  // smoothstep
}

}  // namespace net
