#include "wildlife_targets.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "avatar.h"
#include "game_state.h"
#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/held.h"
#include "net_runtime.h"
#include "paths.h"
#include "seamless_interiors.h"
#include "wildlife_sync.h"

namespace wildlife_targets {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kAnim = "UnityEngine.AnimationModule.dll";
constexpr float kDegToRad = 3.14159265f / 180.0f;
// The paused animal is held on the avatar for the whole fight: the avatar moves with the other player, and nothing
// else keeps the animal grounded (its AI is off; with root motion its pounce clip lifted it 90 m in testing).
constexpr double kPoseCheckMs = 400;   // then check once that its Animator really plays the fight
// The wolf's fight states (Animator fullPathHash = CRC32 of "Base Layer.Struggle.<state>"). Seen in a recorded real
// fight (Intro, Bare) and in the wolf AnimatorController's Struggle sub-state machine (the rest, one per weapon).
constexpr int32_t kWolfFightStates[] = {
    220735380,    // Wolf_Struggle_Intro (0.93 s, then by WeaponId:)
    1140013246,   // Wolf_Struggle_Bare (WeaponId -1, 9 s, does not loop)
    -1385334116,  // Wolf_Struggle_Knife (1)
    -1892796755,  // Wolf_Struggle_Axe (2)
    1784741652,   // Wolf_Struggle_Prybar (3)
    -82076502,    // Wolf_Struggle_Knife_Improvised (4)
    -902449743,   // Wolf_Struggle_Axe_Improvised (5)
    317640769,    // Wolf_Struggle_Hammer (6)
    -133337875,   // Wolf_Struggle_Revolver (7)
    1944561193,   // Wolf_Struggle_CougarClawKnife (10)
    861989745,    // Wolf_Struggle_SurvivalKnife (11)
};
constexpr int32_t kKindWolf = 1;
constexpr int32_t kAiModeFlee = 4;
constexpr int32_t kAiModeDead = 2;
constexpr double kFightTimeoutMs = 45000;  // no answer from their game: the animal gives up and flees
constexpr float kSwitchMargin = 0.6f;  // the other player must be this much closer (fraction) to switch over
// TLDCoop_DLC: and at least this many metres closer, and not sooner than this after its last switch: with three or
// four players close together a wolf switched every second or two and ran in circles between them.
constexpr float kSwitchMinMetres = 8.0f;
constexpr double kSwitchHoldMs = 8000;
constexpr float kSwitchCloserFactor = 0.5f;  // already after one: the other must be under half as far...
constexpr float kSwitchMaxMetres = 30.0f;     // ...and this close
constexpr int32_t kAiModeStruggle = 14;
constexpr int32_t kAiModeWander = 15;
constexpr int32_t kAiModeAttack = 1;
constexpr int32_t kAiModeHoldGround = 6;
constexpr int32_t kDamageSourcePlayer = 1;  // DamageSource.Player
constexpr const char* kKinds[] = {"none", "wolf", "bear", "stag", "rabbit", "moose", "cougar"};

struct Vector3 {
    float x, y, z;
};
struct Quaternion {
    float x, y, z, w;
};
struct AnimatorStateInfo {  // UnityEngine.AnimatorStateInfo, 36 bytes
    int32_t name, path, full_path;
    float normalized_time, length, speed, speed_multiplier;
    int32_t tag, loop;
};

// Where the paused animal goes for the fight (coop.ini [wildlife], read once at init).
struct PoseSettings {
    bool on = true;       // struggle_pose=1 (default): put it on the avatar; 0 = it waits where it landed
    float forward = 0;    // pose_forward (m): its root along the avatar's facing (+) or towards its head (-)
    float side = 0;       // pose_side (m): along the avatar's right
    float turn = 180;     // pose_turn (degrees): its yaw relative to the avatar's
} g_pose;

struct Pose {
    Vector3 pos;
    Quaternion rot;
};

// TLD's own fight keeps the animal still and moves the victim onto it: PlayerStruggle puts the camera rig on the
// animal's "<kind>:game_camera" bone, which the wolf's struggle clips hold on the animal's root, turned 180 degrees.
// The avatar can't be moved (it follows the other player), so the animal moves instead: its root on the avatar's
// root, facing back along it (the "down" clip lies on its back, feet forward, head behind the root).
Pose struggle_pose(const Vector3& avatar_pos, float avatar_yaw_deg, const PoseSettings& s) {
    const float r = avatar_yaw_deg * kDegToRad;
    const float sn = std::sin(r), cs = std::cos(r);  // Unity: forward (sin, 0, cos), right (cos, 0, -sin)
    Pose p;
    p.pos = {avatar_pos.x + sn * s.forward + cs * s.side, avatar_pos.y, avatar_pos.z + cs * s.forward - sn * s.side};
    const float half = (avatar_yaw_deg + s.turn) * 0.5f * kDegToRad;
    p.rot = {0, std::sin(half), 0, std::cos(half)};
    return p;
}

struct Api {
    ptrdiff_t current_target = -1;  // BaseAi.m_CurrentTarget (AiTarget)
    ptrdiff_t player_target = -1;   // BaseAi.m_PlayerTarget (AiTarget)
    ptrdiff_t kind = -1;            // BaseAi.m_AiSubType
    ptrdiff_t mode = -1;            // BaseAi.m_CurrentMode
    const MethodInfo* set_mode = nullptr;  // BaseAi.SetAiMode(AiMode)
    const MethodInfo* clear_target = nullptr;  // BaseAi.ClearTarget()
    const MethodInfo* component_transform = nullptr;
    const MethodInfo* component_go = nullptr;
    const MethodInfo* player_manager = nullptr;  // static GameManager.GetPlayerManagerComponent()
    ptrdiff_t pm_ai_target = -1;                 // PlayerManager.m_AiTarget
    const MethodInfo* get_position = nullptr;
    const MethodInfo* set_enabled = nullptr;  // Behaviour.set_enabled: pauses the animal's AI during their fight
    ptrdiff_t hp = -1;                        // BaseAi.m_CurrentHP
    // What their fight did to it (BaseAi): the same fields BaseAi.ApplyDamage writes.
    ptrdiff_t wounded = -1;           // m_Wounded
    ptrdiff_t wounded_minutes = -1;   // m_ElapsedWoundedMinutes
    ptrdiff_t bleeding = -1;          // m_BleedingOut
    ptrdiff_t bleed_elapsed = -1;     // m_ElapsedBleedingOutMinutes
    ptrdiff_t bleed_limit = -1;       // m_DeathAfterBleeingOutMinutes
    ptrdiff_t damage_source = -1;     // m_DamageSource
    // struggle_pose:
    const MethodInfo* set_pos_rot = nullptr;    // BaseAi.SetPositionAndRotation(Vector3, Quaternion): MoveAgent.Warp
    const MethodInfo* flee_from = nullptr;      // BaseAi.FleeFrom(Vector3): m_FleeFromPos, ClearTarget, SetAiMode(Flee)
    const MethodInfo* get_euler = nullptr;      // Transform.get_eulerAngles
    const MethodInfo* set_position = nullptr;   // Transform.set_position
    const MethodInfo* set_rotation = nullptr;   // Transform.set_rotation
    const MethodInfo* state_info = nullptr;     // Animator.GetCurrentAnimatorStateInfo(int)
    const MethodInfo* set_trigger = nullptr;    // Animator.SetTrigger(int)
    const MethodInfo* get_root_motion = nullptr;  // Animator.get_applyRootMotion
    const MethodInfo* set_root_motion = nullptr;  // Animator.set_applyRootMotion
    ptrdiff_t animator = -1;                    // BaseAi.m_Animator
    ptrdiff_t struggle_start = -1;              // BaseAi.m_AnimParameter_StruggleStart (trigger hash)
    ptrdiff_t eye_offset = -1;                  // AiTarget.m_EyeOffset
    FieldInfo* ai_list = nullptr;               // static BaseAiManager.m_BaseAis (A7: animals after a player gone in)
} api;

using UpdateFn = void (*)(Il2CppObject* self, const MethodInfo* method);
using GetterFn = Il2CppObject* (*)(const MethodInfo* method);
using BeginFn = void (*)(Il2CppObject* self, Il2CppObject* partner, const MethodInfo* method);
using ApplyDamageFn = void (*)(Il2CppObject* self, float damage, int32_t source, Il2CppString* collider,
                               const MethodInfo* method);
using WoundsFn = Il2CppObject* (*)(Il2CppObject* self, Il2CppObject* instigator, int32_t options,
                                   const MethodInfo* method);
using HoldingFn = bool (*)(Il2CppObject* self, const MethodInfo* method);
using HoldingFlareFn = bool (*)(Il2CppObject* self, int32_t flare_type, const MethodInfo* method);
using EyePosFn = Vector3* (*)(Vector3* out, Il2CppObject* self, const MethodInfo* method);
UpdateFn g_update = nullptr;
UpdateFn g_late_update = nullptr;
HoldingFn g_holding_torch = nullptr;
HoldingFn g_holding_spear = nullptr;
HoldingFlareFn g_holding_flare = nullptr;
EyePosFn g_eye_pos = nullptr;
GetterFn g_get_player_object = nullptr;
GetterFn g_get_player_transform = nullptr;
BeginFn g_struggle_begin = nullptr;
ApplyDamageFn g_target_damage = nullptr;
WoundsFn g_wounds = nullptr;

// Set while an animal that hunts a friend is thinking: whom TLD's "the player" means right now.
struct StandIn {
    Il2CppObject* go = nullptr;
    Il2CppObject* transform = nullptr;
    Il2CppObject* animal = nullptr;
    uint8_t player = 0;  // the friend
};
StandIn g_stand_in;  // main thread only (BaseAi runs there)

std::unordered_map<uintptr_t, uint8_t> g_hunts_friend;  // BaseAi -> the player it picked (kLocal: ours)
// Animals fighting a friend on their game: paused here until the result comes back.
struct Fight {
    uintptr_t ai = 0;  // gchandle
    double started_ms = 0;
    uint8_t player = 0;         // whose fight
    bool posed = false;         // struggle_pose: held on the avatar
    bool root_motion = false;   // its Animator's applyRootMotion before the fight (off while held)
    bool states_checked = false;
    double traced_ms = 0;       // the fight trace in the log (where it is, what it plays), every half second
    int traces = 0;
};
std::unordered_map<uint32_t, Fight> g_fights;  // by the id the animal goes by in Animals
int g_switches_logged = 0;
std::unordered_map<uintptr_t, uint8_t> g_holds_ground;  // BaseAi -> the friend it holds ground before (logged once
                                                        // each time it starts)
std::unordered_map<std::string, bool> g_hands_seen;     // "<player> <question>" -> the last answer (for the log)
std::unordered_map<uintptr_t, double> g_last_switch;    // TLDCoop_DLC: BaseAi -> when it last turned to another player

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

// TLDCoop_DLC (v45): with wildlife ownership a friend's game runs real animals too (the ones it owns): they see every
// player (the avatars), as the host's always did. What they would do to another player isn't done there: the host
// hands such an animal to that player's game within a second (it is after them), where TLD's own fight runs.
bool real_animals() { return hosting() || wildlife_sync::ownership(); }
bool runs(Il2CppObject* ai) { return wildlife_sync::ownership() ? wildlife_sync::owned_here(ai) : hosting(); }
constexpr uint8_t kLocal = 0xFE;  // in pick_player and g_hunts_friend: our own player (the host's, on the host)

bool position_of(Il2CppObject* component_or_transform, bool is_transform, Vector3& out) {
    Il2CppObject* tf = is_transform ? component_or_transform : il2::invoke(api.component_transform, component_or_transform);
    return tf && il2::invoke_value(api.get_position, tf, out);
}

Il2CppObject* go_of_component(Il2CppObject* component) {
    return component && api.component_go ? il2::invoke(api.component_go, component) : nullptr;
}

float distance(const Vector3& a, const Vector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

const char* kind_of(Il2CppObject* ai) {
    const int32_t k = il2::read_field<int32_t>(ai, api.kind);
    return k >= 0 && k < static_cast<int32_t>(std::size(kKinds)) ? kKinds[k] : "?";
}

// TLDCoop_DLC: what every animal's Update / LateUpdate asks about the players, worked out once a frame (it was read
// again for each animal, twice a frame: the host's player, the avatars shown, where each stands).
struct FriendSpot {
    uint8_t player = 0;
    Vector3 pos{};
};
struct FrameView {
    uint64_t frame = ~0ull;
    bool host_here = false;      // the host is in the game, outdoors (not in a Seamless Interiors room)
    bool host_inside = false;    // the host is in a Seamless Interiors room
    Il2CppObject* host_target = nullptr;  // our own player's AiTarget (PlayerManager.m_AiTarget, never a stand-in)
    Vector3 host{};
    std::vector<uint8_t> shown;     // avatars shown here
    std::vector<uint8_t> in_room;   // ...of those, the ones inside a Seamless Interiors room
    std::vector<FriendSpot> friends;  // shown, outdoors, with where their avatar stands
};
FrameView g_view;
uint64_t g_frame = 0;  // on_frame counts frames

// Built at the first ask of a frame, before any animal swaps its player (GetPlayerTransform answers for the host then).
const FrameView& view() {
    if (g_view.frame == g_frame || g_stand_in.go) return g_view;  // (mid-swap: the last frame's)
    FrameView v;
    v.frame = g_frame;
    const game::PlayerState p = game::read_player();
    v.host_inside = seamless_interiors::player_inside();
    v.host_here = p.valid && !v.host_inside;
    if (api.pm_ai_target >= 0 && api.player_manager)
        if (Il2CppObject* pm = il2::invoke(api.player_manager, nullptr))
            v.host_target = il2::read_field<Il2CppObject*>(pm, api.pm_ai_target);
    v.host = Vector3{p.position.x, p.position.y, p.position.z};
    v.shown = avatar::shown();
    for (uint8_t id : v.shown) {
        if (const auto st = netrt::remote_state(id); st && !st->place.empty()) {
            v.in_room.push_back(id);
            continue;
        }
        Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
        FriendSpot f;
        f.player = id;
        if (avatar::stand_in(id, go, transform, target) && position_of(transform, true, f.pos)) v.friends.push_back(f);
    }
    g_view = std::move(v);
    return g_view;
}

// Someone the animal could deal with: the host (player 0, TLD's own player) or a friend whose avatar is shown here.
struct Candidate {
    uint8_t player = 0;
    Il2CppObject* go = nullptr;         // friends: their avatar
    Il2CppObject* transform = nullptr;
    Il2CppObject* target = nullptr;     // the AiTarget: the host's own (BaseAi.m_PlayerTarget) or the avatar's
    float metres = 0;                   // from the animal
};

std::vector<Candidate> candidates(Il2CppObject* ai, const Vector3& at) {
    std::vector<Candidate> out;
    const FrameView& v = view();
    // TLDCoop_DLC: a player inside a Seamless Interiors room (80 m above its building) isn't here for the animals
    if (v.host_here) {
        Candidate host;
        host.player = kLocal;
        host.target = il2::read_field<Il2CppObject*>(ai, api.player_target);
        host.metres = distance(at, v.host);
        out.push_back(host);
    }
    for (const FriendSpot& f : v.friends) {  // TLDCoop_DLC: (as above: friends in a room aren't in the list)
        Candidate c;
        c.player = f.player;
        if (!avatar::stand_in(f.player, c.go, c.transform, c.target)) continue;
        c.metres = distance(at, f.pos);
        out.push_back(c);
    }
    return out;
}

const char* who_text(uint8_t player) {
    static char text[32];
    if (player == kLocal) return hosting() ? "the host" : "our player";
    if (player == 0) return "the host";
    snprintf(text, sizeof(text), "the other player (%u)", player);
    return text;
}

// Which player this animal deals with now: the one it is already after, else the nearest one. Our own player is
// kLocal; a friend's stand-in comes back in `out`.
uint8_t pick_player(Il2CppObject* ai, Candidate& out) {
    Il2CppObject* current = il2::read_field<Il2CppObject*>(ai, api.current_target);
    Il2CppObject* host_target = il2::read_field<Il2CppObject*>(ai, api.player_target);  // the host's own, here
    const auto key = reinterpret_cast<uintptr_t>(ai);
    const auto memo = g_hunts_friend.find(key);
    Vector3 me{};
    if (!position_of(ai, false, me)) return kLocal;
    const std::vector<Candidate> all = candidates(ai, me);
    auto find = [&](uint8_t player) -> const Candidate* {
        for (const Candidate& c : all)
            if (c.player == player) return &c;
        return nullptr;
    };
    // What it decided before, unless it is plainly after one of them now (its own player target can still be unset
    // early on, so "not the host's" alone doesn't mean a friend's).
    const Candidate* after = nullptr;
    for (const Candidate& c : all)
        if (current && current == c.target) after = &c;
    const bool busy = after != nullptr;
    const Candidate* was = after ? after : memo != g_hunts_friend.end() ? find(memo->second) : nullptr;
    if (!was) was = find(kLocal);
    if (!was && view().host_inside && !all.empty()) was = &all.front();  // TLDCoop_DLC: the host is in a room
    if (!was) return kLocal;  // our player isn't in the game (loading): leave TLD alone
    const Candidate* nearest = was;
    for (const Candidate& c : all)
        if (c.metres < nearest->metres) nearest = &c;

    const Candidate* now = was;
    const double t = static_cast<double>(GetTickCount64());
    const auto held = g_last_switch.find(key);
    const bool may_switch = held == g_last_switch.end() || t - held->second >= kSwitchHoldMs;
    if (nearest != was && may_switch && was->metres - nearest->metres >= kSwitchMinMetres) {
        if (!busy) {
            // Not after anyone yet: the nearest one, with a little stickiness so it doesn't flip between them.
            if (nearest->metres < was->metres * kSwitchMargin) now = nearest;
        } else {
            // After one of them: it turns on another only when that one is clearly closer, and never mid-fight.
            const bool fighting = il2::read_field<int32_t>(ai, api.mode) == kAiModeStruggle;
            if (!fighting && nearest->metres < kSwitchCloserFactor * was->metres && nearest->metres < kSwitchMaxMetres) {
                now = nearest;
                // Hand it the new target directly: it keeps stalking / charging, at the closer player now.
                Il2CppObject* new_target = now->player != kLocal ? now->target : host_target;
                il2::store_ref(ai, api.current_target, new_target);
            }
        }
    }
    if (now->player != was->player) g_last_switch[key] = t;
    if (now->player != was->player && g_switches_logged < 200) {
        ++g_switches_logged;
        char from[32];
        snprintf(from, sizeof(from), "%s", who_text(was->player));
        logx::info("wildlife: a %s %s %s (%.0f m from it; %s was %.0f m)", kind_of(ai),
                   busy ? "turns on" : "now pays attention to", who_text(now->player), now->metres, from, was->metres);
    }
    g_hunts_friend[key] = now->player;
    out = *now;
    return now->player;
}

// The friend it was after isn't here any more (they left, their link dropped, or they went into another scene): it
// loses them the way TLD's animals lose a target, rather than going after a stand-in that's gone.
void forget_gone_friend(Il2CppObject* ai) {
    const auto memo = g_hunts_friend.find(reinterpret_cast<uintptr_t>(ai));
    if (memo == g_hunts_friend.end() || memo->second == kLocal) return;
    const uint8_t player = memo->second;
    const FrameView& v = view();
    // TLDCoop_DLC: in a Seamless Interiors room (80 m above its building) they're gone for an animal outside too
    const bool in_room = std::find(v.in_room.begin(), v.in_room.end(), player) != v.in_room.end();
    if (!in_room && std::find(v.shown.begin(), v.shown.end(), player) != v.shown.end()) return;
    if (il2::read_field<int32_t>(ai, api.mode) == kAiModeStruggle) return;  // a fight: its end (or time-out) sorts it out
    g_hunts_friend.erase(memo);
    Il2CppObject* current = il2::read_field<Il2CppObject*>(ai, api.current_target);
    if (!current || current == il2::read_field<Il2CppObject*>(ai, api.player_target) || !api.clear_target) return;
    il2::invoke(api.clear_target, ai);
    logx::info("wildlife: a %s lost %s (not here any more)", kind_of(ai), who_text(player));
}

// An animal after a friend that holds ground (as TLD decides: before their lit torch, flare or raised spear): logged
// when it starts, with what is in their hands. TLD's own m_HoldGroundReason says nothing here: EnterHoldGround clears
// it, and neither MaybeHoldGround nor MaybeHoldGroundForAttack sets it again.
void note_holding_ground(Il2CppObject* ai, uint8_t player) {
    const auto key = reinterpret_cast<uintptr_t>(ai);
    if (il2::read_field<int32_t>(ai, api.mode) != kAiModeHoldGround) {
        g_holds_ground.erase(key);
        return;
    }
    const auto it = g_holds_ground.find(key);
    if (it != g_holds_ground.end() && it->second == player) return;
    g_holds_ground[key] = player;
    const auto s = netrt::remote_state(player);
    logx::info("wildlife: a %s holds ground before %s (in their hands: %s)", kind_of(ai), who_text(player),
               s ? net::describe_held(s->held, net::held_kind(s->held_kind), s->held_flags).c_str() : "nothing");
}

// TLD's AiTarget.HoldingLitTorch / HoldingLitFlare / HoldingRaisedSpear answer from our own player's hands for any
// target that is a player (no BaseAi, no NPC), the friends' stand-ins too. For a friend's stand-in the answer is in
// their State (what their game says is in their hands); the host's own target keeps TLD's answer.
std::optional<net::StateMsg> friend_hands(Il2CppObject* target, uint8_t& player) {
    const auto who = avatar::player_of(target);
    if (!who) return std::nullopt;
    player = *who;
    if (auto s = netrt::remote_state(*who)) return s;
    return net::StateMsg{};  // no state (yet): empty hands
}

bool answer(uint8_t player, const char* question, bool yes) {
    const std::string key = std::to_string(player) + " " + question;
    const auto it = g_hands_seen.find(key);
    if (it == g_hands_seen.end() || it->second != yes) {
        g_hands_seen[key] = yes;
        logx::info("wildlife: an animal asks whether %s holds %s: %s", who_text(player), question, yes ? "yes" : "no");
    }
    return yes;
}

bool holding_lit_torch_detour(Il2CppObject* self, const MethodInfo* method) {
    uint8_t player = 0;
    const auto s = friend_hands(self, player);
    if (!s) return g_holding_torch(self, method);
    return answer(player, "a lit torch", net::holds_lit_torch(net::held_kind(s->held_kind), s->held_flags));
}

bool holding_lit_flare_detour(Il2CppObject* self, int32_t flare_type, const MethodInfo* method) {
    uint8_t player = 0;
    const auto s = friend_hands(self, player);
    if (!s) return g_holding_flare(self, flare_type, method);
    return answer(player, flare_type == net::kFlareBlue ? "a lit blue flare" : "a lit red flare",
                  net::holds_lit_flare(net::held_kind(s->held_kind), s->held_flags, flare_type));
}

bool holding_raised_spear_detour(Il2CppObject* self, const MethodInfo* method) {
    uint8_t player = 0;
    const auto s = friend_hands(self, player);
    if (!s) return g_holding_spear(self, method);
    return answer(player, "a raised spear", net::holds_raised_spear(net::held_kind(s->held_kind), s->held_flags));
}

// AiTarget.GetEyePos of a player (no BaseAi, no NPC) is TLD's own player's eyes, wherever the target stands. A
// friend's stand-in has its own: its root, m_EyeOffset up. Without it an animal measures a friend's torch from the
// host (MaybeHoldGroundForTorch: the eyes within reach and in sight), and looks for them where the host is.
Vector3* eye_pos_detour(Vector3* out, Il2CppObject* self, const MethodInfo* method) {
    if (real_animals() && avatar::player_of(self) && position_of(self, false, *out)) {
        if (api.eye_offset >= 0) out->y += il2::read_field<float>(self, api.eye_offset);
        return out;
    }
    return g_eye_pos(out, self, method);
}

// TLDCoop_DLC: the friend made TLD's player for one call, and put back however the call ends: a managed exception
// thrown through it (C++ unwinding) left the host's own player a friend's avatar for good.
struct PlayerSwap {
    Il2CppObject* ai = nullptr;           // its m_PlayerTarget (when set)
    Il2CppObject* pm = nullptr;           // PlayerManager: its m_AiTarget (when set)
    Il2CppObject* friend_target = nullptr;
    Il2CppObject* host_target = nullptr, *host_ai_target = nullptr;
    StandIn outer;
    PlayerSwap(Il2CppObject* animal, Il2CppObject* manager, Il2CppObject* target, const StandIn& who, bool swap_player)
        : friend_target(target), outer(g_stand_in) {
        if (swap_player && animal && api.player_target >= 0) {
            ai = animal;
            host_target = il2::read_field<Il2CppObject*>(ai, api.player_target);
            il2::store_ref(ai, api.player_target, target);
        }
        if (manager && api.pm_ai_target >= 0) {
            pm = manager;
            host_ai_target = il2::read_field<Il2CppObject*>(pm, api.pm_ai_target);
            il2::store_ref(pm, api.pm_ai_target, target);
        }
        g_stand_in = who;
    }
    ~PlayerSwap() {
        g_stand_in = outer;
        if (ai && il2::read_field<Il2CppObject*>(ai, api.player_target) == friend_target)
            il2::store_ref(ai, api.player_target, host_target);
        if (pm && il2::read_field<Il2CppObject*>(pm, api.pm_ai_target) == friend_target)
            il2::store_ref(pm, api.pm_ai_target, host_ai_target);
    }
    PlayerSwap(const PlayerSwap&) = delete;
    PlayerSwap& operator=(const PlayerSwap&) = delete;
};

// A7 (Seamless Interiors): our own player inside a room (80 m above its building) is gone for the animals, the way a
// scene change takes the player from them in the game: out of sight and out of smell (TLD's detection measures every
// target through ComputeDistanceForTarget and CanSmellSource), and on_frame makes one already after us lose us. Solo
// and host alike (a friend's game only mirrors the host's animals; friends in rooms: forget_gone_friend, K7).
using DistanceFn = float (*)(Il2CppObject* self, const Vector3* eye, Il2CppObject* target, const MethodInfo* method);
using SmellFn = bool (*)(Il2CppObject* self, const Vector3* source, Il2CppObject* target, const MethodInfo* method);
DistanceFn g_distance_for = nullptr;
SmellFn g_can_smell = nullptr;
bool our_player_away(Il2CppObject* target) {
    if (!target || !seamless_interiors::player_inside()) return false;
    if (avatar::player_of(target)) return false;  // a friend's stand-in
    return target == view().host_target;
}
float distance_for_detour(Il2CppObject* self, const Vector3* eye, Il2CppObject* target, const MethodInfo* method) {
    if (our_player_away(target)) return 1.0e6f;
    return g_distance_for(self, eye, target, method);
}
bool can_smell_detour(Il2CppObject* self, const Vector3* source, Il2CppObject* target, const MethodInfo* method) {
    if (our_player_away(target)) return false;
    return g_can_smell(self, source, target, method);
}

// Animals after our player when they went into a room: they lose them (target cleared; one stalking, charging,
// holding ground or sniffing them out goes back to wandering), checked twice a second while we're inside.
void lose_player_inside() {
    static double next = 0;
    static int logged = 0;
    if (!seamless_interiors::player_inside()) {
        next = 0;
        return;
    }
    const double t = static_cast<double>(GetTickCount64());
    if (t < next || !api.ai_list || !api.clear_target || !api.set_mode) return;
    next = t + 500;
    Il2CppObject* mine = view().host_target;
    if (!mine || g_stand_in.go) return;
    Il2CppObject* list = nullptr;
    il2::il2cpp_field_static_get_value(api.ai_list, &list);
    if (!list) return;
    Il2CppObject* items = il2::read_field<Il2CppObject*>(list, 0x10);
    const int32_t count = il2::read_field<int32_t>(list, 0x18);
    for (int32_t i = 0; items && i < count && static_cast<size_t>(i) < il2::array_length(items); ++i) {
        Il2CppObject* ai = il2::array_object_at(items, static_cast<size_t>(i));
        if (!ai || il2::read_field<Il2CppObject*>(ai, api.current_target) != mine) continue;
        const int32_t mode = il2::read_field<int32_t>(ai, api.mode);
        if (mode == kAiModeDead || mode == kAiModeStruggle) continue;
        il2::invoke(api.clear_target, ai);
        constexpr int32_t kStalking = 13, kInvestigate = 8, kInvestigateSmell = 10, kPassingAttack = 25;
        if (mode == kAiModeAttack || mode == kStalking || mode == kAiModeHoldGround || mode == kInvestigate ||
            mode == kInvestigateSmell || mode == kPassingAttack) {
            int32_t wander = kAiModeWander;
            void* args[] = {&wander};
            il2::invoke(api.set_mode, ai, args);
        }
        g_hunts_friend.erase(reinterpret_cast<uintptr_t>(ai));
        if (logged++ < 40) logx::info("wildlife: a %s lost the player (they went into a Seamless Interiors room; mode was %d)", kind_of(ai), mode);
    }
}

template <typename Original>
void think(Il2CppObject* self, const MethodInfo* method, Original original) {
    Candidate who;
    const bool ours = runs(self);
    if (ours && !g_stand_in.animal) forget_gone_friend(self);
    if (g_stand_in.animal || !ours || view().shown.empty() || pick_player(self, who) == kLocal) {
        original(self, method);
        return;
    }
    {
        // m_PlayerTarget, and PlayerManager.m_AiTarget too: TLD points the animal at it in places (EnterHoldGround
        // sets m_CurrentTarget to it).
        Il2CppObject* pm = api.pm_ai_target >= 0 ? il2::invoke(api.player_manager, nullptr) : nullptr;
        PlayerSwap swap(self, pm, who.target, StandIn{who.go, who.transform, self, who.player}, true);
        original(self, method);
    }
    note_holding_ground(self, who.player);
}

void update_detour(Il2CppObject* self, const MethodInfo* method) { think(self, method, g_update); }
void late_update_detour(Il2CppObject* self, const MethodInfo* method) { think(self, method, g_late_update); }

Il2CppObject* get_player_object_detour(const MethodInfo* method) {
    return g_stand_in.go ? g_stand_in.go : g_get_player_object(method);
}

Il2CppObject* get_player_transform_detour(const MethodInfo* method) {
    return g_stand_in.transform ? g_stand_in.transform : g_get_player_transform(method);
}

double now_ms() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

void set_ai_enabled(Il2CppObject* ai, bool on) {
    void* args[] = {&on};
    il2::invoke(api.set_enabled, ai, args);
}

// struggle_pose: the paused animal onto the avatar (see struggle_pose()). TLD's own fight never moves the animal (its
// root stood still to 1 cm through a recorded 8 s fight), so this only has to hold while the avatar settles.
// warp: the first time, through BaseAi.SetPositionAndRotation (MoveAgent.Warp keeps its NavMeshAgent with it). Not
// every frame: each warp also lifts the model (MoveAgent.AdjustModelVerticalOnWarp), so repeated warps add up (it rose
// metres a frame in testing). After that the transform is simply held there.
bool pose_on_avatar(Il2CppObject* ai, bool warp, uint8_t player) {
    Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
    if (!g_pose.on || !api.set_pos_rot || !avatar::stand_in(player, go, transform, target)) return false;
    // Wolves only for now: the bear's, cougar's and moose's fight clips (and so where their root goes) aren't checked.
    if (il2::read_field<int32_t>(ai, api.kind) != kKindWolf) return false;
    Vector3 at{}, euler{};
    if (!position_of(transform, true, at) || !il2::invoke_value(api.get_euler, transform, euler)) return false;
    Pose p = struggle_pose(at, euler.y, g_pose);
    if (warp) {
        void* args[] = {&p.pos, &p.rot};
        il2::invoke(api.set_pos_rot, ai, args);
    } else if (Il2CppObject* tf = il2::invoke(api.component_transform, ai)) {
        void* pos_args[] = {&p.pos};
        void* rot_args[] = {&p.rot};
        il2::invoke(api.set_position, tf, pos_args);
        il2::invoke(api.set_rotation, tf, rot_args);
    }
    return true;
}

// TLD already started the animal's fight animation (MaybeApplyAttack: SetAiMode(Struggle) -> EnterStruggle ->
// SetTrigger(StruggleStart), before PlayerStruggle.Begin). Check once that its Animator really went there; a wolf
// that didn't gets the trigger again. Other kinds: logged only (their fight states aren't known yet).
void check_fight_states(Il2CppObject* ai, uint32_t id) {
    Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.animator);
    if (!animator || !api.state_info) return;
    int32_t layer = 0;
    void* args[] = {&layer};
    AnimatorStateInfo st{};
    if (!il2::invoke_value(api.state_info, animator, st, args)) return;
    const bool wolf = il2::read_field<int32_t>(ai, api.kind) == kKindWolf;
    bool fighting = false;
    for (int32_t h : kWolfFightStates) fighting |= st.full_path == h;
    logx::info("wildlife: the paused %s #%u plays state %d (t %.2f)%s", kind_of(ai), id, st.full_path,
               st.normalized_time, !wolf ? "" : fighting ? ", its fight" : ", NOT its fight: starting it again");
    if (wolf && !fighting && api.set_trigger && api.struggle_start >= 0) {
        int32_t trigger = il2::read_field<int32_t>(ai, api.struggle_start);
        void* targs[] = {&trigger};
        il2::invoke(api.set_trigger, animator, targs);
    }
}

// Where the paused animal is and what its Animator plays, while the other player's game runs the fight.
void trace_fight(Il2CppObject* ai, uint32_t id, double ms, uint8_t player) {
    Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
    Vector3 at{}, avatar_at{}, euler{};
    if (!ai || !position_of(ai, false, at)) return;
    const bool have_avatar = avatar::stand_in(player, go, transform, target) && position_of(transform, true, avatar_at);
    Il2CppObject* tf = il2::invoke(api.component_transform, ai);
    if (tf) il2::invoke_value(api.get_euler, tf, euler);
    AnimatorStateInfo st{};
    if (Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.animator)) {
        int32_t layer = 0;
        void* args[] = {&layer};
        il2::invoke_value(api.state_info, animator, st, args);
    }
    logx::info("wildlife: fight #%u +%.1f s: at (%.2f, %.2f, %.2f) yaw %.0f, %.2f m from the avatar, mode %d, state %d t %.2f",
               id, ms / 1000.0, at.x, at.y, at.z, euler.y, have_avatar ? distance(at, avatar_at) : -1.0f,
               il2::read_field<int32_t>(ai, api.mode), st.full_path, st.normalized_time);
}

