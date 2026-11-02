#pragma once
// TLDCoop_DLC (v44): wildlife ownership, the parts that need no game: who should run an animal, when to hand it over,
// and how a game that only watches an animal draws it between records. Shared by the mod and tests/wildlife_own_test.
//
// Who runs it (the host decides, every game's animals alike): each player in the animal's scene gets a score, in metres,
// lower is better:
//   distance to the animal
//   - kLookingBonus when they look at it (within kLookMetres and kLookDegrees of their facing)
//   + their link's round trip x kRttMetresPerMs (the host's own game: 0)
//   - kEngagedBonus when the animal is after them (stalking, attacking, holding ground...: its hunt stays theirs);
//     half of it when it flees from them
//   + kInsideMetres when they are inside a Seamless Interiors room (80 m above the building, not out here)
// Hysteresis: the best one takes over only when it beats the owner by kMarginMetres (or kMarginShare of the owner's
// score) for kHoldMs (kEngagedHoldMs when the animal is after them), never sooner than kMinSwitchMs after the last
// switch, never during a fight (Struggle) or a handover, never for a dead animal. An owner who left the scene is
// replaced at once.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace net::wild {

constexpr uint8_t kNobody = 0xFF;

constexpr float kLookingBonus = 20.0f;
constexpr float kLookMetres = 150.0f;
constexpr float kLookDegrees = 35.0f;
constexpr float kRttMetresPerMs = 0.05f;  // 100 ms of round trip weighs as 5 m
constexpr float kEngagedBonus = 1000.0f;
constexpr float kInsideMetres = 300.0f;
constexpr float kMarginMetres = 12.0f;
constexpr float kMarginShare = 0.25f;
constexpr double kHoldMs = 3000;
constexpr double kEngagedHoldMs = 750;
constexpr double kMinSwitchMs = 5000;

// AiMode values (TLD's)
constexpr uint8_t kModeAttack = 1, kModeDead = 2, kModeFlee = 4, kModeHoldGround = 6, kModeInvestigate = 8,
                  kModeInvestigateSmell = 10, kModeStalking = 13, kModeStruggle = 14, kModeWander = 15,
                  kModePassingAttack = 25;

// How much the animal's current mode ties it to its target: 1 hunting them, 0.5 fleeing from them, 0 not at all.
inline float engagement(uint8_t mode) {
    switch (mode) {
        case kModeAttack:
        case kModeHoldGround:
        case kModeInvestigate:
        case kModeInvestigateSmell:
        case kModeStalking:
        case kModeStruggle:
        case kModePassingAttack: return 1.0f;
        case kModeFlee: return 0.5f;
        default: return 0.0f;
    }
}

struct Seat {
    uint8_t player = kNobody;
    float metres = 0;      // from the animal
    bool looking = false;  // the animal is in front of them, within kLookMetres
    double rtt_ms = 0;     // their link to the host (the host: 0)
    bool inside = false;   // inside a Seamless Interiors room
};

inline float score(const Seat& s, uint8_t target, uint8_t mode) {
    float v = s.metres;
    if (s.looking) v -= kLookingBonus;
    v += static_cast<float>(s.rtt_ms > 0 ? s.rtt_ms : 0) * kRttMetresPerMs;
    if (s.inside) v += kInsideMetres;
    if (target != kNobody && target == s.player) v -= kEngagedBonus * engagement(mode);
    return v;
}

// Whether (dx, dz) from a player facing `yaw_degrees` (Unity: 0 = +z, 90 = +x) is in front of them.
inline bool looking_at(float yaw_degrees, float dx, float dz, float metres) {
    if (metres > kLookMetres) return false;
    if (metres < 1.0f) return true;
    const float yaw = yaw_degrees * 0.0174532925f;
    const float fx = std::sin(yaw), fz = std::cos(yaw);
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-3f) return true;
    const float cosine = (fx * dx + fz * dz) / len;
    return cosine >= std::cos(kLookDegrees * 0.0174532925f);
}

