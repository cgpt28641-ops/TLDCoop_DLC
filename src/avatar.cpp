#include "avatar.h"
#include "wildlife_sync.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "game_state.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/handover.h"
#include "net/held.h"
#include "net/held_pose.h"
#include "net/emotes.h"
#include "net/interp.h"
#include "paths.h"

namespace avatar {
namespace {

constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr const char* kPhysics = "UnityEngine.PhysicsModule.dll";
constexpr const char* kAnimation = "UnityEngine.AnimationModule.dll";
constexpr const char* kAssetBundles = "UnityEngine.AssetBundleModule.dll";

constexpr double kInterpolationDelayMs = 100;  // two 20 Hz updates: always have a pair to blend between
constexpr double kAliveCheckMs = 500;
constexpr double kStaleMs = 6000;  // disconnected or stalled peer: hide instead of freezing in place (TLDCoop_DLC: was 1 s;
                                   // the host's game stalls 1-2 s now and then, and everyone vanished for it)

// Gait selection from the peer's ground speed (m/s).
constexpr float kIdleBelow = 0.25f;
constexpr float kGaitHysteresis = 0.15f;
constexpr float kGaitFadeSeconds = 0.25f;

// Capsule shaped like a person: 1.8 m tall, 0.6 m wide. Unity's capsule is 2 m tall with its pivot in the middle.
constexpr float kCapsuleScaleXZ = 0.6f, kCapsuleScaleY = 0.9f;
constexpr float kCapsuleCentreHeight = 0.9f;  // the network position is at the feet

// The only bodies accepted from the network; anything else shows the capsule. Human characters only (the
// player's choice): the Episode 5 cast, built from the player's own install into TLDCoop\tldcoop_bodies.bundle by
// bodypack\build_bodypack.cmd (prefab "Body_<id>"). The Survival build itself has no living human model.
constexpr const char* kBodies[] = {
    "capsule",
    "mackenzie", "mackenzie_indoor", "astrid", "astrid_outdoor", "molly", "jane", "jane_outdoor",
    "jeremiah", "methuselah", "heller", "jace", "mathis", "terry", "atwood",
    "convict_c", "convict_e", "convict_f", "convict_g", "millworker", "patron1", "patron3", "patron4", "patron5",
};
// TLDCoop_DLC: the characters a player can pick (the co-op setup and the death screen): the ones whose own jacket
// and sweater are separate parts, so the worn clothes (clothespack) fit them cleanly. The rest stay accepted from the
// network, for older builds.
constexpr const char* kChoosableBodies[] = {"mackenzie_indoor", "astrid", "molly"};  // without outdoor hats and coats
constexpr const char* kBodyPackFile = "tldcoop_bodies.bundle";
constexpr const char* kFemaleBodies[] = {"astrid", "astrid_outdoor", "molly", "jane", "jane_outdoor"};

// The bodies' action clips (milestone 13): the animation library's animation-only bundles (bodypack\Actions, in
// TLDCoop\), one clip per rig (folder M or F), added to each body's Animation on layer 1 over the gait. Those that
// can be done on the move are added twice: whole (standing still) and from Spine1_jnt up ("_up": the legs walk or
// crouch under it).
constexpr const char* kActionBundles[] = {"tldcoop_eating.bundle", "tldcoop_sleeping.bundle", "tldcoop_handover.bundle"};
struct ActionClip {
    const char* name;   // on the body
    const char* asset;  // %s: the rig's folder (the body's own; the receive clips' from_m/from_f is the giver's)
    bool loop;          // else held at its last frame (ClampForever), its time set by us
    bool upper;         // also as "<name>_up"
};
constexpr ActionClip kActionClips[] = {
    {"eat", "Assets/Eating/%s/eat.anim", true, true},
    {"sleep", "Assets/Sleeping/%s/sleep.anim", true, false},
    {"give", "Assets/Giving/%s/give.anim", false, true},
    {"give_food", "Assets/Giving/%s/give_food.anim", false, true},
    {"give_wood", "Assets/Giving/%s/give_wood.anim", false, true},
    {"receive_from_m", "Assets/Receiving/%s/receive_from_m.anim", false, true},
    {"receive_from_f", "Assets/Receiving/%s/receive_from_f.anim", false, true},
    {"receive_food_from_m", "Assets/Receiving/%s/receive_food_from_m.anim", false, true},
    {"receive_food_from_f", "Assets/Receiving/%s/receive_food_from_f.anim", false, true},
    {"receive_wood_from_m", "Assets/Receiving/%s/receive_wood_from_m.anim", false, true},
    {"receive_wood_from_f", "Assets/Receiving/%s/receive_wood_from_f.anim", false, true},
};
constexpr int kActionLayer = 1;
constexpr int kWrapLoop = 2, kWrapClampForever = 8;  // UnityEngine.WrapMode
constexpr float kActionFade = 0.3f;
// The food in the eating hand (the library's food-socket.json): its middle here in RightHand_jnt, its length along
// the hand's -y (the bite end down there).
constexpr net::HandPose kEatGrip = {{-0.03f, -0.10f, 0.0f}, {0.0f, 0.0f, -0.70710678f, 0.70710678f}};
constexpr float kDeg = 3.14159265f / 180;

// Our bundle's fallback shaders and the TLD shaders to prefer when the game has them loaded (same property names).
struct ShaderSwap {
    const char* ours;
    const char* tld[3];
};
constexpr ShaderSwap kShaderSwaps[] = {
    {"TLDCoop/Body",
     {"_LongDark/Standard/Skinned (Advanced)", "Shader Forge/TLD_StandardSkinned (Characters)",
      "Shader Forge/TLD_StandardSkinned"}},
    {"TLDCoop/Body2Sided", {"Shader Forge/TLD_StandardSkinned_2Sided", nullptr, nullptr}},
    {"TLDCoop/BodyTransparent", {"Shader Forge/TLD_StandardSkinned_Transparent", nullptr, nullptr}},
};

bool known_body(const std::string& id) {
    for (const char* b : kBodies)
        if (id == b) return true;
    return false;
}

struct Vector3 {
    float x, y, z;
};
struct Color {
    float r, g, b, a;
};

struct Api {
    Il2CppClass* game_object_class = nullptr;
    const MethodInfo* go_ctor = nullptr;            // GameObject(string name)
    const MethodInfo* create_primitive = nullptr;   // static GameObject.CreatePrimitive(PrimitiveType)
    const MethodInfo* set_active = nullptr;         // GameObject.SetActive(bool)
    const MethodInfo* go_transform = nullptr;       // GameObject.get_transform()
    const MethodInfo* get_component = nullptr;      // GameObject.GetComponent(Type)
    const MethodInfo* get_components = nullptr;     // GameObject.GetComponentsInChildren(Type, bool)
    const MethodInfo* instantiate = nullptr;        // static Object.Instantiate(Object, Transform)
    const MethodInfo* destroy = nullptr;            // static Object.Destroy(Object)
    const MethodInfo* destroy_now = nullptr;        // static Object.DestroyImmediate(Object)
    const MethodInfo* alive = nullptr;              // static Object.op_Implicit(Object): false once destroyed
    const MethodInfo* set_name = nullptr;           // Object.set_name(string)
    const MethodInfo* object_name = nullptr;        // Object.get_name()
    const MethodInfo* keep_on_load = nullptr;       // static Object.DontDestroyOnLoad(Object)
    const MethodInfo* set_position = nullptr;       // Transform.set_position(Vector3)
    const MethodInfo* set_euler = nullptr;          // Transform.set_eulerAngles(Vector3)
    const MethodInfo* set_local_scale = nullptr;
    const MethodInfo* set_local_position = nullptr;
    const MethodInfo* set_local_euler = nullptr;
    const MethodInfo* set_parent = nullptr;         // Transform.SetParent(Transform, bool)
    const MethodInfo* get_material = nullptr;       // Renderer.get_material()
    const MethodInfo* shared_materials = nullptr;   // Renderer.get_sharedMaterials()
    const MethodInfo* set_color = nullptr;          // Material.set_color(Color)
    const MethodInfo* get_shader = nullptr;         // Material.get_shader()
    const MethodInfo* set_shader = nullptr;         // Material.set_shader(Shader)
    const MethodInfo* find_shader = nullptr;        // static Shader.Find(string)
    const MethodInfo* load_bundle = nullptr;        // static AssetBundle.LoadFromFile(string)
    const MethodInfo* load_asset = nullptr;         // AssetBundle.LoadAsset(string, Type)
    const MethodInfo* crossfade = nullptr;          // Animation.CrossFade(string, float)
    const MethodInfo* anim_state = nullptr;         // Animation.get_Item(string)
    const MethodInfo* state_speed = nullptr;        // AnimationState.set_speed(float)
    const MethodInfo* state_get_speed = nullptr;    // AnimationState.get_speed()
    const MethodInfo* state_get_time = nullptr;     // AnimationState.get_time()
    const MethodInfo* state_set_time = nullptr;     // AnimationState.set_time(float)
    const MethodInfo* anim_play = nullptr;          // Animation.Play(string)
    const MethodInfo* anim_sample = nullptr;        // Animation.Sample()
    const MethodInfo* anim_is_playing = nullptr;    // Animation.get_isPlaying()
    const MethodInfo* time_scale = nullptr;         // static Time.get_timeScale()
    // The action clips (milestone 13).
    const MethodInfo* add_clip = nullptr;           // Animation.AddClip(AnimationClip, string)
    const MethodInfo* blend = nullptr;              // Animation.Blend(string, float, float)
    const MethodInfo* state_layer = nullptr;        // AnimationState.set_layer(int)
    const MethodInfo* state_wrap = nullptr;         // AnimationState.set_wrapMode(WrapMode)
    const MethodInfo* state_mix = nullptr;          // AnimationState.AddMixingTransform(Transform, bool)
    const MethodInfo* anim_playing_named = nullptr; // Animation.IsPlaying(string) (dev `pose`)
    Il2CppObject* clip_type = nullptr;              // AnimationClip
    Il2CppObject* collider_type = nullptr;
    Il2CppObject* renderer_type = nullptr;
    Il2CppObject* component_type = nullptr;
    Il2CppObject* animation_type = nullptr;
    Il2CppObject* transform_type = nullptr;
    Il2CppObject* game_object_type = nullptr;
    int capsule = 1;
    int cube = 3;
    const MethodInfo* add_component = nullptr;      // GameObject.AddComponent(Type)
    Il2CppObject* ai_target_type = nullptr;          // AiTarget
    ptrdiff_t ai_target_eye = -1;                    // AiTarget.m_EyeOffset
    // What they hold (milestone 11).
    const MethodInfo* load_gear = nullptr;           // static GearItem.LoadGearItemPrefab(string) [2.55]
    const MethodInfo* instantiate_gear = nullptr;    // static GearItem.InstantiateGearItem(string) [2.58]
    Il2CppObject* gear_type = nullptr;               // GearItem
    const MethodInfo* get_position = nullptr;        // Transform.get_position()
    const MethodInfo* get_rotation = nullptr;        // Transform.get_rotation()
    const MethodInfo* set_rotation = nullptr;        // Transform.set_rotation(Quaternion)
    const MethodInfo* get_forward = nullptr;         // Transform.get_forward()
    const MethodInfo* find_child = nullptr;          // Transform.Find(string)
    const MethodInfo* renderer_bounds = nullptr;     // Renderer.get_bounds()
    const MethodInfo* tf_parent = nullptr;           // Transform.get_parent()
    const MethodInfo* renderer_enabled = nullptr;    // Renderer.get_enabled()
    const MethodInfo* renderer_set_enabled = nullptr;  // Renderer.set_enabled(bool)
    const MethodInfo* set_enabled = nullptr;         // Behaviour.set_enabled(bool)
    const MethodInfo* active_and_enabled = nullptr;  // Behaviour.get_isActiveAndEnabled()
    const MethodInfo* in_hierarchy = nullptr;        // GameObject.get_activeInHierarchy()
    const MethodInfo* light_intensity = nullptr;     // Light.get_intensity()
    const MethodInfo* light_range = nullptr;         // Light.get_range()
    const MethodInfo* light_color = nullptr;         // Light.get_color()
    const MethodInfo* light_shadows = nullptr;       // Light.get_shadows()
    const MethodInfo* light_get_mode = nullptr, *light_set_mode = nullptr;  // Light.get/set_renderMode
    const MethodInfo* light_get_mask = nullptr;      // Light.get_cullingMask()
    Il2CppObject* light_type = nullptr;              // Light
} api;

// The parts of TLD's light-giving items: what is shown while lit, and its lights (one for indoors, one for outdoors,
// as TLD switches them). The copy keeps them; its scripts go.
struct Parts {
    ptrdiff_t torch = -1, flare = -1, lamp = -1, flashlight = -1;  // GearItem.m_TorchItem, m_FlareItem, ...
    ptrdiff_t torch_fx = -1, torch_in = -1, torch_out = -1, torch_burnt = -1;  // TorchItem
    ptrdiff_t flare_fx = -1, flare_in = -1, flare_out = -1, flare_cap = -1;    // FlareItem (cap: off while lit)
    ptrdiff_t lamp_fx = -1, lamp_in = -1, lamp_core = -1, lamp_out = -1;      // KeroseneLampItem
    ptrdiff_t fl_fx_low = -1, fl_fx_high = -1, fl_in = -1, fl_out = -1, fl_in_high = -1, fl_out_high = -1;  // Flashlight
    // A torch's flame strength (TorchIntensity): curves of the flame's intensity giving each light's flicker range
    // (LightRandomIntensity min / max), which the script sets every frame.
    Il2CppObject* torch_intensity_type = nullptr;
    ptrdiff_t ti_in_min = -1, ti_in_max = -1, ti_out_min = -1, ti_out_max = -1;  // m_IndoorIntensityMin, ...
    ptrdiff_t ti_in_light = -1, ti_out_light = -1;                                // m_IndoorLight, m_OutdoorLight
    Il2CppObject* random_intensity_type = nullptr;                                 // LightRandomIntensity
    const MethodInfo* set_min_max = nullptr;      // LightRandomIntensity.SetMinMax(float, float)
    const MethodInfo* curve_evaluate = nullptr;   // AnimationCurve.Evaluate(float)
    // TLDCoop_DLC: a flare's flame (FlareIntensity, on its FX object): the same curves for its lights, and its
    // particles' rates, which TLD's FlareIntensityManager sets every frame (without it: no light, no flame).
    Il2CppObject* flare_intensity_type = nullptr;
    ptrdiff_t fi_in_min = -1, fi_in_max = -1, fi_out_min = -1, fi_out_max = -1;
    ptrdiff_t fi_in_light = -1, fi_out_light = -1;
    ptrdiff_t fi_smoke = -1, fi_core = -1, fi_spark = -1;               // GameObjects
    ptrdiff_t fi_smoke_rate = -1, fi_core_rate = -1, fi_spark_rate = -1;  // curves: particles a second
    Il2CppObject* particles_type = nullptr;       // ParticleSystem
    const MethodInfo* ps_emit = nullptr;          // ParticleSystem.Emit(int)
    const MethodInfo* ps_play = nullptr;          // ParticleSystem.Play()
    const MethodInfo* ps_stop = nullptr;          // ParticleSystem.Stop() (TLDCoop_DLC: unlit, its FX may stay shown)
    // TLDCoop_DLC: a light of our own for what lights up without one in its model (a match)
    const MethodInfo* light_range = nullptr;      // Light.set_range(float)
    Il2CppObject* light_tracking_type = nullptr;  // LightTracking (TLD's lighting knows the light)
} parts;

bool g_ready = false;

// The character preview's Unity calls (see preview()).
struct PreviewApi {
    Il2CppClass* render_texture = nullptr;
    const MethodInfo* rt_ctor = nullptr;          // RenderTexture(int width, int height, int depth)
    const MethodInfo* rt_antialiasing = nullptr;  // RenderTexture.set_antiAliasing(int)
    Il2CppObject* camera_type = nullptr;
    Il2CppObject* light_type = nullptr;
    const MethodInfo* camera_target = nullptr;      // Camera.set_targetTexture(RenderTexture)
    const MethodInfo* camera_clear = nullptr;       // Camera.set_clearFlags(CameraClearFlags)
    const MethodInfo* camera_background = nullptr;  // Camera.set_backgroundColor(Color)
    const MethodInfo* camera_mask = nullptr;        // Camera.set_cullingMask(int)
    const MethodInfo* camera_fov = nullptr;         // Camera.set_fieldOfView(float)
    const MethodInfo* camera_near = nullptr;        // Camera.set_nearClipPlane(float)
    const MethodInfo* camera_far = nullptr;         // Camera.set_farClipPlane(float)
    const MethodInfo* camera_path = nullptr;        // Camera.set_renderingPath(RenderingPath)
    const MethodInfo* camera_hdr = nullptr;         // Camera.set_allowHDR(bool)
    const MethodInfo* light_kind = nullptr;         // Light.set_type(LightType)
    const MethodInfo* light_intensity = nullptr;    // Light.set_intensity(float)
    const MethodInfo* light_color = nullptr;        // Light.set_color(Color)
    const MethodInfo* light_mask = nullptr;         // Light.set_cullingMask(int)
    const MethodInfo* go_layer = nullptr;           // GameObject.set_layer(int)
    const MethodInfo* component_go = nullptr;       // Component.get_gameObject()
    const MethodInfo* layer_name = nullptr;         // static LayerMask.LayerToName(int)
    bool ready = false;
} pv;

// The preview's stage: everything under one root far below the menu scene, on a layer of its own.
constexpr Vector3 kPreviewSpot = {0, -2000, 0};
constexpr int kPreviewWidth = 640, kPreviewHeight = 1024;  // texture pixels (the screen shows it 400x640 units)
constexpr float kPreviewFov = 24, kPreviewDistance = 4.6f, kPreviewEye = 0.92f;  // the whole body, head to boots
constexpr float kPreviewYaw = 20;  // three-quarter view
constexpr int kCameraClearSolid = 2, kRenderForward = 1, kLightDirectional = 1;
struct Preview {
    uintptr_t root = 0, root_transform = 0;
    uintptr_t texture = 0;  // RenderTexture
    uintptr_t body = 0;
    std::string shown, failed;
    int layer = -1;
} g_preview;

// The body's legacy Animation component: states "idle", "walk", "run" (see bodypack/Editor/BuildBodies.cs).
// Mecanim Animators flip and fling the ripped NPC clips; the legacy component plays them correctly.
struct Gait {
    uintptr_t animation = 0;
    uintptr_t walk_state = 0, run_state = 0;
    bool has_down = false;  // the body pack's "down" clip: struggling on their back, for fights with animals
    float walk_speed = 1.5f, run_speed = 4.0f;  // m/s the clips cover, from the "TLDCoopGait" marker
    const char* current = "idle";
    const char* paused_clip = nullptr;  // the clip we play ourselves while game time is stopped
};

// What they hold (milestone 11): a copy of the gear's own model (GearItem.LoadGearItemPrefab), stripped to what is
// drawn and its lights, under the avatar's root and put at their hand every frame after the clip (late_update).
struct HeldCopy {
    std::string gear;  // what it's a copy of ("" none)
    net::HeldKind kind = net::HeldKind::None;
    uintptr_t go = 0, transform = 0;
    std::vector<uintptr_t> fx_low, fx_high;            // GameObjects shown while lit (high: a flashlight's high beam)
    std::vector<uintptr_t> lights_in, lights_out;      // Light components for indoors / outdoors
    std::vector<uintptr_t> lights_in_high, lights_out_high;
    std::vector<uintptr_t> hide_lit;                   // GameObjects hidden while lit (a flare's cap)
    std::vector<uintptr_t> hide_always;                // a torch's burnt-out model
    std::string flicker;                               // for the log: a torch's lights' strength ranges as set
    // TLDCoop_DLC: a flare's particles, emitted by us while lit (particles a second; what's left of one)
    struct Emitter {
        uintptr_t ps = 0;
        float rate = 0, carry = 0;
    };
    std::vector<Emitter> emitters;
    double emitted_ms = 0;
    bool own_light = false;  // its lights are ours (made for a match)
    // Its shape in its own space (measured once it is awake, unturned): bounds centre from its pivot, its long axis
    // (towards its front or top), the axis for its roll, half its length.
    bool measured = false;
    net::pose::Vec3 centre, axis{0, 0, 1}, second{0, 1, 0};
    float half = 0.1f;
    int lit_shown = -1;  // what is on: -1 not set yet, 0 nothing, 1 lit, 2 high beam
    bool indoors_shown = false;
    double lit_at = 0;   // TLDCoop_DLC: when it was lit (its lights are looked at in the log a moment later)
    // TLDCoop_DLC: how often each light was found switched off while lit, and how far its scripts were taken away
    std::unordered_map<uintptr_t, int> relit;
    int scripts_stripped = 0;  // 0 none, 1 the light's own object's, 2 every script of the copy
};

// The bones of the body pack's rig (TLD's generic NPC rig) we pose: the arms (shoulder joint, elbow, wrist), and for
// crouching the hips, the spine's base and the legs (thigh joint, knee, ankle).
enum Bone {
    kRightUpArm, kRightElbow, kRightHand, kLeftUpArm, kLeftElbow, kLeftHand,
    kHips, kSpine1, kLeftThigh, kLeftKnee, kLeftFoot, kRightThigh, kRightKnee, kRightFoot,
    kHead,  // TLDCoop_DLC: nodding and shaking it (gestures)
    kBoneCount
};
constexpr int kArmBones = kLeftHand + 1;
constexpr const char* kBoneNames[kBoneCount] = {"RightUpArm_jnt", "RightElbow_jnt", "RightHand_jnt",
                                                "LeftUpArm_jnt",  "LeftElbow_jnt",  "LeftHand_jnt",
                                                "Hips_jnt",       "Spine1_jnt",     "LeftHip_jnt",
                                                "LeftKnee_jnt",   "LeftFoot_jnt",   "RightHip_jnt",
                                                "RightKnee_jnt",  "RightFoot_jnt",  "Head_jnt"};

// One other player as seen here. Everything of it lives under one root that survives scene loads; the root is only
// hidden while they're elsewhere.
struct Avatar {
    uint8_t player = 0;
    uint32_t link = 0;  // which connection of theirs it shows (PlayerInfo.link): another one is someone new
    std::string name;  // for the log
    net::Interpolator interp{kInterpolationDelayMs};
    uintptr_t root = 0, root_transform = 0;
    uintptr_t visual = 0;  // the capsule or the current body
    Gait gait;
    std::string wanted_body = "capsule";
    std::string shown_body;   // what `visual` currently is
    std::string failed_body;  // don't retry a body that failed until they pick another
    bool shown = false;
    std::string status = "no data yet";
    std::string visibility;   // last logged reason for hiding, or "shown"
    bool shown_down = false;  // shown knocked down (in a fight)
    bool on_wolf = false;     // TLDCoop_DLC: and moved under the host's wolf it fights
    float ground_y = 0;       // TLDCoop_DLC: their feet's height when last on their feet (a fight lifts TLD's player)
    // TLDCoop_DLC: the gesture they make (net/emotes.h), from when (our clock), and which of theirs it is
    net::Emote emote = net::Emote::None;
    double emote_start = 0;
    uint8_t emote_seq = 0;
    bool has_ground = false;
    uintptr_t ai_target = 0;  // AiTarget on the root: what the host's animals see, hear and hunt (milestone 7c)
    Vector3 last_pos{};
    double last_pos_time = 0;
    float speed = 0;
    HeldCopy held;
    std::string held_failed;  // a gear that couldn't be made here: not tried again until they hold another
    uint8_t held_flags = 0;
    std::string held_logged;  // what the log last said they hold
    float yaw = 0, pitch = 0;  // their facing and where they look, for the late pass
    float shown_yaw = 0;       // the way the body is turned (a handover turns it to the partner)
    std::array<uintptr_t, kBoneCount> bones{};  // the body's bones we pose (none on the capsule)
    // TLDCoop_DLC: the rest of the rig we pose, by name: fingers, shoulders (clavicles), neck, Spine2 (gestures, grips)
    std::unordered_map<std::string, uintptr_t> rig;
    bool fresh = false;        // their latest state is recent (they're still sending)
    net::StateMsg last;        // that state, for the HUD (seen())
    // What they do (milestone 13): the action clips on this body, the one shown, how much of the body it has.
    int actions = 0;           // action states added to this body (0: none, the capsule or no bundles)
    bool female = false;       // the body's rig
    std::string action;        // the action clip shown ("" none)
    bool action_upper = false; // it has an "_up" state
    float lower = 0;           // 0: the action has the whole body; 1: only from the spine up (they walk or crouch)
    float crouch = 0;          // how far down they are (kStateCrouched, eased in and out)
    // TLDCoop_DLC: how much higher TLD says they are once crouched than where they stood just before (their crouched
    // body floated over the ground with its feet in the air); taken off while crouched.
    bool crouch_seen = false;
    float crouch_rise = 0, stand_x = 0, stand_z = 0;
    bool lying = false;        // in bed: the sleep clip, at the bed
    bool handover_on = false;  // coop_give: a handover this body shows (`handover`)
    HandoverShow handover;
    float handover_blend = 0;  // 0: where they are; 1: at their place for the handover
    net::Spot spot;            // that place (kept while they ease back)
    float clip_hand[3] = {};   // dev `pose`: the right hand as the clips left it (from the root: right, up, forward)
};
std::map<uint8_t, std::unique_ptr<Avatar>> g_avatars;  // by player id (host: the friends; a friend: the host, others)
std::array<bool, net::kMaxPlayers> g_in_game{};  // the ids set_players last listed: states of anyone else are late

uintptr_t g_loader = 0, g_loader_transform = 0;  // inactive parent: new bodies are prepared under it
uintptr_t g_pack = 0;  // the loaded AssetBundle
bool g_pack_tried = false;
std::vector<uintptr_t> g_action_bundles;           // the loaded action clip bundles
bool g_actions_tried = false;
std::map<std::string, uintptr_t> g_action_clips;  // asset path -> clip (0: in none of them)
// Off by default: TLD's Shader Forge skinned shader drew the story characters invisible (it likely alpha-tests
// _MainTex, whose alpha is empty in these textures). Our own shaders are standard lit and ignore alpha.
bool g_use_tld_shaders = false;
std::map<std::string, uintptr_t> g_tld_shader;  // our fallback shader name -> TLD shader to use (0 = keep ours)

double g_last_alive_check = 0;
std::string g_last_local_scene;

// --- small wrappers ------------------------------------------------------------------------------------------------

Il2CppObject* target(uintptr_t handle) { return handle ? il2::il2cpp_gchandle_get_target(handle) : nullptr; }

uintptr_t hold(Il2CppObject* obj) { return obj ? il2::il2cpp_gchandle_new(obj, false) : 0; }

void drop(uintptr_t& handle) {
    if (handle) il2::il2cpp_gchandle_free(handle);
    handle = 0;
}

bool is_alive(Il2CppObject* obj) {
    if (!obj) return false;
    bool alive = false;
    void* args[] = {obj};
    return il2::invoke_value(api.alive, nullptr, alive, args) && alive;
}

std::string name_of(Il2CppObject* obj) {
    return obj ? il2::to_utf8(reinterpret_cast<Il2CppString*>(il2::invoke(api.object_name, obj))) : "<none>";
}

Il2CppObject* transform_of(Il2CppObject* go) { return go ? il2::invoke(api.go_transform, go) : nullptr; }

void set_vec(const MethodInfo* setter, Il2CppObject* transform, Vector3 v) {
    void* args[] = {&v};
    il2::invoke(setter, transform, args);
}

void set_parent(Il2CppObject* child_transform, Il2CppObject* parent_transform) {
    bool keep_world = false;
    void* args[] = {parent_transform, &keep_world};
    il2::invoke(api.set_parent, child_transform, args);
}

void set_active(Il2CppObject* go, bool on) {
    void* args[] = {&on};
    il2::invoke(api.set_active, go, args);
}

void destroy(Il2CppObject* obj, bool now) {
    void* args[] = {obj};
    il2::invoke(now ? api.destroy_now : api.destroy, nullptr, args);
}

Il2CppObject* new_game_object(const char* name) {
    Il2CppObject* go = il2::il2cpp_object_new(api.game_object_class);
    void* args[] = {il2::il2cpp_string_new(name)};
    il2::invoke(api.go_ctor, go, args);
    return go;
}

Il2CppObject* components_in_children(Il2CppObject* go, Il2CppObject* type) {
    bool include_inactive = true;
    void* args[] = {type, &include_inactive};
    return il2::invoke(api.get_components, go, args);
}

void tint(Il2CppObject* go, Color c) {
    void* type_arg[] = {api.renderer_type};
    Il2CppObject* renderer = il2::invoke(api.get_component, go, type_arg);
    if (!renderer) return;
    Il2CppObject* material = il2::invoke(api.get_material, renderer);
    if (!material) return;
    void* args[] = {&c};
    il2::invoke(api.set_color, material, args);
}

// "avatar: ... (whose)" for the log.
void say(const Avatar& a, const char* fmt, ...) {
    char text[900];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    logx::info("avatar: %s (player %u%s%s%s)", text, a.player, a.name.empty() ? "" : ", '", a.name.c_str(),
               a.name.empty() ? "" : "'");
}

// --- the capsule (placeholder and fallback) ------------------------------------------------------------------------

Il2CppObject* make_primitive(int type, const char* name, Color color) {
    void* args[] = {&type};
    Il2CppObject* go = il2::invoke(api.create_primitive, nullptr, args);
    if (!go) return nullptr;
    void* name_arg[] = {il2::il2cpp_string_new(name)};
    il2::invoke(api.set_name, go, name_arg);
    void* type_arg[] = {api.collider_type};
    if (Il2CppObject* collider = il2::invoke(api.get_component, go, type_arg)) destroy(collider, false);
    tint(go, color);
    return go;
}

// One colour per player, so three friends' capsules can be told apart.
Color capsule_color(uint8_t player) {
    constexpr Color kColors[] = {{1.0f, 0.45f, 0.1f, 1.0f}, {0.15f, 0.55f, 1.0f, 1.0f}, {0.3f, 0.85f, 0.3f, 1.0f},
                                 {0.85f, 0.25f, 0.85f, 1.0f}};
    return kColors[player % (sizeof(kColors) / sizeof(kColors[0]))];
}

Il2CppObject* make_capsule(Avatar& a) {
    Il2CppObject* body = make_primitive(api.capsule, "TLDCoop_Capsule", capsule_color(a.player));
    Il2CppObject* body_transform = transform_of(body);
    if (!body_transform) return nullptr;
    set_parent(body_transform, target(a.root_transform));
    set_vec(api.set_local_position, body_transform, {0.0f, kCapsuleCentreHeight, 0.0f});
    set_vec(api.set_local_scale, body_transform, {kCapsuleScaleXZ, kCapsuleScaleY, kCapsuleScaleXZ});
    // A dark "visor" at head height on the front, so you can see which way they face.
    if (Il2CppObject* visor = make_primitive(api.cube, "TLDCoop_Visor", {0.1f, 0.1f, 0.1f, 1.0f})) {
        Il2CppObject* visor_transform = transform_of(visor);
        set_parent(visor_transform, body_transform);
        set_vec(api.set_local_position, visor_transform, {0.0f, 0.7f, 0.45f});
        set_vec(api.set_local_scale, visor_transform, {0.6f, 0.15f, 0.5f});
    }
    return body;
}

// --- bodies from the body pack -------------------------------------------------------------------------------------

bool keep_component(const std::string& cls) {
    return cls == "UnityEngine.Transform" || cls == "UnityEngine.Animation" || cls == "UnityEngine.Animator" ||
           cls == "UnityEngine.SkinnedMeshRenderer" || cls == "UnityEngine.MeshRenderer" ||
           cls == "UnityEngine.MeshFilter" || cls == "UnityEngine.LODGroup";
}

// Safety net: the pack should contain only visuals, but anything else (scripts, physics, audio) is removed while
// the body is still asleep under the inactive loader.
void strip(Il2CppObject* go) {
    std::map<std::string, int> removed;
    for (int pass = 0; pass < 3; ++pass) {
        Il2CppObject* components = components_in_children(go, api.component_type);
        for (size_t i = 0, n = il2::array_length(components); i < n; ++i) {
            Il2CppObject* c = il2::array_object_at(components, i);
            const std::string cls = il2::class_name_of(c);
            if (keep_component(cls)) continue;
            if (pass == 0 && cls.rfind("UnityEngine.", 0) == 0) continue;  // game scripts first
            destroy(c, true);
            ++removed[cls];
        }
    }
    if (removed.empty()) return;
    std::string summary;
    for (const auto& [cls, n] : removed) summary += cls + " x" + std::to_string(n) + ", ";
    // The body pack is built to contain only visuals and animation, so anything removed here is a bug.
    logx::warn("avatar: stripped %s(unexpected for a body-pack body)", summary.c_str());
}

Il2CppObject* tld_shader_for(const std::string& ours) {
    auto cached = g_tld_shader.find(ours);
    if (cached != g_tld_shader.end()) return target(cached->second);
    uintptr_t chosen = 0;
    for (const auto& swap : kShaderSwaps) {
        if (ours != swap.ours) continue;
        for (const char* candidate : swap.tld) {
            if (!candidate) break;
            void* args[] = {il2::il2cpp_string_new(candidate)};
            if (Il2CppObject* shader = il2::invoke(api.find_shader, nullptr, args)) {
                chosen = hold(shader);
                logx::info("avatar: shader %s -> TLD's '%s'", ours.c_str(), candidate);
                break;
            }
        }
        if (!chosen) logx::info("avatar: shader %s: no TLD equivalent loaded; keeping ours", ours.c_str());
    }
    g_tld_shader[ours] = chosen;
    return target(chosen);
}

// Use TLD's own skinned shaders when the game has them loaded, so the characters get its lighting and fog.
void adopt_tld_shaders(Il2CppObject* go) {
    Il2CppObject* renderers = components_in_children(go, api.renderer_type);
    for (size_t r = 0, nr = il2::array_length(renderers); r < nr; ++r) {
        Il2CppObject* materials = il2::invoke(api.shared_materials, il2::array_object_at(renderers, r));
        for (size_t m = 0, nm = il2::array_length(materials); m < nm; ++m) {
            Il2CppObject* material = il2::array_object_at(materials, m);
            if (!material) continue;
            const std::string current = name_of(il2::invoke(api.get_shader, material));
            if (current.rfind("TLDCoop/", 0) != 0) continue;  // already swapped (shared material) or not ours
            if (Il2CppObject* tld = tld_shader_for(current)) {
                void* args[] = {tld};
                il2::invoke(api.set_shader, material, args);
            }
        }
    }
}

Il2CppObject* body_pack() {
    if (g_pack || g_pack_tried) return target(g_pack);
    g_pack_tried = true;
    const auto file = paths::mod_dir() / kBodyPackFile;
    if (!std::filesystem::exists(file)) {
        logx::warn("avatar: %ls not found; story bodies unavailable (build it with bodypack\\build_bodypack.cmd)",
                   file.c_str());
        return nullptr;
    }
    const std::u8string u8 = file.u8string();
    const std::string utf8(reinterpret_cast<const char*>(u8.c_str()), u8.size());
    void* args[] = {il2::il2cpp_string_new(utf8.c_str())};
    Il2CppObject* pack = il2::invoke(api.load_bundle, nullptr, args);
    if (!pack) {
        logx::error("avatar: could not load %ls", file.c_str());
        return nullptr;
    }
    g_pack = hold(pack);
    logx::info("avatar: body pack loaded");
    return pack;
}

void drop_gait(Avatar& a) {
    drop(a.gait.animation);
    drop(a.gait.walk_state);
    drop(a.gait.run_state);
    a.gait = Gait{};
}

Il2CppObject* animation_state(Il2CppObject* animation, const char* name) {
    void* args[] = {il2::il2cpp_string_new(name)};
    return il2::invoke(api.anim_state, animation, args);
}

// Find the body's Animation component and its clips' ground speeds.
void attach_gait(Avatar& a, Il2CppObject* body) {
    drop_gait(a);
    Il2CppObject* animations = components_in_children(body, api.animation_type);
    if (il2::array_length(animations) == 0) return;
    Il2CppObject* animation = il2::array_object_at(animations, 0);
    Gait& g = a.gait;
    g.animation = hold(animation);
    g.walk_state = hold(animation_state(animation, "walk"));
    g.run_state = hold(animation_state(animation, "run"));
    g.has_down = animation_state(animation, "down") != nullptr;

    Il2CppObject* transforms = components_in_children(body, api.transform_type);
    for (size_t i = 0, n = il2::array_length(transforms); i < n; ++i) {
        const std::string name = name_of(il2::array_object_at(transforms, i));
        float walk = 0, run = 0;
        if (name.rfind("TLDCoopGait", 0) == 0 && sscanf(name.c_str(), "TLDCoopGait walk=%f run=%f", &walk, &run) == 2) {
            g.walk_speed = walk;
            g.run_speed = run;
            break;
        }
    }
    say(a, "gait walk %.2f m/s, run %.2f m/s%s", g.walk_speed, g.run_speed,
        g.walk_state && g.run_state ? "" : " (walk/run states missing!)");
}

void drop_bones(Avatar& a) {
    for (uintptr_t& b : a.bones) drop(b);
    for (auto& [name, h] : a.rig) drop(h);  // TLDCoop_DLC
    a.rig.clear();
}

// TLDCoop_DLC: the bones of the rig posed by name (fingers, clavicles, neck, Spine2).
bool rig_bone_wanted(const std::string& n) {
    if (n.size() < 5 || n.compare(n.size() - 4, 4, "_jnt") != 0) return false;
    for (const char* k : {"Index", "Middle", "Ring", "Pinky", "Thumb", "Shoulder_jnt", "Neck_jnt", "Spine2_jnt", "Forearm_jnt"})
        if (n.find(k) != std::string::npos) return true;
    return false;
}

// The arm bones, for holding things (late_update).
void attach_bones(Avatar& a, Il2CppObject* body) {
    drop_bones(a);
    Il2CppObject* transforms = components_in_children(body, api.transform_type);
    int arms = 0, others = 0, heads = 0;
    for (size_t i = 0, n = il2::array_length(transforms); i < n; ++i) {
        Il2CppObject* t = il2::array_object_at(transforms, i);
        const std::string name = name_of(t);
        if (rig_bone_wanted(name) && !a.rig.count(name)) a.rig[name] = hold(t);  // TLDCoop_DLC
        for (int b = 0; b < kBoneCount; ++b)
            if (!a.bones[b] && name == kBoneNames[b]) {
                a.bones[b] = hold(t);
                ++(b < kArmBones ? arms : b == kHead ? heads : others);
            }
    }
    if (arms != kArmBones) say(a, "only %d of the %d arm bones found: held items sit at the body", arms, kArmBones);
    if (!heads) say(a, "no head bone found: their nods and head shakes won't show");
    if (a.rig.size() < 32) say(a, "only %zu finger/shoulder/neck bones found: hands stay as the clip has them", a.rig.size());
    if (others != kBoneCount - kArmBones - 1)
        say(a, "only %d of the %d hip, spine and leg bones found: they won't crouch", others, kBoneCount - kArmBones - 1);
}

// --- what they do (milestone 13) -----------------------------------------------------------------------------------

bool female_rig(const std::string& id) {
    for (const char* f : kFemaleBodies)
        if (id == f) return true;
    return false;
}

// A clip from the action bundles by its asset path (loaded once; null if no bundle has it).
Il2CppObject* action_clip(const std::string& path) {
    if (!g_actions_tried) {
        g_actions_tried = true;
        for (const char* file : kActionBundles) {
            const auto at = paths::mod_dir() / file;
            if (!std::filesystem::exists(at)) {
                logx::warn("avatar: %ls not found: the bodies won't show what is in it (deploy.cmd copies it)", at.c_str());
                continue;
            }
            const std::u8string u8 = at.u8string();
            const std::string utf8(reinterpret_cast<const char*>(u8.c_str()), u8.size());
            void* args[] = {il2::il2cpp_string_new(utf8.c_str())};
            if (Il2CppObject* bundle = il2::invoke(api.load_bundle, nullptr, args))
                g_action_bundles.push_back(hold(bundle));
            else
                logx::error("avatar: could not load %ls", at.c_str());
        }
        logx::info("avatar: %zu of %zu action clip bundles loaded", g_action_bundles.size(), std::size(kActionBundles));
    }
    const auto known = g_action_clips.find(path);
    if (known != g_action_clips.end()) return target(known->second);
    uintptr_t found = 0;
    for (uintptr_t b : g_action_bundles) {
        void* args[] = {il2::il2cpp_string_new(path.c_str()), api.clip_type};
        if (Il2CppObject* clip = il2::invoke(api.load_asset, target(b), args)) {
            found = hold(clip);
            break;
        }
    }
    if (!found) logx::warn("avatar: no clip %s in the action bundles", path.c_str());
    g_action_clips[path] = found;
    return target(found);
}

Il2CppObject* add_action_state(Il2CppObject* animation, Il2CppObject* clip, const std::string& name, bool loop) {
    void* add_args[] = {clip, il2::il2cpp_string_new(name.c_str())};
    il2::invoke(api.add_clip, animation, add_args);
    Il2CppObject* state = animation_state(animation, name.c_str());
    if (!state) return nullptr;
    int layer = kActionLayer, wrap = loop ? kWrapLoop : kWrapClampForever;
    void* layer_args[] = {&layer};
    il2::invoke(api.state_layer, state, layer_args);
    void* wrap_args[] = {&wrap};
    il2::invoke(api.state_wrap, state, wrap_args);
    return state;
}

// The action clips for this body's rig, on its Animation (after attach_gait and attach_bones).
void attach_actions(Avatar& a, const std::string& id) {
    a.actions = 0;
    a.action.clear();
    a.female = female_rig(id);
    Il2CppObject* animation = target(a.gait.animation);
    if (!animation || !api.add_clip || !api.blend || !api.state_layer || !api.state_wrap || !api.clip_type) return;
    Il2CppObject* spine = target(a.bones[kSpine1]);
    char path[160];
    for (const ActionClip& c : kActionClips) {
        snprintf(path, sizeof(path), c.asset, a.female ? "F" : "M");
        Il2CppObject* clip = action_clip(path);
        if (!clip || !add_action_state(animation, clip, c.name, c.loop)) continue;
        ++a.actions;
        if (!c.upper || !spine || !api.state_mix) continue;
        if (Il2CppObject* up = add_action_state(animation, clip, std::string(c.name) + "_up", c.loop)) {
            bool recursive = true;
            void* mix_args[] = {spine, &recursive};
            il2::invoke(api.state_mix, up, mix_args);
            ++a.actions;
        }
    }
    // The clips' curves go by path from the Animation's GameObject (GAME_DATA/Origin_jnt/...).
    Il2CppObject* go = pv.component_go ? il2::invoke(pv.component_go, animation) : nullptr;
    Il2CppObject* tf = go ? il2::invoke(api.go_transform, go) : nullptr;
    void* find_args[] = {il2::il2cpp_string_new("GAME_DATA/Origin_jnt/Hips_jnt")};
    const bool bound = tf && api.find_child && il2::invoke(api.find_child, tf, find_args);
    say(a, "%d action clip states (%s rig), on '%s' (%zu Animation in the body): the clips' bones %s", a.actions,
        a.female ? "female" : "male", name_of(tf).c_str(),
        il2::array_length(components_in_children(target(a.visual), api.animation_type)), bound ? "found" : "NOT found");
}

void drop_visual(Avatar& a) {
    if (Il2CppObject* visual = target(a.visual)) destroy(visual, false);
    drop(a.visual);
    drop_gait(a);
    drop_bones(a);
    a.actions = 0;
    a.action.clear();
    a.shown_body.clear();
}

void show_capsule(Avatar& a) {
    drop_visual(a);
    a.visual = hold(make_capsule(a));
    a.shown_body = "capsule";
}

bool ensure_loader();

// A new copy of body `id`, ready but still asleep under the inactive loader: parenting it into view wakes it up.
Il2CppObject* new_pack_body(const std::string& id) {
    Il2CppObject* pack = body_pack();
    if (!pack || !ensure_loader()) return nullptr;
    const std::string asset = "Body_" + id;
    void* load_args[] = {il2::il2cpp_string_new(asset.c_str()), api.game_object_type};
    Il2CppObject* prefab = il2::invoke(api.load_asset, pack, load_args);
    if (!prefab) {
        logx::warn("avatar: body '%s' is not in the body pack; rebuild it to include this character", id.c_str());
        return nullptr;
    }
    void* inst_args[] = {prefab, target(g_loader_transform)};
    Il2CppObject* body = il2::invoke(api.instantiate, nullptr, inst_args);
    if (!transform_of(body)) return nullptr;
    strip(body);
    if (g_use_tld_shaders) adopt_tld_shaders(body);
    return body;
}

bool show_pack_body(Avatar& a, const std::string& id) {
    Il2CppObject* body = new_pack_body(id);
    Il2CppObject* body_transform = transform_of(body);
    if (!body_transform) return false;

    drop_visual(a);
    set_parent(body_transform, target(a.root_transform));  // becomes active here: the Animator wakes up
    set_vec(api.set_local_position, body_transform, {0, 0, 0});
    set_vec(api.set_local_euler, body_transform, {0, 0, 0});
    a.visual = hold(body);
    a.shown_body = id;
    attach_gait(a, body);
    attach_bones(a, body);
    attach_actions(a, id);
    say(a, "showing '%s' (%s)", id.c_str(), a.gait.animation ? "animated" : "no Animation component");
    return true;
}

// --- what they hold (milestone 11) ---------------------------------------------------------------------------------

using net::pose::Quat;
using PVec = net::pose::Vec3;

struct Bounds {  // UnityEngine.Bounds
    Vector3 centre, extents;
};

PVec pv3(Vector3 v) { return {v.x, v.y, v.z}; }
Vector3 uv3(PVec v) { return {v.x, v.y, v.z}; }

PVec position_of(Il2CppObject* tf) {
    Vector3 p{};
    if (tf) il2::invoke_value(api.get_position, tf, p);
    return pv3(p);
}

Quat rotation_of(Il2CppObject* tf) {
    Quat q{};
    if (tf) il2::invoke_value(api.get_rotation, tf, q);
    return q;
}

void set_rotation(Il2CppObject* tf, Quat q) {
    void* args[] = {&q};
    if (tf) il2::invoke(api.set_rotation, tf, args);
}

void drop_all(std::vector<uintptr_t>& handles) {
    for (uintptr_t& h : handles) drop(h);
    handles.clear();
}

// Its objects went with the avatar's root: only our handles are left.
void forget_held(HeldCopy& h) {
    drop(h.go);
    drop(h.transform);
    for (auto* v : {&h.fx_low, &h.fx_high, &h.lights_in, &h.lights_out, &h.lights_in_high, &h.lights_out_high,
                    &h.hide_lit, &h.hide_always})
        drop_all(*v);
    for (HeldCopy::Emitter& e : h.emitters) drop(e.ps);
    h = HeldCopy{};
}

void remove_held(HeldCopy& h) {
    if (Il2CppObject* go = target(h.go); is_alive(go)) destroy(go, false);
    forget_held(h);
}

Il2CppObject* field_object(Il2CppObject* obj, ptrdiff_t offset) {
    Il2CppObject* v = obj && offset >= 0 ? il2::read_field<Il2CppObject*>(obj, offset) : nullptr;
    return is_alive(v) ? v : nullptr;
}

void keep(std::vector<uintptr_t>& into, Il2CppObject* obj) {
    if (obj) into.push_back(hold(obj));
}

template <typename T>
void set_value(const MethodInfo* setter, Il2CppObject* obj, T value) {
    void* args[] = {&value};
    if (obj) il2::invoke(setter, obj, args);
}

float curve_at_full(Il2CppObject* owner, ptrdiff_t curve) {
    float full = 1.0f, v = 0;
    void* args[] = {&full};
    Il2CppObject* c = owner && curve >= 0 ? il2::read_field<Il2CppObject*>(owner, curve) : nullptr;
    return c && parts.curve_evaluate && il2::invoke_value(parts.curve_evaluate, c, v, args) ? v : -1.0f;
}

// TLDCoop_DLC: a flare's flame. Its FX object (FlareIntensity's) is what shows while lit, its lights flicker in the
// range its curves give at full strength, and its smoke, core and sparks are emitted by us (TLD's
// FlareIntensityManager, which does both every frame, doesn't know our copy).
void read_flare_flame(HeldCopy& h, Il2CppObject* copy) {
    if (!parts.flare_intensity_type) return;
    Il2CppObject* found = components_in_children(copy, parts.flare_intensity_type);
    Il2CppObject* fi = il2::array_length(found) ? il2::array_object_at(found, 0) : nullptr;
    if (!fi) return;
    if (h.fx_low.empty()) keep(h.fx_low, il2::invoke(pv.component_go, fi));
    for (auto [light, lo, hi] : {std::tuple{parts.fi_in_light, parts.fi_in_min, parts.fi_in_max},
                                 std::tuple{parts.fi_out_light, parts.fi_out_min, parts.fi_out_max}}) {
        Il2CppObject* go = field_object(fi, light);
        void* type_arg[] = {parts.random_intensity_type};
        Il2CppObject* random = go && parts.random_intensity_type ? il2::invoke(api.get_component, go, type_arg) : nullptr;
        float min = curve_at_full(fi, lo), max = curve_at_full(fi, hi);
        if (!random || !parts.set_min_max || min < 0 || max < 0) continue;
        if (max <= 0) min = max = 1.0f;  // (curves that say nothing: a plain light)
        void* args[] = {&min, &max};
        il2::invoke(parts.set_min_max, random, args);
        h.flicker += (h.flicker.empty() ? "" : ", ") + name_of(go) + " " + std::to_string(min).substr(0, 4) + "-" +
                     std::to_string(max).substr(0, 4);
    }
    if (!parts.particles_type || !parts.ps_emit) return;
    for (auto [obj, rate] : {std::pair{parts.fi_smoke, parts.fi_smoke_rate}, std::pair{parts.fi_core, parts.fi_core_rate},
                             std::pair{parts.fi_spark, parts.fi_spark_rate}}) {
        Il2CppObject* go = field_object(fi, obj);
        void* type_arg[] = {parts.particles_type};
        Il2CppObject* ps = go ? il2::invoke(api.get_component, go, type_arg) : nullptr;
        const float r = curve_at_full(fi, rate);
        if (ps && r > 0) h.emitters.push_back({hold(ps), std::min(r, 200.0f), 0});
    }
}

// TLDCoop_DLC: a light of our own on the copy, for what has none in its model (a match: TLD lights the hand's match
// with an FX of the player's, not of the item). Warm and small, as a match's flame.
void add_own_light(HeldCopy& h, Il2CppObject* copy) {
    if (!pv.light_type || !api.add_component || !h.lights_in.empty() || !h.lights_out.empty()) return;
    Il2CppObject* go = new_game_object("TLDCoop_Flame");
    Il2CppObject* tf = transform_of(go);
    if (!tf) return;
    set_parent(tf, transform_of(copy));
    set_vec(api.set_local_position, tf, {0, 0, 0});
    void* type_arg[] = {pv.light_type};
    Il2CppObject* light = il2::invoke(api.add_component, go, type_arg);
    if (!light) return;
    set_value(pv.light_kind, light, 2);  // LightType.Point
    set_value(pv.light_intensity, light, 1.6f);
    set_value(pv.light_color, light, Color{1.0f, 0.62f, 0.3f, 1.0f});
    if (parts.light_range) set_value(parts.light_range, light, 6.0f);
    // TLDCoop_DLC: no LightTracking: TLD's lighting switched a friend's tracked lights off (the log showed them "off"
    // a moment after they were lit: nobody saw a friend's flare or match light anything).
    keep(h.lights_in, light);
    keep(h.lights_out, light);
    h.own_light = true;
}

// TLDCoop_DLC: a lit flare's particles, as many as its rates give since the last frame.
void emit_flame(HeldCopy& h) {
    const double now = net::now_ms();
    const float dt = h.emitted_ms > 0 ? static_cast<float>(std::min(0.1, (now - h.emitted_ms) / 1000.0)) : 0;
    h.emitted_ms = now;
    if (h.lit_shown < 1) return;
    for (HeldCopy::Emitter& e : h.emitters) {
        Il2CppObject* ps = target(e.ps);
        if (!is_alive(ps)) continue;
        e.carry += e.rate * dt;
        int32_t n = static_cast<int32_t>(e.carry);
        if (n <= 0) continue;
        e.carry -= static_cast<float>(n);
        void* args[] = {&n};
        il2::invoke(parts.ps_emit, ps, args);
    }
}

// The copy's own lit parts, read from its item script before the scripts go (Instantiate points the copy's fields
// at the copy's own objects).
void read_parts(HeldCopy& h, Il2CppObject* copy) {
    void* type_arg[] = {api.gear_type};
    Il2CppObject* gi = api.gear_type ? il2::invoke(api.get_component, copy, type_arg) : nullptr;
    if (!gi) return;
    if (Il2CppObject* t = field_object(gi, parts.torch)) {
        keep(h.fx_low, field_object(t, parts.torch_fx));
        keep(h.lights_in, field_object(t, parts.torch_in));
        keep(h.lights_out, field_object(t, parts.torch_out));
        keep(h.hide_always, field_object(t, parts.torch_burnt));
    } else if (Il2CppObject* f = field_object(gi, parts.flare)) {
        keep(h.fx_low, field_object(f, parts.flare_fx));
        keep(h.lights_in, field_object(f, parts.flare_in));
        keep(h.lights_out, field_object(f, parts.flare_out));
        keep(h.hide_lit, field_object(f, parts.flare_cap));
    } else if (Il2CppObject* l = field_object(gi, parts.lamp)) {
        keep(h.fx_low, field_object(l, parts.lamp_fx));
        keep(h.lights_in, field_object(l, parts.lamp_in));
        keep(h.lights_in, field_object(l, parts.lamp_core));
        keep(h.lights_out, field_object(l, parts.lamp_out));
    } else if (Il2CppObject* fl = field_object(gi, parts.flashlight)) {
        keep(h.fx_low, field_object(fl, parts.fl_fx_low));
        keep(h.fx_high, field_object(fl, parts.fl_fx_high));
        keep(h.lights_in, field_object(fl, parts.fl_in));
        keep(h.lights_out, field_object(fl, parts.fl_out));
        keep(h.lights_in_high, field_object(fl, parts.fl_in_high));
        keep(h.lights_out_high, field_object(fl, parts.fl_out_high));
    }
    // By name: a torch's ground glow (its script puts it under the player's feet and fades it with the flame; without
    // the script it hangs at the flame at full strength, so it stays hidden: the lights do that work), and a
    // flashlight's low and high beam FX when its fields don't name them (in the world model they don't).
    const bool no_fx = h.fx_low.empty() && h.fx_high.empty();
    Il2CppObject* tfs = components_in_children(copy, api.transform_type);
    for (size_t i = 0, n = il2::array_length(tfs); i < n; ++i) {
        Il2CppObject* go = il2::invoke(pv.component_go, il2::array_object_at(tfs, i));
        const std::string name = go ? name_of(go) : std::string();
        if (name == "RadialGradient") keep(h.hide_always, go);
        if (no_fx && name == "LowFX") keep(h.fx_low, go);
        if (no_fx && name == "HighFX") keep(h.fx_high, go);
    }
    // Lights the item script only finds when it wakes (TorchItem.Awake looks for its FX's children LightIndoor and
    // LightOutdoor by name): found the same way. A light named neither shines indoors and out; one under a high-beam
    // FX object is for the high beam.
    const bool none = h.lights_in.empty() && h.lights_out.empty() && h.lights_in_high.empty() && h.lights_out_high.empty();
    std::vector<Il2CppObject*> high_lights;
    for (uintptr_t fx : h.fx_high) {
        Il2CppObject* under = components_in_children(target(fx), api.light_type);
        for (size_t i = 0, n = il2::array_length(under); i < n; ++i) high_lights.push_back(il2::array_object_at(under, i));
    }
    Il2CppObject* lights = none ? components_in_children(copy, api.light_type) : nullptr;
    for (size_t i = 0, n = il2::array_length(lights); i < n; ++i) {
        Il2CppObject* l = il2::array_object_at(lights, i);
        const std::string name = name_of(il2::invoke(pv.component_go, l));
        const bool high = name.find("High") != std::string::npos ||
                          std::find(high_lights.begin(), high_lights.end(), l) != high_lights.end();
        const bool in = name.find("Indoor") != std::string::npos, out = name.find("Outdoor") != std::string::npos;
        if (in || !out) keep(high ? h.lights_in_high : h.lights_in, l);
        if (out || !in) keep(high ? h.lights_out_high : h.lights_out, l);
    }
    // A torch's flicker range, as its TorchIntensity sets it for a flame at full strength.
    void* intensity_arg[] = {parts.torch_intensity_type};
    Il2CppObject* intensity = parts.torch_intensity_type && parts.curve_evaluate && parts.set_min_max
                                  ? il2::invoke(api.get_component, copy, intensity_arg)
                                  : nullptr;
    if (!intensity) {  // on the FX object, not the item's root
        Il2CppObject* found = parts.torch_intensity_type ? components_in_children(copy, parts.torch_intensity_type) : nullptr;
        intensity = il2::array_length(found) ? il2::array_object_at(found, 0) : nullptr;
    }
    if (intensity && parts.curve_evaluate && parts.set_min_max) {
        auto at_full = [&](ptrdiff_t curve) {
            float full = 1.0f, v = 0;
            void* args[] = {&full};
            Il2CppObject* c = field_object(intensity, curve);
            return c && il2::invoke_value(parts.curve_evaluate, c, v, args) ? v : -1.0f;
        };
        for (auto [light, lo, hi] : {std::tuple{parts.ti_in_light, parts.ti_in_min, parts.ti_in_max},
                                     std::tuple{parts.ti_out_light, parts.ti_out_min, parts.ti_out_max}}) {
            Il2CppObject* go = field_object(intensity, light);
            void* type_arg[] = {parts.random_intensity_type};
            Il2CppObject* random = go ? il2::invoke(api.get_component, go, type_arg) : nullptr;
            float min = at_full(lo), max = at_full(hi);
            if (!random || min < 0 || max < 0) continue;
            void* args[] = {&min, &max};
            il2::invoke(parts.set_min_max, random, args);
            h.flicker += (h.flicker.empty() ? "" : ", ") + name_of(go) + " " + std::to_string(min).substr(0, 4) + "-" +
                         std::to_string(max).substr(0, 4);
        }
    }
    read_flare_flame(h, copy);
}

// TLD's generic light scripts stay: the colour (LightFlickering, between its two colours), the flicker in strength
// (LightRandomIntensity), the light registered as TLD's own (LightTracking: LightingManager) and its shadows as the
// quality setting has them (LightQualitySwitch). Lights placed in the world use them without an item.
bool held_keeps(const std::string& cls) {
    return cls == "UnityEngine.Transform" || cls == "UnityEngine.MeshRenderer" || cls == "UnityEngine.MeshFilter" ||
           cls == "UnityEngine.SkinnedMeshRenderer" || cls == "UnityEngine.LODGroup" || cls == "UnityEngine.Light" ||
           cls == "UnityEngine.ParticleSystem" || cls == "UnityEngine.ParticleSystemRenderer" ||
           cls == "UnityEngine.LensFlare" || cls == "UnityEngine.TrailRenderer" || cls == "UnityEngine.LineRenderer" ||
           cls == "LightFlickering" || cls == "LightRandomIntensity" ||  // (TLDCoop_DLC: not LightTracking, below)
           cls == "LightQualitySwitch";
}

// Everything but what is drawn and its lights: TLD's item scripts (the item's logic, its sounds, its FX scripts, which
// all expect a real item in a real inventory), physics and audio. Done while it is still asleep under the loader.
int strip_held(Il2CppObject* go) {
    int removed = 0;
    for (int pass = 0; pass < 3; ++pass) {
        Il2CppObject* components = components_in_children(go, api.component_type);
        for (size_t i = 0, n = il2::array_length(components); i < n; ++i) {
            Il2CppObject* c = il2::array_object_at(components, i);
            const std::string cls = il2::class_name_of(c);
            if (held_keeps(cls)) continue;
            if (pass == 0 && cls.rfind("UnityEngine.", 0) == 0) continue;  // game scripts first
            destroy(c, true);
            ++removed;
        }
    }
    return removed;
}

void show_objects(const std::vector<uintptr_t>& objects, bool on) {
    for (uintptr_t h : objects)
        if (Il2CppObject* o = target(h); is_alive(o)) set_active(o, on);
}

// By its object, as TLD switches them: its LightTracking leaves TLD's LightingManager with it.
void set_light(uintptr_t handle, bool on) {
    Il2CppObject* light = target(handle);
    if (!is_alive(light)) return;
    void* args[] = {&on};
    il2::invoke(api.set_enabled, light, args);
    Il2CppObject* go = il2::invoke(pv.component_go, light);
    if (go) set_active(go, on);
    if (!on) return;
    // TLDCoop_DLC: a friend's light is as important as our own (not left a per-vertex light by Unity's light count:
    // a friend's flare didn't light anything), and TLD's lighting knows it (LightTracking, if its model had none).
    if (api.light_set_mode) {
        int32_t force_pixel = 1;  // LightRenderMode.ForcePixel
        void* mode_args[] = {&force_pixel};
        il2::invoke(api.light_set_mode, light, mode_args);
    }
}

// TLDCoop_DLC: game scripts left on a held copy (TLD's light scripts we keep) that switch its lights off: a friend's
// flare's LightOutdoor was switched off again every frame, so nobody saw its light outdoors. Taken away, logged.
int strip_light_scripts(Il2CppObject* go, std::string& names) {
    int removed = 0;
    Il2CppObject* components = go ? components_in_children(go, api.component_type) : nullptr;
    for (size_t i = 0, n = il2::array_length(components); i < n; ++i) {
        Il2CppObject* c = il2::array_object_at(components, i);
        const std::string cls = il2::class_name_of(c);
        if (cls.rfind("UnityEngine.", 0) == 0) continue;
        names += (names.empty() ? "" : ", ") + cls;
        destroy(c, true);
        ++removed;
    }
    return removed;
}

// TLDCoop_DLC: while lit, the lights that should be on are kept on (something of TLD's switching them off is logged once).
void keep_lit(const Avatar& a, HeldCopy& h, bool indoors) {
    if (h.lit_shown < 1 || !api.active_and_enabled) return;
    const auto& in = h.lit_shown == 2 && !h.lights_in_high.empty() ? h.lights_in_high : h.lights_in;
    const auto& out = h.lit_shown == 2 && !h.lights_out_high.empty() ? h.lights_out_high : h.lights_out;
    const std::vector<uintptr_t>& on = indoors ? (!in.empty() ? in : out) : (!out.empty() ? out : in);
    static int logged = 0;
    for (uintptr_t l : on) {
        Il2CppObject* light = target(l);
        bool lit = true;
        if (!is_alive(light) || !il2::invoke_value(api.active_and_enabled, light, lit) || lit) continue;
        int& times = h.relit[l];
        ++times;
        // Switched off again and again: the scripts doing it go (first the light's own object's, then all the copy's)
        if ((times == 3 && h.scripts_stripped < 1) || (times == 8 && h.scripts_stripped < 2)) {
            std::string names;
            Il2CppObject* from = h.scripts_stripped < 1 ? il2::invoke(pv.component_go, light) : target(h.go);
            const int removed = strip_light_scripts(from, names);
            h.scripts_stripped = h.scripts_stripped < 1 ? 1 : 2;
            say(a, "%s: %s keeps being switched off; %d script(s) of %s taken away%s%s", h.gear.c_str(),
                name_of(light).c_str(), removed, h.scripts_stripped == 1 ? "the light's object" : "the whole copy",
                names.empty() ? "" : ": ", names.c_str());
        }
        set_light(l, true);
        // TLDCoop_DLC: still dark: an object above the light is hidden (the lantern's light stayed off, frame after
        // frame, with no script left to blame). Its parents up to the copy are shown again.
        bool now_on = true;
        if (il2::invoke_value(api.active_and_enabled, light, now_on) && !now_on && api.tf_parent) {
            Il2CppObject* root = target(h.go);
            Il2CppObject* go = il2::invoke(pv.component_go, light);
            Il2CppObject* tf = go ? il2::invoke(api.go_transform, go) : nullptr;
            for (int up = 0; up < 6 && tf; ++up) {
                tf = il2::invoke(api.tf_parent, tf);
                Il2CppObject* pgo = tf ? il2::invoke(pv.component_go, tf) : nullptr;
                if (!pgo) break;
                bool shown = true;
                il2::invoke_value(api.in_hierarchy, pgo, shown);
                if (!shown) {
                    set_active(pgo, true);
                    if (logged < 20) say(a, "%s: %s sat under hidden '%s': shown again", h.gear.c_str(), name_of(light).c_str(),
                                         name_of(pgo).c_str());
                }
                if (pgo == root) break;
            }
        }
        if (logged++ < 10) say(a, "%s: %s was switched off; on again", h.gear.c_str(), name_of(light).c_str());
    }
}

// TLDCoop_DLC: what its lights are doing a moment after it was lit, for the log.
std::string describe_lights(const HeldCopy& h) {
    std::string out;
    for (const auto* list : {&h.lights_in, &h.lights_out, &h.lights_in_high, &h.lights_out_high})
        for (uintptr_t l : *list) {
            Il2CppObject* light = target(l);
            if (!is_alive(light)) continue;
            bool on = false;
            float intensity = -1, range = -1;
            int32_t mode = -1, mask = 0;
            il2::invoke_value(api.active_and_enabled, light, on);
            il2::invoke_value(api.light_intensity, light, intensity);
            il2::invoke_value(api.light_range, light, range);
            if (api.light_get_mode) il2::invoke_value(api.light_get_mode, light, mode);
            if (api.light_get_mask) il2::invoke_value(api.light_get_mask, light, mask);
            // TLDCoop_DLC: why it's off: the light itself, its object, or an object above it (the first hidden one)
            std::string why;
            if (!on) {
                Il2CppObject* go = il2::invoke(pv.component_go, light);
                bool shown = true;
                if (go) il2::invoke_value(api.in_hierarchy, go, shown);
                if (!shown) {
                    Il2CppObject* tf = il2::invoke(api.go_transform, go);
                    std::string hidden = name_of(go);
                    for (int up = 0; up < 8 && tf && api.tf_parent; ++up) {
                        tf = il2::invoke(api.tf_parent, tf);
                        Il2CppObject* pgo = tf ? il2::invoke(pv.component_go, tf) : nullptr;
                        bool pshown = true;
                        if (!pgo || !il2::invoke_value(api.in_hierarchy, pgo, pshown) || pshown) break;
                        hidden = name_of(pgo);
                    }
                    why = " (hidden from '" + hidden + "' up)";
                } else {
                    why = " (the light itself disabled)";
                }
            }
            char buf[300];
            snprintf(buf, sizeof(buf), "%s%s %s%s, intensity %.2f, range %.1f m, mode %d, mask %08x", out.empty() ? "" : "; ",
                     name_of(light).c_str(), on ? "on" : "off", why.c_str(), intensity, range, mode, static_cast<uint32_t>(mask));
            out += buf;
        }
    return out.empty() ? "no lights" : out;
}

// level: 0 unlit, 1 lit, 2 a flashlight's high beam. TLD has one light for indoors and one for outdoors; a list it
// left empty borrows the other.
void light_up(HeldCopy& h, int level, bool indoors) {
    const bool lit = level >= 1;
    const bool high = level == 2 && (!h.lights_in_high.empty() || !h.lights_out_high.empty() || !h.fx_high.empty());
    show_objects(h.hide_always, false);
    show_objects(h.hide_lit, !lit);
    show_objects(h.fx_high, lit && high);
    show_objects(h.fx_low, lit && !(high && !h.fx_high.empty()));
    auto pick = [&](const std::vector<uintptr_t>& in, const std::vector<uintptr_t>& out) -> const std::vector<uintptr_t>* {
        if (indoors) return !in.empty() ? &in : &out;
        return !out.empty() ? &out : &in;
    };
    const std::vector<uintptr_t>* low = pick(h.lights_in, h.lights_out);
    const std::vector<uintptr_t>* hi = pick(h.lights_in_high, h.lights_out_high);
    const std::vector<uintptr_t>* on = !lit ? nullptr : high && !hi->empty() ? hi : low;
    for (const auto* list : {&h.lights_in, &h.lights_out, &h.lights_in_high, &h.lights_out_high})
        for (uintptr_t l : *list) set_light(l, on && std::find(on->begin(), on->end(), l) != on->end());
    if (!lit && parts.ps_stop)  // TLDCoop_DLC: none while unlit (its FX object may stay shown: it holds the model)
        for (HeldCopy::Emitter& e : h.emitters)
            if (Il2CppObject* ps = target(e.ps); is_alive(ps)) il2::invoke(parts.ps_stop, ps);
    if (lit && parts.ps_play)  // TLDCoop_DLC: the flame's particles live (we emit them)
        for (HeldCopy::Emitter& e : h.emitters)
            if (Il2CppObject* ps = target(e.ps); is_alive(ps)) il2::invoke(parts.ps_play, ps);
    h.lit_shown = level;
    h.indoors_shown = indoors;
}

// Its shape in its own space, from its renderers' bounds while it stands unturned at its pivot: the long axis (its
// front or top end first: towards a torch's or flare's flame, along a flashlight's beam, a gun's muzzle; a lantern
// stands upright).
void measure_held(HeldCopy& h) {
    Il2CppObject* tf = target(h.transform);
    const PVec pivot = position_of(tf);
    PVec lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    int n = 0;
    Il2CppObject* renderers = components_in_children(target(h.go), api.renderer_type);
    for (size_t i = 0, count = il2::array_length(renderers); i < count; ++i) {
        Il2CppObject* r = il2::array_object_at(renderers, i);
        if (il2::class_name_of(r) == "UnityEngine.ParticleSystemRenderer") continue;
        bool enabled = false, active = false;
        Il2CppObject* go = il2::invoke(pv.component_go, r);
        if (!il2::invoke_value(api.renderer_enabled, r, enabled) || !enabled || !go ||
            !il2::invoke_value(api.in_hierarchy, go, active) || !active)
            continue;
        Bounds b{};
        if (!il2::invoke_value(api.renderer_bounds, r, b)) continue;
        lo = {std::fmin(lo.x, b.centre.x - b.extents.x), std::fmin(lo.y, b.centre.y - b.extents.y),
              std::fmin(lo.z, b.centre.z - b.extents.z)};
        hi = {std::fmax(hi.x, b.centre.x + b.extents.x), std::fmax(hi.y, b.centre.y + b.extents.y),
              std::fmax(hi.z, b.centre.z + b.extents.z)};
        ++n;
    }
    h.measured = true;
    if (n == 0) return;
    const PVec centre = (lo + hi) * 0.5f, e = (hi - lo) * 0.5f;
    h.centre = centre - pivot;
    const float ext[3] = {e.x, e.y, e.z};
    const PVec axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    int first = 0;
    for (int i = 1; i < 3; ++i)
        if (ext[i] > ext[first]) first = i;
    int second = first == 0 ? 1 : 0;
    for (int i = 0; i < 3; ++i)
        if (i != first && ext[i] > ext[second]) second = i;
    h.axis = axes[first];
    h.second = axes[second];
    h.half = ext[first];
    auto along = [&](PVec dir) { return std::fabs(dir.x) * e.x + std::fabs(dir.y) * e.y + std::fabs(dir.z) * e.z; };
    if (h.kind == net::HeldKind::Lantern) {
        h.axis = {0, 1, 0};
        h.second = {0, 0, 1};
        h.half = e.y;
    } else if (h.kind == net::HeldKind::Flashlight) {
        const std::vector<uintptr_t>& lights = !h.lights_out.empty() ? h.lights_out : h.lights_in;
        Vector3 beam{};
        if (!lights.empty())
            if (Il2CppObject* light_tf = transform_of(il2::invoke(pv.component_go, target(lights.front()))))
                il2::invoke_value(api.get_forward, light_tf, beam);
        if (net::pose::length(pv3(beam)) > 0.5f) {
            h.axis = net::pose::normalized(pv3(beam));
            h.second = std::fabs(h.axis.y) < 0.9f ? PVec{0, 1, 0} : PVec{0, 0, 1};
            h.half = along(h.axis);
        }
    } else if (!h.fx_low.empty()) {  // a torch or flare: the flame end first
        if (Il2CppObject* fx_tf = transform_of(target(h.fx_low.front())))
            if (net::pose::dot(position_of(fx_tf) - centre, h.axis) < 0) h.axis = h.axis * -1.0f;
    } else if (h.kind == net::HeldKind::Rifle || h.kind == net::HeldKind::Revolver ||
               h.kind == net::HeldKind::FlareGun) {
        h.axis = h.axis * -1.0f;  // one mesh each, no muzzle part to go by: TLD's lie muzzle to their -z
    }
}

// The light details for the log: "2 on, intensity 1.6 range 8".
std::string lights_text(const std::vector<uintptr_t>& lights) {
    std::string out;
    for (uintptr_t h : lights) {
        Il2CppObject* l = target(h);
        float intensity = 0, range = 0;
        il2::invoke_value(api.light_intensity, l, intensity);
        il2::invoke_value(api.light_range, l, range);
        char text[64];
        snprintf(text, sizeof(text), "%s%.2f/%.1f m", out.empty() ? "" : ", ", intensity, range);
        out += text;
    }
    return out.empty() ? "none" : out;
}

// A GameObject's lights for the log: "light LightOutdoor: colour (0.92, 0.45, 0.20), intensity 1.20, range 10.0 m,
// shadows"; only those shining now if `lit_only`.
std::string light_list(Il2CppObject* go, bool lit_only) {
    std::string out;
    char text[200];
    Il2CppObject* lights = go ? components_in_children(go, api.light_type) : nullptr;
    for (size_t i = 0, n = il2::array_length(lights); i < n; ++i) {
        Il2CppObject* l = il2::array_object_at(lights, i);
        float intensity = 0, range = 0;
        struct {
            float r, g, b, a;
        } colour{};
        int32_t shadows = 0;
        bool on = false;
        il2::invoke_value(api.light_intensity, l, intensity);
        il2::invoke_value(api.light_range, l, range);
        il2::invoke_value(api.light_color, l, colour);
        il2::invoke_value(api.light_shadows, l, shadows);
        il2::invoke_value(api.active_and_enabled, l, on);
        if (lit_only && !on) continue;
        snprintf(text, sizeof(text), "%slight %s%s: colour (%.2f, %.2f, %.2f), intensity %.2f, range %.1f m%s",
                 out.empty() ? "" : "; ", name_of(il2::invoke(pv.component_go, l)).c_str(), on ? "" : " (off)",
                 colour.r, colour.g, colour.b, intensity, range, shadows ? ", shadows" : "");
        out += text;
    }
    return out;
}

// What the copy is made of, for the log: its lights (colour, intensity, range, shadows) and what it draws.
std::string parts_text(Il2CppObject* copy) {
    std::string out = light_list(copy, false);
    char text[200];
    Il2CppObject* renderers = components_in_children(copy, api.renderer_type);
    for (size_t i = 0, n = il2::array_length(renderers); i < n; ++i) {
        Il2CppObject* r = il2::array_object_at(renderers, i);
        Il2CppObject* materials = il2::invoke(api.shared_materials, r);
        Il2CppObject* first = il2::array_length(materials) ? il2::array_object_at(materials, 0) : nullptr;
        Bounds b{};
        il2::invoke_value(api.renderer_bounds, r, b);
        std::string cls = il2::class_name_of(r);
        if (cls.rfind("UnityEngine.", 0) == 0) cls = cls.substr(12);
        snprintf(text, sizeof(text), "%s%s %s (%s, %.2f x %.2f x %.2f m)", out.empty() ? "" : "; ", cls.c_str(),
                 name_of(il2::invoke(pv.component_go, r)).c_str(), first ? name_of(first).c_str() : "no material",
                 b.extents.x * 2, b.extents.y * 2, b.extents.z * 2);
        out += text;
    }
    return out.empty() ? "nothing" : out;
}

// TLDCoop_DLC: TLD 2.58 has no GearItem.LoadGearItemPrefab, so a held copy comes from GearItem.InstantiateGearItem: a
// real, awake item. Its own scripts (a flare's FlareItem, say: DisableRendering, m_RenderingDisabled) switch its
// renderers off until TLD treats it as in hand or thrown, and strip_held then removes the scripts that would switch
// them back on: the friend's flare was invisible. With the scripts gone, every renderer of the copy is switched back
// on here (a part that should hide, like a lit flare's cap, is a GameObject light_up switches, not a renderer).
// Returns how many were off.
int wake_renderers(Il2CppObject* copy) {
    if (!api.renderer_enabled || !api.renderer_set_enabled) return 0;
    Il2CppObject* renderers = components_in_children(copy, api.renderer_type);
    int woken = 0;
    for (size_t i = 0, n = il2::array_length(renderers); i < n; ++i) {
        Il2CppObject* r = il2::array_object_at(renderers, i);
        bool enabled = true;
        if (!r || !il2::invoke_value(api.renderer_enabled, r, enabled) || enabled) continue;
        bool on = true;
        void* args[] = {&on};
        il2::invoke(api.renderer_set_enabled, r, args);
        ++woken;
    }
    return woken;
}

bool make_held(Avatar& a, const std::string& gear, net::HeldKind kind) {
    if ((!api.load_gear && !api.instantiate_gear) || !pv.component_go || !ensure_loader()) return false;
    void* name_arg[] = {il2::il2cpp_string_new(gear.c_str())};
    Il2CppObject* copy = nullptr;
    bool awake_copy = false;  // copied from a real item TLD made (2.58): its scripts may have switched renderers off
    if (api.load_gear) {  // 2.55: the prefab itself
        Il2CppObject* prefab_gi = il2::invoke(api.load_gear, nullptr, name_arg);
        Il2CppObject* prefab = prefab_gi ? il2::invoke(pv.component_go, prefab_gi) : nullptr;
        if (prefab) {
            void* inst_args[] = {prefab, target(g_loader_transform)};  // asleep under the inactive loader: no Awake
            copy = il2::invoke(api.instantiate, nullptr, inst_args);
        }
    } else if (api.instantiate_gear) {
        // TLDCoop_DLC, 2.58: no prefab, only GearItem.InstantiateGearItem, which makes a real, awake item. That item
        // can't be the copy: once strip_held removes its GearItem, the GearItem's own OnDestroy destroys the whole
        // object, the copy was gone the next frame and made again every frame (an invisible flare, and Transform
        // calls on a dead object in the log). So it is only the model: the copy is cloned from it asleep under the
        // inactive loader, as from 2.55's prefab (scripts that never woke don't run OnDestroy when stripped), and the
        // real item is switched off and destroyed at once, before the world's item scan can see it.
        Il2CppObject* live_gi = il2::invoke(api.instantiate_gear, nullptr, name_arg);
        Il2CppObject* live = live_gi ? il2::invoke(pv.component_go, live_gi) : nullptr;
        if (live) {
            void* inst_args[] = {live, target(g_loader_transform)};
            copy = il2::invoke(api.instantiate, nullptr, inst_args);
            set_active(live, false);
            destroy(live, true);
            awake_copy = copy != nullptr;
        }
    }
    if (!copy) {
        say(a, "no model for %s here (TLD has no gear prefab by that name)", gear.c_str());
        return false;
    }
    Il2CppObject* tf = transform_of(copy);
    if (!tf) return false;
    HeldCopy& h = a.held;
    h.gear = gear;
    h.kind = kind;
    h.go = hold(copy);
    h.transform = hold(tf);
    const std::string name = "TLDCoop_Held_" + gear;
    void* name_args[] = {il2::il2cpp_string_new(name.c_str())};
    il2::invoke(api.set_name, copy, name_args);
    read_parts(h, copy);
    if (kind == net::HeldKind::Match) add_own_light(h, copy);  // TLDCoop_DLC
    const int removed = strip_held(copy);
    if (awake_copy) {
        if (const int woken = wake_renderers(copy))
            say(a, "%s: %d renderer(s) its own scripts had switched off are on again", gear.c_str(), woken);
    }
    logx::info("avatar: %s is made of: %s", gear.c_str(), parts_text(copy).c_str());
    light_up(h, 0, false);
    set_parent(tf, target(a.root_transform));  // awake here, under the avatar (hidden with it)
    set_vec(api.set_local_position, tf, {0, 0, 0});
    set_rotation(tf, Quat{});
    measure_held(h);
    // TLDCoop_DLC: its main model always shows. A flare's stick sits under the object TLD shows only while it burns
    // (m_FXGameObject): unlit, only the 5 cm cap was left, in the fist, and the friend's flare couldn't be seen.
    // Everything shown for a moment to find its biggest model; what holds that model is no longer hidden when unlit.
    if (api.tf_parent && api.renderer_bounds && !h.fx_low.empty()) {
        show_objects(h.fx_low, true);
        show_objects(h.fx_high, true);
        show_objects(h.hide_lit, true);
        Il2CppObject* renderers = components_in_children(copy, api.renderer_type);
        Il2CppObject* main = nullptr;
        float biggest = -1;
        for (size_t i = 0, n = il2::array_length(renderers); i < n; ++i) {
            Il2CppObject* r = il2::array_object_at(renderers, i);
            const std::string cls = il2::class_name_of(r);
            if (cls != "UnityEngine.MeshRenderer" && cls != "UnityEngine.SkinnedMeshRenderer") continue;
            Bounds b{};
            if (!il2::invoke_value(api.renderer_bounds, r, b)) continue;
            const float size = b.extents.x * b.extents.x + b.extents.y * b.extents.y + b.extents.z * b.extents.z;
            if (size > biggest) biggest = size, main = r;
        }
        std::string freed;
        for (Il2CppObject* t = main ? transform_of(il2::invoke(pv.component_go, main)) : nullptr; t && t != tf;
             t = il2::invoke(api.tf_parent, t)) {
            Il2CppObject* go = il2::invoke(pv.component_go, t);
            bool was_listed = false;
            for (auto* list : {&h.fx_low, &h.fx_high}) {
                const size_t before = list->size();
                list->erase(std::remove_if(list->begin(), list->end(), [&](uintptr_t x) { return target(x) == go; }),
                            list->end());
                was_listed |= list->size() != before;
            }
            if (was_listed) freed += (freed.empty() ? "" : ", ") + name_of(go);
        }
        light_up(h, 0, false);
        if (!freed.empty()) {
            say(a, "%s: its model (%s) was under %s, shown only while lit: shown always now", gear.c_str(),
                name_of(main).c_str(), freed.c_str());
            measure_held(h);
        }
    }
    say(a, "holds a copy of %s: %zu lit part(s); lights indoors %s, outdoors %s%s; %d script/physics part(s) "
           "removed; %.2f m long along (%.0f, %.0f, %.0f)%s",
        gear.c_str(), h.fx_low.size() + h.fx_high.size(), lights_text(h.lights_in).c_str(),
        lights_text(h.lights_out).c_str(),
        h.lights_in_high.empty() && h.lights_out_high.empty() ? "" : ", and for its high beam", removed, h.half * 2,
        h.axis.x, h.axis.y, h.axis.z,
        h.half < 0.005f ? "; nothing of it is drawn (its model is empty in this game: only the arms show the hold)" : "");
    if (!h.flicker.empty()) say(a, "its flame at full strength: lights flicker %s (TorchIntensity's curves)", h.flicker.c_str());
    return true;
}

// Their State's hands, in the Update pass: the copy made, replaced or removed, its lights as theirs.
void sync_held(Avatar& a, const net::StateMsg& s, bool indoors) {
    net::HeldKind kind = net::held_kind(s.held_kind);
    std::string want = kind == net::HeldKind::None ? std::string() : s.held;
    if (a.handover_on) {  // a handover: what is handed over, while it is in this hand; nothing else
        want = a.handover.item_in_hand ? a.handover.gear : std::string();
        if (want != s.held) kind = net::HeldKind::Other;
    } else if (a.lying) {
        want.clear();
    }
    a.held_flags = s.held_flags;
    const std::string said = net::describe_held(want, kind, s.held_flags);
    if (said != a.held_logged) {
        say(a, "holds %s", said.c_str());
        a.held_logged = said;
    }
    if (want != a.held.gear || (a.held.go && !is_alive(target(a.held.go)))) {
        remove_held(a.held);
        if (!want.empty() && want != a.held_failed && !make_held(a, want, kind)) a.held_failed = want;
    }
    if (!a.held.go) return;
    const int level = !net::gives_light(kind, s.held_flags) ? 0 : (s.held_flags & net::kHeldHigh) ? 2 : 1;
    if (level != a.held.lit_shown || indoors != a.held.indoors_shown) {
        if (level >= 1 && a.held.lit_shown < 1) a.held.lit_at = net::now_ms();
        light_up(a.held, level, indoors);
    }
    if (a.held.lit_at > 0 && net::now_ms() - a.held.lit_at > 2000) {  // TLDCoop_DLC
        a.held.lit_at = 0;
        if (a.held.lit_shown >= 1)
            say(a, "%s lit, %s: %s", a.held.gear.c_str(), indoors ? "indoors" : "outdoors", describe_lights(a.held).c_str());
    }
    keep_lit(a, a.held, indoors);  // TLDCoop_DLC
    if (!a.held.emitters.empty()) emit_flame(a.held);  // TLDCoop_DLC
}

// Holds as tuned with the dev command `hold` (else net::pose::hold_for), and items whose long axis is the other way
// round from what measure_held thought.
std::map<std::string, net::pose::Hold> g_holds_tuned;
std::map<int, bool> g_flipped;
float g_pitch_sign = 1;  // State's pitch (vp_FPSCamera.m_Pitch): up positive

std::string hold_key(net::HeldKind kind, uint8_t flags) {
    std::string key = net::held_kind_name(kind);
    std::replace(key.begin(), key.end(), ' ', '_');
    if (flags & net::kHeldAiming) key += "_aim";
    if (kind == net::HeldKind::Spear && (flags & net::kHeldRaised)) key += "_raised";
    return key;
}

net::pose::Hold hold_of(net::HeldKind kind, uint8_t flags) {
    const auto it = g_holds_tuned.find(hold_key(kind, flags));
    return it != g_holds_tuned.end() ? it->second : net::pose::hold_for(kind, flags);
}

struct Arm {
    int up, elbow, hand;
};
constexpr Arm kRightArm{kRightUpArm, kRightElbow, kRightHand}, kLeftArm{kLeftUpArm, kLeftElbow, kLeftHand};

bool has_bones(const Avatar& a) {  // the arms'
    for (int b = 0; b < kArmBones; ++b)
        if (!a.bones[b]) return false;
    return true;
}

Il2CppObject* bone(const Avatar& a, int b) { return target(a.bones[b]); }

// Turns the arm so its hand gets to `to`, the elbow bent towards `pole`: the clip's pose of that arm is replaced.
// Shoulder joint first (its elbow onto the reach's), then the elbow (its hand onto the target).
void reach(const Avatar& a, Arm arm, PVec to, PVec pole) {
    Il2CppObject *up = bone(a, arm.up), *elbow = bone(a, arm.elbow), *hand = bone(a, arm.hand);
    const PVec s = position_of(up), e = position_of(elbow), w = position_of(hand);
    const float upper = net::pose::length(e - s), lower = net::pose::length(w - e);
    if (upper < 0.05f || lower < 0.05f) return;
    PVec reached{};
    const PVec e2 = net::pose::elbow_for(s, upper, lower, to, pole, &reached);
    set_rotation(up, net::pose::from_to(e - s, e2 - s) * rotation_of(up));
    const PVec e3 = position_of(elbow), w3 = position_of(hand);
    set_rotation(elbow, net::pose::from_to(w3 - e3, reached - e3) * rotation_of(elbow));
}

// The middle of the palm, a little past the wrist joint.
PVec palm(const Avatar& a, Arm arm) {
    const PVec e = position_of(bone(a, arm.elbow)), w = position_of(bone(a, arm.hand));
    return w + net::pose::normalized(w - e) * 0.07f;
}

// Elbows hang down, out and a little back.
PVec pole_for(const net::pose::Frame& f) { return f.origin + f.out * 0.35f - f.up * 0.5f - f.forward * 0.25f; }

// --- TLDCoop_DLC: hands (their turn and fingers), for gestures and grips ---------------------------------------------
// Worked out on the body pack's rig offline (its bones and skin rendered in each pose) before going in here. The hand
// is turned by where its knuckles are: fingers = wrist to the middle knuckle, across = the pinky's knuckle to the
// index's, palm = out of the palm (fingers x across for the right hand, the other way for the left).

Il2CppObject* rig_bone(const Avatar& a, const std::string& name) {
    const auto it = a.rig.find(name);
    return it != a.rig.end() ? target(it->second) : nullptr;
}
PVec unit(PVec v) { return net::pose::normalized(v); }
Quat turn_about(PVec axis, float deg) {
    const PVec ax = net::pose::normalized(axis);
    const float h = deg * kDeg * 0.5f, s = std::sin(h);
    return {ax.x * s, ax.y * s, ax.z * s, std::cos(h)};
}
void turn_bone(Il2CppObject* b, PVec axis, float deg) {
    if (b && std::fabs(deg) > 1e-3f) set_rotation(b, turn_about(axis, deg) * rotation_of(b));
}
float smooth01(float x) {
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}
float ease_out_back(float x, float k) {
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    x -= 1;
    return 1 + x * x * ((k + 1) * x + k);
}
PVec lerpv(PVec a, PVec b, float t) { return a + (b - a) * t; }

struct HandDirs {
    PVec fingers, across, palm;
    bool ok = false;
};
// The hand's directions now (world), from its knuckles.
HandDirs hand_dirs(const Avatar& a, bool right) {
    HandDirs d;
    const std::string s = right ? "Right" : "Left";
    Il2CppObject* hand = bone(a, right ? kRightHand : kLeftHand);
    Il2CppObject *mid = rig_bone(a, s + "Middle1_jnt"), *ix = rig_bone(a, s + "Index1_jnt"), *pk = rig_bone(a, s + "Pinky1_jnt");
    if (!hand || !mid || !ix || !pk) return d;
    d.fingers = unit(position_of(mid) - position_of(hand));
    PVec l = position_of(ix) - position_of(pk);
    d.across = unit(l - d.fingers * net::pose::dot(l, d.fingers));
    d.palm = net::pose::cross(d.fingers, d.across) * (right ? 1.0f : -1.0f);
    d.ok = net::pose::length(d.fingers) > 0.5f && net::pose::length(d.across) > 0.5f;
    return d;
}
// Turn the hand so its fingers point along `fingers` and its palm faces `palm` (blended by w from the clip's).
void orient_hand(const Avatar& a, bool right, PVec fingers, PVec palm, float w = 1) {
    const HandDirs d = hand_dirs(a, right);
    Il2CppObject* hand = bone(a, right ? kRightHand : kLeftHand);
    if (!d.ok || !hand) return;
    const PVec f = unit(lerpv(d.fingers, fingers, w)), p = unit(lerpv(d.palm, palm, w));
    const Quat delta = net::pose::look_rotation(f, p) * net::pose::inverse(net::pose::look_rotation(d.fingers, d.palm));
    set_rotation(hand, delta * rotation_of(hand));
}
// Bend a finger's three joints towards the palm (degrees; negative straightens it past the clip's).
void curl(const Avatar& a, bool right, const char* finger, float d1, float d2, float d3) {
    const HandDirs h = hand_dirs(a, right);
    if (!h.ok) return;
    const std::string s = std::string(right ? "Right" : "Left") + finger;
    const float degs[3] = {d1, d2, d3};
    for (int k = 1; k <= 3; ++k) {
        Il2CppObject* j = rig_bone(a, s + std::to_string(k) + "_jnt");
        Il2CppObject* c = rig_bone(a, s + std::to_string(k + 1) + "_jnt");
        if (!j || !c) continue;
        const PVec ax = net::pose::cross(position_of(c) - position_of(j), h.palm);
        if (net::pose::length(ax) > 1e-5f) turn_bone(j, ax, degs[k - 1]);
    }
}
void thumb_curl(const Avatar& a, bool right, float d1, float d2, float d3) {
    const HandDirs h = hand_dirs(a, right);
    if (!h.ok) return;
    const std::string s = std::string(right ? "Right" : "Left") + "Thumb";
    const float degs[3] = {d1, d2, d3};
    for (int k = 1; k <= 3; ++k) {
        Il2CppObject* j = rig_bone(a, s + std::to_string(k) + "_jnt");
        Il2CppObject* c = rig_bone(a, s + std::to_string(k < 3 ? k + 1 : k) + "_jnt");
        Il2CppObject* p = rig_bone(a, s + std::to_string(k > 1 ? k - 1 : k) + "_jnt");
        if (!j) continue;
        const PVec along = k < 3 && c ? position_of(c) - position_of(j) : (p ? position_of(j) - position_of(p) : h.fingers);
        const PVec ax = net::pose::cross(along, h.palm);
        if (net::pose::length(ax) > 1e-5f) turn_bone(j, ax, degs[k - 1]);
    }
}
enum class HandShape { Flat, Open, Point, Grip, Fist, Beckon };
// The fingers over the clip's (`w`: how much; `extra`: a beckon's curl, 0..1).
void hand_shape(const Avatar& a, bool right, HandShape shape, float w, float extra = 0) {
    static const char* kFingers[] = {"Index", "Middle", "Ring", "Pinky"};
    switch (shape) {
        case HandShape::Flat:
            for (const char* f : kFingers) curl(a, right, f, -14 * w, -10 * w, -6 * w);
            thumb_curl(a, right, -8 * w, 0, 0);
            break;
        case HandShape::Open:
            for (const char* f : kFingers) curl(a, right, f, 4 * w, 6 * w, 4 * w);
            break;
        case HandShape::Point:
            curl(a, right, "Index", -6 * w, -4 * w, -2 * w);
            for (const char* f : {"Middle", "Ring", "Pinky"}) curl(a, right, f, 80 * w, 95 * w, 60 * w);
            thumb_curl(a, right, 20 * w, 25 * w, 20 * w);
            break;
        case HandShape::Grip:
        case HandShape::Fist: {
            const float c = shape == HandShape::Fist ? 1.0f : 0.75f;
            for (const char* f : kFingers) curl(a, right, f, 70 * w * c, 85 * w * c, 55 * w * c);
            thumb_curl(a, right, 25 * w, 30 * w, 20 * w);
            break;
        }
        case HandShape::Beckon:
            curl(a, right, "Index", (10 + 70 * extra) * w, (10 + 80 * extra) * w, (5 + 50 * extra) * w);
            curl(a, right, "Middle", (10 + 65 * extra) * w, (10 + 75 * extra) * w, (5 + 45 * extra) * w);
            for (const char* f : {"Ring", "Pinky"}) curl(a, right, f, (20 + 50 * extra) * w, (20 + 60 * extra) * w, (10 + 40 * extra) * w);
            break;
    }
}
// Inside the closed fist: where a handle runs.
PVec palm_centre(const Avatar& a, bool right) {
    const HandDirs d = hand_dirs(a, right);
    const PVec h = position_of(bone(a, right ? kRightHand : kLeftHand));
    if (!d.ok) return palm(a, right ? kRightArm : kLeftArm);
    return h + d.fingers * 0.075f + d.palm * 0.04f;  // (inside the curled fingers, measured on the rig)
}
// Half of the hand's twist about the forearm goes to the forearm's twist bone: no wrung wrist.
void relax_wrist(const Avatar& a, bool right) {
    Il2CppObject* e = bone(a, right ? kRightElbow : kLeftElbow);
    Il2CppObject* h = bone(a, right ? kRightHand : kLeftHand);
    Il2CppObject* fa = rig_bone(a, right ? "RightForearm_jnt" : "LeftForearm_jnt");
    if (!e || !h || !fa) return;
    const Quat qe = rotation_of(e);
    const Quat rel = net::pose::inverse(qe) * rotation_of(h);
    const PVec ax = unit(net::pose::rotate(net::pose::inverse(qe), position_of(h) - position_of(e)));
    const float along = rel.x * ax.x + rel.y * ax.y + rel.z * ax.z;
    const float n = std::sqrt(along * along + rel.w * rel.w);
    if (n < 1e-6f) return;
    float deg = 2 * std::atan2(along / n, rel.w / n) / kDeg;
    if (deg > 180) deg -= 360;
    if (deg < -180) deg += 360;
    turn_bone(fa, position_of(h) - position_of(e), 0.5f * deg);
}
// A raised arm takes its shoulder (clavicle) up with it: degrees it went up.
float lift_shoulder(const Avatar& a, bool right) {
    Il2CppObject* cl = rig_bone(a, right ? "RightShoulder_jnt" : "LeftShoulder_jnt");
    if (!cl) return 0;
    const PVec sh = position_of(bone(a, right ? kRightUpArm : kLeftUpArm)), el = position_of(bone(a, right ? kRightElbow : kLeftElbow));
    float c = net::pose::dot(unit(el - sh), PVec{0, -1, 0});
    c = c < -1 ? -1 : c > 1 ? 1 : c;
    const float elev = std::acos(c) / kDeg;
    const float deg = std::min(16.0f, std::max(0.0f, (elev - 55) * 0.3f));
    if (deg <= 0) return 0;
    const net::pose::Frame f = net::pose::frame(sh, a.shown_yaw, 0, true);
    turn_bone(cl, f.forward, right ? -deg : deg);
    return deg;
}
// reach, the shoulder up with a raised arm, then reach again from where the shoulder is now.
void reach2(const Avatar& a, Arm arm, PVec to, PVec pole) {
    reach(a, arm, to, pole);
    if (lift_shoulder(a, arm.hand == kRightHand) > 0) reach(a, arm, to, pole);
}

// How a hand closes round an item: through the fist, index side towards its front (Stick: torch, flare, flashlight,
// spear, bow, most things); round a grip under it, the wrist in line with it (Pistol: guns, camera); a handle at the
// side (Hang: lantern); under it, palm up (Support: the other hand on a gun or spear); cupped (Palm: a stone).
enum class Wrap { Stick, Pistol, Hang, Support, Palm };
Wrap wrap_for(net::HeldKind kind) {
    switch (kind) {
        case net::HeldKind::Rifle:
        case net::HeldKind::Revolver:
        case net::HeldKind::FlareGun:
        case net::HeldKind::Camera:
            return Wrap::Pistol;
        case net::HeldKind::Lantern:
            return Wrap::Hang;
        case net::HeldKind::Stone:
        case net::HeldKind::NoiseMaker:
            return Wrap::Palm;
        default:
            return Wrap::Stick;
    }
}
// Turn the hand round an item along `dir` (its up `up`) and close the fingers on it.
void wrap_hand(const Avatar& a, bool right, Wrap mode, PVec dir, PVec up, bool shape = true) {
    const float sgn = right ? 1.0f : -1.0f;
    Il2CppObject* hand = bone(a, right ? kRightHand : kLeftHand);
    Il2CppObject* elbow = bone(a, right ? kRightElbow : kLeftElbow);
    if (!hand || !elbow) return;
    const PVec fore = unit(position_of(hand) - position_of(elbow));
    PVec f, l, p;
    switch (mode) {
        case Wrap::Stick:
        case Wrap::Hang:
            l = dir;
            f = fore - l * net::pose::dot(fore, l);
            f = net::pose::length(f) < 1e-3f ? up : unit(f);
            p = net::pose::cross(f, l) * sgn;
            break;
        case Wrap::Pistol:
            l = unit(up - dir * net::pose::dot(up, dir));
            f = unit(dir - l * net::pose::dot(dir, l));
            p = net::pose::cross(f, l) * sgn;
            break;
        case Wrap::Support:
            l = dir;
            p = unit(up - dir * net::pose::dot(up, dir));
            f = net::pose::cross(l, p) * sgn;
            orient_hand(a, right, f, p);
            if (shape) hand_shape(a, right, HandShape::Grip, 0.55f);
            return;
        case Wrap::Palm:
            f = fore;
            l = unit(net::pose::cross(fore, PVec{0, 1, 0})) * sgn;
            p = net::pose::cross(f, l) * sgn;
            orient_hand(a, right, f, p);
            if (shape) hand_shape(a, right, HandShape::Open, 1);
            return;
    }
    orient_hand(a, right, f, p);
    if (shape) hand_shape(a, right, HandShape::Grip, 1);  // (the bends add to the clip's: once a frame)
}

// The copy, turned as the hold says (its long axis along item_dir, in frame f), with its grip at `grip`.
void place_held(const Avatar& a, const net::pose::Hold& h, PVec grip, const net::pose::Frame& f) {
    const HeldCopy& c = a.held;
    Il2CppObject* tf = target(c.transform);
    if (!tf || !c.measured) return;
    const PVec axis = g_flipped[static_cast<int>(c.kind)] ? c.axis * -1.0f : c.axis;
    PVec second = c.second - axis * net::pose::dot(c.second, axis);
    if (net::pose::length(second) < 1e-3f) second = std::fabs(axis.y) < 0.9f ? PVec{0, 1, 0} : PVec{0, 0, 1};
    const PVec level_forward = net::pose::normalized(PVec{f.forward.x, 0, f.forward.z});
    const PVec dir = h.hangs ? PVec{0, 1, 0} : f.dir(h.item_dir);
    const PVec up = h.hangs ? level_forward : f.dir(h.item_up);
    const Quat q = net::pose::look_rotation(dir, up) * net::pose::inverse(net::pose::look_rotation(axis, second));
    const PVec grip_local = c.centre + axis * (h.grip * c.half);
    set_vec(api.set_position, tf, uv3(grip - net::pose::rotate(q, grip_local)));
    set_rotation(tf, q);
}

bool has_legs(const Avatar& a) {
    for (int b = kArmBones; b < kBoneCount; ++b)
        if (!a.bones[b]) return false;
    return true;
}

// The copy in a hand as the action clips hold their stand-in props (lying along the prop's +x with its grip at the
// prop's origin): `pose` is the prop in the bone's space; `along`, where on the item that grip is (in half lengths
// from its middle, towards its front).
void place_on_bone(const Avatar& a, int b, const net::HandPose& pose, float along) {
    const HeldCopy& c = a.held;
    Il2CppObject* tf = target(c.transform);
    Il2CppObject* hand = bone(a, b);
    if (!tf || !hand || !c.measured) return;
    const Quat hq = rotation_of(hand);
    const Quat prop = hq * Quat{pose.rot[0], pose.rot[1], pose.rot[2], pose.rot[3]};
    const PVec origin = position_of(hand) + net::pose::rotate(hq, PVec{pose.pos[0], pose.pos[1], pose.pos[2]});
    const PVec axis = g_flipped[static_cast<int>(c.kind)] ? c.axis * -1.0f : c.axis;
    PVec second = c.second - axis * net::pose::dot(c.second, axis);
    if (net::pose::length(second) < 1e-3f) second = std::fabs(axis.y) < 0.9f ? PVec{0, 1, 0} : PVec{0, 0, 1};
    const Quat q = net::pose::look_rotation(net::pose::rotate(prop, {1, 0, 0}), net::pose::rotate(prop, {0, 1, 0})) *
                   net::pose::inverse(net::pose::look_rotation(axis, second));
    const PVec grip_local = c.centre + axis * (along * c.half);
    set_vec(api.set_position, tf, uv3(origin - net::pose::rotate(q, grip_local)));
    set_rotation(tf, q);
}

// Crouching (kStateCrouched) has no clip: after whatever clip plays, the hips go down and back, the pelvis and the
// spine lean forward, and each leg bends (thigh and knee turned, the knee forward and a little out) to keep its foot
// where the clip put it, turned as it was. The arms (and what they hold) are posed after this, from where the
// shoulders are now. Depth and back are of the leg's length; tilt and lean in degrees.
struct CrouchShape {
    float depth = 0.36f, back = 0.07f, tilt = 12, lean = 20, knees_out = 0.12f;
} g_crouch;

// How a body lies in bed: TLD's body placement turned this much about its up (the sleep clip lies head to its -z;
// unturned it lies across the bed: -90 puts the head at the pillow, seen in LakeCabinC's and CampOffice's bunks and
// a trailer's bed), and raised this much (onto the mattress).
float g_bed_yaw = -90, g_bed_lift = 0.0f;
std::string g_forced_action;  // dev `pose act NAME T`: every body shows this action clip at time T (-1: playing)
float g_forced_time = -1;

void crouch_pose(const Avatar& a, float amount) {
    const float yaw = a.shown_yaw * kDeg;
    const PVec fwd{std::sin(yaw), 0, std::cos(yaw)}, right{std::cos(yaw), 0, -std::sin(yaw)}, up{0, 1, 0};
    struct Leg {
        Arm bones;
        float out;
        PVec foot;
        Quat turn;
    } legs[2] = {{{kLeftThigh, kLeftKnee, kLeftFoot}, -1, {}, {}}, {{kRightThigh, kRightKnee, kRightFoot}, 1, {}, {}}};
    for (Leg& l : legs) {
        l.foot = position_of(bone(a, l.bones.hand));
        l.turn = rotation_of(bone(a, l.bones.hand));
    }
    const float leg = net::pose::length(position_of(bone(a, kLeftThigh)) - position_of(bone(a, kLeftKnee))) +
                      net::pose::length(position_of(bone(a, kLeftKnee)) - legs[0].foot);
    Il2CppObject* hips = bone(a, kHips);
    const PVec h = position_of(hips);
    set_vec(api.set_position, hips, uv3(h - up * (g_crouch.depth * leg * amount) - fwd * (g_crouch.back * leg * amount)));
    const auto lean = [&](Il2CppObject* b, float degrees) {
        const float r = degrees * amount * kDeg;
        set_rotation(b, net::pose::from_to(up, up * std::cos(r) + fwd * std::sin(r)) * rotation_of(b));
    };
    lean(hips, g_crouch.tilt);
    lean(bone(a, kSpine1), g_crouch.lean);
    for (const Leg& l : legs) {
        const PVec knee = position_of(bone(a, l.bones.elbow));
        reach(a, l.bones, l.foot, knee + fwd * 0.6f + right * (l.out * g_crouch.knees_out));
        set_rotation(bone(a, l.bones.hand), l.turn);
    }
}

// The item an action has in hand: being eaten, or handed over (in the giver's right hand until the transfer, then
// the receiver's left). False: no action has it (m11's holds pose the arms).
bool place_action_item(const Avatar& a) {
    if (a.action.empty() || a.lying) return a.lying;  // in bed: nothing in hand
    if (a.handover_on) {
        if (!a.handover.item_in_hand) return true;  // the other one has it: this hand is empty
        const net::HandItem k = net::hand_item(a.handover.item);
        if (a.handover.giver)
            place_on_bone(a, kRightHand, net::kGiverGrip, net::grip_along(k));
        else
            place_on_bone(a, kLeftHand, net::receiver_grip(a.handover.giver_female, a.female, k), net::grip_along(k));
        return true;
    }
    if (a.action == "eat") {
        place_on_bone(a, kRightHand, kEatGrip, 0);
        return true;
    }
    return false;
}

// After the clip has posed the body (GameManager.LateUpdate): crouching, then the item in the hand (an action's, or
// the arms as the hold says).
// TLDCoop_DLC: their gesture over the clip: the right arm reaches where the gesture has it (blended in and out), or
// the head nods or shakes. Returns false when there's none to show now.
bool gesture(Avatar& a) {
    // (not over an action's clip: those don't set the fingers each frame, so a bend would add up)
    if (a.emote == net::Emote::None || !has_bones(a) || a.shown_down || a.lying || !a.action.empty()) return false;
    const float t = static_cast<float>((net::now_ms() - a.emote_start) / 1000.0);
    const float total = net::emote_seconds(a.emote);
    if (t < 0 || t >= total) return false;
    constexpr float kTau = 6.2831853f;
    const PVec up{0, 1, 0};
    // In over `rise` seconds (a little past and back when `back`), out over the last `fall`.
    auto envelope = [&](float rise, float fall, bool back) {
        if (t < rise) return back ? ease_out_back(t / rise, 1.2f) : smooth01(t / rise);
        if (total - t < fall) return smooth01((total - t) / fall);
        return 1.0f;
    };
    if (a.emote == net::Emote::Yes || a.emote == net::Emote::No) {
        Il2CppObject* head = bone(a, kHead);
        if (!head) return false;
        Il2CppObject* neck = rig_bone(a, "Neck_jnt");
        const float w = envelope(0.15f, 0.25f, false), k = t / total;
        const net::pose::Frame f = net::pose::frame(position_of(head), a.shown_yaw, 0, true);
        if (a.emote == net::Emote::Yes) {  // down and back up, three times, each smaller; the neck takes a share
            const float deg = 17 * (1 - 0.55f * k) * w * 0.5f * (1 - std::cos(t * kTau * 2.0f));
            turn_bone(neck, f.out, deg * 0.35f);
            turn_bone(head, f.out, neck ? deg * 0.65f : deg);
        } else {  // side to side, getting smaller, the head a beat behind the neck
            const float amp = 24 * (1 - 0.5f * k) * w;
            turn_bone(neck, up, amp * 0.4f * std::sin(t * kTau * 2.3f));
            turn_bone(head, up, amp * (neck ? 0.6f : 1.0f) * std::sin(t * kTau * 2.3f - 0.5f));
        }
        return false;  // (the arms and what they hold as ever)
    }
    const PVec shoulder = position_of(bone(a, kRightUpArm));
    const net::pose::Frame f0 = net::pose::frame(shoulder, a.shown_yaw, 0, true);
    const PVec start = position_of(bone(a, kRightHand));  // the clip's hand
    const HandDirs h0 = hand_dirs(a, true);
    Il2CppObject* clavicle = rig_bone(a, "RightShoulder_jnt");
    Il2CppObject* spine2 = rig_bone(a, "Spine2_jnt");
    switch (a.emote) {
        case net::Emote::Wave: {  // forearm up beside the head, swinging from the elbow, the open hand a beat behind
            const float w = envelope(0.4f, 0.4f, true);
            const float live = smooth01((t - 0.3f) / 0.3f) * smooth01((total - 0.35f - t) / 0.3f);
            const float swing = t > 0.3f ? 22 * std::sin((t - 0.4f) * kTau * 1.9f) * live : 0;
            const float lag = t > 0.3f ? 22 * std::sin((t - 0.4f) * kTau * 1.9f - 0.9f) * live : 0;
            const float upper = net::pose::length(position_of(bone(a, kRightElbow)) - shoulder);
            const float lower = net::pose::length(position_of(bone(a, kRightHand)) - position_of(bone(a, kRightElbow)));
            const PVec elbow = shoulder + unit(f0.out * 0.92f - f0.up * 0.25f + f0.forward * 0.3f) * upper;
            const float sa = (swing - 8) * kDeg;
            const PVec fore = unit(f0.up * std::cos(sa) + f0.out * std::sin(sa) + f0.forward * 0.12f);
            turn_bone(clavicle, f0.forward, -10 * w);  // the shoulder rises a little
            turn_bone(spine2, f0.forward, 3 * w);      // leaning a touch away
            reach(a, kRightArm, lerpv(start, elbow + fore * lower, w), elbow + f0.out * 0.4f - f0.up * 0.3f);
            (void)clavicle;
            const float b = (swing * 0.6f + lag * 0.5f - 8) * kDeg;
            if (h0.ok)
                orient_hand(a, true, unit(f0.up * std::cos(b) + f0.out * std::sin(b)), unit(f0.forward - f0.out * 0.15f), w);
            hand_shape(a, true, HandShape::Flat, w);
            break;
        }
        case net::Emote::Point: {  // arm out where they look, index out, the shoulder brought round
            const float w = envelope(0.3f, 0.35f, true);
            const net::pose::Frame look = net::pose::frame(shoulder, a.shown_yaw, g_pitch_sign * a.pitch, true);
            turn_bone(spine2, up, -8 * w);
            turn_bone(clavicle, up, -6 * w);
            const PVec s2 = position_of(bone(a, kRightUpArm));
            reach2(a, kRightArm, lerpv(start, s2 + unit(look.forward + f0.out * 0.08f) * 0.56f, w),
                   s2 + f0.out * 0.3f - f0.up * 0.5f);
            if (h0.ok) orient_hand(a, true, unit(look.forward + f0.out * 0.05f), unit(look.up * -0.75f - f0.out * 0.25f), w);
            hand_shape(a, true, HandShape::Point, w);
            break;
        }
        case net::Emote::ComeHere: {  // palm up in front, the fingers and wrist drawing in
            const float w = envelope(0.35f, 0.35f, false);
            float ph = t > 0.35f ? 0.5f - 0.5f * std::cos((t - 0.35f) * kTau * 1.7f) : 0;
            ph *= smooth01((total - 0.3f - t) / 0.3f);
            reach(a, kRightArm, lerpv(start, f0.at({0.10f, -0.30f, 0.30f - 0.05f * ph}), w),
                  shoulder + f0.out * 0.4f - f0.up * 0.5f - f0.forward * 0.2f);
            const float bend = 25 * ph * kDeg;
            if (h0.ok) orient_hand(a, true, unit(f0.forward * std::cos(bend) + f0.up * std::sin(bend) + f0.out * 0.05f), up, w);
            hand_shape(a, true, HandShape::Beckon, w, ph);
            break;
        }
        case net::Emote::Stop: {  // the flat hand pushed out in front, palm forward, fingers up
            const float w = envelope(0.22f, 0.35f, true);
            const float push = t < 0.6f ? 0.04f * smooth01(t / 0.3f) * smooth01((0.6f - t) / 0.3f) : 0;
            reach2(a, kRightArm, lerpv(start, f0.at({0.04f, -0.06f, 0.44f + push}), w), shoulder + f0.out * 0.45f - f0.up * 0.5f);
            if (h0.ok) orient_hand(a, true, unit(f0.up + f0.forward * 0.15f - f0.out * 0.05f), f0.forward, w);
            hand_shape(a, true, HandShape::Flat, w);
            break;
        }
        default:
            return false;
    }
    relax_wrist(a, true);
    const net::pose::Frame right = net::pose::frame(position_of(bone(a, kRightUpArm)), a.shown_yaw, 0, true);
    if (a.held.go) place_held(a, hold_of(a.held.kind, a.held_flags), palm(a, kRightArm), right);
    return true;
}

void late_one(Avatar& a) {
    if (!a.shown || a.visibility != "shown") return;
    if (has_bones(a)) {
        const float yaw = a.shown_yaw * kDeg;
        const PVec d = position_of(bone(a, kRightHand)) - position_of(target(a.root_transform));
        a.clip_hand[0] = d.x * std::cos(yaw) - d.z * std::sin(yaw);
        a.clip_hand[1] = d.y;
        a.clip_hand[2] = d.x * std::sin(yaw) + d.z * std::cos(yaw);
    }
    if (a.crouch > 0.01f && has_legs(a) && !a.shown_down && !a.lying) crouch_pose(a, a.crouch);
    if (gesture(a)) return;  // TLDCoop_DLC: (it placed what they hold too)
    if (!a.held.go) return;
    if (has_bones(a) && !a.shown_down && place_action_item(a)) return;
    const net::pose::Hold h = hold_of(a.held.kind, a.held_flags);
    const float pitch = h.look ? g_pitch_sign * a.pitch : 0;
    if (!has_bones(a) || a.shown_down) {
        // The capsule, or on their back in a fight: nothing to pose; at the hand, or in front of the capsule.
        const PVec chest = position_of(target(a.root_transform)) + PVec{0, 1.3f, 0};
        const net::pose::Frame f = net::pose::frame(chest, a.shown_yaw, pitch, true);
        const Arm arm = h.in_left ? kLeftArm : kRightArm;
        place_held(a, h, has_bones(a) ? palm(a, arm) : f.at({0.3f, -0.25f, 0.35f}), f);
        return;
    }
    if (h.lean != 0 || h.head != 0) {  // TLDCoop_DLC: into the aim (spine and neck forward, the head down to the sights)
        const net::pose::Frame f = net::pose::frame(position_of(bone(a, kRightUpArm)), a.shown_yaw, 0, true);
        turn_bone(rig_bone(a, "Spine2_jnt"), f.out, h.lean);
        turn_bone(rig_bone(a, "Neck_jnt"), f.out, h.lean);
        turn_bone(bone(a, kHead), f.out, h.head);
    }
    const net::pose::Frame right = net::pose::frame(position_of(bone(a, kRightUpArm)), a.shown_yaw, pitch, true);
    const net::pose::Frame left = net::pose::frame(position_of(bone(a, kLeftUpArm)), a.shown_yaw, pitch, false);
    const Arm main = h.in_left ? kLeftArm : kRightArm, other = h.in_left ? kRightArm : kLeftArm;
    const net::pose::Frame& main_frame = h.in_left ? left : right;
    const net::pose::Frame& other_frame = h.in_left ? right : left;
    // TLDCoop_DLC: a rifle at the shoulder: the right elbow raised out, the left one under the fore-end
    const PVec main_pole = h.elbow_out && !h.in_left
                               ? main_frame.origin + main_frame.out * 0.5f - main_frame.up * 0.05f - main_frame.forward * 0.15f
                               : pole_for(main_frame);
    const PVec other_pole = h.elbow_out && !h.in_left
                                ? other_frame.origin - other_frame.up * 0.6f + other_frame.forward * 0.1f - other_frame.out * 0.05f
                                : pole_for(other_frame);
    if (h.in_left ? h.left : h.right) reach2(a, main, main_frame.at(h.in_left ? h.left_hand : h.right_hand), main_pole);
    // TLDCoop_DLC: the hand turned round the item and closed on it; the item through the fist (or on top of it, a gun)
    const Wrap wrap = wrap_for(a.held.kind);
    const PVec level_fwd = unit(PVec{right.forward.x, 0, right.forward.z});
    const PVec item_dir = h.hangs ? level_fwd : unit(right.dir(h.item_dir));
    const PVec item_up = h.hangs ? PVec{0, 1, 0} : unit(right.dir(h.item_up));
    const bool main_right = !h.in_left;
    const bool shaped = hand_dirs(a, main_right).ok;
    if (shaped) {
        wrap_hand(a, main_right, wrap, item_dir, item_up);
        relax_wrist(a, main_right);
    }
    PVec grip = shaped ? palm_centre(a, main_right) : palm(a, main);
    if (shaped && wrap == Wrap::Pistol) grip = grip + hand_dirs(a, main_right).across * 0.05f;  // above the fist
    place_held(a, h, grip, right);
    if (a.held_flags & net::kHeldIgniting) {
        // TLDCoop_DLC: being lit (a flare's cap, a match on the box): the other hand strikes along it, back and forth.
        const double t = std::fmod(net::now_ms() / 1000.0 * 2.5, 1.0);
        const float along = 0.04f + 0.18f * static_cast<float>(t < 0.5 ? t * 2 : 2 - t * 2);
        reach(a, other, grip + right.dir(h.item_dir) * along, pole_for(other_frame));
        return;
    }
    if (h.in_left ? h.right : h.left) {
        const PVec to = h.left_on_item >= 0 ? grip + right.dir(h.item_dir) * h.left_on_item
                                            : other_frame.at(h.in_left ? h.right_hand : h.left_hand);
        const bool other_right = h.in_left;
        if (h.left_on_item >= 0 && hand_dirs(a, other_right).ok) {
            // TLDCoop_DLC: the other hand under the item, palm up against it (its palm, not its wrist, at the spot)
            const Wrap other_wrap = h.left_fist ? Wrap::Stick : Wrap::Support;  // round it, or under it
            const PVec want = h.left_fist ? to : to - item_up * 0.035f;
            for (int i = 0; i < 3; ++i) {  // (the palm's offset depends on the hand's turn: settle it)
                wrap_hand(a, other_right, other_wrap, item_dir, item_up, false);
                const PVec off = palm_centre(a, other_right) - position_of(bone(a, other_right ? kRightHand : kLeftHand));
                if (i < 2)
                    reach(a, other, want - off, other_pole);
                else
                    reach2(a, other, want - off, other_pole);  // the shoulder up once, at the end
            }
            wrap_hand(a, other_right, other_wrap, item_dir, item_up, false);
            hand_shape(a, other_right, h.left_fist ? HandShape::Fist : HandShape::Grip, h.left_fist ? 1.0f : 0.55f);
            relax_wrist(a, other_right);
        } else {
            reach(a, other, to, pole_for(other_frame));
            if (hand_dirs(a, other_right).ok) hand_shape(a, other_right, HandShape::Grip, 0.6f);
        }
    }
}

bool g_indoors = false;
double g_indoors_checked = 0;

// --- lifecycle -----------------------------------------------------------------------------------------------------

// Its objects are gone (e.g. a return to the main menu destroyed them): only our handles are left.
void release(Avatar& a) {
    drop(a.root);
    drop(a.root_transform);
    drop(a.ai_target);
    drop(a.visual);
    drop_gait(a);
    drop_bones(a);
    forget_held(a.held);
    a.held_logged.clear();
    a.shown_body.clear();
    a.shown = false;
}

// They left the game.
void remove(Avatar& a) {
    if (Il2CppObject* root = target(a.root); is_alive(root)) destroy(root, false);
    release(a);
}

bool ensure_loader() {
    if (g_loader) return true;
    Il2CppObject* loader = new_game_object("TLDCoop_BodyLoader");
    if (!loader) return false;
    set_active(loader, false);
    void* loader_arg[] = {loader};
    il2::invoke(api.keep_on_load, nullptr, loader_arg);
    g_loader = hold(loader);
    g_loader_transform = hold(transform_of(loader));
    return true;
}

bool ensure_root(Avatar& a) {
    if (a.root) return true;
    if (!ensure_loader()) return false;
    const std::string name = "TLDCoop_RemotePlayer" + std::to_string(a.player);
    Il2CppObject* root = new_game_object(name.c_str());
    if (!root) return false;
    void* root_arg[] = {root};
    il2::invoke(api.keep_on_load, nullptr, root_arg);
    a.root = hold(root);
    a.root_transform = hold(transform_of(root));
    // An AiTarget without BaseAi or NPC counts as "the player" to TLD's animals; wildlife_targets lets them hunt it.
    if (api.add_component && api.ai_target_type) {
        void* args[] = {api.ai_target_type};
        if (Il2CppObject* target_component = il2::invoke(api.add_component, root, args)) {
            if (api.ai_target_eye >= 0) {
                const float eye = 1.6f;
                memcpy(reinterpret_cast<char*>(target_component) + api.ai_target_eye, &eye, sizeof(eye));
            }
            a.ai_target = hold(target_component);
        }
    }
    a.shown = true;
    say(a, "created");
    return true;
}

void show(Avatar& a, bool on) {
    if (on == a.shown) return;
    if (Il2CppObject* root = target(a.root)) set_active(root, on);
    a.shown = on;
}

// Bring the visual in line with the chosen body; the capsule stands in for anything unavailable.
void sync_body(Avatar& a) {
    if (a.wanted_body == a.shown_body) return;
    if (a.wanted_body == a.failed_body) {
        if (a.shown_body.empty()) show_capsule(a);
        return;
    }
    if (a.wanted_body == "capsule" || !show_pack_body(a, a.wanted_body)) {
        if (a.wanted_body != "capsule") a.failed_body = a.wanted_body;
        show_capsule(a);
    }
}

// TLD's pause stops game time (timeScale 0), and legacy Animation with it, but the other player keeps moving.
// This IL2CPP build has no AnimationState weight / enabled setters, so while paused we Play() the current gait
// alone, advance its clip on real time and Sample() it. Normal blending takes over again when the game runs.
void play_on_real_time(Avatar& a, Il2CppObject* animation, float dt) {
    Gait& g = a.gait;
    if (!g.paused_clip || std::strcmp(g.paused_clip, g.current) != 0) {
        void* args[] = {il2::il2cpp_string_new(g.current)};
        il2::invoke(api.anim_play, animation, args);
        g.paused_clip = g.current;
    }
    void* name_args[] = {il2::il2cpp_string_new(g.current)};
    Il2CppObject* state = il2::invoke(api.anim_state, animation, name_args);
    if (!state) return;
    float time = 0, speed = 1;
    il2::invoke_value(api.state_get_time, state, time);
    il2::invoke_value(api.state_get_speed, state, speed);
    time += dt * speed;
    void* time_args[] = {&time};
    il2::invoke(api.state_set_time, state, time_args);
    il2::invoke(api.anim_sample, animation);
}

void blend(Il2CppObject* animation, const std::string& name, float weight, float fade) {
    void* args[] = {il2::il2cpp_string_new(name.c_str()), &weight, &fade};
    il2::invoke(api.blend, animation, args);
}

bool has_upper(const std::string& name) {
    for (const ActionClip& c : kActionClips)
        if (name == c.name) return c.upper;
    return false;
}

// The action clip over the gait (layer 1): a handover (coop_give says which and when), sleeping in a bed, eating;
// none while they fight an animal. Standing still it has the whole body; walking or crouching, the spine up.
void drive_actions(Avatar& a, Il2CppObject* animation, const net::StateMsg& s, float dt) {
    if (!a.actions) return;
    std::string want;
    float time = -1;
    bool fading = false;
    const bool down = (s.flags & net::kStateStruggling) != 0;
    if (down) {
    } else if (a.handover_on && a.handover.t >= 0) {
        const net::HandItem k = net::hand_item(a.handover.item);
        want = a.handover.giver ? net::give_clip(k) : net::receive_clip(k, a.handover.giver_female);
        time = a.handover.t;
        fading = a.handover.fading;
    } else if (a.lying) {
        want = "sleep";
    } else if (s.flags & net::kStateEating) {
        want = "eat";
    }
    if (!g_forced_action.empty()) {
        want = g_forced_action;
        time = g_forced_time;
    }
    const bool upper = has_upper(want);
    const float lower_to = upper && (a.speed > kIdleBelow || (s.flags & net::kStateCrouched)) ? 1.0f : 0.0f;
    a.lower += (lower_to - a.lower) * std::fmin(1.0f, dt * 5);
    if (want != a.action) {
        say(a, "action %s -> %s", a.action.empty() ? "none" : a.action.c_str(), want.empty() ? "none" : want.c_str());
        if (!a.action.empty()) {
            blend(animation, a.action, 0, kActionFade);
            if (a.action_upper) blend(animation, a.action + "_up", 0, kActionFade);
        }
        for (const std::string& name : {want, want + "_up"}) {  // from its start (a state keeps its time when let go)
            Il2CppObject* st = want.empty() || time >= 0 ? nullptr : animation_state(animation, name.c_str());
            if (!st) continue;
            float zero = 0;
            void* time_args[] = {&zero};
            il2::invoke(api.state_set_time, st, time_args);
        }
    }
    a.action = want;
    a.action_upper = upper;
    if (want.empty()) return;
    const float weight = fading ? 0.0f : 1.0f;
    blend(animation, want, weight * (upper ? 1 - a.lower : 1), kActionFade);
    if (upper) blend(animation, want + "_up", weight * a.lower, kActionFade);
    if (time < 0) return;
    for (const std::string& name : {want, want + "_up"}) {  // on the pair's timeline: still, at its time
        if (name != want && !upper) break;
        Il2CppObject* st = animation_state(animation, name.c_str());
        if (!st) continue;
        float speed = 0;
        void* speed_args[] = {&speed};
        il2::invoke(api.state_speed, st, speed_args);
        void* time_args[] = {&time};
        il2::invoke(api.state_set_time, st, time_args);
    }
}

void drive_animation(Avatar& a, const net::StateMsg& s, double now) {
    const double dt = (now - a.last_pos_time) / 1000.0;
    if (a.last_pos_time > 0 && dt > 0 && dt < 0.5) {
        const float dx = s.x - a.last_pos.x, dz = s.z - a.last_pos.z;
        const float v = std::sqrt(dx * dx + dz * dz) / static_cast<float>(dt);
        a.speed += (v - a.speed) * static_cast<float>(std::fmin(1.0, dt * 8.0));  // smooth over ~1/8 s
    }
    a.last_pos = {s.x, s.y, s.z};
    a.last_pos_time = now;

    Gait& g = a.gait;
    Il2CppObject* animation = target(g.animation);
    if (!animation) return;

    // Pick the gait, with a little hysteresis so it doesn't flicker at the boundaries.
    const float run_from = (g.walk_speed + g.run_speed) * 0.5f;
    const char* want = g.current;
    if ((s.flags & net::kStateStruggling) && g.has_down)
        want = "down";
    else if (a.speed < kIdleBelow)
        want = "idle";
    else if (a.speed > kIdleBelow + kGaitHysteresis && a.speed < run_from - kGaitHysteresis)
        want = "walk";
    else if (a.speed > run_from + kGaitHysteresis)
        want = "run";
    if (std::strcmp(want, g.current) != 0) {
        float fade = kGaitFadeSeconds;
        void* args[] = {il2::il2cpp_string_new(want), &fade};
        il2::invoke(api.crossfade, animation, args);
        g.current = want;
    }
    // Hiding the avatar (another scene, a load) stops legacy Animation; when it's shown again the gait may be the
    // same, so nothing above restarts it and it would glide frozen. Restart whatever should be playing.
    bool playing = true;
    if (api.anim_is_playing && api.anim_play && il2::invoke_value(api.anim_is_playing, animation, playing) && !playing) {
        void* args[] = {il2::il2cpp_string_new(g.current)};
        il2::invoke(api.anim_play, animation, args);
        say(a, "animation had stopped; playing '%s' again", g.current);
    }

    // Scale playback so the feet keep pace with the ground.
    auto set_speed = [](uintptr_t state, float value) {
        if (Il2CppObject* st = target(state)) {
            void* args[] = {&value};
            il2::invoke(api.state_speed, st, args);
        }
    };
    set_speed(g.walk_state, std::clamp(a.speed / g.walk_speed, 0.6f, 1.6f));
    set_speed(g.run_state, std::clamp(a.speed / g.run_speed, 0.6f, 1.5f));
    drive_actions(a, animation, s, dt > 0 && dt < 0.5 ? static_cast<float>(dt) : 0.0f);

    float time_scale = 1;
    if (api.time_scale && api.anim_play && api.anim_sample && api.state_set_time)
        il2::invoke_value(api.time_scale, nullptr, time_scale);
    if (time_scale < 0.01f) {
        if (dt > 0 && dt < 0.5) play_on_real_time(a, animation, static_cast<float>(dt));
    } else {
        g.paused_clip = nullptr;
    }
}

void update_one(Avatar& a, const std::string& local_scene, bool local_in_game, double now) {
    net::StateMsg s;
    const char* hidden = nullptr;
    std::string other_scene;
    const bool sampled = a.interp.sample(now, s);
    a.fresh = sampled && now - a.interp.last_received_ms() <= kStaleMs;
    if (sampled) a.last = s;
    if (!sampled)
        hidden = "no data from the other player";
    else if (!a.fresh)
        hidden = "no updates from the other player for over 6 seconds";
    else if (!local_in_game)
        hidden = "we are not in game";
    else if (!s.valid)
        hidden = "other player is in a menu or loading";
    else if (s.scene != local_scene)
        hidden = (other_scene = "other scene (" + s.scene + ")").c_str();
    else if (s.flags & net::kStateDead)
        hidden = "dead (their body lies where they died)";
    if (hidden) {
        a.status = std::string("hidden: ") + hidden;
        if (a.visibility != a.status) {
            a.visibility = a.status;
            say(a, "%s", a.status.c_str());
        }
        show(a, false);
        return;
    }

    if (!ensure_root(a)) {
        a.status = "hidden: could not create the avatar";
        return;
    }
    show(a, true);
    sync_body(a);
    if (a.visibility != "shown") {
        // Where it appears matters: right after a load it must be where the other player really is.
        a.visibility = "shown";
        const game::PlayerState me = game::read_player();
        const float dx = s.x - me.position.x, dy = s.y - me.position.y, dz = s.z - me.position.z;
        say(a, "shown in '%s' at (%.1f, %.1f, %.1f), %.1f m from us", s.scene.c_str(), s.x, s.y, s.z,
            std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    // In a fight with an animal (their game runs TLD's struggle): shown knocked down, on their back.
    const bool down = (s.flags & net::kStateStruggling) != 0;
    if (down != a.shown_down) {
        a.shown_down = down;
        say(a, "the other player is %s", down ? "fighting an animal (shown knocked down)" : "back on their feet");
    }
    Il2CppObject* root_transform = target(a.root_transform);
    const float dt = a.last_pos_time > 0 ? static_cast<float>(std::clamp((now - a.last_pos_time) / 1000.0, 0.0, 0.5)) : 0;
    // In a bed (milestone 13): the body lies where TLD lays a sleeper. Crouching: eased down and up.
    a.lying = (s.flags & net::kStateInBed) && (s.flags & net::kStateBed) && a.actions && !down;
    const bool crouched = (s.flags & net::kStateCrouched) && !down && !a.lying;
    a.crouch += ((crouched ? 1.0f : 0.0f) - a.crouch) * std::fmin(1.0f, dt * 6);
    // A handover: they face their partner, the clips' distance apart (eased there and back).
    if (a.handover_on) {
        const HandoverShow& h = a.handover;
        a.spot = net::handover_spot(s.x, s.z, h.partner_x, h.partner_z,
                                    net::pair_distance(h.giver ? a.female : h.partner_female,
                                                       h.giver ? h.partner_female : a.female),
                                    h.partner_moves);
    }
    a.handover_blend += ((a.handover_on && !a.handover.fading ? 1.0f : 0.0f) - a.handover_blend) * std::fmin(1.0f, dt * 6);
    // With the body pack's "down" clip they struggle on the ground (drive_animation); without it (capsule, an older
    // pack), the body is tipped over instead.
    const bool tip_over = down && !a.gait.has_down;
    if (a.lying) {
        const Quat bed = net::pose::normalized(Quat{s.bed[3], s.bed[4], s.bed[5], s.bed[6]});
        const float r = g_bed_yaw * 0.5f * kDeg;
        set_vec(api.set_position, root_transform, {s.bed[0], s.bed[1] + g_bed_lift, s.bed[2]});
        set_rotation(root_transform, bed * Quat{0, std::sin(r), 0, std::cos(r)});
        a.shown_yaw = s.yaw;
    } else {
        float x = s.x, z = s.z, yaw = s.yaw;
        if (a.handover_blend > 0.001f) {
            const float f = a.handover_blend, d = std::fmod(a.spot.yaw - s.yaw + 540.0f, 360.0f) - 180.0f;
            x += (a.spot.x - x) * f;
            z += (a.spot.z - z) * f;
            yaw += d * f;
        }
        // TLDCoop_DLC: in a fight, on the ground where they stood (TLD moves its player up and about for the
        // struggle's camera: the body floated).
        const bool crouching = crouched;
        if (crouching && !a.crouch_seen && a.has_ground) {  // TLDCoop_DLC: just crouched: how much higher it says
            const float dx = s.x - a.stand_x, dz = s.z - a.stand_z;
            a.crouch_rise = dx * dx + dz * dz < 0.5f * 0.5f ? std::clamp(s.y - a.ground_y, 0.0f, 0.8f) : 0.0f;
            if (a.crouch_rise > 0.03f) say(a, "crouched: TLD has them %.2f m higher; shown on the ground", a.crouch_rise);
        }
        a.crouch_seen = crouching;
        if (!down && !crouching) {
            a.ground_y = s.y;
            a.has_ground = true;
            a.stand_x = s.x, a.stand_z = s.z;
        }
        float y = down && a.has_ground ? std::min(s.y, a.ground_y) : s.y;
        if (!down) y -= a.crouch_rise * a.crouch;  // (eased with the crouch)
        // TLDCoop_DLC: in a fight with the host's wolf, on the wolf: its fight clips play on its root with the victim
        // lying there, facing back along it. Where they stood the wolf leapt about beside them, at nothing.
        float wx = 0, wy = 0, wz = 0, wyaw = 0;
        if (down && wildlife_sync::fight_spot(x, y, z, wx, wy, wz, wyaw)) {
            if (!a.on_wolf) say(a, "fighting the host's wolf: shown under it");
            a.on_wolf = true;
            x = wx, z = wz, y = std::min(y, wy);
            yaw = wyaw + 180.0f;
        } else {
            a.on_wolf = false;
        }
        set_vec(api.set_position, root_transform, {x, y + (tip_over ? 0.2f : 0.0f), z});
        set_vec(api.set_euler, root_transform, {tip_over ? -80.0f : 0.0f, yaw, 0.0f});
        a.shown_yaw = yaw;
    }
    drive_animation(a, s, now);
    a.yaw = s.yaw;
    a.pitch = s.pitch;
    sync_held(a, s, g_indoors);
    if (const net::Emote e = net::emote_of(s.emote); e != net::Emote::None && (s.emote_seq != a.emote_seq || a.emote == net::Emote::None)) {
        a.emote = e;  // TLDCoop_DLC: a new gesture of theirs
        a.emote_start = now;
        a.emote_seq = s.emote_seq;
        say(a, "makes a gesture: %s", net::emote_key_name(e));
    }
    if (a.emote != net::Emote::None && (now - a.emote_start) / 1000.0 >= net::emote_seconds(a.emote)) a.emote = net::Emote::None;

    char text[200];
    if (a.lying)
        snprintf(text, sizeof(text), "%s shown lying in bed at (%.2f, %.2f, %.2f)", a.shown_body.c_str(), s.bed[0],
                 s.bed[1] + g_bed_lift, s.bed[2]);
    else
        snprintf(text, sizeof(text), "%s shown at (%.2f, %.2f, %.2f) yaw %.1f speed %.2f m/s", a.shown_body.c_str(),
                 s.x, s.y, s.z, s.yaw, a.speed);
    a.status = text;
}

Avatar& avatar_of(uint8_t player) {
    auto& slot = g_avatars[player];
    if (!slot) {
        slot = std::make_unique<Avatar>();
        slot->player = player;
    }
    return *slot;
}

void set_body_of(Avatar& a, const std::string& id) {
    const std::string chosen = id.empty() ? "capsule" : id;
    if (chosen == a.wanted_body) return;
    if (!known_body(chosen)) {
        logx::warn("avatar: unknown body '%s' from player %u; showing the capsule", chosen.c_str(), a.player);
        a.wanted_body = "capsule";
        return;
    }
    a.wanted_body = chosen;
    a.failed_body.clear();
}

bool shown_here(const Avatar& a) { return g_ready && a.shown && a.visibility == "shown"; }

// --- the character preview on the co-op screens --------------------------------------------------------------------

// A layer the game doesn't name (so doesn't use): the preview camera sees only what's on it.
int free_layer() {
    for (int layer = 31; layer >= 8; --layer) {
        void* args[] = {&layer};
        Il2CppObject* name = il2::invoke(pv.layer_name, nullptr, args);
        if (!name || il2::to_utf8(reinterpret_cast<Il2CppString*>(name)).empty()) return layer;
    }
    return 31;
}

void set_layer_everywhere(Il2CppObject* go, int layer) {
    Il2CppObject* transforms = components_in_children(go, api.transform_type);
    for (size_t i = 0, n = il2::array_length(transforms); i < n; ++i) {
        Il2CppObject* each = il2::invoke(pv.component_go, il2::array_object_at(transforms, i));
        void* args[] = {&layer};
        if (each) il2::invoke(pv.go_layer, each, args);
    }
}

// A component on a new child of the stage, placed and turned (local).
Il2CppObject* stage_part(const char* name, Il2CppObject* type, Vector3 position, Vector3 euler) {
    Il2CppObject* go = new_game_object(name);
    Il2CppObject* tf = transform_of(go);
    if (!tf) return nullptr;
    set_parent(tf, target(g_preview.root_transform));
    set_vec(api.set_local_position, tf, position);
    set_vec(api.set_local_euler, tf, euler);
    void* args[] = {type};
    return il2::invoke(api.add_component, go, args);
}

void stage_light(const char* name, Vector3 euler, float intensity, Color color, int mask) {
    Il2CppObject* light = stage_part(name, pv.light_type, {0, 0, 0}, euler);
    set_value(pv.light_kind, light, kLightDirectional);
    set_value(pv.light_intensity, light, intensity);
    set_value(pv.light_color, light, color);
    set_value(pv.light_mask, light, mask);
}

void end_preview_objects() {
    if (Il2CppObject* root = target(g_preview.root); is_alive(root)) destroy(root, false);
    if (Il2CppObject* texture = target(g_preview.texture); is_alive(texture)) destroy(texture, false);
    drop(g_preview.root);
    drop(g_preview.root_transform);
    drop(g_preview.texture);
    drop(g_preview.body);
    g_preview = Preview{};
}

// The camera (into the texture, clear around the character), a warm key light from the front, a cool fill and a rim
// light from behind, all for the preview's layer only.
bool ensure_stage() {
    if (is_alive(target(g_preview.root)) && is_alive(target(g_preview.texture))) return true;
    end_preview_objects();
    if (!ensure_loader()) return false;
    g_preview.layer = free_layer();
    const int mask = 1 << g_preview.layer;
    Il2CppObject* root = new_game_object("TLDCoop_CharacterPreview");
    if (!root) return false;
    void* keep_args[] = {root};
    il2::invoke(api.keep_on_load, nullptr, keep_args);
    set_vec(api.set_position, transform_of(root), kPreviewSpot);
    g_preview.root = hold(root);
    g_preview.root_transform = hold(transform_of(root));

    Il2CppObject* texture = il2::il2cpp_object_new(pv.render_texture);
    int width = kPreviewWidth, height = kPreviewHeight, depth = 24;
    void* texture_args[] = {&width, &height, &depth};
    il2::invoke(pv.rt_ctor, texture, texture_args);
    set_value(pv.rt_antialiasing, texture, 4);
    g_preview.texture = hold(texture);

    Il2CppObject* camera = stage_part("Camera", pv.camera_type, {0, kPreviewEye, kPreviewDistance}, {0, 180, 0});
    if (!camera) return false;
    set_value(pv.camera_clear, camera, kCameraClearSolid);
    set_value(pv.camera_background, camera, Color{0, 0, 0, 0});
    set_value(pv.camera_mask, camera, mask);
    set_value(pv.camera_fov, camera, kPreviewFov);
    set_value(pv.camera_near, camera, 0.3f);
    set_value(pv.camera_far, camera, 20.0f);
    set_value(pv.camera_path, camera, kRenderForward);
    set_value(pv.camera_hdr, camera, false);
    void* target_args[] = {texture};
    il2::invoke(pv.camera_target, camera, target_args);

    stage_light("KeyLight", {25, 160, 0}, 1.1f, {1.0f, 0.94f, 0.86f, 1}, mask);
    stage_light("FillLight", {10, 225, 0}, 0.35f, {0.75f, 0.85f, 1.0f, 1}, mask);
    stage_light("RimLight", {20, 10, 0}, 0.6f, {0.85f, 0.9f, 1.0f, 1}, mask);
    logx::info("avatar: character preview stage ready (layer %d)", g_preview.layer);
    return true;
}

}  // namespace

Il2CppObject* preview(const std::string& id) {
    if (!g_ready || !pv.ready || id.empty() || id == "capsule" || id == g_preview.failed) return nullptr;
    if (!ensure_stage()) return nullptr;
    Il2CppObject* body = target(g_preview.body);
    if (id == g_preview.shown && is_alive(body)) return target(g_preview.texture);
    Il2CppObject* fresh = new_pack_body(id);
    Il2CppObject* tf = transform_of(fresh);
    if (!tf) {
        g_preview.failed = id;
        return nullptr;
    }
    if (is_alive(body)) destroy(body, false);
    drop(g_preview.body);
    set_layer_everywhere(fresh, g_preview.layer);
    set_parent(tf, target(g_preview.root_transform));  // awake here
    set_vec(api.set_local_position, tf, {0, 0, 0});
    set_vec(api.set_local_euler, tf, {0, kPreviewYaw, 0});
    Il2CppObject* animations = components_in_children(fresh, api.animation_type);
    if (il2::array_length(animations) > 0) {
        void* args[] = {il2::il2cpp_string_new("idle")};
        il2::invoke(api.anim_play, il2::array_object_at(animations, 0), args);
    }
    g_preview.body = hold(fresh);
    g_preview.shown = id;
    logx::info("avatar: preview shows '%s'", id.c_str());
    return target(g_preview.texture);
}

void end_preview() {
    if (g_preview.root || g_preview.texture) logx::info("avatar: character preview closed");
    end_preview_objects();
}

bool init() {
    Il2CppClass* game_object = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* transform = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* primitive_type = il2::find_class(kCore, "UnityEngine", "PrimitiveType");
    Il2CppClass* collider = il2::find_class(kPhysics, "UnityEngine", "Collider");
    Il2CppClass* animation = il2::find_class(kAnimation, "UnityEngine", "Animation");
    Il2CppClass* animation_state = il2::find_class(kAnimation, "UnityEngine", "AnimationState");
    Il2CppClass* asset_bundle = il2::find_class(kAssetBundles, "UnityEngine", "AssetBundle");

    api.game_object_class = game_object;
    api.go_ctor = il2::find_method_params(game_object, ".ctor", {"System.String"});
    api.create_primitive = il2::find_method(game_object, "CreatePrimitive", 1);
    api.set_active = il2::find_method(game_object, "SetActive", 1);
    api.go_transform = il2::find_method(game_object, "get_transform", 0);
    api.get_component = il2::find_method_params(game_object, "GetComponent", {"System.Type"});
    api.get_components =
        il2::find_method_params(game_object, "GetComponentsInChildren", {"System.Type", "System.Boolean"});
    api.instantiate =
        il2::find_method_params(object, "Instantiate", {"UnityEngine.Object", "UnityEngine.Transform"});
    api.destroy = il2::find_method(object, "Destroy", 1);
    api.destroy_now = il2::find_method(object, "DestroyImmediate", 1);
    api.alive = il2::find_method(object, "op_Implicit", 1);
    api.set_name = il2::find_method(object, "set_name", 1);
    api.object_name = il2::find_method(object, "get_name", 0);
    api.keep_on_load = il2::find_method(object, "DontDestroyOnLoad", 1);
    api.set_position = il2::find_method(transform, "set_position", 1);
    api.set_euler = il2::find_method(transform, "set_eulerAngles", 1);
    api.set_local_scale = il2::find_method(transform, "set_localScale", 1);
    api.set_local_position = il2::find_method(transform, "set_localPosition", 1);
    api.set_local_euler = il2::find_method(transform, "set_localEulerAngles", 1);
    api.set_parent = il2::find_method(transform, "SetParent", 2);
    api.get_material = il2::find_method(renderer, "get_material", 0);
    api.shared_materials = il2::find_method(renderer, "get_sharedMaterials", 0);
    api.set_color = il2::find_method(material, "set_color", 1);
    api.get_shader = il2::find_method(material, "get_shader", 0);
    api.set_shader = il2::find_method(material, "set_shader", 1);
    api.find_shader = il2::find_method(shader, "Find", 1);
    api.load_bundle = il2::find_method_params(asset_bundle, "LoadFromFile", {"System.String"});
    api.load_asset = il2::find_method_params(asset_bundle, "LoadAsset", {"System.String", "System.Type"});
    api.crossfade = il2::find_method_params(animation, "CrossFade", {"System.String", "System.Single"});
    api.anim_state = il2::find_method_params(animation, "get_Item", {"System.String"});
    api.state_speed = il2::find_method(animation_state, "set_speed", 1);
    api.state_get_speed = il2::find_method(animation_state, "get_speed", 0);
    api.state_get_time = il2::find_method(animation_state, "get_time", 0);
    api.state_set_time = il2::find_method(animation_state, "set_time", 1);
    api.anim_play = il2::find_method_params(animation, "Play", {"System.String"});
    api.anim_sample = il2::find_method(animation, "Sample", 0);
    api.anim_is_playing = il2::find_method(animation, "get_isPlaying", 0);
    api.add_component = il2::find_method_params(game_object, "AddComponent", {"System.Type"});
    api.add_clip = il2::find_method_params(animation, "AddClip", {"UnityEngine.AnimationClip", "System.String"});
    api.blend = il2::find_method_params(animation, "Blend", {"System.String", "System.Single", "System.Single"});
    api.state_layer = il2::find_method(animation_state, "set_layer", 1);
    api.state_wrap = il2::find_method(animation_state, "set_wrapMode", 1);
    api.state_mix = il2::find_method(animation_state, "AddMixingTransform", 2);
    api.anim_playing_named = il2::find_method_params(animation, "IsPlaying", {"System.String"});
    api.clip_type = il2::type_object(il2::find_class(kAnimation, "UnityEngine", "AnimationClip"));
    if (api.clip_type) il2::il2cpp_gchandle_new(api.clip_type, false);
    if (!api.add_clip || !api.blend || !api.state_layer || !api.state_wrap || !api.state_mix || !api.clip_type)
        logx::warn("avatar: the bodies won't eat, sleep or hand things over (a Unity animation API is missing)");
    if (Il2CppClass* ai_target = il2::find_class("Assembly-CSharp.dll", "", "AiTarget")) {
        api.ai_target_type = il2::type_object(ai_target);
        if (api.ai_target_type) il2::il2cpp_gchandle_new(api.ai_target_type, false);
        api.ai_target_eye = il2::field_offset(ai_target, "m_EyeOffset");
    }
    api.time_scale = il2::find_method(il2::find_class(kCore, "UnityEngine", "Time"), "get_timeScale", 0);
    api.collider_type = il2::type_object(collider);
    api.renderer_type = il2::type_object(renderer);
    api.component_type = il2::type_object(component);
    api.animation_type = il2::type_object(animation);
    api.transform_type = il2::type_object(transform);
    api.game_object_type = il2::type_object(game_object);
    // Kept for the whole session: hold strong handles (never freed) so the GC can't reclaim them.
    for (Il2CppObject* t : {api.collider_type, api.renderer_type, api.component_type, api.animation_type,
                            api.transform_type, api.game_object_type})
        if (t) il2::il2cpp_gchandle_new(t, false);

    Il2CppClass* camera = il2::find_class(kCore, "UnityEngine", "Camera");
    Il2CppClass* light = il2::find_class(kCore, "UnityEngine", "Light");
    pv.render_texture = il2::find_class(kCore, "UnityEngine", "RenderTexture");
    pv.rt_ctor = il2::find_method_params(pv.render_texture, ".ctor", {"System.Int32", "System.Int32", "System.Int32"});
    pv.rt_antialiasing = il2::find_method(pv.render_texture, "set_antiAliasing", 1);
    pv.camera_type = il2::type_object(camera);
    pv.light_type = il2::type_object(light);
    pv.camera_target = il2::find_method(camera, "set_targetTexture", 1);
    pv.camera_clear = il2::find_method(camera, "set_clearFlags", 1);
    pv.camera_background = il2::find_method(camera, "set_backgroundColor", 1);
    pv.camera_mask = il2::find_method(camera, "set_cullingMask", 1);
    pv.camera_fov = il2::find_method(camera, "set_fieldOfView", 1);
    pv.camera_near = il2::find_method(camera, "set_nearClipPlane", 1);
    pv.camera_far = il2::find_method(camera, "set_farClipPlane", 1);
    pv.camera_path = il2::find_method(camera, "set_renderingPath", 1);
    pv.camera_hdr = il2::find_method(camera, "set_allowHDR", 1);
    pv.light_kind = il2::find_method(light, "set_type", 1);
    pv.light_intensity = il2::find_method(light, "set_intensity", 1);
    pv.light_color = il2::find_method(light, "set_color", 1);
    pv.light_mask = il2::find_method(light, "set_cullingMask", 1);
    pv.go_layer = il2::find_method(game_object, "set_layer", 1);
    pv.component_go = il2::find_method(component, "get_gameObject", 0);
    pv.layer_name = il2::find_method(il2::find_class(kCore, "UnityEngine", "LayerMask"), "LayerToName", 1);
    for (Il2CppObject* t : {pv.camera_type, pv.light_type})
        if (t) il2::il2cpp_gchandle_new(t, false);
    pv.ready = pv.render_texture && pv.rt_ctor && pv.camera_type && pv.light_type && pv.camera_target &&
               pv.camera_clear && pv.camera_background && pv.camera_mask && pv.camera_fov && pv.light_kind &&
               pv.light_mask && pv.go_layer && pv.component_go && pv.layer_name && api.anim_play && api.add_component;
    if (!pv.ready) logx::warn("avatar: no character preview on the co-op screens (a Unity camera/light API is missing)");

    // What they hold (milestone 11).
    Il2CppClass* gear = il2::find_class("Assembly-CSharp.dll", "", "GearItem");
    Il2CppClass* behaviour = il2::find_class(kCore, "UnityEngine", "Behaviour");
    api.load_gear = il2::find_method_quiet(gear, "LoadGearItemPrefab", 1);
    api.instantiate_gear = il2::find_method_params_quiet(gear, "InstantiateGearItem", {"System.String"});
    api.gear_type = il2::type_object(gear);
    api.light_type = il2::type_object(light);
    for (Il2CppObject* t : {api.gear_type, api.light_type})
        if (t) il2::il2cpp_gchandle_new(t, false);
    api.get_position = il2::find_method(transform, "get_position", 0);
    api.get_rotation = il2::find_method(transform, "get_rotation", 0);
    api.set_rotation = il2::find_method(transform, "set_rotation", 1);
    api.get_forward = il2::find_method(transform, "get_forward", 0);
    api.find_child = il2::find_method_params(transform, "Find", {"System.String"});
    api.renderer_bounds = il2::find_method(renderer, "get_bounds", 0);
    api.renderer_enabled = il2::find_method(renderer, "get_enabled", 0);
    api.renderer_set_enabled = il2::find_method_quiet(renderer, "set_enabled", 1);
    api.set_enabled = il2::find_method(behaviour, "set_enabled", 1);
    api.active_and_enabled = il2::find_method(behaviour, "get_isActiveAndEnabled", 0);
    api.in_hierarchy = il2::find_method(game_object, "get_activeInHierarchy", 0);
    api.tf_parent = il2::find_method_quiet(transform, "get_parent", 0);
    api.light_intensity = il2::find_method(light, "get_intensity", 0);
    api.light_range = il2::find_method(light, "get_range", 0);
    api.light_color = il2::find_method(light, "get_color", 0);
    api.light_get_mode = il2::find_method_quiet(light, "get_renderMode", 0);
    api.light_set_mode = il2::find_method_quiet(light, "set_renderMode", 1);
    api.light_get_mask = il2::find_method_quiet(light, "get_cullingMask", 0);
    api.light_shadows = il2::find_method(light, "get_shadows", 0);
    {
        constexpr const char* kGame = "Assembly-CSharp.dll";
        parts.torch = il2::field_offset(gear, "m_TorchItem");
        parts.flare = il2::field_offset(gear, "m_FlareItem");
        parts.lamp = il2::field_offset(gear, "m_KeroseneLampItem");
        parts.flashlight = il2::field_offset(gear, "m_FlashlightItem");
        Il2CppClass* torch = il2::find_class(kGame, "", "TorchItem");
        parts.torch_fx = il2::field_offset(torch, "m_FXGameObject");
        parts.torch_in = il2::field_offset(torch, "m_LightIndoor");
        parts.torch_out = il2::field_offset(torch, "m_LightOutdoor");
        parts.torch_burnt = il2::field_offset(torch, "m_TorchBurntMeshGameObject");
        Il2CppClass* flare = il2::find_class(kGame, "", "FlareItem");
        parts.flare_fx = il2::field_offset(flare, "m_FXGameObject");
        parts.flare_in = il2::field_offset(flare, "m_LightIndoor");
        parts.flare_out = il2::field_offset(flare, "m_LightOutdoor");
        parts.flare_cap = il2::field_offset(flare, "m_FlareCapMeshObject");
        Il2CppClass* lamp = il2::find_class(kGame, "TLD.Gear", "KeroseneLampItem");
        parts.lamp_fx = il2::field_offset(lamp, "m_FXGameObject");
        parts.lamp_in = il2::field_offset(lamp, "m_LightIndoor");
        parts.lamp_core = il2::field_offset(lamp, "m_LightIndoorCore");
        parts.lamp_out = il2::field_offset(lamp, "m_LightOutdoor");
        Il2CppClass* flashlight = il2::find_class(kGame, "", "FlashlightItem");
        parts.fl_fx_low = il2::field_offset(flashlight, "m_FxObjectLow");
        parts.fl_fx_high = il2::field_offset(flashlight, "m_FxObjectHigh");
        parts.fl_in = il2::field_offset(flashlight, "m_LightIndoor");
        parts.fl_out = il2::field_offset(flashlight, "m_LightOutdoor");
        parts.fl_in_high = il2::field_offset(flashlight, "m_LightIndoorHigh");
        parts.fl_out_high = il2::field_offset(flashlight, "m_LightOutdoorHigh");
        Il2CppClass* intensity = il2::find_class(kGame, "", "TorchIntensity");
        parts.torch_intensity_type = il2::type_object(intensity);
        parts.ti_in_min = il2::field_offset(intensity, "m_IndoorIntensityMin");
        parts.ti_in_max = il2::field_offset(intensity, "m_IndoorIntensityMax");
        parts.ti_out_min = il2::field_offset(intensity, "m_OutdoorIntensityMin");
        parts.ti_out_max = il2::field_offset(intensity, "m_OutdoorIntensityMax");
        parts.ti_in_light = il2::field_offset(intensity, "m_IndoorLight");
        parts.ti_out_light = il2::field_offset(intensity, "m_OutdoorLight");
        Il2CppClass* random = il2::find_class(kGame, "", "LightRandomIntensity");
        parts.random_intensity_type = il2::type_object(random);
        parts.set_min_max = il2::find_method(random, "SetMinMax", 2);
        parts.curve_evaluate = il2::find_method(il2::find_class(kCore, "UnityEngine", "AnimationCurve"), "Evaluate", 1);
        if (Il2CppClass* fi = il2::find_class(kGame, "", "FlareIntensity")) {  // TLDCoop_DLC
            parts.flare_intensity_type = il2::type_object(fi);
            parts.fi_in_min = il2::field_offset(fi, "m_IndoorIntensityMin");
            parts.fi_in_max = il2::field_offset(fi, "m_IndoorIntensityMax");
            parts.fi_out_min = il2::field_offset(fi, "m_OutdoorIntensityMin");
            parts.fi_out_max = il2::field_offset(fi, "m_OutdoorIntensityMax");
            parts.fi_in_light = il2::field_offset(fi, "m_IndoorLight");
            parts.fi_out_light = il2::field_offset(fi, "m_OutdoorLight");
            parts.fi_smoke = il2::field_offset(fi, "m_SmokeParticles");
            parts.fi_core = il2::field_offset(fi, "m_CoreParticles");
            parts.fi_spark = il2::field_offset(fi, "m_SparkParticles");
            parts.fi_smoke_rate = il2::field_offset(fi, "m_SmokeRate");
            parts.fi_core_rate = il2::field_offset(fi, "m_CoreRate");
            parts.fi_spark_rate = il2::field_offset(fi, "m_SparkRate");
        }
        if (Il2CppClass* ps = il2::find_class("UnityEngine.ParticleSystemModule.dll", "UnityEngine", "ParticleSystem")) {
            parts.particles_type = il2::type_object(ps);
            parts.ps_emit = il2::find_method_params(ps, "Emit", {"System.Int32"});
            parts.ps_play = il2::find_method(ps, "Play", 0);
            parts.ps_stop = il2::find_method_quiet(ps, "Stop", 0);
        }
        if (Il2CppClass* tracking = il2::find_class(kGame, "", "LightTracking"))
            parts.light_tracking_type = il2::type_object(tracking);
        parts.light_range = il2::find_method(il2::find_class(kCore, "UnityEngine", "Light"), "set_range", 1);
        for (Il2CppObject* t : {parts.torch_intensity_type, parts.random_intensity_type, parts.flare_intensity_type,
                                parts.particles_type, parts.light_tracking_type})
            if (t) il2::il2cpp_gchandle_new(t, false);
    }
    const bool holding = (api.load_gear || api.instantiate_gear) && api.gear_type && api.get_position && api.get_rotation && api.set_rotation &&
                         api.renderer_bounds && api.renderer_enabled && api.set_enabled && api.in_hierarchy &&
                         pv.component_go && api.instantiate;
    if (!holding) {
        logx::warn("avatar: what the others hold won't be shown (a TLD or Unity API is missing)");
        api.load_gear = nullptr;
        api.instantiate_gear = nullptr;
    }

    int64_t value = 0;
    if (primitive_type && il2::enum_value(primitive_type, "Capsule", value)) api.capsule = static_cast<int>(value);
    if (primitive_type && il2::enum_value(primitive_type, "Cube", value)) api.cube = static_cast<int>(value);

    g_ready = api.go_ctor && api.create_primitive && api.set_active && api.go_transform && api.get_component &&
              api.get_components && api.destroy && api.destroy_now && api.alive && api.keep_on_load &&
              api.set_position && api.set_euler && api.set_parent && api.collider_type && api.component_type;
    const bool bodies = api.load_bundle && api.load_asset && api.instantiate && api.game_object_type &&
                        api.animation_type && api.crossfade && api.anim_state && api.state_speed;
    logx::info("avatar: %s; story bodies %s",
               g_ready ? "ready" : "INCOMPLETE (see warnings); remote player won't be drawn",
               bodies ? "supported" : "unsupported (capsule only)");
    return g_ready;
}

void use_tld_shaders(bool on) { g_use_tld_shaders = on; }

std::string describe_held() {
    std::string out;
    char text[400];
    for (const auto& [id, a] : g_avatars) {
        if (!out.empty()) out += "; ";
        snprintf(text, sizeof(text), "player %u: %s", id, a->held_logged.empty() ? "nothing" : a->held_logged.c_str());
        out += text;
        const HeldCopy& c = a->held;
        Il2CppObject* tf = target(c.transform);
        if (!tf) {
            if (!a->held_failed.empty()) out += " (no copy: " + a->held_failed + " couldn't be made)";
            continue;
        }
        const std::string lights_on = light_list(target(c.go), true);
        const PVec at = position_of(tf), root = position_of(target(a->root_transform));
        const Quat q = rotation_of(tf);
        const PVec axis = net::pose::rotate(q, g_flipped[static_cast<int>(c.kind)] ? c.axis * -1.0f : c.axis);
        const net::pose::Hold h = hold_of(c.kind, a->held_flags);
        const PVec grip = at + net::pose::rotate(q, c.centre + (g_flipped[static_cast<int>(c.kind)] ? c.axis * -1.0f
                                                                                                  : c.axis) *
                                                                        (h.grip * c.half));
        const bool bones = has_bones(*a);
        const float from_hand = bones ? net::pose::length(grip - palm(*a, h.in_left ? kLeftArm : kRightArm)) : -1;
        snprintf(text, sizeof(text),
                 " [hold %s: copy at (%.2f, %.2f, %.2f), %.2f m up and %.2f m out from their feet, pointing (%.2f, "
                 "%.2f, %.2f), %s; %s; ",
                 hold_key(c.kind, a->held_flags).c_str(), at.x, at.y, at.z, at.y - root.y,
                 net::pose::length(PVec{at.x - root.x, 0, at.z - root.z}), axis.x, axis.y, axis.z,
                 bones ? ("grip " + std::to_string(from_hand).substr(0, 4) + " m from the hand").c_str()
                       : "no arm bones (at the body)",
                 c.lit_shown == 2 ? "high beam" : c.lit_shown == 1 ? "lit" : "unlit");
        out += text;
        out += (lights_on.empty() ? "no light on" : lights_on) + "]";
    }
    return out.empty() ? "no other players" : out;
}

std::string describe_lights(Il2CppObject* go) {
    const std::string lights = light_list(go, false);
    return lights.empty() ? "no lights" : lights;
}

std::string tune_hold(const std::string& args) {
    std::istringstream in(args);
    std::string key, field;
    in >> key >> field;
    if (key == "part") {  // hold part NAME 0|1: a part of each copy shown or hidden (field holds the name)
        int on = 1;
        in >> on;
        int changed = 0;
        for (const auto& [id, a] : g_avatars) {
            Il2CppObject* copy = target(a->held.go);
            Il2CppObject* tfs = copy ? components_in_children(copy, api.transform_type) : nullptr;
            for (size_t i = 0, n = il2::array_length(tfs); i < n; ++i) {
                Il2CppObject* go = il2::invoke(pv.component_go, il2::array_object_at(tfs, i));
                if (go && name_of(go) == field) {
                    set_active(go, on != 0);
                    ++changed;
                }
            }
        }
        return "hold: part " + field + (on ? " shown" : " hidden") + " in " + std::to_string(changed) + " copy part(s)";
    }
    if (key == "pitch") {
        float sign = 1;
        if (in >> sign) g_pitch_sign = sign < 0 ? -1.0f : 1.0f;
        return "hold: their pitch counts " + std::string(g_pitch_sign > 0 ? "up" : "down") + " as positive";
    }
    net::HeldKind kind = net::HeldKind::None;
    uint8_t flags = 0;
    bool found = false;
    for (int k = 1; k < static_cast<int>(net::HeldKind::Count) && !found; ++k)
        for (uint8_t f : {uint8_t{0}, net::kHeldAiming, net::kHeldRaised})
            if (hold_key(static_cast<net::HeldKind>(k), f) == key) {
                kind = static_cast<net::HeldKind>(k);
                flags = f;
                found = true;
                break;
            }
    if (!found) return "hold: no hold '" + key + "' (e.g. torch, red_flare, lantern, flashlight, rifle, rifle_aim, bow_aim)";
    if (field == "reset") {
        g_holds_tuned.erase(key);
        g_flipped.erase(static_cast<int>(kind));
    } else if (!field.empty()) {
        net::pose::Hold& h = g_holds_tuned.emplace(key, hold_of(kind, flags)).first->second;
        auto vec = [&](PVec& v) { in >> v.x >> v.y >> v.z; };
        int on = 0;
        if (field == "rh") vec(h.right_hand);
        else if (field == "lh") vec(h.left_hand);
        else if (field == "dir") vec(h.item_dir);
        else if (field == "up") vec(h.item_up);
        else if (field == "grip") in >> h.grip;
        else if (field == "left") in >> h.left_on_item;
        else if (field == "look" && (in >> on)) h.look = on != 0;
        else if (field == "right" && (in >> on)) h.right = on != 0;
        else if (field == "leftarm" && (in >> on)) h.left = on != 0;
        else if (field == "hangs" && (in >> on)) h.hangs = on != 0;
        else if (field == "flip" && (in >> on)) g_flipped[static_cast<int>(kind)] = on != 0;
        else if (field != "show") return "hold: no field '" + field + "' (rh, lh, dir, up, grip, left, look, right, "
                                          "leftarm, hangs, flip, reset)";
    }
    const net::pose::Hold h = hold_of(kind, flags);
    char text[400];
    snprintf(text, sizeof(text),
             "hold %s%s: rh (%.2f, %.2f, %.2f)%s, lh (%.2f, %.2f, %.2f)%s, left on item %.2f, dir (%.2f, %.2f, %.2f), "
             "up (%.2f, %.2f, %.2f), grip %.2f%s%s%s%s",
             key.c_str(), g_holds_tuned.count(key) ? " (tuned)" : "", h.right_hand.x, h.right_hand.y, h.right_hand.z,
             h.right ? "" : " (off)", h.left_hand.x, h.left_hand.y, h.left_hand.z, h.left ? "" : " (off)",
             h.left_on_item, h.item_dir.x, h.item_dir.y, h.item_dir.z, h.item_up.x, h.item_up.y, h.item_up.z, h.grip,
             h.look ? ", follows the look" : "", h.hangs ? ", hangs" : "", h.in_left ? ", in the left hand" : "",
             g_flipped[static_cast<int>(kind)] ? ", flipped" : "");
    return text;
}

void push(std::vector<net::TimedState>& states) {
    for (const auto& ts : states)
        if (ts.state.player < net::kMaxPlayers && g_in_game[ts.state.player])
            avatar_of(ts.state.player).interp.add(ts.state, ts.received_ms);
    states.clear();
}

void set_players(const std::vector<net::PlayerInfo>& players) {
    g_in_game.fill(false);
    for (const net::PlayerInfo& p : players) {
        if (p.id >= net::kMaxPlayers) continue;
        g_in_game[p.id] = true;
        if (const auto it = g_avatars.find(p.id); it != g_avatars.end() && it->second->link && it->second->link != p.link) {
            // Another connection in their place (they came back, or someone new): start over.
            if (it->second->root) say(*it->second, "removed: another connection in this place");
            remove(*it->second);
            g_avatars.erase(it);
        }
        Avatar& a = avatar_of(p.id);
        a.link = p.link;
        a.name = p.name;
        if (!p.body.empty()) set_body_of(a, p.body);  // "" until their profile arrives
    }
    for (auto it = g_avatars.begin(); it != g_avatars.end();) {
        if (g_in_game[it->first]) {
            ++it;
            continue;
        }
        if (it->second->root) say(*it->second, "removed: not in the game any more (they left, or the link dropped)");
        remove(*it->second);
        it = g_avatars.erase(it);
    }
}

void update(const std::string& local_scene, bool local_in_game) {
    if (!g_ready) return;
    const double now = net::now_ms();

    // The roots survive scene loads, but anything can destroy them (e.g. a return to the main menu).
    if (local_scene != g_last_local_scene || now - g_last_alive_check >= kAliveCheckMs) {
        g_last_alive_check = now;
        if (g_loader && !is_alive(target(g_loader))) {
            drop(g_loader);
            drop(g_loader_transform);
        }
        for (auto& [id, a] : g_avatars) {
            if (!a->root || is_alive(target(a->root))) continue;
            say(*a, "was destroyed; will rebuild");
            release(*a);
        }
    }
    g_last_local_scene = local_scene;
    if (local_in_game && now - g_indoors_checked >= 1000) {  // which of a held light's two lights to use
        g_indoors_checked = now;
        g_indoors = game::read_world().indoors;
    }
    for (auto& [id, a] : g_avatars) update_one(*a, local_scene, local_in_game, now);
}

void late_update() {
    if (!g_ready) return;
    for (auto& [id, a] : g_avatars) late_one(*a);
}

std::string describe() {
    if (g_avatars.empty()) return "no other players";
    std::string out;
    for (const auto& [id, a] : g_avatars) {
        if (!out.empty()) out += "; ";
        out += "player " + std::to_string(id) + (a->name.empty() ? "" : " '" + a->name + "'") + ": " + a->status;
    }
    return out;
}

bool stand_in(uint8_t player, Il2CppObject*& go, Il2CppObject*& transform, Il2CppObject*& ai_target) {
    const auto it = g_avatars.find(player);
    if (it == g_avatars.end() || !shown_here(*it->second)) return false;
    const Avatar& a = *it->second;
    go = target(a.root);
    transform = target(a.root_transform);
    ai_target = target(a.ai_target);
    return go && transform && ai_target;
}

std::vector<uint8_t> shown() {
    std::vector<uint8_t> out;
    for (const auto& [id, a] : g_avatars)
        if (shown_here(*a)) out.push_back(id);
    return out;
}

std::vector<Seen> seen() {
    std::vector<Seen> out;
    for (const auto& [id, a] : g_avatars) {
        if (!g_in_game[id]) continue;
        Seen v;
        v.player = id;
        v.name = a->name;
        v.playing = a->fresh && a->last.valid;
        v.dead = (a->last.flags & net::kStateDead) != 0;
        v.crouched = (a->last.flags & net::kStateCrouched) != 0;
        v.condition = a->last.condition;
        v.afflictions = a->last.afflictions;
        v.at_x = a->last.x;
        v.at_z = a->last.z;
        if (v.playing) v.scene = a->last.scene;
        v.here = shown_here(*a);
        if (v.here) {
            const PVec p = position_of(target(a->root_transform));
            v.x = p.x;
            v.y = p.y;
            v.z = p.z;
            v.yaw = a->yaw;
        } else {
            v.x = a->last.x;
            v.y = a->last.y;
            v.z = a->last.z;
            v.yaw = a->last.yaw;
        }
        out.push_back(std::move(v));
    }
    return out;
}

std::optional<uint8_t> player_of(Il2CppObject* obj) {
    if (!obj) return std::nullopt;
    for (const auto& [id, a] : g_avatars)
        if (a->root && (target(a->ai_target) == obj || target(a->root) == obj)) return id;
    return std::nullopt;
}

bool is_known_body(const std::string& id) { return known_body(id); }

bool female_body(const std::string& id) { return female_rig(id); }

bool female(uint8_t player) {
    const auto it = g_avatars.find(player);
    return it != g_avatars.end() && female_rig(it->second->wanted_body);
}

void show_handover(uint8_t player, const HandoverShow* show) {
    const auto it = g_avatars.find(player);
    if (it == g_avatars.end()) return;
    Avatar& a = *it->second;
    if (!show) {
        a.handover_on = false;
        return;
    }
    a.handover_on = true;
    a.handover = *show;
}

std::string tune_pose(const std::string& args) {
    std::istringstream in(args);
    std::string what, field;
    float v = 0;
    in >> what >> field;
    const bool value = static_cast<bool>(in >> v);
    if (what == "act") {
        g_forced_action = field == "off" ? std::string() : field;
        g_forced_time = value ? v : -1;
    } else if (what == "bed" && value) {
        if (field == "yaw") g_bed_yaw = v;
        else if (field == "lift") g_bed_lift = v;
        else return "pose: bed yaw|lift VALUE";
    } else if (what == "crouch" && value) {
        if (field == "depth") g_crouch.depth = v;
        else if (field == "back") g_crouch.back = v;
        else if (field == "tilt") g_crouch.tilt = v;
        else if (field == "lean") g_crouch.lean = v;
        else if (field == "knees") g_crouch.knees_out = v;
        else return "pose: crouch depth|back|tilt|lean|knees VALUE";
    } else if (!what.empty() && what != "show") {
        return "pose: bed yaw|lift VALUE, or crouch depth|back|tilt|lean|knees VALUE";
    }
    char text[300];
    snprintf(text, sizeof(text),
             "pose: bed yaw %.0f lift %.2f; crouch depth %.2f back %.2f (of the leg) tilt %.0f lean %.0f knees %.2f",
             g_bed_yaw, g_bed_lift, g_crouch.depth, g_crouch.back, g_crouch.tilt, g_crouch.lean, g_crouch.knees_out);
    std::string out = text;
    float time_scale = -1;
    if (api.time_scale) il2::invoke_value(api.time_scale, nullptr, time_scale);
    snprintf(text, sizeof(text), "; time scale %.2f", time_scale);
    out += text;
    for (const auto& [id, a] : g_avatars) {
        snprintf(text, sizeof(text),
                 "; player %u: %s%s%s, crouch %.2f, speed %.2f, upper %.2f, %d action states, yaw %.0f, right hand "
                 "from the clips (%.2f right, %.2f up, %.2f forward)",
                 id, a->action.empty() ? "no action" : a->action.c_str(), a->lying ? " (in bed)" : "",
                 a->handover_on ? " (handover)" : "", a->crouch, a->speed, a->lower, a->actions, a->shown_yaw,
                 a->clip_hand[0], a->clip_hand[1], a->clip_hand[2]);
        out += text;
        Il2CppObject* animation = target(a->gait.animation);
        for (const std::string& name : {a->action, a->action + "_up"}) {  // the clip as Unity has it now
            Il2CppObject* st = animation && !a->action.empty() ? animation_state(animation, name.c_str()) : nullptr;
            float time = -1;
            bool playing = false;
            if (!st) continue;
            il2::invoke_value(api.state_get_time, st, time);
            void* name_args[] = {il2::il2cpp_string_new(name.c_str())};
            if (api.anim_playing_named) il2::invoke_value(api.anim_playing_named, animation, playing, name_args);
            snprintf(text, sizeof(text), " [%s time %.2f %s]", name.c_str(), time, playing ? "playing" : "stopped");
            out += text;
        }
    }
    return out;
}

std::string choosable_body_ids() {
    std::string ids;
    for (const char* b : kChoosableBodies) ids += std::string(ids.empty() ? "" : ", ") + b;
    return ids;
}

bool is_choosable_body(const std::string& id) {
    for (const char* b : kChoosableBodies)
        if (id == b) return true;
    return false;
}

std::string body_ids() {
    std::string ids;
    for (const char* b : kBodies) ids += std::string(ids.empty() ? "" : ", ") + b;
    return ids;
}

}  // namespace avatar