net::AttackMsg attack_from(Il2CppObject* ai, net::AttackType type, uint8_t player) {
    net::AttackMsg a;
    a.player = player;
    a.animal = ai ? wildlife_sync::host_animal_id(ai) : 0;
    a.kind = ai ? static_cast<uint8_t>(il2::read_field<int32_t>(ai, api.kind)) : 0;
    a.type = type;
    return a;
}

// An animal hunting the other player jumped them: their game runs the fight; this one waits.
void struggle_begin_detour(Il2CppObject* self, Il2CppObject* partner, const MethodInfo* method) {
    Il2CppObject* ai = g_stand_in.animal;
    if (!ai) {
        g_struggle_begin(self, partner, method);
        return;
    }
    if (!hosting()) {
        // v45: a friend's game: no fight with another player's avatar here; the animal keeps after them (Attack) and
        // the host hands it to their game, where it jumps them for real
        static int held = 0;
        if (held++ < 20) logx::info("wildlife: a %s we run reached %s: it keeps after them until their game takes it",
                                    kind_of(ai), who_text(g_stand_in.player));
        int32_t mode = kAiModeAttack;
        void* args[] = {&mode};
        il2::invoke(api.set_mode, ai, args);
        return;
    }
    const net::AttackMsg attack = attack_from(ai, net::AttackType::Struggle, g_stand_in.player);
    if (!attack.animal || g_fights.count(attack.animal)) {
        logx::info("wildlife: a %s jumped the other player but isn't shared with them; it backs off", kind_of(ai));
        int32_t mode = kAiModeFlee;
        void* args[] = {&mode};
        il2::invoke(api.set_mode, ai, args);
        return;
    }
    netrt::send_attack(attack);
    const double now = now_ms();
    Fight& fight = g_fights[attack.animal];
    fight = {il2::il2cpp_gchandle_new(ai, false), now, attack.player};
    set_ai_enabled(ai, false);
    const bool posed = pose_on_avatar(ai, true, attack.player);
    if (posed) {
        fight.posed = true;
        if (Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.animator)) {
            il2::invoke_value(api.get_root_motion, animator, fight.root_motion);
            bool off = false;
            void* args[] = {&off};
            il2::invoke(api.set_root_motion, animator, args);
        }
    }
    logx::info("wildlife: the %s #%u jumped the other player; their game runs the fight, it %s (player %u)", kind_of(ai),
               attack.animal, posed ? "acts it out on their avatar" : "waits here", attack.player);
}