// What the arbiter knows of one animal.
struct Facts {
    uint8_t owner = kNobody;
    bool owner_present = false;  // the owner is in the animal's scene (in game)
    uint8_t target = kNobody;    // who it's after
    uint8_t mode = 0;            // AiMode
    bool dead = false;
    bool pending = false;        // a handover is under way
};

// Per animal, between decisions.
struct Track {
    uint8_t candidate = kNobody;
    double since_ms = 0;
    double last_switch_ms = -1e18;
};

enum class Why : uint8_t { None = 0, OwnerGone, Closer, Looking, Engaged };
inline const char* why_text(Why w) {
    switch (w) {
        case Why::OwnerGone: return "its owner left the scene";
        case Why::Closer: return "closer";
        case Why::Looking: return "looking at it";
        case Why::Engaged: return "it is after them";
        default: return "-";
    }
}

// The player who should run the animal from now on, or kNobody to leave it where it is.
inline uint8_t decide(Track& t, const Facts& f, const std::vector<Seat>& seats, double now_ms, Why& why) {
    why = Why::None;
    if (seats.empty() || f.pending) {
        t.candidate = kNobody;
        return kNobody;
    }
    const Seat* best = &seats.front();
    for (const Seat& s : seats)
        if (score(s, f.target, f.mode) < score(*best, f.target, f.mode)) best = &s;
    if (!f.owner_present || f.owner == kNobody) {
        // Nobody runs it here (the owner left, or it was frozen): the best one at once. A dead one goes too (its owner
        // left): someone has to keep its body.
        t.candidate = kNobody;
        t.last_switch_ms = now_ms;
        why = Why::OwnerGone;
        return best->player;
    }
    if (f.dead || f.mode == kModeStruggle) {
        t.candidate = kNobody;
        return kNobody;
    }
    const Seat* current = nullptr;
    for (const Seat& s : seats)
        if (s.player == f.owner) current = &s;
    if (!current || best->player == f.owner) {
        t.candidate = kNobody;
        return kNobody;
    }
    const float mine = score(*current, f.target, f.mode), theirs = score(*best, f.target, f.mode);
    const bool engaged = f.target == best->player && engagement(f.mode) > 0;
    const float margin = engaged ? 0.0f : std::max(kMarginMetres, kMarginShare * std::fabs(mine));
    if (!(theirs < mine - margin)) {
        t.candidate = kNobody;
        return kNobody;
    }
    if (t.candidate != best->player) {
        t.candidate = best->player;
        t.since_ms = now_ms;
    }
    const double hold = engaged ? kEngagedHoldMs : kHoldMs;
    if (now_ms - t.since_ms < hold || now_ms - t.last_switch_ms < kMinSwitchMs) return kNobody;
    t.candidate = kNobody;
    t.last_switch_ms = now_ms;
    why = engaged ? Why::Engaged : (best->looking && !current->looking) ? Why::Looking : Why::Closer;
    return best->player;
}

// At most `per_second` handovers a second, `burst` at once (the host back in a busy scene doesn't take every animal
// in one frame).
class RateLimit {
public:
    RateLimit(double per_second, double burst) : rate_(per_second), burst_(burst), tokens_(burst) {}
    bool take(double now_ms) {
        if (last_ms_ >= 0) tokens_ = std::min(burst_, tokens_ + (now_ms - last_ms_) / 1000.0 * rate_);
        last_ms_ = now_ms;
        if (tokens_ < 1.0) return false;
        tokens_ -= 1.0;
        return true;
    }

private:
    double rate_, burst_, tokens_;
    double last_ms_ = -1;
};

// A watched animal (a puppet) drawn ahead of its last record: where it would be by now at the speed it had, eased
// towards that so corrections don't jump. Times: the owner's clock (t_ms) and ours (received_ms, now_ms).
class Predictor {
public:
    struct Pose {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0;
    };

    void reset() { have_ = 0; shown_ok_ = false; offsets_.clear(); }

