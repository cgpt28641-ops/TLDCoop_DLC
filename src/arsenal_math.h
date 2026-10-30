#pragma once
// TLDCoop_DLC: Adaptive Arsenal's numbers, without the game. Ported from AdaptiveArsenal (Deaadman;
// https://github.com/Deaadman/adaptive-arsenal), built in with its author's permission: ProjectileItem's muzzle
// velocities, accuracy and damage drop-off, and the flight of its bullet (a Rigidbody: gravity, drag 0.1).

#include <algorithm>
#include <cmath>
#include <cstring>

namespace arsenal {

enum Gun { kRevolver = 0, kRifle = 1 };  // TLD's GunType

constexpr float kDamageRange = 500;  // metres over which a bullet's damage falls from all of it to a tenth
constexpr float kMinDamage = 0.1f;
constexpr float kScale = 0.5f;       // the mod's ScaleMultiplier on the muzzle velocity
constexpr float kDrag = 0.1f;
constexpr float kGravity = 9.81f;
constexpr int kRevolverRange[] = {40, 50, 60, 80, 100};  // by the revolver skill's tier

// m/s as the mod (the first name the item's name contains, in this order), already scaled.
inline float muzzle_velocity(const char* item, int gun) {
    static const struct { const char* name; int v; } kGuns[] = {
        {"GEAR_Rifle_Barbs", 800}, {"GEAR_Rifle_Curators", 1000}, {"GEAR_Rifle_Traders", 1100},
        {"GEAR_Rifle_Vaughns", 700}, {"GEAR_Rifle", 800},          {"GEAR_RevolverFancy", 600},
        {"GEAR_RevolverGreen", 400}, {"GEAR_RevolverStubNosed", 300}, {"GEAR_Revolver", 400}};
    for (const auto& g : kGuns)
        if (item && std::strstr(item, g.name)) return g.v * kScale;
    return (gun == kRifle ? 800 : 400) * kScale;  // a gun the mod doesn't know (an update's new one)
}

inline float revolver_range(int tier) {
    return static_cast<float>(kRevolverRange[std::clamp(tier, 0, 4)]);
}

// ProjectileItem.CalculateAccuracy: `base` is the rifle skill's effective range, or for a revolver
// revolver_range(tier) + the gun's m_AccuracyRange.
inline float accuracy(float base, bool hip_fire, bool standing, bool moving) {
    float m = 1.2f;
    if (hip_fire) m *= 0.7f;
    if (standing) m *= 0.8f;
    if (moving) m *= 0.9f;
    return base * m;
}

// Degrees either way the shot may go (Mathf.Lerp(0, 10, 1 - accuracy / 100), t clamped).
inline float spread_degrees(float acc) {
    return 10.0f * std::clamp(1.0f - acc / 100.0f, 0.0f, 1.0f);
}

// The share of the gun's damage left after `metres` of flight.
inline float damage_share(float metres) {
    const float t = std::clamp(metres / kDamageRange, 0.0f, 1.0f);
    return 1.0f + (kMinDamage - 1.0f) * t;
}

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float length(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline V3 normalized(V3 a) {
    const float l = length(a);
    return l > 1e-6f ? a * (1.0f / l) : V3{0, 0, 1};
}
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// The shot turned `pitch` degrees down and `yaw` degrees right of `forward` (Unity's Euler(x, y, 0) after it).
inline V3 turned(V3 forward, float pitch, float yaw) {
    const V3 f = normalized(forward);
    V3 right = cross(V3{0, 1, 0}, f);
    if (length(right) < 1e-4f) right = {1, 0, 0};
    right = normalized(right);
    const V3 up = cross(f, right);
    const float d = 3.14159265f / 180.0f;
    return normalized(f + right * std::tan(yaw * d) - up * std::tan(pitch * d));
}

// One step of the bullet's flight (a Rigidbody with gravity and drag): its new velocity.
inline V3 step_velocity(V3 v, float dt) {
    v.y -= kGravity * dt;
    return v * (1.0f / (1.0f + kDrag * dt));
}

}  // namespace arsenal