// Damage meant for a friend's stand-in goes to them; the host's own condition is never touched.
void target_damage_detour(Il2CppObject* self, float damage, int32_t source, Il2CppString* collider,
                          const MethodInfo* method) {
    if (const auto player = real_animals() ? avatar::player_of(self) : std::nullopt) {
        if (!hosting()) {  // v45: a friend's game: theirs to take, once their game runs the animal
            static int dropped = 0;
            if (dropped++ < 20)
                logx::info("wildlife: %.1f damage for another player (%u) from an animal we run: not dealt here (their "
                           "game gets the animal)", damage, *player);
            return;
        }
        net::AttackMsg a = attack_from(g_stand_in.animal, net::AttackType::Damage, *player);
        a.damage = damage;
        a.source = source;
        netrt::send_attack(a);
        logx::info("wildlife: %.1f damage (source %d) for the other player, sent to them (player %u)", damage, source,
                   *player);
        return;
    }
    g_target_damage(self, damage, source, collider, method);
}

// The animal's wound events (bleeding, sprains, clothing) while it deals with a friend: theirs, not ours.
Il2CppObject* wounds_detour(Il2CppObject* self, Il2CppObject* instigator, int32_t options, const MethodInfo* method) {
    std::optional<uint8_t> player;
    if (g_stand_in.animal && instigator == g_stand_in.animal) {
        player = g_stand_in.player;
    } else if (instigator && real_animals() && api.current_target >= 0) {
        // TLDCoop_DLC: outside its Update too (a cougar's passing attack bites on an animation event): an animal after a
        // friend wounds them, not the host.
        player = avatar::player_of(il2::read_field<Il2CppObject*>(instigator, api.current_target));
    }
    if (player && !hosting()) {  // v45: as for damage (a friend's game)
        static int dropped = 0;
        if (dropped++ < 20)
            logx::info("wildlife: wound events for another player (%u) from an animal we run: not dealt here", *player);
        return nullptr;
    }
    if (player) {
        net::AttackMsg a = attack_from(instigator, net::AttackType::Wounds, *player);
        a.options = options;
        netrt::send_attack(a);
        logx::info("wildlife: a %s's wound events for the other player, sent to them (player %u)", kind_of(instigator), *player);
        return nullptr;
    }
    return g_wounds(self, instigator, options, method);
}