    void add(const Pose& p, double t_ms, double received_ms) {
        if (have_ && t_ms <= last_t_) return;  // a duplicate or late record
        offsets_.push_back(received_ms - t_ms);
        if (offsets_.size() > kOffsetWindow) offsets_.erase(offsets_.begin());
        offset_ = *std::min_element(offsets_.begin(), offsets_.end());
        if (have_) {
            const double dt = (t_ms - last_t_) / 1000.0;
            const float dx = p.x - last_.x, dy = p.y - last_.y, dz = p.z - last_.z;
            if (dt > 0.02 && dt < 1.0 && dx * dx + dy * dy + dz * dz < kTeleportMetres * kTeleportMetres) {
                vx_ = static_cast<float>(dx / dt), vy_ = static_cast<float>(dy / dt), vz_ = static_cast<float>(dz / dt);
                yaw_rate_ = static_cast<float>(turn(last_.yaw, p.yaw) / dt);
                const float speed = std::sqrt(vx_ * vx_ + vz_ * vz_);
                if (speed > kMaxSpeed) {  // a glitch, not a run
                    vx_ *= kMaxSpeed / speed, vz_ *= kMaxSpeed / speed;
                }
                yaw_rate_ = std::clamp(yaw_rate_, -kMaxYawRate, kMaxYawRate);
            } else {
                vx_ = vy_ = vz_ = yaw_rate_ = 0;
                if (dx * dx + dy * dy + dz * dz >= kTeleportMetres * kTeleportMetres) shown_ok_ = false;  // a jump
            }
        }
        last_ = p;
        last_t_ = t_ms;
        have_ = 1;
    }

    // Where to draw it at local time now_ms. False before the first record.
    bool sample(double now_ms, Pose& out) {
        if (!have_) return false;
        const double t = now_ms - offset_ - kJitterMs;  // on the owner's clock
        const double ahead = std::clamp((t - last_t_) / 1000.0, 0.0, kMaxAheadMs / 1000.0);
        Pose target = last_;
        target.x += static_cast<float>(vx_ * ahead);
        target.y += static_cast<float>(vy_ * ahead);
        target.z += static_cast<float>(vz_ * ahead);
        target.yaw += static_cast<float>(yaw_rate_ * ahead);
        if (!shown_ok_) {
            shown_ = target;
            shown_ok_ = true;
        } else {
            const double dt = std::clamp((now_ms - shown_ms_) / 1000.0, 0.0, 0.25);
            const float ex = target.x - shown_.x, ey = target.y - shown_.y, ez = target.z - shown_.z;
            if (ex * ex + ey * ey + ez * ez > kSnapMetres * kSnapMetres) {
                shown_ = target;
            } else {
                const float k = static_cast<float>(std::min(1.0, dt * kEase));
                shown_.x += ex * k, shown_.y += ey * k, shown_.z += ez * k;
                shown_.yaw += turn(shown_.yaw, target.yaw) * k;
                shown_.pitch += (target.pitch - shown_.pitch) * k;
                shown_.roll += (target.roll - shown_.roll) * k;
            }
        }
        shown_ms_ = now_ms;
        out = shown_;
        out.yaw = std::fmod(out.yaw + 720.0f, 360.0f);
        return true;
    }

    float speed() const { return std::sqrt(vx_ * vx_ + vz_ * vz_); }

    static float turn(float from, float to) { return std::fmod(to - from + 540.0f, 360.0f) - 180.0f; }

private:
    static constexpr size_t kOffsetWindow = 50;
    static constexpr double kJitterMs = 40;
    static constexpr double kMaxAheadMs = 400;
    static constexpr float kTeleportMetres = 10.0f;
    static constexpr float kSnapMetres = 4.0f;
    static constexpr float kMaxSpeed = 20.0f;      // m/s: nothing in TLD runs faster
    static constexpr float kMaxYawRate = 360.0f;   // degrees/s
    static constexpr double kEase = 10.0;          // 1/s: how fast a correction closes

    int have_ = 0;
    Pose last_{};
    double last_t_ = 0;
    float vx_ = 0, vy_ = 0, vz_ = 0, yaw_rate_ = 0;
    std::vector<double> offsets_;
    double offset_ = 0;
    Pose shown_{};
    bool shown_ok_ = false;
    double shown_ms_ = 0;
};

}  // namespace net::wild
