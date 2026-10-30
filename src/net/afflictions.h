#pragma once
// TLDCoop_DLC: how a friend is doing, as State carries it (condition, afflictions) and the others show it when they
// look at them up close: their afflictions, and their condition in a word. Plain code, no IL2CPP (afflictions_test).

#include <cstdint>
#include <string>

namespace net {

// One bit each in State's afflictions.
enum class Affliction : uint8_t {
    Hypothermia, Frostbite, FoodPoisoning, Dysentery, SprainedAnkle, SprainedWrist, BloodLoss, Infection,
    InfectionRisk, BrokenRib, Burns, IntestinalParasites, CabinFever, Starving, Dehydrated, Exhausted, Freezing, Count
};

inline uint32_t affliction_bit(Affliction a) { return 1u << static_cast<uint32_t>(a); }

// Names for when TLD's own (localized) one isn't there.
inline const char* affliction_name(Affliction a) {
    constexpr const char* kNames[] = {"Hypothermia", "Frostbite", "Food poisoning", "Dysentery", "Sprained ankle",
                                      "Sprained wrist", "Bleeding", "Infection", "Infection risk", "Broken ribs",
                                      "Burns", "Intestinal parasites", "Cabin fever", "Starving", "Dehydrated",
                                      "Exhausted", "Freezing"};
    const auto i = static_cast<size_t>(a);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "";
}

// Their condition (0-100 %) in a word.
inline const char* condition_word(uint8_t percent) {
    if (percent >= 70) return "Healthy";
    if (percent >= 35) return "Hurt";
    return "Badly hurt";
}

}  // namespace net