void pin_on_friend(Il2CppObject* ai, Il2CppObject* target, uint8_t player) {
    il2::store_ref(ai, api.current_target, target);
    g_hunts_friend[reinterpret_cast<uintptr_t>(ai)] = player;
}

// Test helpers: the first friend shown here.
bool first_shown(uint8_t& player, Il2CppObject*& go, Il2CppObject*& transform, Il2CppObject*& target) {
    const std::vector<uint8_t> ids = avatar::shown();
    if (ids.empty()) return false;
    player = ids.front();
    return avatar::stand_in(player, go, transform, target);
}

}  // namespace

bool as_friends_doing(Il2CppObject* ai, uint8_t player, const std::function<void(const FriendStandIn&)>& action) {
    Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
    if (!ai || !real_animals() || !avatar::stand_in(player, go, transform, target)) return false;
    Il2CppObject* pm = il2::invoke(api.player_manager, nullptr);
    if (!pm || api.pm_ai_target < 0) return false;
    // PlayerManager.m_AiTarget: what TLD's reactions aim at (MaybeFleeOrAttackFromProjectileHit sets m_CurrentTarget
    // to it, and Flee runs from it). GetPlayerObject / GetPlayerTransform answer with the avatar meanwhile; all put
    // back however the action ends (PlayerSwap).
    FriendStandIn who{go, transform, target, {}, player};
    position_of(transform, true, reinterpret_cast<Vector3&>(who.position));
    {
        PlayerSwap swap(ai, pm, target, StandIn{go, transform, ai, player}, false);
        action(who);
    }
    if (il2::read_field<Il2CppObject*>(ai, api.current_target) == target) g_hunts_friend[reinterpret_cast<uintptr_t>(ai)] = player;
    return true;
}

bool test_attack(Il2CppObject* ai) {
    Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
    uint8_t player = 0;
    if (!ai || !hosting() || !first_shown(player, go, transform, target)) return false;
    pin_on_friend(ai, target, player);
    int32_t mode = kAiModeAttack;
    void* args[] = {&mode};
    il2::invoke(api.set_mode, ai, args);
    logx::info("wildlife: test: the %s goes for the other player", kind_of(ai));
    return true;
}

bool test_attack_host(Il2CppObject* ai) {
    // The host's AiTarget as PlayerManager has it (an animal's own m_PlayerTarget can still be unset).
    Il2CppObject* pm = ai && hosting() && api.pm_ai_target >= 0 ? il2::invoke(api.player_manager, nullptr) : nullptr;
    Il2CppObject* target = pm ? il2::read_field<Il2CppObject*>(pm, api.pm_ai_target) : nullptr;
    if (!target) return false;
    pin_on_friend(ai, target, kLocal);  // our own player
    int32_t mode = kAiModeAttack;
    void* args[] = {&mode};
    il2::invoke(api.set_mode, ai, args);
    logx::info("wildlife: test: the %s goes for the host", kind_of(ai));
    return true;
}

bool test_fight(Il2CppObject* ai) {
    Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
    uint8_t player = 0;
    if (!ai || !hosting() || !first_shown(player, go, transform, target)) return false;
    pin_on_friend(ai, target, player);
    // What BaseAi.MaybeApplyAttack does when it reaches its target: Struggle mode (EnterStruggle starts the fight
    // animation), then PlayerStruggle.Begin, which is refused for the other player and sent to their game.
    int32_t mode = kAiModeStruggle;
    void* args[] = {&mode};
    il2::invoke(api.set_mode, ai, args);
    logx::info("wildlife: test: the %s jumps the other player", kind_of(ai));
    const StandIn outer = g_stand_in;
    g_stand_in = {go, transform, ai, player};
    struggle_begin_detour(nullptr, go_of_component(ai), nullptr);
    g_stand_in = outer;
    return true;
}

bool init() {
    Il2CppClass* base_ai = il2::find_class(kGame, "", "BaseAi");
    Il2CppClass* game_manager = il2::find_class(kGame, "", "GameManager");
    Il2CppClass* struggle = il2::find_class(kGame, "", "PlayerStruggle");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    api.current_target = il2::field_offset(base_ai, "m_CurrentTarget");
    api.player_target = il2::field_offset(base_ai, "m_PlayerTarget");
    api.kind = il2::field_offset(base_ai, "m_AiSubType");
    api.mode = il2::field_offset(base_ai, "m_CurrentMode");
    api.set_mode = il2::find_method(base_ai, "SetAiMode", 1);
    api.clear_target = il2::find_method(base_ai, "ClearTarget", 0);
    api.hp = il2::field_offset(base_ai, "m_CurrentHP");
    api.wounded = il2::field_offset(base_ai, "m_Wounded");
    api.wounded_minutes = il2::field_offset(base_ai, "m_ElapsedWoundedMinutes");
    api.bleeding = il2::field_offset(base_ai, "m_BleedingOut");
    api.bleed_elapsed = il2::field_offset(base_ai, "m_ElapsedBleedingOutMinutes");
    api.bleed_limit = il2::field_offset(base_ai, "m_DeathAfterBleeingOutMinutes");
    api.damage_source = il2::field_offset(base_ai, "m_DamageSource");
    api.set_enabled = il2::find_method(il2::find_class(kCore, "UnityEngine", "Behaviour"), "set_enabled", 1);
    {
        const auto ini = paths::instance_dir() / L"coop.ini";
        wchar_t buf[32];
        auto read_float = [&](const wchar_t* key, float def) {
            GetPrivateProfileStringW(L"wildlife", key, L"", buf, 32, ini.c_str());
            return buf[0] ? static_cast<float>(_wtof(buf)) : def;
        };
        g_pose.on = GetPrivateProfileIntW(L"wildlife", L"struggle_pose", 1, ini.c_str()) != 0;
        g_pose.forward = read_float(L"pose_forward", g_pose.forward);
        g_pose.side = read_float(L"pose_side", g_pose.side);
        g_pose.turn = read_float(L"pose_turn", g_pose.turn);
        Il2CppClass* animator = il2::find_class(kAnim, "UnityEngine", "Animator");
        api.set_pos_rot = il2::find_method_params(base_ai, "SetPositionAndRotation",
                                                  {"UnityEngine.Vector3", "UnityEngine.Quaternion"});
        api.flee_from = il2::find_method_params(base_ai, "FleeFrom", {"UnityEngine.Vector3"});
        Il2CppClass* transform_class = il2::find_class(kCore, "UnityEngine", "Transform");
        api.get_euler = il2::find_method(transform_class, "get_eulerAngles", 0);
        api.set_position = il2::find_method(transform_class, "set_position", 1);
        api.set_rotation = il2::find_method(transform_class, "set_rotation", 1);
        api.state_info = il2::find_method_params(animator, "GetCurrentAnimatorStateInfo", {"System.Int32"});
        api.set_trigger = il2::find_method_params(animator, "SetTrigger", {"System.Int32"});
        api.get_root_motion = il2::find_method(animator, "get_applyRootMotion", 0);
        api.set_root_motion = il2::find_method(animator, "set_applyRootMotion", 1);
        api.animator = il2::field_offset(base_ai, "m_Animator");
        api.struggle_start = il2::field_offset(base_ai, "m_AnimParameter_StruggleStart");
        if (g_pose.on && !(api.set_pos_rot && api.get_euler)) {
            logx::warn("wildlife: struggle_pose is on but TLD's API for it is missing; animals wait where they landed");
            g_pose.on = false;
        }
        logx::info("wildlife: struggle_pose %s (forward %.2f m, side %.2f m, turn %.0f)", g_pose.on ? "ON" : "off",
                   g_pose.forward, g_pose.side, g_pose.turn);
    }
    Il2CppClass* ai_target = il2::find_class(kGame, "", "AiTarget");
    Il2CppClass* damage_table = il2::find_class(kGame, "", "DamageEventTable");
    const MethodInfo* target_damage = il2::find_method_params(
        ai_target, "ApplyDamage", {"System.Single", "DamageSource", "System.String"});
    const MethodInfo* wounds = il2::find_method(damage_table, "MaybeApplyAttack", 2);
    api.component_transform = il2::find_method(component, "get_transform", 0);
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.player_manager = il2::find_method(game_manager, "GetPlayerManagerComponent", 0);
    api.pm_ai_target = il2::field_offset(il2::find_class(kGame, "", "PlayerManager"), "m_AiTarget");
    api.get_position = il2::find_method(transform, "get_position", 0);
    const MethodInfo* update = il2::find_method(base_ai, "Update", 0);
    const MethodInfo* late_update = il2::find_method(base_ai, "LateUpdate", 0);
    const MethodInfo* player_object = il2::find_method(game_manager, "GetPlayerObject", 0);
    const MethodInfo* player_transform = il2::find_method(game_manager, "GetPlayerTransform", 0);
    const MethodInfo* begin = il2::find_method_params(struggle, "Begin", {"UnityEngine.GameObject"});
    const MethodInfo* holding_torch = il2::find_method(ai_target, "HoldingLitTorch", 0);
    const MethodInfo* holding_flare = il2::find_method(ai_target, "HoldingLitFlare", 1);
    const MethodInfo* holding_spear = il2::find_method(ai_target, "HoldingRaisedSpear", 0);
    const MethodInfo* eye_pos = il2::find_method(ai_target, "GetEyePos", 0);
    api.eye_offset = il2::field_offset(ai_target, "m_EyeOffset");
    if (Il2CppClass* ai_manager = il2::find_class(kGame, "", "BaseAiManager"))
        api.ai_list = il2::il2cpp_class_get_field_from_name(ai_manager, "m_BaseAis");
    const MethodInfo* distance_for =
        il2::find_method_params(base_ai, "ComputeDistanceForTarget", {"UnityEngine.Vector3", "AiTarget"});
    const MethodInfo* can_smell = il2::find_method_params(base_ai, "CanSmellSource", {"UnityEngine.Vector3", "AiTarget"});
    const bool ready = api.current_target >= 0 && api.player_target >= 0 && api.kind >= 0 && api.set_mode &&
                       api.component_transform && api.get_position && update && late_update && player_object &&
                       player_transform && begin && api.hp >= 0 && api.set_enabled && target_damage && wounds;
    if (!ready) {
        logx::warn("wildlife targets: unavailable (TLD API missing); animals won't notice the other player");
        return false;
    }
    auto hook = [](const char* name, const MethodInfo* m, auto* detour, auto** original) {
        return hooks::install_method(name, m, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original));
    };
    const bool ok = hook("BaseAi.Update", update, &update_detour, &g_update) &&
                    hook("BaseAi.LateUpdate", late_update, &late_update_detour, &g_late_update) &&
                    hook("GameManager.GetPlayerObject", player_object, &get_player_object_detour, &g_get_player_object) &&
                    hook("GameManager.GetPlayerTransform", player_transform, &get_player_transform_detour,
                         &g_get_player_transform) &&
                    hook("PlayerStruggle.Begin", begin, &struggle_begin_detour, &g_struggle_begin) &&
                    hook("AiTarget.ApplyDamage", target_damage, &target_damage_detour, &g_target_damage) &&
                    hook("DamageEventTable.MaybeApplyAttack", wounds, &wounds_detour, &g_wounds);
    // What an animal sees in a friend's hands (milestone 11), and from where: without these it fears the host's torch
    // instead, measured from the host.
    const bool hands = holding_torch && holding_flare && holding_spear && eye_pos &&
                       hook("AiTarget.GetEyePos", eye_pos, &eye_pos_detour, &g_eye_pos) &&
                       hook("AiTarget.HoldingLitTorch", holding_torch, &holding_lit_torch_detour, &g_holding_torch) &&
                       hook("AiTarget.HoldingLitFlare", holding_flare, &holding_lit_flare_detour, &g_holding_flare) &&
                       hook("AiTarget.HoldingRaisedSpear", holding_spear, &holding_raised_spear_detour,
                            &g_holding_spear);
    // A7: our player inside a Seamless Interiors room is out of the animals' reach (sight and smell).
    const bool rooms = distance_for && can_smell &&
                       hook("BaseAi.ComputeDistanceForTarget", distance_for, &distance_for_detour, &g_distance_for) &&
                       hook("BaseAi.CanSmellSource", can_smell, &can_smell_detour, &g_can_smell);
    if (!rooms) logx::warn("wildlife targets: animals may still sense a player inside a Seamless Interiors room (TLD API missing)");
    if (!hands) logx::warn("wildlife targets: animals can't see what's in a friend's hands (TLD API missing)");
    logx::info("wildlife targets: %s", ok ? "ready (animals can hunt the other player)" : "PARTLY hooked");
    return ok;
}

template <typename T>
void write_field(Il2CppObject* obj, ptrdiff_t offset, T value) {
    if (obj && offset >= 0) memcpy(reinterpret_cast<char*>(obj) + offset, &value, sizeof(T));
}

// What their fight did to the animal, written the way BaseAi.ApplyDamage would have (not called: on the host it would
// count the kill for the host's achievements and scale the damage for the aurora a second time).
void apply_fight_damage(Il2CppObject* ai, const net::AttackResultMsg& r) {
    if (r.damage > 0) {
        const float hp = il2::read_field<float>(ai, api.hp) - r.damage;
        write_field<float>(ai, api.hp, hp > 0 ? hp : 0);
        write_field<bool>(ai, api.wounded, true);
        write_field<float>(ai, api.wounded_minutes, 0);
        write_field<int32_t>(ai, api.damage_source, kDamageSourcePlayer);
    }
    if ((r.flags & net::kFightBleeding) && r.bleed_minutes > 0) {
        const float elapsed = il2::read_field<float>(ai, api.bleed_elapsed);
        if (!il2::read_field<bool>(ai, api.bleeding)) {
            write_field<bool>(ai, api.bleeding, true);
            write_field<float>(ai, api.bleed_elapsed, 0);
            write_field<float>(ai, api.bleed_limit, r.bleed_minutes);
        } else if (il2::read_field<float>(ai, api.bleed_limit) - elapsed > r.bleed_minutes) {
            write_field<float>(ai, api.bleed_limit, elapsed + r.bleed_minutes);  // the sooner of the two
        }
        write_field<int32_t>(ai, api.damage_source, kDamageSourcePlayer);
    }
}

const char* outcome_text(net::FightOutcome outcome) {
    switch (outcome) {
        case net::FightOutcome::Died: return "died";
        case net::FightOutcome::Fled: return "flees";
        case net::FightOutcome::WalkedOff: return "walks off (they were knocked out)";
        default: return "gives up and flees";
    }
}

// Their fight is over: the animal comes back to life here with what the fight did to it, and leaves the fight the way
// TLD's did there. Every way out goes through ExitStruggle (StruggleEnd trigger), so the Animator leaves the fight state
// for Locomotion.Flee / Wander, or (Dead set by EnterDead) for Struggle Death.
void finish_fight(uint32_t id, Fight& f, const net::AttackResultMsg* result) {
    Il2CppObject* ai = il2::il2cpp_gchandle_get_target(f.ai);
    if (ai) {
        set_ai_enabled(ai, true);
        if (f.posed) {
            if (Il2CppObject* animator = il2::read_field<Il2CppObject*>(ai, api.animator)) {
                bool was = f.root_motion;
                void* args[] = {&was};
                il2::invoke(api.set_root_motion, animator, args);
            }
        }
        net::FightOutcome outcome = result ? result->outcome : net::FightOutcome::NoFight;
        if (result) apply_fight_damage(ai, *result);
        if (il2::read_field<float>(ai, api.hp) <= 1e-4f)  // their fight, or someone's shots meanwhile, killed it
            outcome = net::FightOutcome::Died;
        if (outcome == net::FightOutcome::Died) write_field<float>(ai, api.hp, 0);  // dead there, dead here
        Il2CppObject *go = nullptr, *transform = nullptr, *target = nullptr;
        Vector3 from{};
        const bool flee = outcome == net::FightOutcome::Fled || outcome == net::FightOutcome::NoFight;
        if (flee && api.flee_from && avatar::stand_in(f.player, go, transform, target) && position_of(transform, true, from)) {
            void* args[] = {&from};  // away from the avatar, as TLD's own fight does from the player
            il2::invoke(api.flee_from, ai, args);
        } else {
            int32_t mode = outcome == net::FightOutcome::Died ? kAiModeDead
                           : outcome == net::FightOutcome::WalkedOff ? kAiModeWander
                                                                     : kAiModeFlee;
            void* args[] = {&mode};
            il2::invoke(api.set_mode, ai, args);
        }
        g_hunts_friend.erase(reinterpret_cast<uintptr_t>(ai));
        const bool bleeding = il2::read_field<bool>(ai, api.bleeding);
        logx::info("wildlife: the other player's fight with the %s #%u is over: it %s (hp %.2f, %.2f damage from the "
                   "fight%s)", kind_of(ai), id, outcome_text(outcome), il2::read_field<float>(ai, api.hp),
                   result ? result->damage : 0.0f,
                   bleeding ? ", bleeding out" : "");
        if (bleeding)
            logx::info("wildlife:   #%u bleeds out in %.0f game minutes", id,
                       il2::read_field<float>(ai, api.bleed_limit) - il2::read_field<float>(ai, api.bleed_elapsed));
    }
    il2::il2cpp_gchandle_free(f.ai);
}

void on_frame() {
    ++g_frame;  // TLDCoop_DLC: the players' view (view()) is worked out again
    lose_player_inside();  // A7
    // How their fights ended.
    std::vector<net::AttackResultMsg> results;
    netrt::drain_attack_results(results);
    for (const net::AttackResultMsg& r : results) {
        auto it = g_fights.find(r.animal);
        if (it == g_fights.end() || it->second.player != r.player) continue;  // not that friend's fight (any more)
        finish_fight(r.animal, it->second, &r);
        g_fights.erase(it);
    }
    const double now = now_ms();
    const std::vector<uint8_t> shown = avatar::shown();
    for (auto it = g_fights.begin(); it != g_fights.end();) {
        // TLDCoop_DLC: the friend it had down left the game (or their link dropped): the fight ends now, rather than the
        // bear standing over the empty spot for 45 s, deaf to shots ("it bites a player who left and ignores bullets").
        if (std::find(shown.begin(), shown.end(), it->second.player) == shown.end() &&
            now - it->second.started_ms > 1500) {
            logx::info("wildlife: the player in the fight with #%u is gone; it gives up", it->first);
            finish_fight(it->first, it->second, nullptr);
            it = g_fights.erase(it);
            continue;
        }
        // Shot dead by someone else while it had a friend down: it dies now (their game lets go when it hears).
        if (Il2CppObject* ai = il2::il2cpp_gchandle_get_target(it->second.ai); ai && il2::read_field<float>(ai, api.hp) <= 1e-4f) {
            logx::info("wildlife: #%u was killed during the other player's fight with it", it->first);
            finish_fight(it->first, it->second, nullptr);
            it = g_fights.erase(it);
            continue;
        }
        if (now - it->second.started_ms < kFightTimeoutMs && hosting()) {
            Fight& f = it->second;
            if (now - f.traced_ms >= 1000 && f.traces < 6) {  // the first seconds: where it is, what it plays
                f.traced_ms = now;
                ++f.traces;
                trace_fight(il2::il2cpp_gchandle_get_target(f.ai), it->first, now - f.started_ms, f.player);
            }
            if (f.posed || !f.states_checked) {
                if (Il2CppObject* ai = il2::il2cpp_gchandle_get_target(f.ai)) {
                    if (f.posed) pose_on_avatar(ai, false, f.player);
                    if (!f.states_checked && now - f.started_ms >= kPoseCheckMs) {
                        f.states_checked = true;
                        check_fight_states(ai, it->first);
                    }
                }
            }
            ++it;
            continue;
        }
        logx::info("wildlife: no word on the fight with #%u; it gives up", it->first);
        finish_fight(it->first, it->second, nullptr);
        it = g_fights.erase(it);
    }
    if (g_hunts_friend.size() > 512) g_hunts_friend.clear();
    {  // TLDCoop_DLC: switches long past hold nothing back any more
        const double t = static_cast<double>(GetTickCount64());
        for (auto it = g_last_switch.begin(); it != g_last_switch.end();)
            it = t - it->second >= kSwitchHoldMs ? g_last_switch.erase(it) : std::next(it);
    }
}

}  // namespace wildlife_targets
