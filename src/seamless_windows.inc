// TLDCoop_DLC: Seamless Interiors, Stage 2: seeing through windows (SI: WindowPortals.cs, WindowOpenings.cs,
// WindowPairing.cs, ShellWindows.cs, ParkedRoomYaw.cs, ParkedCullEye.cs + Patches/ParkedCullEyePatches.cs,
// ParkedWeather.cs, Patches/ParkedSnowBoxPatches.cs, Patches/PortalFrustumPatch.cs, OutsideInPortals.cs,
// OutsideInBuildings.cs). Included by seamless_interiors.cpp (inside its anonymous namespace), after seamless_fx.inc.
//
// INSIDE-OUT. A parked room stands 80 m above its building, so its windows would look at the sky. Each visible window
// of the room the player is in gets a view: a camera of ours placed where the player's eye would be at the building
// (eye + the delta from the room's pane to the building's paired pane: SI's per-window pairing, FitWindowPattern), an
// off-axis projection fitted to the pane (Matrix4x4.Frustum through the part of the pane on screen), rendering into a
// RenderTexture shown on a quad (Unlit/Texture, queue 3001, layer 7, which those cameras don't draw) standing between
// the frost and the frame. As in SI the cameras are ENABLED with a negative depth so Unity draws them at its own point
// in the frame (a hand Render() from a mod draws the world before TLD has settled its trees: SI measured "a hill with
// nothing standing on it"). Slots (camera + target + material) are made on demand up to the quality's limit; a window
// already drawing keeps its slot unless another beats it by 25 % (SI's hysteresis).
// The trees and rocks are culled by TLD for its own camera (RenderObjectInstance.m_Camera/m_CameraComponent,
// MainCameraFrustrum.RecalculateFrustum*): while a view is live a stand-in camera at the player's eye minus the park
// offset is given to every RenderObjectInstance and swapped into the frustum calls (SI: ParkedCullEye), and
// Utils.PositionIsOnscreen answers for that eye; the snow simulation box is carried down to the views
// (WeatherParticleManager.Schedule*SnowParticleJobs, SI: ParkedSnowBoxShift). The building's shell stays on while
// inside (SI: KeepParkedShellOn: the porch and the roof show in the views). The painted backdrop behind a window is
// switched off only while that window shows a live view (and back on otherwise, and always at the end).
// The room is turned to its building first (SI: TurnParkedRoomToShell, or the building's hand turn from
// BuildingWindowOverrides.cs), in align_room, whatever the windows setting: every game of a co-op session turns it the
// same way (the yaw is in parked_point too).
//
// OUTSIDE-IN. From the yard, up to N nearby built rooms (by quality) are held switched on, every collider of theirs
// switched off every frame and every renderer moved to a private layer (3, 4 or 30, one per room, as SI) that the
// player's camera no longer draws; a camera at the eye + the delta from the building's pane to the room's paired pane
// renders only that layer by hand (Camera.Render) into a quad 1 cm in front of the building's glass. A friend standing
// in a held room is drawn on that room's layer (avatar.cpp asks view_layer()).
//
// ONE RECONCILER. Everything above is decided once a frame in tick_windows (after vp_FPSCamera.LateUpdate, where the
// player's camera and zoom are final) from what should be, and every change to the game (the cull eye, the frustum
// swap, the main camera's culling mask, the held rooms' layers and colliders, the backdrops, the fog switch) is undone
// by release_windows: when the views aren't wanted, on a region change or scene load, on the way to the main menu, when
// the reconciler hasn't run for 30 frames, and when a door opens a room (its colliders come back before the teleport).
//
// NOT CARRIED (see the HANDOFF entry): frost/glass materials and snow slabs of SI's own, wall cameras, the fog pass and
// shared command buffers of the views, TAA motion vectors, shaped (non-rectangular) views, bent/loose/band glass and the
// rows that change how a room's windows are found, shell frame relief measurement (the view starts 4 cm out),
// outside-in frost overlays, recesses, bar caps and drifts.

struct WinSpot {
    float x, y, z, nx, nz, w, h;
};
struct ShellSpots {
    const char* shell;
    int first, count;
};
struct WinViewOut {
    int room;
    float right, up, out;
};
struct WinRow {
    const char* shell;
    float turn, in_place_cm, view_from_out_cm, plane_cm;
    bool scan_altered;
    int pair_first, pair_count, left_first, left_count, out_first, out_count;
};
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
#include "seamless_windows_data.inc"

// --- constants from SI -------------------------------------------------------------------------------------------------

constexpr int kQuadLayer = 7;                       // SI: WINDOW_PORTAL_QUAD_LAYER
constexpr int kRoomLayers[] = {3, 4, 30};           // SI: OUTSIDE_IN_ROOM_LAYERS
constexpr int kRoomLayersMask = (1 << 3) | (1 << 4) | (1 << 30);
constexpr float kMinEyeDistance = 0.05f;            // SI: WINDOW_PORTAL_MIN_EYE_DISTANCE
constexpr float kViewStartsOutsideShellGlass = 0.04f;
constexpr int kViewOutQueue = 3001, kViewInQueue = 2600;
constexpr int kMinTarget = 128;
constexpr float kSubrectMargin = 0.03f, kSubrectMarginFloor = 0.01f;
constexpr float kHysteresis = 1.25f;
constexpr float kPortalDefaultOffset = 0.093f, kPortalClearance = 0.002f;
constexpr float kWindowLargest = 6.0f;
constexpr float kYawMatchDistance = 6.0f, kYawAgreement = 1.0f, kYawMatchDot = 0.99f;
constexpr float kPatternSameFacing = 0.85f, kPatternPlanTie = 0.005f;
constexpr int kPatternBudget = 200000, kPatternExhaustiveUpTo = 100;
constexpr float kDoubleWiderBy = 1.4f, kDoubleSamePlane = 0.05f, kDoubleWidthWithin = 0.30f;
constexpr float kShellJoinGap = 0.15f, kShellPlaneTolerance = 0.12f, kShellSquareCos = 0.985f;
constexpr float kOutsideInToward = 0.01f, kPretendEyeInRoom = 0.5f, kOutsideInFarClip = 500.0f;
constexpr float kOutsideInReleaseMargin = 5.0f, kOutsideInRoomReleaseMargin = 15.0f, kHeldRoomAdvantage = 0.85f;
constexpr int kStaleFrames = 30;

struct WinQuality {
    int in_slots;        // SI: WindowPortalLimit (10)
    float scale;         // SI: WindowPortalResolutionScale
    int oi_slots;        // views into rooms drawn per frame
    int oi_rooms;        // SI: OutsideInMaxHeldRooms (3)
    float oi_range;      // SI: OutsideInPortalRange (100 m)
};
constexpr WinQuality kQualities[] = {
    {2, 0.35f, 2, 1, 30.0f},   // 0 low
    {4, 0.6f, 4, 2, 50.0f},    // 1 medium
    {8, 1.0f, 8, 3, 100.0f},   // 2 high
};

uint64_t g_win_frame_now = 0;  // frames counted by the reconciler's watch (GameManager.Update)
bool g_windows = true;      // coop.ini [interiors] windows
int g_window_quality = 1;   // coop.ini [interiors] window_quality
int g_indoor_light = 0;     // coop.ini [interiors] indoor_light (%; 0: the game's own interior light)
const WinQuality& quality() { return kQualities[std::clamp(g_window_quality, 0, 2)]; }
// Door portals (seamless_doors.inc): coop.ini [interiors] door_portals / door_walk, and the building whose door view is
// wanted this frame (held for it first, also with the window views off)
bool g_door_portals = true;
bool g_door_walk = true;
const Config* g_door_want = nullptr;
constexpr size_t kDoorFxBase = 32;  // g_slot_fx entries from here are the door cameras' (seamless_doors.inc)

// --- the game ----------------------------------------------------------------------------------------------------------

struct WinApi {
    bool ok = false, cull_ok = false, snow_ok = false, fog_ok = false;
    Il2CppClass* rt_class = nullptr, *mat_class = nullptr;
    const MethodInfo* cam_render = nullptr, *cam_copy = nullptr, *cam_set_target = nullptr, *cam_set_depth = nullptr,
                      *cam_get_mask = nullptr, *cam_set_mask = nullptr, *cam_set_occlusion = nullptr, *cam_get_fov = nullptr,
                      *cam_set_fov = nullptr, *cam_get_aspect = nullptr, *cam_set_aspect = nullptr, *cam_get_near = nullptr,
                      *cam_set_near = nullptr, *cam_get_far = nullptr, *cam_set_far = nullptr, *cam_get_proj = nullptr,
                      *cam_set_proj = nullptr, *cam_get_w2c = nullptr, *cam_reset_w2c = nullptr, *cam_reset_proj = nullptr,
                      *cam_reset_cull = nullptr, *cam_set_hdr = nullptr;
    const MethodInfo* rt_ctor = nullptr, *rt_create = nullptr, *rt_release = nullptr, *supports_format = nullptr;
    const MethodInfo* shader_find = nullptr, *mat_ctor = nullptr, *mat_set_queue = nullptr, *mat_set_main = nullptr,
                      *mat_get_main = nullptr;
    const MethodInfo* create_primitive = nullptr, *r_set_shared = nullptr, *r_set_shadows = nullptr, *r_set_receive = nullptr;
    const MethodInfo* screen_w = nullptr, *screen_h = nullptr;
    const MethodInfo* fog_get = nullptr, *fog_set = nullptr;
    const MethodInfo* mesh_readable = nullptr, *mesh_vertices = nullptr, *mesh_triangles = nullptr, *mesh_submeshes = nullptr;
    const MethodInfo* tf_to_local = nullptr, *keep_on_load = nullptr, *hide_flags = nullptr;
    int32_t fmt_hdr = 2, prim_quad = 5;
    FieldInfo* roi_list = nullptr;
    ptrdiff_t roi_cam_tf = -1, roi_cam = -1, roi_frustum_type = -1;
    ptrdiff_t snow_cam_mat = -1, snow_cam_inv = -1, snow_cam_pos = -1;  // into WeatherParticleManager (m_Common's fields)
} wapi;

// --- small maths ---------------------------------------------------------------------------------------------------------

float w_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 w_cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float w_len(V3 a) { return std::sqrt(w_dot(a, a)); }
V3 w_norm(V3 a) {
    const float l = w_len(a);
    return l > 1e-8f ? a * (1.0f / l) : V3{0, 0, 1};
}
V3 w_flat(V3 v) {
    v.y = 0;
    return w_dot(v, v) > 1e-8f ? w_norm(v) : V3{0, 0, 1};
}
const V3 kUp{0, 1, 0};
// Vector3.SignedAngle(from, to, up) for level vectors, degrees
float w_signed_angle(V3 from, V3 to) {
    from = w_flat(from);
    to = w_flat(to);
    const float c = std::clamp(w_dot(from, to), -1.0f, 1.0f);
    float a = std::acos(c) * 57.29578f;
    if (w_dot(w_cross(from, to), kUp) < 0) a = -a;
    return a;
}
float w_delta_angle(float a, float b) {  // Mathf.DeltaAngle
    float d = std::fmod(b - a, 360.0f);
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}
float w_yaw_of(Quat q) {  // eulerAngles.y of an upright rotation
    const V3 f = rotate(q, {0, 0, 1});
    return std::atan2(f.x, f.z) * 57.29578f;
}
// Quaternion.LookRotation(forward, up)
Quat w_look(V3 fwd, V3 up) {
    const V3 f = w_norm(fwd);
    const V3 r = w_norm(w_cross(up, f));
    const V3 u = w_cross(f, r);
    const float m00 = r.x, m01 = u.x, m02 = f.x, m10 = r.y, m11 = u.y, m12 = f.y, m20 = r.z, m21 = u.z, m22 = f.z;
    Quat q;
    const float tr = m00 + m11 + m22;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q;
}

struct M4 {  // UnityEngine.Matrix4x4: column-major (m[c * 4 + r])
    float m[16] = {};
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }
};
M4 m4_mul(const M4& a, const M4& b) {
    M4 o;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(r, k) * b.at(k, c);
            o.at(r, c) = s;
        }
    return o;
}
V3 m4_point(const M4& a, V3 p) {
    return {a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z + a.at(0, 3),
            a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z + a.at(1, 3),
            a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z + a.at(2, 3)};
}
V3 m4_dir(const M4& a, V3 p) {
    return {a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z, a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z,
            a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z};
}
bool m4_inverse(const M4& a, M4& out) {  // the general 4x4 inverse (cofactors)
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] -
             m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] +
             m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] -
             m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] +
              m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] +
             m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] -
             m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] +
             m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] -
              m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] -
             m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] +
             m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] -
              m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] +
              m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] +
             m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] -
             m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] +
              m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] -
              m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-12f) return false;
    det = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * det;
    return true;
}
// Matrix4x4.Frustum (OpenGL's glFrustum, which is what Camera.projectionMatrix takes)
M4 m4_frustum(float l, float r, float b, float t, float zn, float zf) {
    M4 o;
    o.at(0, 0) = 2 * zn / (r - l);
    o.at(0, 2) = (r + l) / (r - l);
    o.at(1, 1) = 2 * zn / (t - b);
    o.at(1, 2) = (t + b) / (t - b);
    o.at(2, 2) = -(zf + zn) / (zf - zn);
    o.at(2, 3) = -2 * zf * zn / (zf - zn);
    o.at(3, 2) = -1;
    return o;
}

struct Plane4 {
    float x = 0, y = 0, z = 0, w = 0;
};

// --- the player's camera, as rendered this frame (SI: CaptureMainCameraFrustumLate) --------------------------------------

struct MainCam {
    bool ok = false;
    uintptr_t cam = 0;
    Il2CppObject* raw = nullptr;  // compared with the cameras the engine renders (never dereferenced)
    V3 pos;
    Quat rot;
    M4 vp;
    Plane4 planes[5];
    float fov = 60, aspect = 1.78f, znear = 0.1f, zfar = 1000;
    int mask = -1;
    int sw = 1920, sh = 1080;
} g_main;

Il2CppObject* main_camera() { return il2::invoke(api.gm_main_camera, nullptr); }

template <typename T>
T cam_value(const MethodInfo* m, Il2CppObject* cam, T fallback) {
    T v{};
    return m && il2::invoke_value(m, cam, v) ? v : fallback;
}
template <typename T>
void cam_set(const MethodInfo* m, Il2CppObject* cam, T v) {
    if (!m || !alive(cam)) return;
    void* a[] = {&v};
    il2::invoke(m, cam, a);
}

bool capture_main() {
    g_main.ok = false;
    Il2CppObject* cam = main_camera();
    if (!alive(cam)) return false;
    if (live(g_main.cam) != cam) {
        drop(g_main.cam);
        g_main.cam = hold(cam);
    }
    g_main.raw = cam;
    Il2CppObject* t = tf_of(cam);
    g_main.pos = position(t);
    g_main.rot = rotation(t);
    M4 proj, w2c;
    if (!il2::invoke_value(wapi.cam_get_proj, cam, proj) || !il2::invoke_value(wapi.cam_get_w2c, cam, w2c)) return false;
    g_main.vp = m4_mul(proj, w2c);
    const M4& v = g_main.vp;
    auto row = [&](int r) { return Plane4{v.at(r, 0), v.at(r, 1), v.at(r, 2), v.at(r, 3)}; };
    const Plane4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](Plane4 a, Plane4 b) { return Plane4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; };
    auto sub = [](Plane4 a, Plane4 b) { return Plane4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; };
    g_main.planes[0] = add(r3, r0);
    g_main.planes[1] = sub(r3, r0);
    g_main.planes[2] = add(r3, r1);
    g_main.planes[3] = sub(r3, r1);
    g_main.planes[4] = add(r3, r2);
    g_main.fov = cam_value<float>(wapi.cam_get_fov, cam, 60);
    g_main.aspect = cam_value<float>(wapi.cam_get_aspect, cam, 1.78f);
    g_main.znear = cam_value<float>(wapi.cam_get_near, cam, 0.1f);
    g_main.zfar = cam_value<float>(wapi.cam_get_far, cam, 1000);
    g_main.mask = cam_value<int32_t>(wapi.cam_get_mask, cam, -1);
    g_main.sw = std::max(1, cam_value<int32_t>(wapi.screen_w, nullptr, 1920));
    g_main.sh = std::max(1, cam_value<int32_t>(wapi.screen_h, nullptr, 1080));
    g_main.ok = true;
    return true;
}

// Sutherland-Hodgman against one plane (SI: ClipWindowPortalPolygon)
void clip_poly(const std::vector<V3>& in, std::vector<V3>& out, const Plane4& p) {
    out.clear();
    const size_t n = in.size();
    for (size_t i = 0; i < n; ++i) {
        const V3 a = in[i], b = in[(i + 1) % n];
        const float da = p.x * a.x + p.y * a.y + p.z * a.z + p.w, db = p.x * b.x + p.y * b.y + p.z * b.z + p.w;
        if (da >= 0) out.push_back(a);
        if ((da >= 0) != (db >= 0)) {
            const float t = da / (da - db);
            out.push_back(a + (b - a) * t);
        }
    }
}
// SI: TryComputeVisibleSubRect: the part of a rectangle (corner pa, right/up, w x h) on screen, with a margin.
bool visible_subrect(V3 pa, V3 right, V3 up, float w, float h, float& u0, float& u1, float& v0, float& v1) {
    static std::vector<V3> a, b;
    a.assign({pa, pa + right * w, pa + right * w + up * h, pa + up * h});
    for (const Plane4& p : g_main.planes) {
        clip_poly(a, b, p);
        a.swap(b);
        if (a.empty()) return false;
    }
    if (a.size() < 3) return false;
    float mnu = 1e30f, mxu = -1e30f, mnv = 1e30f, mxv = -1e30f;
    for (const V3& q : a) {
        const float u = w_dot(q - pa, right), v = w_dot(q - pa, up);
        mnu = std::min(mnu, u);
        mxu = std::max(mxu, u);
        mnv = std::min(mnv, v);
        mxv = std::max(mxv, v);
    }
    const float mu = std::max((mxu - mnu) * kSubrectMargin, kSubrectMarginFloor);
    const float mv = std::max((mxv - mnv) * kSubrectMargin, kSubrectMarginFloor);
    u0 = std::clamp(mnu - mu, 0.0f, w);
    u1 = std::clamp(mxu + mu, 0.0f, w);
    v0 = std::clamp(mnv - mv, 0.0f, h);
    v1 = std::clamp(mxv + mv, 0.0f, h);
    return u1 - u0 > 0.005f && v1 - v0 > 0.005f;
}
// SI: ComputeWindowPortalPixelSize: the target as big as the quad on screen (power of two, quality-scaled).
void pixel_size(V3 qa, V3 qb, V3 qc, float scale, int& w, int& h) {
    const V3 qd = qb + (qc - qa);
    float mnx = 1e30f, mxx = -1e30f, mny = 1e30f, mxy = -1e30f;
    for (V3 p : {qa, qb, qc, qd}) {
        const M4& v = g_main.vp;
        const float cx = v.at(0, 0) * p.x + v.at(0, 1) * p.y + v.at(0, 2) * p.z + v.at(0, 3);
        const float cy = v.at(1, 0) * p.x + v.at(1, 1) * p.y + v.at(1, 2) * p.z + v.at(1, 3);
        const float cw = v.at(3, 0) * p.x + v.at(3, 1) * p.y + v.at(3, 2) * p.z + v.at(3, 3);
        if (cw <= 1e-6f) {
            mnx = 0, mxx = 1, mny = 0, mxy = 1;
            break;
        }
        const float x = cx / cw * 0.5f + 0.5f, y = cy / cw * 0.5f + 0.5f;
        mnx = std::min(mnx, x), mxx = std::max(mxx, x), mny = std::min(mny, y), mxy = std::max(mxy, y);
    }
    mnx = std::clamp(mnx, 0.0f, 1.0f), mxx = std::clamp(mxx, 0.0f, 1.0f);
    mny = std::clamp(mny, 0.0f, 1.0f), mxy = std::clamp(mxy, 0.0f, 1.0f);
    auto pow2 = [](int v) {
        int r = kMinTarget;
        while (r < v && r < 8192) r <<= 1;
        return r;
    };
    w = std::clamp(pow2(static_cast<int>((mxx - mnx) * g_main.sw * scale + 0.5f)), kMinTarget, std::max(kMinTarget, g_main.sw));
    h = std::clamp(pow2(static_cast<int>((mxy - mny) * g_main.sh * scale + 0.5f)), kMinTarget, std::max(kMinTarget, g_main.sh));
}

// --- hand data (BuildingWindowOverrides.cs) ----------------------------------------------------------------------------

const WinRow* win_row(const char* shell) {
    if (!shell || !*shell) return nullptr;
    for (const WinRow& r : kWinRows)
        if (std::strcmp(r.shell, shell) == 0) return &r;
    return nullptr;
}
bool left_alone(const WinRow* row, int window) {  // SI: HasNoView
    if (!row) return false;
    for (int i = 0; i < row->left_count; ++i)
        if (kWinInts[row->left_first + i] == window) return true;
    return false;
}
V3 view_out_offset(const WinRow* row, int room_window, V3 outward) {  // SI: ViewOutOffset
    if (!row || row->scan_altered || !row->out_count) return {};
    outward.y = 0;
    if (w_dot(outward, outward) < 1e-6f) return {};
    outward = w_norm(outward);
    const V3 right = w_norm(w_cross(kUp, outward));
    for (int i = 0; i < row->out_count; ++i) {
        const WinViewOut& v = kWinViewOuts[row->out_first + i];
        if (v.room == room_window) return right * (v.right / 100) + kUp * (v.up / 100) + outward * (v.out / 100);
    }
    return {};
}

// --- the room's windows (SI: WindowOpenings.cs, the ordinary rule) -------------------------------------------------------

struct Opening {  // room-local (the room's root): the room may be turned after it was read
    V3 glass, portal, out, right, up;
    float w = 0, h = 0;
    uintptr_t backdrop = 0;  // Renderer: the painted outside behind the glass
    std::string name;
};
struct WinRoom {
    bool scanned = false;
    std::vector<Opening> openings;
    ~WinRoom() {
        for (Opening& o : openings) drop(o.backdrop);
    }
};

bool starts_with(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool contains_nocase(const std::string& s, const char* p) {
    std::string a = s, b = p;
    for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(b) != std::string::npos;
}
std::string material_name(Il2CppObject* renderer) {
    Il2CppObject* mat = alive(renderer) ? il2::invoke(api.r_material, renderer) : nullptr;
    return alive(mat) ? name_of(mat) : std::string();
}
// role: 0 glass, 1 snow, 2 frame, 3 backdrop (SI: MatchesWindowRole)
bool matches_role(int role, const std::string& obj, const std::string& mat) {
    if (role == 0) return starts_with(mat, "STR_WindowGlassMix");
    if (role == 1) return contains_nocase(obj, "WindowSnow");
    if (role == 2) return (obj.size() >= 5 && obj.compare(obj.size() - 5, 5, "_LOD0") == 0) || contains_nocase(obj, "WindowFrame");
    return contains_nocase(obj, "InteriorWindow") || starts_with(mat, "GLB_GlowExt");
}
// SI: FindWindowRoot: the nearest ancestor (3 up at most, never the room's root) with a backdrop under it.
Il2CppObject* window_root(Il2CppObject* glass_tf, Il2CppObject* room_tf) {
    Il2CppObject* first = parent_of(glass_tf);
    Il2CppObject* cur = first;
    for (int up = 0; up < 3 && cur && cur != room_tf; ++up, cur = parent_of(cur)) {
        bool found = false;
        each_in(go_of(cur), api.t_mesh_renderer, [&](Il2CppObject* r) {
            if (!found && matches_role(3, name_of(go_of(r)), material_name(r))) found = true;
        });
        if (found) return cur;
    }
    return first;
}
// SI: TryLocalBoundsOfRole: mesh bounds through the transform (works while the room is switched off), room-local.
bool role_box(Instance& in, Il2CppObject* root_tf, int role, Box& box, Il2CppObject** backdrop) {
    bool any = false;
    each_in(go_of(root_tf), api.t_mesh_filter, [&](Il2CppObject* mf) {
        Il2CppObject* go = go_of(mf);
        Il2CppObject* mr = component(go, api.t_mesh_renderer);
        if (!matches_role(role, name_of(go), mr ? material_name(mr) : std::string())) return;
        Il2CppObject* mesh = il2::invoke(api.mesh_filter_mesh, mf);
        if (!alive(mesh)) return;
        UBounds b;
        M4 l2w;
        if (!il2::invoke_value(api.mesh_bounds, mesh, b) || !il2::invoke_value(api.tf_to_world, tf_of(mf), l2w)) return;
        if (role == 3 && mr && backdrop) *backdrop = mr;
        for (int k = 0; k < 8; ++k) {
            const V3 c{b.center.x + ((k & 1) ? -b.extents.x : b.extents.x), b.center.y + ((k & 2) ? -b.extents.y : b.extents.y),
                       b.center.z + ((k & 4) ? -b.extents.z : b.extents.z)};
            box.add(to_local(in, m4_point(l2w, c)));
        }
        any = true;
    });
    return any;
}
float axis_of(V3 v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); }
V3 with_axis(V3 v, int a, float f) {
    (a == 0 ? v.x : (a == 1 ? v.y : v.z)) = f;
    return v;
}
V3 unit_axis(int a, float s = 1) { return with_axis({}, a, s); }

// Every renderer of a role under root, each with its own room-local box (role 3: the renderer kept too).
struct RolePart {
    Box box;
    Il2CppObject* renderer = nullptr;
};
std::vector<RolePart> role_parts(Instance& in, Il2CppObject* root_tf, int role) {
    std::vector<RolePart> out;
    each_in(go_of(root_tf), api.t_mesh_filter, [&](Il2CppObject* mf) {
        Il2CppObject* go = go_of(mf);
        Il2CppObject* mr = component(go, api.t_mesh_renderer);
        if (!matches_role(role, name_of(go), mr ? material_name(mr) : std::string())) return;
        if (role == 0 && !active_self(go)) return;  // glass the game keeps switched off is no window
        Il2CppObject* mesh = il2::invoke(api.mesh_filter_mesh, mf);
        if (!alive(mesh)) return;
        UBounds b;
        M4 l2w;
        if (!il2::invoke_value(api.mesh_bounds, mesh, b) || !il2::invoke_value(api.tf_to_world, tf_of(mf), l2w)) return;
        RolePart part;
        for (int k = 0; k < 8; ++k) {
            const V3 c{b.center.x + ((k & 1) ? -b.extents.x : b.extents.x), b.center.y + ((k & 2) ? -b.extents.y : b.extents.y),
                       b.center.z + ((k & 4) ? -b.extents.z : b.extents.z)};
            part.box.add(to_local(in, m4_point(l2w, c)));
        }
        part.renderer = mr;
        out.push_back(part);
    });
    return out;
}
float box_gap(const Box& a, const Box& b) {
    const float dx = std::max({0.0f, a.min.x - b.max.x, b.min.x - a.max.x});
    const float dy = std::max({0.0f, a.min.y - b.max.y, b.min.y - a.max.y});
    const float dz = std::max({0.0f, a.min.z - b.max.z, b.min.z - a.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// SI: ScanInteriorWindowOpenings, its ordinary rule (a pane under a window root, faced by its snow or its backdrop).
// TLDCoop_DLC: the panes under one root are split into windows by where they are (most rooms keep all their windows'
// glass under one object: SI's rule saw one huge "window" and skipped it, "0 window(s) found" in nearly every room);
// each window is faced by the snow or backdrop parts next to it.
void scan_openings(Instance& in, WinRoom& wr) {
    wr.scanned = true;
    Il2CppObject* m = master_of(in);
    Il2CppObject* mt = live(in.master_tf);
    if (!m || !mt) return;
    std::unordered_set<Il2CppObject*> seen;
    int skipped = 0, unfaced = 0, split = 0, flipped = 0;
    each_in(m, api.t_mesh_renderer, [&](Il2CppObject* glass_r) {
        Il2CppObject* go = go_of(glass_r);
        if (!active_self(go)) return;
        if (!starts_with(material_name(glass_r), "STR_WindowGlassMix")) return;
        Il2CppObject* root = window_root(tf_of(glass_r), mt);
        if (!root || !seen.insert(root).second) return;
        const std::vector<RolePart> panes = role_parts(in, root, 0);
        if (panes.empty()) return;
        const std::vector<RolePart> snows = role_parts(in, root, 1), frames = role_parts(in, root, 2),
                                    backs = role_parts(in, root, 3);
        // panes 25 cm apart or less are one window (the sashes of one frame)
        std::vector<int> group(panes.size(), -1);
        int groups = 0;
        for (size_t i = 0; i < panes.size(); ++i) {
            if (group[i] >= 0) continue;
            group[i] = groups;
            for (bool grew = true; grew;) {
                grew = false;
                for (size_t j = 0; j < panes.size(); ++j) {
                    if (group[j] >= 0) continue;
                    for (size_t k = 0; k < panes.size(); ++k)
                        if (group[k] == groups && box_gap(panes[j].box, panes[k].box) <= 0.25f) {
                            group[j] = groups;
                            grew = true;
                            break;
                        }
                }
            }
            ++groups;
        }
        if (groups > 1) split += groups - 1;
        for (int gi = 0; gi < groups; ++gi) {
            Box glass;
            for (size_t i = 0; i < panes.size(); ++i)
                if (group[i] == gi) glass.add(panes[i].box.min), glass.add(panes[i].box.max);
            auto near_box = [&](const std::vector<RolePart>& parts, Box& out, Il2CppObject** keep) {
                bool any = false;
                float best = 1e30f;
                for (const RolePart& part : parts) {
                    const float gap = box_gap(part.box, glass);
                    if (gap > (groups > 1 ? 0.6f : 1e30f)) continue;  // (one window under the root: all of them, as SI)
                    out.add(part.box.min), out.add(part.box.max);
                    if (keep && gap < best) best = gap, *keep = part.renderer;
                    any = true;
                }
                return any;
            };
            Box snow, frame, back;
            Il2CppObject* backdrop = nullptr;
            const bool has_snow = near_box(snows, snow, nullptr);
            const bool has_frame = near_box(frames, frame, nullptr);
            const bool has_back = near_box(backs, back, &backdrop);
            const V3 gs = glass.max - glass.min;
            int axis = 0;
            if (gs.y < gs.x && gs.y < gs.z) axis = 1;
            else if (gs.z < gs.x && gs.z < gs.y) axis = 2;
            auto center = [](const Box& b, int a) { return (axis_of(b.min, a) + axis_of(b.max, a)) * 0.5f; };
            float sign;
            if (has_snow) sign = center(snow, axis) - center(glass, axis) >= 0 ? 1.0f : -1.0f;
            else if (has_back) sign = center(back, axis) - center(glass, axis) >= 0 ? 1.0f : -1.0f;
            else sign = 0;
            // TLDCoop_DLC: outward is away from the room's middle (its InteriorTrigger box): the snow/backdrop rule
            // pointed most windows into the room, and a view out is drawn only toward where it faces (from inside not
            // one window showed the world: "0 view(s) drawn this frame of 0 visible", Karol "z żadnego nie widać").
            const float from_mid = center(glass, axis) - axis_of(in.trig_center, axis);
            if (in.have_trigger && std::fabs(from_mid) > 0.5f) {
                const float by_room = from_mid > 0 ? 1.0f : -1.0f;
                if (sign != 0 && sign != by_room) ++flipped;
                sign = by_room;
            }
            if (sign == 0) {
                ++unfaced;
                continue;
            }
            const float plane = center(glass, axis);
            const float target = plane + sign * kPortalDefaultOffset;
            float inner = plane + sign * kPortalClearance;
            if (has_snow) inner = (sign > 0 ? axis_of(snow.max, axis) : axis_of(snow.min, axis)) + sign * kPortalClearance;
            bool has_outer = false;
            float outer = 0;
            if (has_frame) {
                outer = (sign > 0 ? axis_of(frame.max, axis) : axis_of(frame.min, axis)) - sign * kPortalClearance;
                has_outer = true;
            }
            if (has_back) {
                const float cand = (sign > 0 ? axis_of(back.min, axis) : axis_of(back.max, axis)) - sign * kPortalClearance;
                if (!has_outer || (sign > 0 ? cand < outer : cand > outer)) {
                    outer = cand;
                    has_outer = true;
                }
            }
            float chosen = target;
            bool cin = false, cout = false;
            if (has_outer && (sign > 0 ? chosen > outer : chosen < outer)) {
                chosen = outer;
                cin = true;
            }
            if (sign > 0 ? chosen < inner : chosen > inner) {
                chosen = inner;
                cout = true;
            }
            if (cin && cout) chosen = target;  // limits crossed: the fixed distance
            // the in-plane axes: up is the room's up (the room stands upright), right = up x outward
            const int a_up = axis == 1 ? 2 : 1;
            const int a_right = 3 - axis - a_up;
            Opening o;
            const V3 gc = (glass.min + glass.max) * 0.5f;
            o.glass = gc;
            o.portal = with_axis(gc, axis, chosen);
            o.out = unit_axis(axis, sign);
            o.up = unit_axis(a_up);
            o.right = w_norm(w_cross(o.up, o.out));
            o.w = axis_of(gs, a_right);
            o.h = axis_of(gs, a_up);
            o.name = name_of(root) + (groups > 1 ? "#" + std::to_string(gi) : std::string());
            if (std::fabs(o.out.y) > 0.3f || o.w > kWindowLargest || o.h > kWindowLargest || o.w < 0.15f || o.h < 0.15f) {
                ++skipped;  // SI: "not a window"
                continue;
            }
            if (backdrop) o.backdrop = hold(backdrop);
            wr.openings.push_back(o);
        }
    });
    logx::info("interiors: windows: %s: %zu window(s) found in the room (%d not windows, %d with nothing to face them by, "
               "%d split from panes grouped together, %d turned to face away from the room's middle)",
               in.cfg->id, wr.openings.size(), skipped, unfaced, split, flipped);
}
WinRoom& room_windows(Instance& in) {
    if (!in.win) in.win = std::make_shared<WinRoom>();
    if (!in.win->scanned) scan_openings(in, *in.win);
    return *in.win;
}
struct WorldOpening {
    V3 glass, portal, out, right, up;
    float w, h;
};
WorldOpening world_of(const Instance& in, const Opening& o) {
    return {to_world(in, o.glass), to_world(in, o.portal), rotate(in.master_rot, o.out), rotate(in.master_rot, o.right),
            rotate(in.master_rot, o.up), o.w, o.h};
}

// --- the building's windows (SI: ShellWindows.cs; s_ShellWindows from ExteriorFireEffects.cs) -----------------------------

struct ShellWin {  // shell-local
    V3 c;
    float nx, nz, w, h;
};
std::unordered_map<std::string, std::vector<ShellWin>> g_measured_shells;  // by shell name; a failure is kept (empty)

struct Cluster {
    V3 n;
    float plane, ulo, uhi, vlo, vhi;
    int tris = 0;
};
bool cluster_touches(const Cluster& c, V3 n, float plane, float ulo, float uhi, float vlo, float vhi) {
    if (w_dot(c.n, n) < 0.9f) return false;
    if (std::fabs(c.plane - plane) > kShellPlaneTolerance) return false;
    if (ulo > c.uhi + kShellJoinGap || uhi < c.ulo - kShellJoinGap) return false;
    if (vlo > c.vhi + kShellJoinGap || vhi < c.vlo - kShellJoinGap) return false;
    return true;
}
bool glass_material(Il2CppObject* mat) {  // SI: IsShellGlassMaterial
    if (!alive(mat)) return false;
    if (name_of(mat).find("WindowGlassMix") != std::string::npos) return true;
    Il2CppObject* tex = wapi.mat_get_main ? il2::invoke(wapi.mat_get_main, mat) : nullptr;
    return alive(tex) && name_of(tex).find("WindowGlassMix") != std::string::npos;
}
// SI: MeasureShellWindows: the shell's glass submesh, its triangles clustered into panes (shell-local).
std::vector<ShellWin> measure_shell(Il2CppObject* shell_go, const std::string& shell_name) {
    std::vector<ShellWin> out;
    Il2CppObject* stf = go_tf(shell_go);
    M4 w2l;
    if (!stf || !wapi.tf_to_local || !il2::invoke_value(wapi.tf_to_local, stf, w2l)) return out;
    Il2CppObject* best_mesh = nullptr, *best_tf = nullptr;
    int best_sub = -1;
    size_t best_count = 0;
    int unreadable = 0;
    each_in(shell_go, api.t_mesh_renderer, [&](Il2CppObject* r) {
        Il2CppObject* filter = component(go_of(r), api.t_mesh_filter);
        Il2CppObject* mesh = filter ? il2::invoke(api.mesh_filter_mesh, filter) : nullptr;
        if (!alive(mesh)) return;
        Il2CppObject* mats = il2::invoke(api.r_materials, r);
        const int subs = cam_value<int32_t>(wapi.mesh_submeshes, mesh, 0);
        for (int k = 0; k < static_cast<int>(il2::array_length(mats)) && k < subs; ++k) {
            if (!glass_material(il2::array_object_at(mats, k))) continue;
            bool readable = false;
            if (!il2::invoke_value(wapi.mesh_readable, mesh, readable) || !readable) {
                ++unreadable;
                continue;
            }
            int32_t sub = k;
            void* a[] = {&sub};
            Il2CppObject* tris = il2::invoke(wapi.mesh_triangles, mesh, a);
            const size_t n = il2::array_length(tris) / 3;
            if (n > best_count) {
                best_count = n;
                best_mesh = mesh;
                best_tf = tf_of(filter);
                best_sub = k;
            }
        }
    });
    if (!best_mesh) {
        logx::info("interiors: windows: %s: no readable glass in the building's mesh (unreadable: %d); no window views for it",
                   shell_name.c_str(), unreadable);
        return out;
    }
    Il2CppObject* verts = il2::invoke(wapi.mesh_vertices, best_mesh);
    int32_t sub = best_sub;
    void* a[] = {&sub};
    Il2CppObject* tris = il2::invoke(wapi.mesh_triangles, best_mesh, a);
    M4 l2w;
    if (!verts || !tris || !il2::invoke_value(api.tf_to_world, best_tf, l2w)) return out;
    const M4 to_shell = m4_mul(w2l, l2w);
    const size_t nv = il2::array_length(verts);
    const V3* vp = reinterpret_cast<const V3*>(reinterpret_cast<const char*>(verts) + 0x20);
    const int32_t* tp = reinterpret_cast<const int32_t*>(reinterpret_cast<const char*>(tris) + 0x20);
    const size_t nt = il2::array_length(tris);
    std::vector<V3> local(nv);
    V3 middle{};
    for (size_t i = 0; i < nv; ++i) {
        local[i] = m4_point(to_shell, vp[i]);
        middle = middle + local[i];
    }
    if (nv) middle = middle * (1.0f / static_cast<float>(nv));
    std::vector<Cluster> cl;
    for (size_t t = 0; t + 2 < nt; t += 3) {
        const int32_t i0 = tp[t], i1 = tp[t + 1], i2 = tp[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(std::max({i0, i1, i2})) >= nv) continue;
        const V3 p0 = local[i0], p1 = local[i1], p2 = local[i2];
        V3 n = w_cross(p1 - p0, p2 - p0);
        if (w_dot(n, n) < 1e-10f) continue;
        n = w_norm(n);
        if (std::fabs(n.y) > 0.3f) continue;  // a skylight is not a window to look into
        V3 axis = std::fabs(n.x) > std::fabs(n.z) ? V3{n.x > 0 ? 1.0f : -1.0f, 0, 0} : V3{0, 0, n.z > 0 ? 1.0f : -1.0f};
        const V3 level = w_flat(n);
        if (w_dot(level, axis) < kShellSquareCos) axis = level;
        const V3 c = (p0 + p1 + p2) * (1.0f / 3);
        V3 from_mid = c - middle;
        from_mid.y = 0;
        if (w_dot(from_mid, axis) < 0) continue;  // an inner face
        const V3 right = w_cross(kUp, axis);
        const float plane = w_dot(c, axis);
        float ulo = 1e30f, uhi = -1e30f, vlo = 1e30f, vhi = -1e30f;
        for (V3 p : {p0, p1, p2}) {
            const float u = w_dot(p, right);
            ulo = std::min(ulo, u), uhi = std::max(uhi, u), vlo = std::min(vlo, p.y), vhi = std::max(vhi, p.y);
        }
        Cluster* hit = nullptr;
        for (Cluster& k : cl)
            if (!hit && cluster_touches(k, axis, plane, ulo, uhi, vlo, vhi)) hit = &k;
        if (!hit) {
            cl.push_back({axis, plane, ulo, uhi, vlo, vhi, 0});
            hit = &cl.back();
        } else {
            hit->plane = (hit->plane * static_cast<float>(hit->tris) + plane) / static_cast<float>(hit->tris + 1);
            hit->ulo = std::min(hit->ulo, ulo), hit->uhi = std::max(hit->uhi, uhi);
            hit->vlo = std::min(hit->vlo, vlo), hit->vhi = std::max(hit->vhi, vhi);
        }
        ++hit->tris;
    }
    for (size_t i = 0; i < cl.size(); ++i)
        for (size_t j = cl.size(); j-- > i + 1;) {
            Cluster& a2 = cl[i];
            const Cluster& b = cl[j];
            if (!cluster_touches(a2, b.n, b.plane, b.ulo, b.uhi, b.vlo, b.vhi)) continue;
            a2.ulo = std::min(a2.ulo, b.ulo), a2.uhi = std::max(a2.uhi, b.uhi);
            a2.vlo = std::min(a2.vlo, b.vlo), a2.vhi = std::max(a2.vhi, b.vhi);
            a2.tris += b.tris;
            cl.erase(cl.begin() + static_cast<ptrdiff_t>(j));
        }
    for (const Cluster& c : cl) {
        const V3 right = w_cross(kUp, c.n);
        const V3 center = c.n * c.plane + right * ((c.ulo + c.uhi) * 0.5f) + kUp * ((c.vlo + c.vhi) * 0.5f);
        out.push_back({center, c.n.x, c.n.z, c.uhi - c.ulo, c.vhi - c.vlo});
    }
    logx::info("interiors: windows: %s: %zu window(s) measured from the building's mesh (%zu glass triangles, '%s')",
               shell_name.c_str(), out.size(), best_count, name_of(go_of(best_tf)).c_str());
    return out;
}
// SI: TryGetShellWindows: the table first, else measured (once per shell name).
const std::vector<ShellWin>* shell_windows(Instance& in) {
    Il2CppObject* shell = live(in.shell);
    if (!shell) return nullptr;
    const std::string nm = name_of(shell);
    const char* cfg_shell = in.cfg->shell;
    const std::string key = (*cfg_shell && starts_with(nm, cfg_shell)) ? std::string(cfg_shell) : nm;
    auto it = g_measured_shells.find(key);
    if (it == g_measured_shells.end()) {
        std::vector<ShellWin> v;
        bool table = false;
        for (const ShellSpots& s : kShellSpots)
            if (key == s.shell) {
                table = true;
                for (int i = 0; i < s.count; ++i) {
                    const WinSpot& p = kWinSpots[s.first + i];
                    v.push_back({{p.x, p.y, p.z}, p.nx, p.nz, p.w, p.h});
                }
            }
        if (!table) v = measure_shell(shell, key);
        it = g_measured_shells.emplace(key, std::move(v)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}
struct WorldSpot {
    V3 c, out;
    float w, h;
};
std::vector<WorldSpot> world_spots(Instance& in, const std::vector<ShellWin>& spots) {
    std::vector<WorldSpot> out;
    M4 l2w;
    Il2CppObject* stf = go_tf(live(in.shell));
    if (!stf || !il2::invoke_value(api.tf_to_world, stf, l2w)) return out;
    for (const ShellWin& s : spots) out.push_back({m4_point(l2w, s.c), w_norm(m4_dir(l2w, {s.nx, 0, s.nz})), s.w, s.h});
    return out;
}

// --- pairing (SI: WindowPairing.cs) ------------------------------------------------------------------------------------

bool mean_slide(const std::vector<V3>& room, const std::vector<V3>& shell, const std::vector<int>& map, V3& slide) {
    slide = {};
    int n = 0;
    for (size_t i = 0; i < map.size(); ++i)
        if (map[i] >= 0) {
            slide = slide + (room[i] - shell[map[i]]);
            ++n;
        }
    if (!n) return false;
    slide = slide * (1.0f / static_cast<float>(n));
    return true;
}
bool pair_in_place(const std::vector<V3>& room, const std::vector<V3>& room_out, const std::vector<V3>& shell,
                   const std::vector<V3>& shell_out, float within, std::vector<int>& map, V3& slide) {
    map.assign(room.size(), -1);
    std::vector<std::pair<float, int>> pairs;
    const int shells = static_cast<int>(shell.size());
    for (size_t i = 0; i < room.size(); ++i)
        for (int s = 0; s < shells; ++s) {
            if (w_dot(shell_out[s], room_out[i]) < kPatternSameFacing) continue;
            const V3 d = room[i] - shell[s];
            const float dd = w_dot(d, d);
            if (dd <= within * within) pairs.emplace_back(dd, static_cast<int>(i) * shells + s);
        }
    std::sort(pairs.begin(), pairs.end());
    std::vector<char> used(shell.size(), 0);
    for (auto& [d, k] : pairs) {
        const int i = k / shells, s = k % shells;
        if (map[i] >= 0 || used[s]) continue;
        map[i] = s;
        used[s] = 1;
    }
    return mean_slide(room, shell, map, slide);
}
struct PatternSearch {
    const std::vector<V3>* rc, *ro, *sc, *so;
    std::vector<int> cur, best;
    std::vector<char> used;
    int best_pairs = -1, budget = kPatternBudget;
    float best_err = 1e30f, best_up = 1e30f;
    void step(size_t room, int so_far) {
        if (budget-- <= 0) return;
        if (so_far + static_cast<int>(rc->size() - room) < best_pairs) return;
        if (room == rc->size()) {
            if (!so_far) return;
            V3 mean{};
            float mean_up = 0;
            for (size_t i = 0; i < cur.size(); ++i)
                if (cur[i] >= 0) {
                    V3 d = (*rc)[i] - (*sc)[cur[i]];
                    mean_up += d.y;
                    d.y = 0;
                    mean = mean + d;
                }
            mean = mean * (1.0f / static_cast<float>(so_far));
            mean_up /= static_cast<float>(so_far);
            float err = 0, up = 0;
            for (size_t i = 0; i < cur.size(); ++i)
                if (cur[i] >= 0) {
                    V3 d = (*rc)[i] - (*sc)[cur[i]];
                    up += std::fabs(d.y - mean_up);
                    d.y = 0;
                    err += w_len(d - mean);
                }
            err /= static_cast<float>(so_far);
            up /= static_cast<float>(so_far);
            const bool better = so_far > best_pairs || (so_far == best_pairs && err < best_err - kPatternPlanTie) ||
                                (so_far == best_pairs && std::fabs(err - best_err) <= kPatternPlanTie && up < best_up);
            if (better) {
                best_pairs = so_far, best_err = err, best_up = up;
                best = cur;
            }
            return;
        }
        for (size_t s = 0; s < sc->size(); ++s) {
            if (used[s] || w_dot((*so)[s], (*ro)[room]) < kPatternSameFacing) continue;
            used[s] = 1;
            cur[room] = static_cast<int>(s);
            step(room + 1, so_far + 1);
            used[s] = 0;
        }
        cur[room] = -1;
        step(room + 1, so_far);
    }
};
bool fit_by_steps(const std::vector<V3>& rc, const std::vector<V3>& ro, const std::vector<V3>& sc, const std::vector<V3>& so,
                  std::vector<int>& map) {
    V3 slide{};
    bool any = false;
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<char> used(sc.size(), 0);
        std::vector<int> next(rc.size(), -1);
        V3 sum{};
        int pairs = 0;
        for (size_t i = 0; i < rc.size(); ++i) {
            float bd = 1e30f;
            for (size_t s = 0; s < sc.size(); ++s) {
                if (used[s] || w_dot(so[s], ro[i]) < kPatternSameFacing) continue;
                const V3 d = rc[i] - slide - sc[s];
                const float dd = w_dot(d, d);
                if (dd < bd) bd = dd, next[i] = static_cast<int>(s);
            }
            if (next[i] < 0) continue;
            used[next[i]] = 1;
            sum = sum + (rc[i] - sc[next[i]]);
            ++pairs;
        }
        if (!pairs) return any;
        const bool same = any && next == map;
        map = next;
        any = true;
        if (same) break;
        slide = sum * (1.0f / static_cast<float>(pairs));
    }
    return any;
}
bool fit_by_guesses(const std::vector<V3>& rc, const std::vector<V3>& ro, const std::vector<V3>& sc, const std::vector<V3>& so,
                    std::vector<int>& out) {
    const size_t rooms = rc.size(), shells = sc.size();
    std::vector<int> pr, ps;
    for (size_t i = 0; i < rooms; ++i)
        for (size_t s = 0; s < shells; ++s)
            if (w_dot(so[s], ro[i]) >= kPatternSameFacing) pr.push_back(static_cast<int>(i)), ps.push_back(static_cast<int>(s));
    const size_t pc = pr.size();
    std::vector<int> order(pc), map(rooms, -1), next(rooms);
    std::vector<float> dist2(pc);
    std::vector<char> room_used(rooms), used(shells);
    std::set<std::vector<int>> seen_first;
    int best_pairs = -1;
    float best_err = 1e30f, best_up = 1e30f;
    for (size_t g = 0; g < pc; ++g) {
        V3 slide = rc[pr[g]] - sc[ps[g]];
        bool have = false, repeat = false;
        for (int pass = 0; pass < 4; ++pass) {
            for (size_t k = 0; k < pc; ++k) {
                order[k] = static_cast<int>(k);
                const V3 d = rc[pr[k]] - slide - sc[ps[k]];
                dist2[k] = w_dot(d, d);
            }
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return dist2[a] < dist2[b]; });
            std::fill(next.begin(), next.end(), -1);
            std::fill(room_used.begin(), room_used.end(), 0);
            std::fill(used.begin(), used.end(), 0);
            for (size_t k = 0; k < pc; ++k) {
                const int p = order[k], i = pr[p], s = ps[p];
                if (room_used[i] || used[s]) continue;
                next[i] = s, room_used[i] = 1, used[s] = 1;
            }
            if (pass == 0 && !seen_first.insert(next).second) {
                repeat = true;
                break;
            }
            const bool same = have && map == next;
            map = next;
            have = true;
            if (same) break;
            V3 sum{};
            int counted = 0;
            for (size_t i = 0; i < rooms; ++i)
                if (map[i] >= 0) sum = sum + (rc[i] - sc[map[i]]), ++counted;
            if (!counted) break;
            slide = sum * (1.0f / static_cast<float>(counted));
        }
        if (repeat) continue;
        int pairs = 0;
        V3 mean{};
        for (size_t i = 0; i < rooms; ++i)
            if (map[i] >= 0) mean = mean + (rc[i] - sc[map[i]]), ++pairs;
        if (!pairs) continue;
        mean = mean * (1.0f / static_cast<float>(pairs));
        float err = 0, up = 0;
        for (size_t i = 0; i < rooms; ++i) {
            if (map[i] < 0) continue;
            V3 off = rc[i] - sc[map[i]] - mean;
            up += std::fabs(off.y);
            off.y = 0;
            err += w_len(off);
        }
        err /= static_cast<float>(pairs);
        up /= static_cast<float>(pairs);
        const bool better = pairs > best_pairs || (pairs == best_pairs && err < best_err - kPatternPlanTie) ||
                            (pairs == best_pairs && std::fabs(err - best_err) <= kPatternPlanTie && up < best_up);
        if (!better) continue;
        best_pairs = pairs, best_err = err, best_up = up;
        out = map;
    }
    return best_pairs > 0;
}
// SI: FitWindowPattern (without its last-input cache: called once per setup)
bool fit_pattern(const std::vector<V3>& rc, const std::vector<V3>& ro, const std::vector<V3>& sc, const std::vector<V3>& so,
                 std::vector<int>& map, V3& slide) {
    map.assign(rc.size(), -1);
    slide = {};
    if (rc.empty() || sc.empty()) return false;
    if (rc.size() * sc.size() > static_cast<size_t>(kPatternExhaustiveUpTo)) {
        if (!fit_by_guesses(rc, ro, sc, so, map)) return false;
        return mean_slide(rc, sc, map, slide);
    }
    PatternSearch ps{&rc, &ro, &sc, &so, std::vector<int>(rc.size(), -1), std::vector<int>(rc.size(), -1),
                     std::vector<char>(sc.size(), 0)};
    ps.step(0, 0);
    if (ps.budget <= 0 || ps.best_pairs <= 0) {
        if (!fit_by_steps(rc, ro, sc, so, map)) return false;
    } else {
        map = ps.best;
    }
    return mean_slide(rc, sc, map, slide);
}
// SI: ApplyHandPairs: the row's PairedRoomWindows (index = building window, value = room window, -1 none).
bool hand_pairs(const WinRow* row, std::vector<int>& shell_to_room, int room_windows) {
    if (!row || row->scan_altered || !row->pair_count) return false;
    const int n = std::min(row->pair_count, static_cast<int>(shell_to_room.size()));
    for (int s = 0; s < n; ++s) {
        const int room = kWinInts[row->pair_first + s];
        if (room >= room_windows) continue;
        if (room >= 0)
            for (int& o : shell_to_room)
                if (o == room) o = -1;
        shell_to_room[s] = room;
    }
    return true;
}

// --- the room turned to its building (SI: ParkedRoomYaw.cs TurnParkedRoomToShell) --------------------------------------

void turn_room_to_shell(Instance& in) {
    in.yaw = 0;
    Il2CppObject* mt = live(in.master_tf);
    Il2CppObject* shell = live(in.shell);
    if (!mt || !shell) return;
    const WinRow* row = win_row(in.cfg->shell);
    float yaw = kNaN;
    const char* how = "";
    int matched = 0, needed = 0, openings = 0, spots_n = 0;
    float mean_sq = 0;
    if (row && !std::isnan(row->turn)) {
        yaw = row->turn;
        how = "by hand";
    } else {
        const std::vector<ShellWin>* spots = shell_windows(in);
        WinRoom& wr = room_windows(in);
        openings = static_cast<int>(wr.openings.size());
        if (!spots || wr.openings.empty()) return;
        spots_n = static_cast<int>(spots->size());
        const std::vector<WorldSpot> ws = world_spots(in, *spots);
        const V3 pivot = in.master_pos;
        std::vector<V3> rn, rcen;
        for (const Opening& o : wr.openings) {
            const WorldOpening w = world_of(in, o);
            rn.push_back(w_flat(w.out));
            rcen.push_back(w.glass);
        }
        const float transform_yaw = w_delta_angle(w_yaw_of(in.master_rot), w_yaw_of(rotation(go_tf(shell))));
        int best_m = 0;
        bool best_agrees = false;
        float best_cost = 1e30f, best_yaw = 0;
        std::vector<float> tried;
        for (size_t i = 0; i < rn.size(); ++i)
            for (const WorldSpot& s : ws) {
                const float y = w_signed_angle(rn[i], w_flat(s.out));
                bool seen = false;
                for (float t : tried) seen = seen || std::fabs(w_delta_angle(t, y)) < 0.25f;
                if (seen) continue;
                tried.push_back(y);
                // SI: ScoreParkedYaw
                const Quat turn = axis_angle(y, kUp);
                std::vector<char> taken(ws.size(), 0);
                int m = 0;
                float sum_sq = 0, sum_angle = 0;
                for (size_t k = 0; k < rn.size(); ++k) {
                    const V3 c = pivot + rotate(turn, rcen[k] - pivot);
                    const V3 n = rotate(turn, rn[k]);
                    int best = -1;
                    float bsq = kYawMatchDistance * kYawMatchDistance;
                    for (size_t s2 = 0; s2 < ws.size(); ++s2) {
                        if (taken[s2] || w_dot(n, w_flat(ws[s2].out)) < kYawMatchDot) continue;
                        const float dx = c.x - ws[s2].c.x, dz = c.z - ws[s2].c.z;
                        if (dx * dx + dz * dz < bsq) bsq = dx * dx + dz * dz, best = static_cast<int>(s2);
                    }
                    if (best < 0) continue;
                    taken[best] = 1;
                    ++m;
                    sum_sq += bsq;
                    sum_angle += w_signed_angle(n, w_flat(ws[best].out));
                }
                if (!m) continue;
                const float cost = sum_sq / static_cast<float>(m), refine = sum_angle / static_cast<float>(m);
                const bool agrees = std::fabs(w_delta_angle(y, transform_yaw)) <= kYawAgreement;
                bool better = m > best_m;
                if (!better && m == best_m) better = agrees != best_agrees ? agrees : cost < best_cost;
                if (better) best_m = m, best_agrees = agrees, best_cost = cost, best_yaw = y + refine;
            }
        needed = std::max(2, std::min(openings, spots_n) - 1);
        matched = best_m;
        mean_sq = best_cost;
        if (best_m < needed) {
            logx::info("interiors: windows: %s: room NOT turned to its building: the best angle, %.1f deg, lined up %d window(s) "
                       "(needed %d; the room has %d, the building %d)",
                       in.cfg->id, best_yaw, best_m, needed, openings, spots_n);
            return;
        }
        yaw = best_yaw;
        how = "by its windows";
    }
    if (std::isnan(yaw) || std::fabs(yaw) < 1e-4f) {
        if (*how) logx::info("interiors: windows: %s: room stays as placed (turn 0, %s)", in.cfg->id, how);
        return;
    }
    const Quat r = mul(axis_angle(yaw, kUp), in.master_rot);
    Quat rr = r;
    void* ra[] = {&rr};
    il2::invoke(api.tf_set_rotation, mt, ra);
    in.master_rot = rotation(mt);
    in.yaw = yaw;
    if (matched)
        logx::info("interiors: windows: %s: room turned %.1f deg to its building %s, %d window(s) lined up (room %d, building %d), "
                   "on average %.2f m from their twins",
                   in.cfg->id, yaw, how, matched, openings, spots_n, std::sqrt(mean_sq));
    else
        logx::info("interiors: windows: %s: room turned %.1f deg to its building %s", in.cfg->id, yaw, how);
}

// --- our objects (cameras, targets, quads) -------------------------------------------------------------------------------

void keep_on_load(Il2CppObject* o) {
    if (!wapi.keep_on_load || !alive(o)) return;
    void* a[] = {o};
    il2::invoke(wapi.keep_on_load, nullptr, a);
}
void set_layer(Il2CppObject* go, int layer) {
    if (!alive(go)) return;
    int32_t l = layer;
    void* a[] = {&l};
    il2::invoke(api.go_set_layer, go, a);
}
int layer_of(Il2CppObject* go) {
    int32_t l = 0;
    return alive(go) && il2::invoke_value(api.go_layer, go, l) ? l : -1;
}
uintptr_t g_unlit_shader = 0;
bool g_unlit_failed = false;
Il2CppObject* unlit_shader() {
    if (Il2CppObject* s = live(g_unlit_shader)) return s;
    if (g_unlit_failed || !wapi.shader_find) return nullptr;
    void* a[] = {str("Unlit/Texture")};
    Il2CppObject* s = il2::invoke(wapi.shader_find, nullptr, a);
    if (!alive(s)) {
        g_unlit_failed = true;
        logx::warn("interiors: windows: the shader Unlit/Texture isn't in this build; no window views");
        return nullptr;
    }
    g_unlit_shader = hold(s);
    return s;
}
Il2CppObject* new_material(int queue) {
    Il2CppObject* sh = unlit_shader();
    if (!sh || !wapi.mat_class || !wapi.mat_ctor) return nullptr;
    Il2CppObject* m = il2::il2cpp_object_new(wapi.mat_class);
    void* a[] = {sh};
    il2::invoke(wapi.mat_ctor, m, a);
    if (!alive(m)) return nullptr;
    cam_set<int32_t>(wapi.mat_set_queue, m, queue);
    return m;
}
Il2CppObject* new_target(int w, int h) {
    if (!wapi.rt_class || !wapi.rt_ctor) return nullptr;
    Il2CppObject* rt = il2::il2cpp_object_new(wapi.rt_class);
    int32_t ww = w, hh = h, depth = 24, fmt = wapi.fmt_hdr;
    void* a[] = {&ww, &hh, &depth, &fmt};
    il2::invoke(wapi.rt_ctor, rt, a);
    bool made = false;
    if (!alive(rt) || !il2::invoke_value(wapi.rt_create, rt, made) || !made) {
        if (alive(rt)) destroy(rt);
        return nullptr;
    }
    cam_set<int32_t>(wapi.hide_flags, rt, 32);  // HideFlags.DontUnloadUnusedAsset
    return rt;
}
void free_target(uintptr_t& h) {
    if (Il2CppObject* rt = live(h)) {
        if (wapi.rt_release) il2::invoke(wapi.rt_release, rt);
        destroy(rt);
    }
    drop(h);
}
Il2CppObject* new_camera(const char* name, Il2CppObject* main, int depth) {
    Il2CppObject* go = new_game_object(name);
    if (!go) return nullptr;
    keep_on_load(go);
    Il2CppObject* cam = add_component(go, api.t_camera);
    if (!alive(cam)) {
        destroy(go);
        return nullptr;
    }
    set_behaviour(cam, false);
    if (alive(main) && wapi.cam_copy) {
        void* a[] = {main};
        il2::invoke(wapi.cam_copy, cam, a);
    }
    set_behaviour(cam, false);
    void* nt[] = {nullptr};
    il2::invoke(wapi.cam_set_target, cam, nt);
    if (wapi.cam_reset_w2c) il2::invoke(wapi.cam_reset_w2c, cam);
    if (wapi.cam_reset_proj) il2::invoke(wapi.cam_reset_proj, cam);
    if (wapi.cam_reset_cull) il2::invoke(wapi.cam_reset_cull, cam);
    cam_set<float>(wapi.cam_set_depth, cam, static_cast<float>(depth));
    cam_set<bool>(wapi.cam_set_occlusion, cam, false);  // baked for the player's viewpoint, wrong from here
    cam_set<bool>(wapi.cam_set_hdr, cam, true);         // LDR here double-tonemaps (SI)
    return cam;
}
// A quad (Unity's primitive: 1 x 1 in its XY plane, seen from its -Z side), no collider, on the quads' layer.
Il2CppObject* new_quad(const char* name) {
    if (!wapi.create_primitive) return nullptr;
    int32_t kind = wapi.prim_quad;
    void* a[] = {&kind};
    Il2CppObject* go = il2::invoke(wapi.create_primitive, nullptr, a);
    if (!alive(go)) return nullptr;
    if (Il2CppObject* col = component(go, api.t_collider)) destroy_now(col);
    void* n[] = {str(name)};
    il2::invoke(api.set_name, go, n);
    keep_on_load(go);
    set_layer(go, kQuadLayer);
    Il2CppObject* r = component(go, api.t_renderer);
    cam_set<int32_t>(wapi.r_set_shadows, r, 0);
    cam_set<bool>(wapi.r_set_receive, r, false);
    set_renderer(r, false);
    return go;
}
// The quad over a sub-rectangle (corner pa, along right and up) facing `away` from the viewer.
void place_quad(Il2CppObject* quad, V3 pa, V3 right, V3 up, V3 away, float u0, float u1, float v0, float v1) {
    Il2CppObject* t = go_tf(quad);
    if (!t) return;
    set_position(t, pa + right * ((u0 + u1) * 0.5f) + up * ((v0 + v1) * 0.5f));
    Quat q = w_look(away, up);
    void* ra[] = {&q};
    il2::invoke(api.tf_set_rotation, t, ra);
    V3 s{u1 - u0, v1 - v0, 1};
    void* sa[] = {&s};
    il2::invoke(api.tf_set_local_scale, t, sa);
}
// SI: one slot: one camera, its own full-resolution target, the material that shows it
struct Slot {
    uintptr_t cam = 0, rt = 0, mat = 0;
    int w = 0, h = 0;
};
// TLDCoop_DLC: the cameras that draw the outdoors from inside a room (window views out, door views out): lit as the
// outdoors while they draw, not as the room (see view_sky_on).
std::unordered_set<Il2CppObject*> g_out_cams;
void free_slot(Slot& s) {
    if (Il2CppObject* c = live(s.cam)) {
        g_out_cams.erase(c);
        set_behaviour(c, false);
        void* nt[] = {nullptr};
        il2::invoke(wapi.cam_set_target, c, nt);
        destroy(go_of(c));
    }
    drop(s.cam);
    free_target(s.rt);
    if (Il2CppObject* m = live(s.mat)) destroy(m);
    drop(s.mat);
    s.w = s.h = 0;
}
// SI: EnsureWindowPortalSlot
bool ensure_slot(Slot& s, const char* name, int depth, int queue, int w, int h, int mask) {
    Il2CppObject* rt = live(s.rt);
    Il2CppObject* cam = live(s.cam);
    if (rt && (s.w != w || s.h != h)) {
        if (cam) {
            void* nt[] = {nullptr};
            il2::invoke(wapi.cam_set_target, cam, nt);
        }
        free_target(s.rt);
        rt = nullptr;
    }
    if (!rt) {
        rt = new_target(w, h);
        if (!rt) return false;
        s.rt = hold(rt);
        s.w = w, s.h = h;
    }
    Il2CppObject* mat = live(s.mat);
    if (!mat) {
        mat = new_material(queue);
        if (!mat) return false;
        s.mat = hold(mat);
    }
    if (wapi.mat_get_main && il2::invoke(wapi.mat_get_main, mat) != rt) {
        void* a[] = {rt};
        il2::invoke(wapi.mat_set_main, mat, a);
    }
    if (!cam) {
        cam = new_camera(name, live(g_main.cam), depth);
        if (!cam) return false;
        s.cam = hold(cam);
    }
    cam_set<int32_t>(wapi.cam_set_mask, cam, mask);
    void* ta[] = {rt};
    il2::invoke(wapi.cam_set_target, cam, ta);
    return true;
}
// The camera at `pos` looking along `fwd` (up `up`), its off-axis frustum through the sub-rectangle of a pane seen from
// `eye`; FOV and aspect first (they reset the projection), the matrix last (SI: DrawOneWindowPortal).
bool aim_camera(Il2CppObject* cam, V3 pos, V3 fwd, V3 up, V3 right, V3 eye, V3 qa, V3 qb, V3 qc, float zfar) {
    const V3 va = qa - eye, vb = qb - eye, vc = qc - eye;
    const float zn = w_dot(va, fwd);
    if (zn <= kMinEyeDistance) return false;
    const float l = w_dot(right, va), r = w_dot(right, vb), b = w_dot(up, va), t = w_dot(up, vc);
    if (r - l < 1e-4f || t - b < 1e-4f) return false;
    Il2CppObject* tf = tf_of(cam);
    set_position(tf, pos);
    Quat q = w_look(fwd, up);
    void* ra[] = {&q};
    il2::invoke(api.tf_set_rotation, tf, ra);
    if (wapi.cam_reset_w2c) il2::invoke(wapi.cam_reset_w2c, cam);
    cam_set<float>(wapi.cam_set_near, cam, zn);
    cam_set<float>(wapi.cam_set_far, cam, zfar);
    cam_set<float>(wapi.cam_set_fov, cam, g_main.fov);  // the player's: LOD as the game picks it outdoors (SI)
    cam_set<float>(wapi.cam_set_aspect, cam, (r - l) / (t - b));
    cam_set<M4>(wapi.cam_set_proj, cam, m4_frustum(l, r, b, t, zn, zfar));
    return true;
}

// --- the cull eye (SI: ParkedCullEye.cs, Patches/ParkedCullEyePatches.cs) ------------------------------------------------

struct CullEye {
    bool in_force = false;
    uintptr_t cam = 0;
    struct Saved {
        uintptr_t roi = 0, tf = 0, cam = 0;
    };
    std::unordered_map<int, Saved> saved;  // by the RenderObjectInstance's instance id
    // TLDCoop_DLC: the rooms' own (furniture, props): they keep the player's camera, and are culled by its direction
    // (FrustrmType DotProduct) instead of the frustum shared by all, which is the eye's 80 m below (they popped in and
    // out with the view); their type put back at the end
    struct RoomRoi {
        uintptr_t roi = 0;
        int32_t type = 1;
    };
    std::unordered_map<int, RoomRoi> room;
} g_eye;

void sync_cull_eye() {
    Il2CppObject* eye = live(g_eye.cam);
    Il2CppObject* main = live(g_main.cam);
    if (!eye || !main) return;
    Il2CppObject* mt = tf_of(main);
    Il2CppObject* et = tf_of(eye);
    set_position(et, position(mt) - kParkOffset);
    Quat q = rotation(mt);
    void* ra[] = {&q};
    il2::invoke(api.tf_set_rotation, et, ra);
    cam_set<float>(wapi.cam_set_fov, eye, cam_value<float>(wapi.cam_get_fov, main, 60));
    cam_set<float>(wapi.cam_set_aspect, eye, cam_value<float>(wapi.cam_get_aspect, main, 1.78f));
    cam_set<float>(wapi.cam_set_far, eye, cam_value<float>(wapi.cam_get_far, main, 1000));
    cam_set<float>(wapi.cam_set_near, eye, cam_value<float>(wapi.cam_get_near, main, 0.1f));
    if (behaviour_on(eye)) set_behaviour(eye, false);
}
void release_cull_eye(const char* why) {
    if (!g_eye.in_force && !g_eye.cam && g_eye.saved.empty()) return;
    g_eye.in_force = false;
    Il2CppObject* main = main_camera();
    int restored = 0;
    for (auto& [id, s] : g_eye.saved) {
        Il2CppObject* roi = live(s.roi);
        if (roi) {
            Il2CppObject* cam = live(s.cam);
            if (!cam) cam = alive(main) ? main : nullptr;
            Il2CppObject* tf = live(s.tf);
            if (!tf && cam) tf = tf_of(cam);
            put_ref(roi, wapi.roi_cam_tf, tf);
            put_ref(roi, wapi.roi_cam, cam);
            ++restored;
        }
        drop(s.roi);
        drop(s.tf);
        drop(s.cam);
    }
    g_eye.saved.clear();
    for (auto& [id, r] : g_eye.room) {
        if (Il2CppObject* roi = live(r.roi); roi && wapi.roi_frustum_type >= 0) put<int32_t>(roi, wapi.roi_frustum_type, r.type);
        drop(r.roi);
    }
    g_eye.room.clear();
    if (Il2CppObject* e = live(g_eye.cam)) destroy(go_of(e));
    drop(g_eye.cam);
    logx::info("interiors: windows: stand-in eye removed (%s); %d tree/rock group(s) culled from the player's camera again", why,
               restored);
}
void acquire_cull_eye() {
    if (!wapi.cull_ok) return;
    Il2CppObject* main = live(g_main.cam);
    if (!main) return;
    if (!live(g_eye.cam)) {
        drop(g_eye.cam);
        Il2CppObject* eye = new_camera("TLDCoop_SI_ParkedCullEye", main, -100);
        if (!eye) return;
        g_eye.cam = hold(eye);
        logx::info("interiors: windows: stand-in eye set up at ground level, %.0f m below the room: the trees and rocks in the "
                   "window views are culled from there",
                   kParkOffset.y);
    }
    sync_cull_eye();
    g_eye.in_force = true;
    Il2CppObject* eye = live(g_eye.cam);
    Il2CppObject* list = static_object(wapi.roi_list);
    if (!eye || !list) return;
    Il2CppObject* eye_tf = tf_of(eye);
    each_listed(list, [&](Il2CppObject* roi, int) {
        if (!alive(roi)) return;
        if (get<Il2CppObject*>(roi, wapi.roi_cam) == eye) return;
        const int id = instance_id(roi);
        if (wapi.roi_frustum_type >= 0 && (g_eye.room.count(id) || under_any_room(roi))) {
            if (!g_eye.room.count(id)) {
                CullEye::RoomRoi r;
                r.roi = hold(roi);
                r.type = get<int32_t>(roi, wapi.roi_frustum_type);
                g_eye.room.emplace(id, r);
                static int told = 0;
                if (told++ < 5) logx::info("interiors: windows: a room's own instanced objects kept culled from the player's eye");
            }
            put<int32_t>(roi, wapi.roi_frustum_type, 0);  // DotProduct: by its own camera's (the player's) direction
            return;
        }
        if (!g_eye.saved.count(id)) {
            CullEye::Saved s;
            s.roi = hold(roi);
            s.tf = hold(get<Il2CppObject*>(roi, wapi.roi_cam_tf));
            s.cam = hold(get<Il2CppObject*>(roi, wapi.roi_cam));
            g_eye.saved.emplace(id, s);
        }
        put_ref(roi, wapi.roi_cam_tf, eye_tf);
        put_ref(roi, wapi.roi_cam, eye);
    });
}
Il2CppObject* cull_eye_for(Il2CppObject* c) {
    if (!g_eye.in_force || !c) return nullptr;
    Il2CppObject* eye = live(g_eye.cam);
    if (!eye || c != main_camera()) return nullptr;
    sync_cull_eye();
    return eye;
}

using FrustumAFn = void (*)(Il2CppObject* c, const MethodInfo* m);
using FrustumBFn = void (*)(Il2CppObject* c, void* out_ref, float expand, const MethodInfo* m);
FrustumAFn g_frustum_planes1 = nullptr;
FrustumBFn g_frustum_planes3 = nullptr, g_frustum_corners = nullptr;
void frustum_planes1_detour(Il2CppObject* c, const MethodInfo* m) {
    if (Il2CppObject* e = cull_eye_for(c)) c = e;
    g_frustum_planes1(c, m);
}
void frustum_planes3_detour(Il2CppObject* c, void* out_ref, float expand, const MethodInfo* m) {
    if (Il2CppObject* e = cull_eye_for(c)) c = e;
    g_frustum_planes3(c, out_ref, expand, m);
}
void frustum_corners_detour(Il2CppObject* c, void* out_ref, float expand, const MethodInfo* m) {
    if (Il2CppObject* e = cull_eye_for(c)) c = e;
    g_frustum_corners(c, out_ref, expand, m);
}
struct WVec3 {
    float x, y, z;
};
using OnscreenFn = bool (*)(WVec3* pos, float adjust, const MethodInfo* m);
OnscreenFn g_onscreen = nullptr;
bool onscreen_detour(WVec3* pos, float adjust, const MethodInfo* m) {
    if (g_eye.in_force && pos && !(g_inside && in_parked_room(*g_inside, V3{pos->x, pos->y, pos->z}))) {
        // SI: ParkedCullEyePositionOnScreenPatch: asked of the eye at the building (TLDCoop_DLC: not for a spot in the
        // room the player is in: that one is on screen as it is)
        WVec3 p = *pos;
        p.x += kParkOffset.x, p.y += kParkOffset.y, p.z += kParkOffset.z;
        return g_onscreen(&p, adjust, m);
    }
    return g_onscreen(pos, adjust, m);
}

// --- the snow box carried down to the views (SI: ParkedSnowBoxPatches.cs) -----------------------------------------------

float g_snow_push = 0;  // SI: NearestWindowGlassAlongGaze, this frame
using SnowJobsFn = void (*)(Il2CppObject* self, const MethodInfo* m);
SnowJobsFn g_falling_jobs = nullptr, g_blowing_jobs = nullptr;
void shift_snow_box(Il2CppObject* mgr) {
    // TLDCoop_DLC: also in a room with no live window view (a basement): the snow fell around the player in the room
    if ((!g_eye.in_force && !(g_inside && engaged())) || !mgr || !wapi.snow_ok) return;
    Il2CppObject* main = main_camera();
    if (!alive(main)) return;
    const V3 mp = position(tf_of(main));
    char* base = reinterpret_cast<char*>(mgr);
    V3* cam_pos = reinterpret_cast<V3*>(base + wapi.snow_cam_pos);
    const V3 d0 = *cam_pos - mp;
    if (w_dot(d0, d0) > 1.0f) return;  // not this frame's camera (already shifted, or something else)
    const V3 d = kParkOffset * -1.0f;
    *cam_pos = *cam_pos + d;
    M4 mat;
    std::memcpy(&mat, base + wapi.snow_cam_mat, sizeof(M4));
    mat.at(0, 3) += d.x, mat.at(1, 3) += d.y, mat.at(2, 3) += d.z;
    if (g_snow_push > 0) {
        const V3 axis = w_norm({mat.at(0, 2), mat.at(1, 2), mat.at(2, 2)});
        mat.at(0, 3) += axis.x * g_snow_push, mat.at(1, 3) += axis.y * g_snow_push, mat.at(2, 3) += axis.z * g_snow_push;
    }
    M4 inv;
    if (!m4_inverse(mat, inv)) return;
    std::memcpy(base + wapi.snow_cam_mat, &mat, sizeof(M4));
    std::memcpy(base + wapi.snow_cam_inv, &inv, sizeof(M4));
}
void falling_jobs_detour(Il2CppObject* self, const MethodInfo* m) {
    shift_snow_box(self);
    g_falling_jobs(self, m);
}
void blowing_jobs_detour(Il2CppObject* self, const MethodInfo* m) {
    shift_snow_box(self);
    g_blowing_jobs(self, m);
}

// --- the views' sheets unfogged in the player's camera (SI: ParkedWeather.cs HookParkedSheetFog) -------------------------

bool g_sheet_fog_wanted = false, g_sheet_fog_off = false, g_sheet_fog_was = false;
using CamFireFn = void (*)(Il2CppObject* cam, const MethodInfo* m);
CamFireFn g_fire_pre_cull = nullptr, g_fire_post_render = nullptr;

// TLDCoop_DLC: the indoor look. A room stands in the open and is lit like outdoors; the game's own interiors are much
// darker. TLD sets the sky's light once a frame (TodAmbientLight -> Utils.SetAmbientLightScaled(colour, probe scale):
// RenderSettings.ambientLight and the ambient probe): while the player is in a room it goes in at [interiors]
// indoor_light %. (Per camera before, which flickered when another camera drew in between.)
struct IlColor {
    float r, g, b, a;
};
using AmbientFn = void (*)(IlColor* colour, float probe, const MethodInfo* m);
AmbientFn g_set_ambient = nullptr;
int g_indoor_logged = 0;

// TLDCoop_DLC: [interiors] indoor_light=0 (default): the room as the game lights its interior. Its own lighting objects
// (InteriorLightingManager_Prefab, Daytime/Daylight/Midday/AmbientLight/Nighttime: kept off for the outdoor look) are
// switched on while the player is in it, and the sky's light is the interior's (its InteriorLightingManager.Update ->
// TodAmbientLight: the game's indoor day/night colours): the outdoors' own calls are left out meanwhile. A room with
// no lighting manager of its own: the sky's light at 45 %.
Instance* g_game_light = nullptr;
bool g_game_light_ilm = false;
std::vector<uintptr_t> g_room_lights;  // the room's own lights (see view_sky_on)
void view_sky_off();
void forget_room_lights();
int g_ilm_depth = 0;
void game_light_switch(Instance& in, bool on, int& n, bool& ilm) {
    Il2CppObject* m = master_of(in);
    if (!m) return;
    each_in(m, api.t_transform, [&](Il2CppObject* t) {
        if (!listed(kDarkOnly, std::size(kDarkOnly), name_of(t))) return;
        set_active(go_of(t), on);
        ++n;
        if (on) each_in(go_of(t), api.t_light, [&](Il2CppObject* l) { g_room_lights.push_back(hold(l)); });
    });
    if (on) each_in(m, api.t_ilm, [&](Il2CppObject*) { ilm = true; });
}
void tick_game_light() {
    Instance* want = g_indoor_light == 0 && g_inside && engaged() && in_room(g_inside) ? g_inside : nullptr;
    if (want == g_game_light) return;
    int n = 0;
    bool ilm = false;
    view_sky_off();
    forget_room_lights();
    if (g_game_light) game_light_switch(*g_game_light, false, n, ilm);
    g_game_light = want;
    g_game_light_ilm = false;
    if (!want) return;
    n = 0;
    game_light_switch(*want, true, n, g_game_light_ilm);
    logx::info("interiors: %s: lit as the game lights it (%d lighting object(s) on, %s)", want->cfg->id, n,
               g_game_light_ilm ? "its lighting manager sets the sky's light" : "no lighting manager: the sky's light at 45%");
}
using IlmUpdateFn = void (*)(Il2CppObject* self, const MethodInfo* m);
IlmUpdateFn g_ilm_update = nullptr;
void ilm_update_detour(Il2CppObject* self, const MethodInfo* m) {
    ++g_ilm_depth;
    g_ilm_update(self, m);
    --g_ilm_depth;
}

// TLDCoop_DLC: the sky's light as the outdoors set it last (the outdoors' TodAmbientLight call) and as it was last put in
// (the room's, while the player is in a room). Karol: "drzewa są nienaturalnie jasne w pewnym momencie": at dusk, looking
// out of a room's open door, the trees outside were drawn bright cream and the ground black: the views out were lit by
// the room (its indoor sky's light and its Daytime/Daylight/... lights), not by the evening outside.
struct SkyLight {
    bool have = false;
    IlColor colour{};
    float probe = 1;
};
SkyLight g_sky_out, g_sky_put;
const MethodInfo* g_sky_method = nullptr;
void put_sky(IlColor* colour, float probe, const MethodInfo* m) {
    g_sky_method = m;
    if (colour) g_sky_put.have = true, g_sky_put.colour = *colour, g_sky_put.probe = probe;
    g_set_ambient(colour, probe, m);
}
double g_sky_logged_ms = 0;
void set_ambient_detour(IlColor* colour, float probe, const MethodInfo* m) {
    tick_game_light();
    // TLDCoop_DLC: the sky's light once a minute (Karol's trees drawn flat cream with black ground at dusk: what the
    // ambient was then)
    if (colour && now_ms() - g_sky_logged_ms > 60000) {
        g_sky_logged_ms = now_ms();
        logx::info("interiors: sky's light %.2f %.2f %.2f (probe %.2f) from %s; player %s", colour->r, colour->g,
                   colour->b, probe, g_ilm_depth > 0 ? "a room's lighting manager" : "the outdoors",
                   g_inside ? "in a room" : "outside");
    }
    if (colour && g_ilm_depth == 0) g_sky_out.have = true, g_sky_out.colour = *colour, g_sky_out.probe = probe;
    if (g_game_light && g_game_light == g_inside && g_game_light_ilm) {
        if (g_ilm_depth > 0) put_sky(colour, probe, m);  // the room's own (the game's indoor light)
        return;                                         // (the outdoors' meanwhile left out)
    }
    const int pct = g_indoor_light == 0 ? 45 : g_indoor_light;
    if (colour && g_inside && engaged() && pct < 100) {
        const float k = static_cast<float>(pct) / 100.0f;
        IlColor dim{colour->r * k, colour->g * k, colour->b * k, colour->a};
        if (g_indoor_logged++ == 0)
            logx::info("interiors: in a room: the sky's light at %d%% (ambient %.2f %.2f %.2f)", pct, colour->r,
                       colour->g, colour->b);
        put_sky(&dim, probe, m);
        return;
    }
    put_sky(colour, probe, m);
}

// The room's own lights (under its Daytime/Daylight/Midday/AmbientLight/Nighttime objects), switched off while a view
// out draws and on again after it.
std::vector<Il2CppObject*> g_lights_off;
bool g_view_sky = false;
int g_view_sky_logged = 0;
void forget_room_lights() {
    for (uintptr_t h : g_room_lights) drop(h);
    g_room_lights.clear();
}
void view_sky_on() {
    if (g_view_sky || !g_sky_out.have || !g_sky_put.have || !g_inside || !engaged()) return;
    g_view_sky = true;
    IlColor c = g_sky_out.colour;
    g_set_ambient(&c, g_sky_out.probe, g_sky_method);
    g_lights_off.clear();
    for (uintptr_t h : g_room_lights)
        if (Il2CppObject* l = live(h); l && behaviour_on(l)) {
            set_behaviour(l, false);
            g_lights_off.push_back(l);
        }
    if (g_view_sky_logged++ == 0)
        logx::info("interiors: a view out of the room drawn with the outdoors' light (ambient %.2f %.2f %.2f, the room's "
                   "%.2f %.2f %.2f; %zu room light(s) off for it)",
                   c.r, c.g, c.b, g_sky_put.colour.r, g_sky_put.colour.g, g_sky_put.colour.b, g_lights_off.size());
}
void view_sky_off() {
    if (!g_view_sky) return;
    g_view_sky = false;
    IlColor c = g_sky_put.colour;
    g_set_ambient(&c, g_sky_put.probe, g_sky_method);
    for (Il2CppObject* l : g_lights_off) set_behaviour(l, true);
    g_lights_off.clear();
}

void fire_pre_cull_detour(Il2CppObject* cam, const MethodInfo* m) {
    if (cam && !g_out_cams.empty() && g_out_cams.count(cam)) view_sky_on();
    else view_sky_off();  // (another camera: the room's own light)
    if (g_sheet_fog_wanted && !g_sheet_fog_off && cam && cam == g_main.raw && wapi.fog_ok) {
        bool was = false;
        if (il2::invoke_value(wapi.fog_get, nullptr, was)) {
            g_sheet_fog_was = was;
            g_sheet_fog_off = true;
            cam_set<bool>(wapi.fog_set, nullptr, false);
        }
    }
    g_fire_pre_cull(cam, m);
}
void restore_sheet_fog() {
    if (!g_sheet_fog_off) return;
    g_sheet_fog_off = false;
    cam_set<bool>(wapi.fog_set, nullptr, g_sheet_fog_was);
}
void fire_post_render_detour(Il2CppObject* cam, const MethodInfo* m) {
    g_fire_post_render(cam, m);
    if (g_view_sky && cam && g_out_cams.count(cam)) view_sky_off();
    if (g_sheet_fog_off && cam && cam == g_main.raw) restore_sheet_fog();
}

// --- the views' fog and the player's shared buffers (SI: WindowViewFog.cs) ------------------------------------------------
// The player's camera draws TLD's fog as a post-process (PostProcessLayer's deferred fog), which our cameras don't have:
// each window camera gets a command buffer (BeforeImageEffectsOpaque) that copies its picture and draws the same fog
// shader over it with this frame's RenderSettings fog (SI: UseWindowViewFogPass, the sky left out: pass 1). And the
// player's camera's own command buffers that draw the aurora, blowing snow, snow patches, decals, distance field
// shadows and AO are added to the window cameras too (SI: SharePlayerBuffersWith).

struct FogPassApi {
    bool ok = false, share_ok = false;
    Il2CppClass* cb_class = nullptr;
    const MethodInfo* cb_ctor = nullptr, *cb_get_temp = nullptr, *cb_blit = nullptr, *cb_set_global = nullptr,
                      *cb_set_target = nullptr, *cb_draw_mesh = nullptr, *cb_release_temp = nullptr, *cb_name = nullptr;
    const MethodInfo* rti_builtin = nullptr, *rti_id = nullptr;
    const MethodInfo* cam_add = nullptr, *cam_remove = nullptr, *cam_get = nullptr;
    const MethodInfo* fog_color = nullptr, *fog_density = nullptr, *fog_start = nullptr, *fog_end = nullptr;
    const MethodInfo* mat_color = nullptr, *mat_vector = nullptr, *mat_float = nullptr;
    const MethodInfo* triangle = nullptr;
    ptrdiff_t pl_resources = -1, res_shaders = -1, shaders_fog = -1;
    int32_t id_copy = -1, id_main = -1, id_fog_color = -1, id_fog_params = -1, id_viewport = -1;
    int32_t ev_before_opaque_fx = 12, builtin_target = 2, filter_bilinear = 1, fmt_default_hdr = 9;
    int32_t ev_shared[5] = {-1, -1, -1, -1, -1};
    uintptr_t material = 0, mesh = 0;
    bool failed = false;
    uint32_t frame_set = 0;
} fpa;

struct Rti {  // UnityEngine.Rendering.RenderTargetIdentifier (40 bytes)
    uint8_t b[40] = {};
};
bool make_rti(const MethodInfo* m, int32_t v, Rti& out) {
    void* a[] = {&v};
    Il2CppObject* boxed = m ? il2::invoke(m, nullptr, a) : nullptr;
    if (!boxed) return false;
    std::memcpy(out.b, il2::il2cpp_object_unbox(boxed), sizeof(out.b));
    return true;
}
struct Rgba4 {
    float r, g, b, a;
};
bool ensure_fog_material() {
    if (live(fpa.material) && live(fpa.mesh)) return true;
    if (fpa.failed || !fpa.ok) return false;
    Il2CppObject* main = live(g_main.cam);
    Il2CppObject* layer = main && api.t_post_layer ? component(go_of(main), api.t_post_layer) : nullptr;
    Il2CppObject* res = layer ? get<Il2CppObject*>(layer, fpa.pl_resources) : nullptr;
    Il2CppObject* shaders = alive(res) ? get<Il2CppObject*>(res, fpa.res_shaders) : nullptr;
    Il2CppObject* shader = shaders ? get<Il2CppObject*>(shaders, fpa.shaders_fog) : nullptr;
    Il2CppObject* tri = il2::invoke(fpa.triangle, nullptr);
    if (!alive(shader) || !alive(tri)) {
        fpa.failed = true;
        logx::warn("interiors: windows: the player's fog shader wasn't found; the window views have no fog");
        return false;
    }
    Il2CppObject* m = il2::il2cpp_object_new(wapi.mat_class);
    void* a[] = {shader};
    il2::invoke(wapi.mat_ctor, m, a);
    if (!alive(m)) {
        fpa.failed = true;
        return false;
    }
    fpa.material = hold(m);
    fpa.mesh = hold(tri);
    logx::info("interiors: windows: the views get the player's fog (shader '%s')", name_of(shader).c_str());
    return true;
}
struct SlotFx {
    uintptr_t buffer = 0;  // the fog pass, on this slot's camera
    int cam_id = 0;        // the camera it is on
    int share_cam_id = 0;  // the camera the shared buffers are on
    std::vector<uintptr_t> shared;  // the player's buffers added to it
    std::vector<int32_t> shared_events;
    int shared_version = -1;
};
std::vector<SlotFx> g_slot_fx;  // by inside-out slot (from kDoorFxBase: the door cameras')
// The entries [first, last) let go (their cameras are being destroyed with them): the window views' by default.
void free_slot_fx(size_t first = 0, size_t last = kDoorFxBase) {
    for (size_t i = first; i < last && i < g_slot_fx.size(); ++i) {
        SlotFx& f = g_slot_fx[i];
        drop(f.buffer);
        for (uintptr_t& h : f.shared) drop(h);
        f = SlotFx{};
    }
}
// SI: UseWindowViewFogPass (the buffer is built once per camera; only the fog values change)
void use_fog_pass(int slot, Il2CppObject* cam) {
    if (!ensure_fog_material()) return;
    if (static_cast<int>(g_slot_fx.size()) <= slot) g_slot_fx.resize(static_cast<size_t>(slot) + 1);
    SlotFx& f = g_slot_fx[static_cast<size_t>(slot)];
    const int id = instance_id(cam);
    if (f.cam_id != id || !live(f.buffer)) {
        drop(f.buffer);
        Il2CppObject* cb = il2::il2cpp_object_new(fpa.cb_class);
        il2::invoke(fpa.cb_ctor, cb);
        Rti target, copy;
        if (!cb) return;
        if (!make_rti(fpa.rti_builtin, fpa.builtin_target, target) || !make_rti(fpa.rti_id, fpa.id_copy, copy)) return;
        {
            int32_t nid = fpa.id_copy, w = -1, h = -1, depth = 0, filt = fpa.filter_bilinear, fmt = fpa.fmt_default_hdr;
            void* a[] = {&nid, &w, &h, &depth, &filt, &fmt};
            il2::invoke(fpa.cb_get_temp, cb, a);
        }
        {
            void* a[] = {target.b, copy.b};
            il2::invoke(fpa.cb_blit, cb, a);
        }
        {
            int32_t nid = fpa.id_main;
            void* a[] = {&nid, copy.b};
            il2::invoke(fpa.cb_set_global, cb, a);
        }
        {
            void* a[] = {target.b};
            il2::invoke(fpa.cb_set_target, cb, a);
        }
        {
            M4 ident;
            ident.at(0, 0) = ident.at(1, 1) = ident.at(2, 2) = ident.at(3, 3) = 1;
            int32_t sub = 0, pass = 1;  // SI: WINDOW_FOG_PASS_SKY_EXCLUDED
            void* a[] = {live(fpa.mesh), &ident, live(fpa.material), &sub, &pass};
            il2::invoke(fpa.cb_draw_mesh, cb, a);
        }
        {
            int32_t nid = fpa.id_copy;
            void* a[] = {&nid};
            il2::invoke(fpa.cb_release_temp, cb, a);
        }
        int32_t ev = fpa.ev_before_opaque_fx;
        void* a[] = {&ev, cb};
        il2::invoke(fpa.cam_add, cam, a);
        f.buffer = hold(cb);
        f.cam_id = id;
    }
    if (fpa.frame_set != static_cast<uint32_t>(g_win_frame_now)) {  // the fog values, once a frame
        fpa.frame_set = static_cast<uint32_t>(g_win_frame_now);
        Il2CppObject* m = live(fpa.material);
        Rgba4 c{0.5f, 0.5f, 0.5f, 1};
        il2::invoke_value(fpa.fog_color, nullptr, c);
        float dens = 0, fs = 0, fe = 0;
        il2::invoke_value(fpa.fog_density, nullptr, dens);
        il2::invoke_value(fpa.fog_start, nullptr, fs);
        il2::invoke_value(fpa.fog_end, nullptr, fe);
        int32_t idc = fpa.id_fog_color, idp = fpa.id_fog_params, idv = fpa.id_viewport;
        Rgba4 params{dens, fs, fe, 0};
        float one = 1;
        void* a1[] = {&idc, &c};
        il2::invoke(fpa.mat_color, m, a1);
        void* a2[] = {&idp, &params};
        il2::invoke(fpa.mat_vector, m, a2);
        void* a3[] = {&idv, &one};
        il2::invoke(fpa.mat_float, m, a3);
    }
}
// SI: SharePlayerBuffersWith: the player's camera's buffers that draw the aurora, snow and decals, added to ours
bool shared_buffer_name(const std::string& n) {
    if (n == "SnowPatchHeightMapCommandBuffer") return false;
    return n == "SCRIPT_EnvironmentSystems" || n == "Snow_Blowing" || n == "Distance Field Shadows" ||
           n == "Deferred Ambient Occlusion" || starts_with(n, "SnowPatch") || starts_with(n, "ScreenSpaceDecal");
}
struct SharedBuffers {
    std::vector<uintptr_t> buffers;
    std::vector<int32_t> events;
    int version = 0;
    uint64_t scanned_frame = 0;
} g_shared;
void share_player_buffers(int slot, Il2CppObject* cam) {
    if (!fpa.share_ok) return;
    Il2CppObject* main = live(g_main.cam);
    if (!main) return;
    if (g_shared.scanned_frame == 0 || g_win_frame_now - g_shared.scanned_frame >= 30) {
        g_shared.scanned_frame = g_win_frame_now;
        std::vector<Il2CppObject*> found;
        std::vector<int32_t> evs;
        for (int32_t ev : fpa.ev_shared) {
            if (ev < 0) continue;
            int32_t e = ev;
            void* a[] = {&e};
            Il2CppObject* arr = il2::invoke(fpa.cam_get, main, a);
            for (size_t i = 0, n = il2::array_length(arr); i < n; ++i) {
                Il2CppObject* b = il2::array_object_at(arr, i);
                if (b && shared_buffer_name(text(il2::invoke(fpa.cb_name, b)))) found.push_back(b), evs.push_back(ev);
            }
        }
        bool changed = found.size() != g_shared.buffers.size();
        for (size_t i = 0; !changed && i < found.size(); ++i)
            changed = live(g_shared.buffers[i]) != found[i] || g_shared.events[i] != evs[i];
        if (changed) {
            for (uintptr_t& h : g_shared.buffers) drop(h);
            g_shared.buffers.clear();
            for (Il2CppObject* b : found) g_shared.buffers.push_back(hold(b));
            g_shared.events = evs;
            ++g_shared.version;
            static size_t told_count = ~size_t{0};  // (the game makes some anew now and then: told when the count changes)
            if (found.size() != told_count)
                logx::info("interiors: windows: %zu of the player's camera's buffers (aurora, snow, decals) shared with the views",
                           found.size());
            told_count = found.size();
        }
    }
    if (static_cast<int>(g_slot_fx.size()) <= slot) g_slot_fx.resize(static_cast<size_t>(slot) + 1);
    SlotFx& f = g_slot_fx[static_cast<size_t>(slot)];
    const int id = instance_id(cam);
    if (f.share_cam_id == id && f.shared_version == g_shared.version) return;
    if (f.share_cam_id == id)
        for (size_t i = 0; i < f.shared.size(); ++i)
            if (Il2CppObject* b = live(f.shared[i])) {
                int32_t e = f.shared_events[i];
                void* a[] = {&e, b};
                il2::invoke(fpa.cam_remove, cam, a);
            }
    for (uintptr_t& h : f.shared) drop(h);
    f.shared.clear();
    f.shared_events.clear();
    for (size_t i = 0; i < g_shared.buffers.size(); ++i)
        if (Il2CppObject* b = live(g_shared.buffers[i])) {
            int32_t e = g_shared.events[i];
            void* a[] = {&e, b};
            il2::invoke(fpa.cam_add, cam, a);
            f.shared.push_back(hold(b));
            f.shared_events.push_back(e);
        }
    f.share_cam_id = id;
    f.shared_version = g_shared.version;
}
void resolve_fog_pass() {
    Il2CppClass* cb = il2::find_class(kCore, "UnityEngine.Rendering", "CommandBuffer");
    Il2CppClass* rti = il2::find_class(kCore, "UnityEngine.Rendering", "RenderTargetIdentifier");
    Il2CppClass* camera = il2::find_class(kCore, "UnityEngine", "Camera");
    Il2CppClass* settings = il2::find_class(kCore, "UnityEngine", "RenderSettings");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* ru = il2::find_class(kGame, "UnityEngine.Rendering.PostProcessing", "RuntimeUtilities");
    Il2CppClass* pl = il2::find_class(kGame, "UnityEngine.Rendering.PostProcessing", "PostProcessLayer");
    Il2CppClass* res = il2::find_class(kGame, "UnityEngine.Rendering.PostProcessing", "PostProcessResources");
    Il2CppClass* sh = res ? il2::find_nested_class(res, "Shaders") : nullptr;
    if (!cb || !rti || !camera || !settings || !shader || !wapi.mat_class) return;
    fpa.cb_class = cb;
    fpa.cb_ctor = il2::find_method(cb, ".ctor", 0);
    fpa.cb_get_temp = il2::find_method_params(cb, "GetTemporaryRT",
                                              {"System.Int32", "System.Int32", "System.Int32", "System.Int32",
                                               "UnityEngine.FilterMode", "UnityEngine.RenderTextureFormat"});
    fpa.cb_blit = il2::find_method_params(cb, "Blit", {"UnityEngine.Rendering.RenderTargetIdentifier",
                                                       "UnityEngine.Rendering.RenderTargetIdentifier"});
    fpa.cb_set_global = il2::find_method_params(cb, "SetGlobalTexture", {"System.Int32", "UnityEngine.Rendering.RenderTargetIdentifier"});
    fpa.cb_set_target = il2::find_method_params(cb, "SetRenderTarget", {"UnityEngine.Rendering.RenderTargetIdentifier"});
    fpa.cb_draw_mesh = il2::find_method_params(cb, "DrawMesh", {"UnityEngine.Mesh", "UnityEngine.Matrix4x4", "UnityEngine.Material",
                                                                "System.Int32", "System.Int32"});
    fpa.cb_release_temp = il2::find_method(cb, "ReleaseTemporaryRT", 1);
    fpa.cb_name = il2::find_method(cb, "get_name", 0);
    fpa.rti_builtin = il2::find_method_params(rti, "op_Implicit", {"UnityEngine.Rendering.BuiltinRenderTextureType"});
    fpa.rti_id = il2::find_method_params(rti, "op_Implicit", {"System.Int32"});
    fpa.cam_add = il2::find_method(camera, "AddCommandBuffer", 2);
    fpa.cam_remove = il2::find_method(camera, "RemoveCommandBuffer", 2);
    fpa.cam_get = il2::find_method(camera, "GetCommandBuffers", 1);
    fpa.fog_color = il2::find_method(settings, "get_fogColor", 0);
    fpa.fog_density = il2::find_method(settings, "get_fogDensity", 0);
    fpa.fog_start = il2::find_method(settings, "get_fogStartDistance", 0);
    fpa.fog_end = il2::find_method(settings, "get_fogEndDistance", 0);
    fpa.mat_color = il2::find_method_params(wapi.mat_class, "SetColor", {"System.Int32", "UnityEngine.Color"});
    fpa.mat_vector = il2::find_method_params(wapi.mat_class, "SetVector", {"System.Int32", "UnityEngine.Vector4"});
    fpa.mat_float = il2::find_method_params(wapi.mat_class, "SetFloat", {"System.Int32", "System.Single"});
    fpa.triangle = ru ? il2::find_method(ru, "get_fullscreenTriangle", 0) : nullptr;
    fpa.pl_resources = pl ? il2::field_offset(pl, "m_Resources") : -1;
    fpa.res_shaders = res ? il2::field_offset(res, "shaders") : -1;
    fpa.shaders_fog = sh ? il2::field_offset(sh, "deferredFog") : -1;
    if (const MethodInfo* to_id = il2::find_method_params(shader, "PropertyToID", {"System.String"})) {
        auto id = [&](const char* n) {
            int32_t v = -1;
            void* a[] = {str(n)};
            il2::invoke_value(to_id, nullptr, v, a);
            return v;
        };
        fpa.id_copy = id("_SIWindowFogCopy");
        fpa.id_main = id("_MainTex");
        fpa.id_fog_color = id("_FogColor");
        fpa.id_fog_params = id("_FogParams");
        fpa.id_viewport = id("_RenderViewportScaleFactor");
    }
    int64_t v = 0;
    if (Il2CppClass* e = il2::find_class(kCore, "UnityEngine.Rendering", "CameraEvent")) {
        if (il2::enum_value(e, "BeforeImageEffectsOpaque", v)) fpa.ev_before_opaque_fx = static_cast<int32_t>(v);
        const char* names[] = {"BeforeLighting", "BeforeReflections", "BeforeImageEffectsOpaque", "BeforeForwardAlpha",
                               "AfterForwardAlpha"};
        for (int i = 0; i < 5; ++i)
            if (il2::enum_value(e, names[i], v)) fpa.ev_shared[i] = static_cast<int32_t>(v);
    }
    if (Il2CppClass* e = il2::find_class(kCore, "UnityEngine.Rendering", "BuiltinRenderTextureType"))
        if (il2::enum_value(e, "CameraTarget", v)) fpa.builtin_target = static_cast<int32_t>(v);
    if (Il2CppClass* e = il2::find_class(kCore, "UnityEngine", "FilterMode"))
        if (il2::enum_value(e, "Bilinear", v)) fpa.filter_bilinear = static_cast<int32_t>(v);
    if (Il2CppClass* e = il2::find_class(kCore, "UnityEngine", "RenderTextureFormat"))
        if (il2::enum_value(e, "DefaultHDR", v)) fpa.fmt_default_hdr = static_cast<int32_t>(v);
    fpa.ok = fpa.cb_ctor && fpa.cb_get_temp && fpa.cb_blit && fpa.cb_set_global && fpa.cb_set_target && fpa.cb_draw_mesh &&
             fpa.cb_release_temp && fpa.rti_builtin && fpa.rti_id && fpa.cam_add && fpa.fog_color && fpa.fog_density &&
             fpa.fog_start && fpa.fog_end && fpa.mat_color && fpa.mat_vector && fpa.mat_float && fpa.triangle &&
             fpa.pl_resources >= 0 && fpa.res_shaders >= 0 && fpa.shaders_fog >= 0 && fpa.id_copy != -1 && api.t_post_layer;
    fpa.share_ok = fpa.cam_add && fpa.cam_remove && fpa.cam_get && fpa.cb_name;
    if (!fpa.ok) logx::warn("interiors: windows: the fog pass can't be built in this build; the window views have no fog");
}

// --- INSIDE-OUT (SI: WindowPortals.cs) -------------------------------------------------------------------------------------

struct InsideOut {
    const Config* cfg = nullptr;  // the room set up (its Instance looked up again every frame)
    struct View {
        WorldOpening o;
        V3 delta;
        float view_start = kViewStartsOutsideShellGlass;
        int spot = -1;
        uintptr_t quad = 0, quad_r = 0;
        int tile = -1;
        bool was_drawn = false, drawn = false;
        uintptr_t backdrop = 0;
        bool backdrop_was_on = false, backdrop_hidden = false;
    };
    std::vector<View> views;
    std::vector<Slot> slots;
    int paired = 0;
    bool shell_forced = false;
    uint64_t frames = 0, drawn_total = 0;
    double next_report = 0;
} g_io;

void set_backdrop_hidden(InsideOut::View& v, bool hidden) {
    if (v.backdrop_hidden == hidden) return;
    Il2CppObject* r = live(v.backdrop);
    if (!r) return;
    if (hidden) {
        v.backdrop_was_on = renderer_on(r);
        if (v.backdrop_was_on) set_renderer(r, false);
    } else if (v.backdrop_was_on) {
        set_renderer(r, true);
    }
    v.backdrop_hidden = hidden;
}
void teardown_inside_out(const char* why) {
    if (!g_io.cfg && g_io.views.empty() && g_io.slots.empty()) return;
    for (InsideOut::View& v : g_io.views) {
        set_backdrop_hidden(v, false);
        if (Il2CppObject* q = live(v.quad)) destroy(q);
        drop(v.quad);
        drop(v.quad_r);
        drop(v.backdrop);
    }
    for (Slot& s : g_io.slots) free_slot(s);
    free_slot_fx();
    logx::info("interiors: windows: views out of %s taken down (%s; %llu frame(s), %.2f views a frame on average)",
               g_io.cfg ? g_io.cfg->id : "?", why, static_cast<unsigned long long>(g_io.frames),
               g_io.frames ? static_cast<double>(g_io.drawn_total) / static_cast<double>(g_io.frames) : 0.0);
    g_io = InsideOut{};
}
// SI: SetupWindowPortals + PairWindowOpeningsToShell
bool setup_inside_out(Instance& in) {
    g_io = InsideOut{};
    g_io.cfg = in.cfg;
    WinRoom& wr = room_windows(in);
    if (wr.openings.empty()) return false;
    const WinRow* row = win_row(in.cfg->shell);
    Il2CppObject* shell = live(in.shell);
    const V3 building_delta = position(go_tf(shell)) - in.master_pos;  // SI: ShellPosition - the room's
    for (const Opening& o : wr.openings) {
        InsideOut::View v;
        v.o = world_of(in, o);
        v.delta = building_delta;
        if (Il2CppObject* b = live(o.backdrop)) v.backdrop = hold(b);
        g_io.views.push_back(v);
    }
    const std::vector<ShellWin>* spots = shell_windows(in);
    if (spots) {
        const std::vector<WorldSpot> ws = world_spots(in, *spots);
        std::vector<V3> sc, so, rc, ro;
        for (const WorldSpot& s : ws) sc.push_back(s.c), so.push_back(s.out);
        for (const InsideOut::View& v : g_io.views) rc.push_back(v.o.portal + building_delta), ro.push_back(v.o.out);
        std::vector<int> map;
        V3 slide;
        bool fitted = row && !std::isnan(row->in_place_cm) ? pair_in_place(rc, ro, sc, so, row->in_place_cm / 100, map, slide)
                                                           : fit_pattern(rc, ro, sc, so, map, slide);
        std::vector<int> shell_to_room(ws.size(), -1);
        if (fitted)
            for (size_t i = 0; i < map.size(); ++i)
                if (map[i] >= 0) shell_to_room[map[i]] = static_cast<int>(i);
        if (hand_pairs(row, shell_to_room, static_cast<int>(rc.size()))) {
            map.assign(rc.size(), -1);
            for (size_t s = 0; s < shell_to_room.size(); ++s)
                if (shell_to_room[s] >= 0) map[shell_to_room[s]] = static_cast<int>(s);
            V3 hs;
            if (mean_slide(rc, sc, map, hs)) slide = hs, fitted = true;
        }
        std::vector<char> taken(ws.size(), 0);
        for (size_t i = 0; i < g_io.views.size(); ++i) {
            InsideOut::View& v = g_io.views[i];
            const int best = fitted && i < map.size() ? map[i] : -1;
            if (best < 0) {
                if (fitted) v.delta = building_delta - slide + view_out_offset(row, static_cast<int>(i), v.o.out);
                continue;
            }
            taken[best] = 1;
            V3 view_world = ws[best].c;
            if (v.o.w > (*spots)[best].w * kDoubleWiderBy) {  // a double window: its two panes' middle
                const V3 out_here = ws[best].out;
                const V3 right_here = w_norm(w_cross(kUp, out_here * -1.0f));
                int partner = -1;
                float best_miss = kDoubleWidthWithin;
                for (size_t s = 0; s < ws.size(); ++s) {
                    if (static_cast<int>(s) == best) continue;
                    bool elsewhere = false;
                    for (int r : map) elsewhere = elsewhere || r == static_cast<int>(s);
                    if (elsewhere || w_dot(ws[s].out, out_here) < 0.999f) continue;
                    const V3 between = ws[s].c - ws[best].c;
                    if (std::fabs(w_dot(between, out_here)) > kDoubleSamePlane || std::fabs(between.y) > kDoubleSamePlane) continue;
                    const float miss = std::fabs(std::fabs(w_dot(between, right_here)) + ((*spots)[best].w + (*spots)[s].w) * 0.5f - v.o.w);
                    if (miss >= best_miss) continue;
                    best_miss = miss, partner = static_cast<int>(s);
                }
                if (partner >= 0) {
                    const float across = w_dot(ws[partner].c - ws[best].c, right_here);
                    const float l = std::min(-(*spots)[best].w * 0.5f, across - (*spots)[partner].w * 0.5f);
                    const float r = std::max((*spots)[best].w * 0.5f, across + (*spots)[partner].w * 0.5f);
                    view_world = ws[best].c + right_here * ((l + r) * 0.5f);
                    taken[partner] = 1;
                }
            }
            v.delta = view_world - v.o.portal + view_out_offset(row, static_cast<int>(i), v.o.out);
            v.spot = best;
            ++g_io.paired;
        }
        // TLDCoop_DLC: the room's windows left unpaired (Karol's drawing: the view through them shifted along and across
        // the wall: they were carried by the config's building-wide shift, which is off by up to a metre). The paired
        // ones' average correction first; then each still unpaired one to the nearest free building pane facing the same
        // way (within 2 m once corrected).
        V3 fix{0, 0, 0};
        int fixes = 0;
        for (const InsideOut::View& v : g_io.views)
            if (v.spot >= 0) fix = fix + (v.delta - building_delta), ++fixes;
        if (fixes) fix = fix * (1.0f / static_cast<float>(fixes));
        int late = 0, shifted = 0;
        for (size_t i = 0; i < g_io.views.size(); ++i) {
            InsideOut::View& v = g_io.views[i];
            if (v.spot >= 0) continue;
            const V3 here = v.o.portal + building_delta + fix;
            int near = -1;
            float best_d = 2.0f;
            for (size_t sidx = 0; sidx < ws.size(); ++sidx) {
                if (taken[sidx] || w_dot(ws[sidx].out, v.o.out) < 0.9f) continue;
                const float d = w_len(ws[sidx].c - here);
                if (d < best_d) best_d = d, near = static_cast<int>(sidx);
            }
            if (near >= 0) {
                taken[near] = 1;
                v.delta = ws[near].c - v.o.portal + view_out_offset(row, static_cast<int>(i), v.o.out);
                v.spot = near;
                ++g_io.paired;
                ++late;
            } else if (fixes) {
                v.delta = building_delta + fix + view_out_offset(row, static_cast<int>(i), v.o.out);
                ++shifted;
            }
        }
        if (late || shifted)
            logx::info("interiors: windows: %s: %d more window(s) paired to the nearest building pane, %d shifted by the "
                       "paired ones' average (%.2f, %.2f, %.2f m)", in.cfg->id, late, shifted, fix.x, fix.y, fix.z);
    }
    for (InsideOut::View& v : g_io.views) {
        Il2CppObject* q = new_quad("TLDCoop_SI_WindowView");
        if (!q) {
            teardown_inside_out("a quad could not be made");
            return false;
        }
        v.quad = hold(q);
        v.quad_r = hold(component(q, api.t_renderer));
    }
    logx::info("interiors: windows: %s: %zu view(s) out of the room ready, %d matched to a window of the building (%s), at most "
               "%d drawn at once; the player's camera %s layer %d (the views' sheets), mask 0x%08x",
               in.cfg->id, g_io.views.size(), g_io.paired, spots ? "its windows known" : "no window data: one building-wide shift",
               quality().in_slots, (g_main.mask & (1 << kQuadLayer)) ? "draws" : "DOES NOT DRAW", kQuadLayer,
               static_cast<unsigned>(g_main.mask));
    return true;
}
// SI: NearestWindowGlassAlongGaze
float nearest_glass_along_gaze() {
    const V3 gaze = rotate(g_main.rot, {0, 0, 1});
    float nearest = 1e30f;
    for (const InsideOut::View& v : g_io.views) {
        const V3 d = v.o.glass - g_main.pos;
        const float along = w_dot(d, gaze);
        if (along <= 0) continue;
        // on screen (the frustum planes)
        bool in = true;
        for (const Plane4& p : g_main.planes) in = in && p.x * v.o.glass.x + p.y * v.o.glass.y + p.z * v.o.glass.z + p.w >= 0;
        if (in && along < nearest) nearest = along;
    }
    return nearest >= 1e29f ? 0.0f : std::max(0.0f, nearest - 0.3f);
}
// SI: RenderWindowPortals + DrawOneWindowPortal
void render_inside_out() {
    const WinQuality& qy = quality();
    if (static_cast<int>(g_io.slots.size()) < qy.in_slots) g_io.slots.resize(static_cast<size_t>(qy.in_slots));
    const V3 eye = g_main.pos;
    struct Cand {
        int index;
        float priority;
    };
    std::vector<Cand> vis;
    for (size_t i = 0; i < g_io.views.size(); ++i) {
        const InsideOut::View& v = g_io.views[i];
        const V3 corner = v.o.portal - v.o.right * (v.o.w * 0.5f) - v.o.up * (v.o.h * 0.5f);
        const float eye_d = w_dot(corner - eye, v.o.out);
        if (eye_d <= kMinEyeDistance) continue;  // behind the player, or the eye outside the window
        float u0, u1, v0, v1;
        if (!visible_subrect(corner, v.o.right, v.o.up, v.o.w, v.o.h, u0, u1, v0, v1)) continue;
        const float straight = std::max(w_len(v.o.portal - eye), 0.1f);
        float pr = (v.o.w * v.o.h) * (eye_d / straight) / (straight * straight);
        if (v.was_drawn) pr *= kHysteresis;
        vis.push_back({static_cast<int>(i), pr});
    }
    std::stable_sort(vis.begin(), vis.end(), [](const Cand& a, const Cand& b) { return a.priority > b.priority; });
    const int draw = std::min(static_cast<int>(vis.size()), qy.in_slots);
    for (InsideOut::View& v : g_io.views) v.drawn = false;
    int used = 0;
    const int mask = g_main.mask & ~(1 << kQuadLayer) & ~kRoomLayersMask;
    for (int k = 0; k < draw; ++k) {
        InsideOut::View& v = g_io.views[static_cast<size_t>(vis[k].index)];
        Slot& s = g_io.slots[static_cast<size_t>(used)];
        const V3 pa = v.o.portal - v.o.right * (v.o.w * 0.5f) - v.o.up * (v.o.h * 0.5f);
        float u0, u1, v0, v1;
        if (!visible_subrect(pa, v.o.right, v.o.up, v.o.w, v.o.h, u0, u1, v0, v1)) continue;
        const V3 qa = pa + v.o.right * u0 + v.o.up * v0, qb = pa + v.o.right * u1 + v.o.up * v0,
                 qc = pa + v.o.right * u0 + v.o.up * v1;
        int tw, th;
        pixel_size(qa, qb, qc, qy.scale, tw, th);
        char name[48];
        std::snprintf(name, sizeof(name), "TLDCoop_SI_WindowCamera%d", used);
        if (!ensure_slot(s, name, -50 - used, kViewOutQueue, tw, th, mask)) continue;
        Il2CppObject* cam = live(s.cam);
        if (cam) g_out_cams.insert(cam);
        share_player_buffers(used, cam);  // SI: SharePlayerBuffersWith
        use_fog_pass(used, cam);          // SI: UseWindowViewFogPass
        // the eye stands to the building's pane as it stands to the room's; the view starts past the shell's frame
        const V3 cam_pos = eye + v.delta + v.o.out * v.view_start;
        if (!aim_camera(cam, cam_pos, v.o.out, v.o.up, v.o.right, eye, qa, qb, qc, g_main.zfar)) continue;
        set_behaviour(cam, true);  // Unity draws it, later in the frame (SI: NO Render() CALL)
        Il2CppObject* quad = live(v.quad);
        Il2CppObject* qr = live(v.quad_r);
        if (!quad || !qr) continue;
        place_quad(quad, pa, v.o.right, v.o.up, v.o.out, u0, u1, v0, v1);
        if (v.tile != used) {
            void* ma[] = {live(s.mat)};
            il2::invoke(wapi.r_set_shared, qr, ma);
            v.tile = used;
        }
        if (!renderer_on(qr)) set_renderer(qr, true);
        v.drawn = true;
        ++used;
    }
    // what has no slot this frame: its quad off (it would show another window's view), the slots left over stop drawing
    for (InsideOut::View& v : g_io.views) {
        if (!v.drawn) {
            if (Il2CppObject* qr = live(v.quad_r); qr && renderer_on(qr)) set_renderer(qr, false);
            v.tile = v.drawn ? v.tile : -1;
        }
        v.was_drawn = v.drawn;
    }
    for (size_t k = static_cast<size_t>(used); k < g_io.slots.size(); ++k)
        if (Il2CppObject* c = live(g_io.slots[k].cam); c && behaviour_on(c)) set_behaviour(c, false);
    // the painted backdrop off only behind a window that shows a live view (one shared by several: all of them)
    std::unordered_map<uintptr_t, std::pair<int, int>> by_backdrop;  // handle -> (windows, drawn)
    std::unordered_map<Il2CppObject*, uintptr_t> handle_of;
    for (InsideOut::View& v : g_io.views)
        if (Il2CppObject* b = live(v.backdrop)) {
            auto [it, fresh] = handle_of.emplace(b, v.backdrop);
            auto& c = by_backdrop[it->second];
            ++c.first;
            c.second += v.drawn ? 1 : 0;
        }
    for (InsideOut::View& v : g_io.views)
        if (Il2CppObject* b = live(v.backdrop)) {
            const auto& c = by_backdrop[handle_of[b]];
            set_backdrop_hidden(v, c.first > 0 && c.first == c.second);
        }
    ++g_io.frames;
    g_io.drawn_total += static_cast<uint64_t>(used);
    const double now = now_ms();
    if (now >= g_io.next_report) {
        g_io.next_report = now + 30000;
        logx::info("interiors: windows: %s: %d view(s) drawn this frame of %zu visible (limit %d, quality %d)",
                   g_io.cfg ? g_io.cfg->id : "?", used, vis.size(), qy.in_slots, g_window_quality);
    }
}

// --- OUTSIDE-IN (SI: OutsideInPortals.cs, OutsideInBuildings.cs) ---------------------------------------------------------

struct Aperture {
    V3 center, out, right, up, delta;  // the view's plane (1 cm toward the viewer), what carries the eye into the room
    float w = 0, h = 0;
    bool paired = false, pretend = false;
};
struct HeldRoom {
    const Config* cfg = nullptr;
    std::vector<Aperture> aps;
    bool unsupported = false;
    float shell_d = 1e30f;
    bool want = false, borrowed = false, activated = false, cameras_on = false;
    int layer = 3;
    struct Saved {
        uintptr_t go = 0;
        int layer = 0;
    };
    std::vector<Saved> relayered;
    std::vector<uintptr_t> colliders;
    std::unordered_set<int> collider_ids;
    double next_relayer = 0;
    std::vector<char> was_drawn;
};
struct OutsideIn {
    std::vector<HeldRoom> rooms;
    std::unordered_set<const Config*> unsupported;
    std::vector<Slot> slots;
    std::vector<uintptr_t> quads, quad_rs;
    bool mask_edited = false;
    int saved_mask = -1;
    uint64_t frames = 0, drawn_total = 0;
    double next_report = 0;
} g_oi;

Instance* instance_of(const Config* cfg) {
    if (!cfg) return nullptr;
    for (Instance& in : g_instances)
        if (in.cfg == cfg) return &in;
    return nullptr;
}
float shell_distance(Instance& in, V3 eye) {
    Il2CppObject* shell = live(in.shell);
    return shell ? dist(position(go_tf(shell)), eye) : dist(in.cfg->fallback, eye);
}
// SI: SetupOutsideInPortals (its pairing and apertures)
bool setup_held(Instance& in, HeldRoom& h) {
    h.cfg = in.cfg;
    const std::vector<ShellWin>* spots = shell_windows(in);
    if (!spots) return false;
    WinRoom& wr = room_windows(in);
    const WinRow* row = win_row(in.cfg->shell);
    const std::vector<WorldSpot> ws = world_spots(in, *spots);
    std::vector<V3> sc, so, rc, ro;
    std::vector<WorldOpening> wo;
    for (const WorldSpot& s : ws) sc.push_back(s.c), so.push_back(s.out);
    for (const Opening& o : wr.openings) {
        wo.push_back(world_of(in, o));
        rc.push_back(wo.back().portal - kParkOffset);
        ro.push_back(wo.back().out);
    }
    std::vector<int> map;
    V3 slide;
    bool fitted = row && !std::isnan(row->in_place_cm) ? pair_in_place(rc, ro, sc, so, row->in_place_cm / 100, map, slide)
                                                       : fit_pattern(rc, ro, sc, so, map, slide);
    std::vector<int> shell_to_room(ws.size(), -1);
    if (fitted)
        for (size_t i = 0; i < map.size(); ++i)
            if (map[i] >= 0) shell_to_room[map[i]] = static_cast<int>(i);
    if (hand_pairs(row, shell_to_room, static_cast<int>(rc.size()))) {
        std::vector<int> back(rc.size(), -1);
        for (size_t s = 0; s < shell_to_room.size(); ++s)
            if (shell_to_room[s] >= 0) back[shell_to_room[s]] = static_cast<int>(s);
        V3 hs;
        if (mean_slide(rc, sc, back, hs)) slide = hs, fitted = true;
    }
    const float toward = row && !std::isnan(row->plane_cm) ? row->plane_cm / 100 : kOutsideInToward;
    int paired = 0, pretend = 0, late = 0;
    // TLDCoop_DLC: the paired panes' average carry (building pane -> the room behind it), and which room windows are
    // taken: a building pane left unpaired goes to the nearest free room window facing the same way (Karol's drawing:
    // the room seen through such a pane was shifted along and across the wall)
    std::vector<char> room_taken(wo.size(), 0);
    V3 carry{0, 0, 0};
    int carries = 0;
    for (size_t s = 0; s < ws.size(); ++s) {
        const int b = left_alone(row, static_cast<int>(s)) ? -1 : shell_to_room[s];
        if (b < 0) continue;
        room_taken[static_cast<size_t>(b)] = 1;
        carry = carry + (wo[static_cast<size_t>(b)].portal - ws[s].c), ++carries;
    }
    if (carries) carry = carry * (1.0f / static_cast<float>(carries));
    for (size_t s = 0; s < ws.size(); ++s) {
        Aperture a;
        a.out = ws[s].out;
        a.up = kUp;
        a.right = w_norm(w_cross(kUp, a.out * -1.0f));
        a.w = ws[s].w;
        a.h = ws[s].h;
        a.center = ws[s].c;
        const int best = left_alone(row, static_cast<int>(s)) ? -1 : shell_to_room[s];
        if (best >= 0) {
            const WorldOpening& o = wo[static_cast<size_t>(best)];
            // SI: RoomPointBehindPane (no WindowFit rows, no frame measuring): the row's start, else the view's plane
            const V3 behind = row && !std::isnan(row->view_from_out_cm) ? o.glass + o.out * (row->view_from_out_cm / 100) : o.portal;
            a.delta = behind - a.center;
            a.paired = true;
            ++paired;
        } else if (int near = -1; carries && !left_alone(row, static_cast<int>(s)) && [&] {
                       float best_d = 2.0f;
                       for (size_t r = 0; r < wo.size(); ++r) {
                           if (room_taken[r] || w_dot(wo[r].out, a.out) < 0.9f) continue;
                           const float d = w_len(wo[r].portal - (a.center + carry));
                           if (d < best_d) best_d = d, near = static_cast<int>(r);
                       }
                       return near >= 0;
                   }()) {
            const WorldOpening& o = wo[static_cast<size_t>(near)];
            room_taken[static_cast<size_t>(near)] = 1;
            const V3 behind = row && !std::isnan(row->view_from_out_cm) ? o.glass + o.out * (row->view_from_out_cm / 100) : o.portal;
            a.delta = behind - a.center;
            a.paired = true;
            ++paired, ++late;
        } else if (fitted && !left_alone(row, static_cast<int>(s)) &&
                   (!in.have_trigger || in_volume(in, a.center + slide + kParkOffset, kPretendEyeInRoom))) {
            a.delta = slide + kParkOffset;
            a.paired = a.pretend = true;
            ++pretend;
        }
        // SI: RenderOutsideInPane: the view's plane a centimetre toward the viewer, the eye's carry unchanged
        a.center = a.center + a.out * toward;
        a.delta = a.delta - a.out * toward - a.out * toward;
        h.aps.push_back(a);
    }
    h.was_drawn.assign(h.aps.size(), 0);
    logx::info("interiors: windows: %s: %zu window(s) on the building, %d matched to a window of the room, %d shown with no "
               "window in the room behind them (%d paired to the nearest room window)",
               in.cfg->id, h.aps.size(), paired, pretend, late);
    return paired + pretend > 0;
}
// SI: RelayerInteriorForOutsideIn
void relayer_held(Instance& in, HeldRoom& h) {
    each_in(master_of(in), api.t_renderer, [&](Il2CppObject* r) {
        Il2CppObject* go = go_of(r);
        const int l = layer_of(go);
        if (l < 0 || l == h.layer) return;
        h.relayered.push_back({hold(go), l});
        set_layer(go, h.layer);
    });
}
// SI: DisableInteriorCollisionForOutsideIn (every frame)
void colliders_off(Instance& in, HeldRoom& h) {
    each_in(master_of(in), api.t_collider, [&](Il2CppObject* c) {
        if (!collider_on(c)) return;
        set_collider(c, false);
        if (h.collider_ids.insert(instance_id(c)).second) h.colliders.push_back(hold(c));
    });
}
void colliders_back(HeldRoom& h) {
    for (uintptr_t& c : h.colliders) {
        if (Il2CppObject* col = live(c)) set_collider(col, true);
        drop(c);
    }
    h.colliders.clear();
    h.collider_ids.clear();
}
int pick_layer(const HeldRoom& me) {  // SI: PickOutsideInRoomLayer
    for (int l : kRoomLayers) {
        bool taken = false;
        for (const HeldRoom& o : g_oi.rooms) taken = taken || (&o != &me && o.borrowed && o.layer == l);
        if (!taken) return l;
    }
    return kRoomLayers[0];
}
void restore_main_mask() {
    if (!g_oi.mask_edited) return;
    g_oi.mask_edited = false;
    if (Il2CppObject* main = main_camera(); alive(main) && g_oi.saved_mask != -1) cam_set<int32_t>(wapi.cam_set_mask, main, g_oi.saved_mask);
}
// SI: BorrowInteriorForOutsideIn
bool borrow(Instance& in, HeldRoom& h) {
    Il2CppObject* m = master_of(in);
    if (!m || !in_room(&in) || !in.hydrated) return false;
    h.activated = !active_self(m);
    if (h.activated) {
        set_active(m, true);
        show_room_renderers(in);
        set_outsiders_visible(in, true);
    }
    h.layer = pick_layer(h);
    relayer_held(in, h);
    colliders_off(in, h);
    h.next_relayer = now_ms() + 500;
    Il2CppObject* main = main_camera();
    if (alive(main)) {
        if (!g_oi.mask_edited) {
            g_oi.saved_mask = cam_value<int32_t>(wapi.cam_get_mask, main, -1);
            g_oi.mask_edited = g_oi.saved_mask != -1;
        }
        if (g_oi.mask_edited) cam_set<int32_t>(wapi.cam_set_mask, main, g_oi.saved_mask & ~kRoomLayersMask);
    }
    h.borrowed = true;
    logx::info("interiors: windows: %s: room switched on to be seen through its windows (building %.0f m away), %zu object(s) "
               "on its own layer %d, %zu collider(s) off",
               in.cfg->id, h.shell_d, h.relayered.size(), h.layer, h.colliders.size());
    return true;
}
// SI: ReturnInteriorFromOutsideIn. hide: the room is switched off again (unless the player is in it).
void give_back(HeldRoom& h, bool hide, const char* why) {
    if (!h.borrowed) return;
    for (HeldRoom::Saved& s : h.relayered) {
        if (Il2CppObject* go = live(s.go)) set_layer(go, s.layer);
        drop(s.go);
    }
    h.relayered.clear();
    const size_t cols = h.colliders.size();
    colliders_back(h);
    Instance* in = instance_of(h.cfg);
    bool hidden = false;
    if (hide && h.activated && in && g_inside != in && master_of(*in)) {
        hide_room(*in);
        hidden = true;
    }
    h.activated = false;
    h.borrowed = false;
    bool any = false;
    for (const HeldRoom& o : g_oi.rooms) any = any || o.borrowed;
    if (!any) restore_main_mask();
    logx::info("interiors: windows: %s: room given back (%s): layers and %zu collider(s) restored%s", h.cfg ? h.cfg->id : "?", why,
               cols, hidden ? ", switched off" : "");
}
void teardown_outside_in(const char* why) {
    if (g_oi.rooms.empty() && g_oi.slots.empty() && g_oi.quads.empty() && !g_oi.mask_edited) return;
    for (HeldRoom& h : g_oi.rooms) give_back(h, true, why);
    restore_main_mask();
    for (Slot& s : g_oi.slots) free_slot(s);
    for (uintptr_t& q : g_oi.quads) {
        if (Il2CppObject* go = live(q)) destroy(go);
        drop(q);
    }
    for (uintptr_t& r : g_oi.quad_rs) drop(r);
    if (g_oi.frames)
        logx::info("interiors: windows: views into the rooms taken down (%s; %llu frame(s), %.2f views a frame on average)", why,
                   static_cast<unsigned long long>(g_oi.frames), static_cast<double>(g_oi.drawn_total) / static_cast<double>(g_oi.frames));
    std::unordered_set<const Config*> keep = std::move(g_oi.unsupported);
    g_oi = OutsideIn{};
    g_oi.unsupported = std::move(keep);
}
// SI: UpdateOutsideInBuildingSet + ChooseOutsideInHeldRooms
void choose_held(V3 eye) {
    const WinQuality& qy = quality();
    const float keep_range = qy.oi_range + kOutsideInRoomReleaseMargin + 20;
    // drop what is gone or far
    for (size_t i = g_oi.rooms.size(); i-- > 0;) {
        HeldRoom& h = g_oi.rooms[i];
        Instance* in = instance_of(h.cfg);
        const bool alive_room = in && in_room(in) && master_of(*in) && live(in->shell);
        if (alive_room) h.shell_d = shell_distance(*in, eye);
        if (!alive_room || h.shell_d > keep_range) {
            give_back(h, alive_room, alive_room ? "far away" : "the room is gone");
            g_oi.rooms.erase(g_oi.rooms.begin() + static_cast<ptrdiff_t>(i));
        }
    }
    // one new building a frame (the one whose door view is wanted, else the nearest not set up yet)
    Instance* next = nullptr;
    float next_d = g_windows ? qy.oi_range + kOutsideInReleaseMargin : 0.0f;
    for (Instance& in : g_instances) {
        if (!in_room(&in) || !in.hydrated || !master_of(in) || !live(in.shell) || g_oi.unsupported.count(in.cfg)) continue;
        bool have = false;
        for (const HeldRoom& h : g_oi.rooms) have = have || h.cfg == in.cfg;
        if (have) continue;
        const float d = shell_distance(in, eye);
        if (in.cfg == g_door_want) {
            next = &in, next_d = d;
            break;
        }
        if (d < next_d) next_d = d, next = &in;
    }
    if (next) {
        HeldRoom h;
        h.cfg = next->cfg;
        h.shell_d = next_d;
        const bool views = g_windows && setup_held(*next, h);
        if (!views) h.aps.clear(), h.was_drawn.clear();  // (held for its doors only)
        if (views || g_door_portals) g_oi.rooms.push_back(std::move(h));
        else g_oi.unsupported.insert(next->cfg);
    }
    // which are held: nearest first (a held one a little favoured), never one the player may be in, never overlapping
    std::vector<HeldRoom*> order;
    for (HeldRoom& h : g_oi.rooms) {
        h.want = false;
        h.cameras_on = g_windows && h.shell_d <= qy.oi_range + (h.cameras_on ? kOutsideInReleaseMargin : 0);
        Instance* in = instance_of(h.cfg);
        if (!in || (!h.cameras_on && h.cfg != g_door_want)) continue;
        if (in_parked_room(*in, eye, 2.0f)) continue;  // SI: PlayerMayBeInsideForOutsideIn
        order.push_back(&h);
    }
    std::sort(order.begin(), order.end(), [](const HeldRoom* a, const HeldRoom* b) {
        const bool da = a->cfg == g_door_want, db = b->cfg == g_door_want;  // the door being looked through first
        if (da != db) return da;
        return a->shell_d * (a->borrowed ? kHeldRoomAdvantage : 1.0f) < b->shell_d * (b->borrowed ? kHeldRoomAdvantage : 1.0f);
    });
    std::vector<Box> chosen;
    int taken = 0;
    for (HeldRoom* h : order) {
        if (taken >= qy.oi_rooms) break;
        Instance* in = instance_of(h->cfg);
        Box mine;
        bool overlaps = false;
        if (in && in->have_trigger) {
            const V3 c = to_world(*in, in->trig_center);
            const V3 e = in->trig_size * 0.5f;
            const float r = std::max(e.x, e.z) - 0.6f;
            mine.add({c.x - r, c.y - e.y, c.z - r});
            mine.add({c.x + r, c.y + e.y, c.z + r});
            for (const Box& b : chosen)
                overlaps = overlaps || (mine.min.x <= b.max.x && mine.max.x >= b.min.x && mine.min.y <= b.max.y &&
                                        mine.max.y >= b.min.y && mine.min.z <= b.max.z && mine.max.z >= b.min.z);
        }
        if (overlaps) continue;
        if (mine.valid()) chosen.push_back(mine);
        h->want = true;
        ++taken;
    }
}
void render_outside_in(V3 eye) {
    const WinQuality& qy = quality();
    struct Cand {
        HeldRoom* h;
        int index;
        float priority;
    };
    std::vector<Cand> cands;
    for (HeldRoom& h : g_oi.rooms) {
        if (!h.borrowed) continue;
        for (size_t i = 0; i < h.aps.size(); ++i) {
            const Aperture& a = h.aps[i];
            if (!a.paired) continue;
            const float eye_d = w_dot(eye - a.center, a.out);
            if (eye_d <= kMinEyeDistance) continue;
            const float straight = std::max(w_len(eye - a.center), 0.1f);
            float pr = (a.w * a.h) * (eye_d / straight) / (straight * straight);
            if (h.was_drawn[i]) pr *= kHysteresis;
            cands.push_back({&h, static_cast<int>(i), pr});
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.priority > b.priority; });
    for (HeldRoom& h : g_oi.rooms) std::fill(h.was_drawn.begin(), h.was_drawn.end(), 0);
    if (static_cast<int>(g_oi.slots.size()) < qy.oi_slots) {
        g_oi.slots.resize(static_cast<size_t>(qy.oi_slots));
        while (g_oi.quads.size() < g_oi.slots.size()) {
            Il2CppObject* q = new_quad("TLDCoop_SI_RoomView");
            g_oi.quads.push_back(hold(q));
            g_oi.quad_rs.push_back(hold(q ? component(q, api.t_renderer) : nullptr));
        }
    }
    int used = 0;
    for (const Cand& c : cands) {
        if (used >= qy.oi_slots) break;
        const Aperture& a = c.h->aps[static_cast<size_t>(c.index)];
        const V3 fwd = a.out * -1.0f;  // looking into the building
        const V3 pa = a.center - a.right * (a.w * 0.5f) - a.up * (a.h * 0.5f);
        float u0, u1, v0, v1;
        if (!visible_subrect(pa, a.right, a.up, a.w, a.h, u0, u1, v0, v1)) continue;
        const V3 qa = pa + a.right * u0 + a.up * v0, qb = pa + a.right * u1 + a.up * v0, qc = pa + a.right * u0 + a.up * v1;
        int tw, th;
        // far away: half the resolution again
        pixel_size(qa, qb, qc, qy.scale * (c.h->shell_d > 25 ? 0.5f : 1.0f), tw, th);
        Slot& s = g_oi.slots[static_cast<size_t>(used)];
        char name[48];
        std::snprintf(name, sizeof(name), "TLDCoop_SI_RoomCamera%d", used);
        if (!ensure_slot(s, name, -80 - used, kViewInQueue, tw, th, 1 << c.h->layer)) continue;
        Il2CppObject* cam = live(s.cam);
        if (!aim_camera(cam, eye + a.delta, fwd, a.up, a.right, eye, qa, qb, qc, kOutsideInFarClip)) continue;
        set_behaviour(cam, false);
        il2::invoke(wapi.cam_render, cam);  // by hand: only the room's layer (SI: cam.Render())
        Il2CppObject* quad = live(g_oi.quads[static_cast<size_t>(used)]);
        Il2CppObject* qr = live(g_oi.quad_rs[static_cast<size_t>(used)]);
        if (!quad || !qr) continue;
        place_quad(quad, pa, a.right, a.up, fwd, u0, u1, v0, v1);
        void* ma[] = {live(s.mat)};
        il2::invoke(wapi.r_set_shared, qr, ma);
        if (!renderer_on(qr)) set_renderer(qr, true);
        c.h->was_drawn[static_cast<size_t>(c.index)] = 1;
        ++used;
    }
    for (size_t k = static_cast<size_t>(used); k < g_oi.quad_rs.size(); ++k)
        if (Il2CppObject* qr = live(g_oi.quad_rs[k]); qr && renderer_on(qr)) set_renderer(qr, false);
    ++g_oi.frames;
    g_oi.drawn_total += static_cast<uint64_t>(used);
    const double now = now_ms();
    if (now >= g_oi.next_report && used > 0) {
        g_oi.next_report = now + 30000;
        int held = 0;
        for (const HeldRoom& h : g_oi.rooms) held += h.borrowed ? 1 : 0;
        logx::info("interiors: windows: %d view(s) into %d held room(s) drawn this frame (limit %d, quality %d)", used, held,
                   qy.oi_slots, g_window_quality);
    }
}
void tick_outside_in(V3 eye) {
    choose_held(eye);
    bool borrowed_now = false;
    for (HeldRoom& h : g_oi.rooms) {
        Instance* in = instance_of(h.cfg);
        if (!in) continue;
        if (h.want && !h.borrowed) {
            if (!borrowed_now) borrowed_now = borrow(*in, h);  // one room switched on a frame (SI)
        } else if (!h.want && h.borrowed) {
            give_back(h, true, "no longer wanted");
        }
        if (!h.borrowed) continue;
        // SI: ReassertOutsideInHold: on, no collision, on its layer, out of the player's camera
        Il2CppObject* m = master_of(*in);
        if (m && !active_self(m)) {
            set_active(m, true);
            h.activated = true;
        }
        colliders_off(*in, h);
        if (now_ms() >= h.next_relayer) {
            h.next_relayer = now_ms() + 500;
            relayer_held(*in, h);
        }
    }
    bool any = false;
    for (const HeldRoom& h : g_oi.rooms) any = any || h.borrowed;
    if (any && g_oi.mask_edited)
        if (Il2CppObject* main = live(g_main.cam)) {
            const int want = g_oi.saved_mask & ~kRoomLayersMask;
            if (g_main.mask != want) cam_set<int32_t>(wapi.cam_set_mask, main, want);
            g_main.mask = want;
        }
    if (any) render_outside_in(eye);
    else
        for (uintptr_t& r : g_oi.quad_rs)
            if (Il2CppObject* qr = live(r); qr && renderer_on(qr)) set_renderer(qr, false);
}

// --- the reconciler --------------------------------------------------------------------------------------------------------

uint64_t g_win_frame = 0, g_win_reconciled = 0;
bool g_win_active = false;  // something of ours stands (views, eye, held rooms)

// Door portals (seamless_doors.inc, included after this file)
void doors_pick(Instance* inside, V3 eye);
int doors_render(Instance* inside, V3 eye);
void teardown_doors(const char* why);
void forget_doors_region();
bool doors_standing();
void resolve_doors();

void forget_windows_region() {  // what was refused in the region left may work in the next one
    g_oi.unsupported.clear();
    forget_doors_region();
}
void release_windows(const char* why) {
    g_door_want = nullptr;
    if (!g_win_active && !g_eye.in_force && !g_io.cfg && g_oi.rooms.empty() && !g_oi.mask_edited && !doors_standing()) return;
    teardown_doors(why);
    teardown_inside_out(why);
    teardown_outside_in(why);
    release_cull_eye(why);
    g_sheet_fog_wanted = false;
    restore_sheet_fog();
    g_snow_push = 0;
    g_win_active = false;
}
// A door opens a room: the held rooms' colliders are back before the player is moved (SI: NotifyPortalUsed), and the
// room itself isn't switched off under them.
void windows_before_open(Instance& in) {
    for (HeldRoom& h : g_oi.rooms) give_back(h, h.cfg != in.cfg, "a door opened a room");
    teardown_outside_in("a door opened a room");
}
// The room the player stands in keeps its building's shell on while its windows show views (SI: KeepParkedShellOn).
// (also for the door views: the porch shows through the room's door)
bool shell_stays_on() { return (g_windows || g_door_portals) && wapi.ok && !g_unlit_failed; }
// A held room (for the views into it) isn't closed by the watchdog.
bool windows_hold(const Instance& in) {
    for (const HeldRoom& h : g_oi.rooms)
        if (h.borrowed && h.cfg == in.cfg) return true;
    return false;
}
int held_layer(const std::string& place) {
    for (const HeldRoom& h : g_oi.rooms)
        if (h.borrowed && h.cfg && place == h.cfg->id) return h.layer;
    return -1;
}

// Once a frame after vp_FPSCamera.LateUpdate (the camera and its zoom are final): what should stand, made so.
void tick_windows() {
    g_win_reconciled = g_win_frame;
    restore_sheet_fog();  // (a post-render missed: never left off)
    const bool on = api.ok && wapi.ok && (g_windows || g_door_portals) && engaged() && !g_unlit_failed && !g_region.empty();
    Instance* inside = (on && g_inside && in_room(g_inside) && live(g_inside->shell)) ? g_inside : nullptr;
    if (!on || !capture_main()) {
        release_windows(!on ? "the views are off" : "no camera");
        return;
    }
    doors_pick(inside, g_main.pos);  // first: the building whose door is looked through is held before the others
    // inside-out
    if (!inside || !g_windows || (g_io.cfg && g_io.cfg != inside->cfg))
        teardown_inside_out(!inside ? "the player is outside" : g_windows ? "another room" : "the window views are off");
    if (inside) {
        teardown_outside_in("the player went in");
        if (g_windows && !g_io.cfg && !setup_inside_out(*inside)) g_io.cfg = inside->cfg;  // (no windows: not scanned again)
        if (Il2CppObject* s = live(inside->shell); s && !active_self(s)) set_active(s, true);  // SI: KeepParkedShellOn
        const bool window_views = g_windows && !g_io.views.empty();
        if (window_views) {
            acquire_cull_eye();
            g_snow_push = nearest_glass_along_gaze();
            render_inside_out();
        }
        const int door_views = doors_render(inside, g_main.pos);
        if (door_views > 0 && !window_views) acquire_cull_eye();  // (the door view needs the trees too)
        g_sheet_fog_wanted = window_views || door_views > 0;
    } else {
        if (g_eye.in_force || g_eye.cam) release_cull_eye(g_inside ? "a room with no building of its own" : "the player is outside");
        g_sheet_fog_wanted = false;
        g_snow_push = 0;
        if (g_inside) {
            teardown_outside_in("the player is in a room");  // (a sub-interior: no views either way)
            doors_render(nullptr, g_main.pos);               // (only puts its own away)
        } else if (!g_fade.busy) {
            tick_outside_in(g_main.pos);
            doors_render(nullptr, g_main.pos);
        }
    }
    g_win_active = true;
}
// From GameManager.Update: the reconciler must have run lately, or everything of ours is taken down.
void watch_windows() {
    ++g_win_frame;
    g_win_frame_now = g_win_frame;
    if (!g_win_active) return;
    if (g_win_frame - g_win_reconciled > static_cast<uint64_t>(kStaleFrames)) release_windows("the camera stopped updating");
}

using FpsLateFn = void (*)(Il2CppObject* self, const MethodInfo* m);
FpsLateFn g_fps_late = nullptr;
void fps_late_detour(Il2CppObject* self, const MethodInfo* m) {
    g_fps_late(self, m);
    if (g_win_reconciled != g_win_frame) tick_windows();  // (once a frame)
}
using MenuFn = void (*)(const MethodInfo* m);
MenuFn g_exit_to_menu = nullptr, g_load_menu = nullptr;
void exit_to_menu_detour(const MethodInfo* m) {
    release_windows("to the main menu");
    g_exit_to_menu(m);
}
void load_menu_detour(const MethodInfo* m) {
    release_windows("to the main menu");
    g_load_menu(m);
}

void resolve_windows() {
    Il2CppClass* camera = il2::find_class(kCore, "UnityEngine", "Camera");
    Il2CppClass* rt = il2::find_class(kCore, "UnityEngine", "RenderTexture");
    Il2CppClass* shader = il2::find_class(kCore, "UnityEngine", "Shader");
    Il2CppClass* material = il2::find_class(kCore, "UnityEngine", "Material");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* renderer = il2::find_class(kCore, "UnityEngine", "Renderer");
    Il2CppClass* screen = il2::find_class(kCore, "UnityEngine", "Screen");
    Il2CppClass* settings = il2::find_class(kCore, "UnityEngine", "RenderSettings");
    Il2CppClass* sysinfo = il2::find_class(kCore, "UnityEngine", "SystemInfo");
    Il2CppClass* mesh = il2::find_class(kCore, "UnityEngine", "Mesh");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    if (!camera || !rt || !shader || !material || !go || !renderer || !screen || !mesh || !tf || !object) {
        logx::warn("interiors: windows: Unity's rendering classes are missing; no window views");
        return;
    }
    wapi.rt_class = rt;
    wapi.mat_class = material;
    wapi.cam_render = il2::find_method(camera, "Render", 0);
    wapi.cam_copy = il2::find_method_params(camera, "CopyFrom", {"UnityEngine.Camera"});
    wapi.cam_set_target = il2::find_method(camera, "set_targetTexture", 1);
    wapi.cam_set_depth = il2::find_method(camera, "set_depth", 1);
    wapi.cam_get_mask = il2::find_method(camera, "get_cullingMask", 0);
    wapi.cam_set_mask = il2::find_method(camera, "set_cullingMask", 1);
    wapi.cam_set_occlusion = il2::find_method(camera, "set_useOcclusionCulling", 1);
    wapi.cam_get_fov = il2::find_method(camera, "get_fieldOfView", 0);
    wapi.cam_set_fov = il2::find_method(camera, "set_fieldOfView", 1);
    wapi.cam_get_aspect = il2::find_method(camera, "get_aspect", 0);
    wapi.cam_set_aspect = il2::find_method(camera, "set_aspect", 1);
    wapi.cam_get_near = il2::find_method(camera, "get_nearClipPlane", 0);
    wapi.cam_set_near = il2::find_method(camera, "set_nearClipPlane", 1);
    wapi.cam_get_far = il2::find_method(camera, "get_farClipPlane", 0);
    wapi.cam_set_far = il2::find_method(camera, "set_farClipPlane", 1);
    wapi.cam_get_proj = il2::find_method(camera, "get_projectionMatrix", 0);
    wapi.cam_set_proj = il2::find_method(camera, "set_projectionMatrix", 1);
    wapi.cam_get_w2c = il2::find_method(camera, "get_worldToCameraMatrix", 0);
    wapi.cam_reset_w2c = il2::find_method(camera, "ResetWorldToCameraMatrix", 0);
    wapi.cam_reset_proj = il2::find_method(camera, "ResetProjectionMatrix", 0);
    wapi.cam_reset_cull = il2::find_method(camera, "ResetCullingMatrix", 0);
    wapi.cam_set_hdr = il2::find_method(camera, "set_allowHDR", 1);
    wapi.rt_ctor = il2::find_method_params(rt, ".ctor", {"System.Int32", "System.Int32", "System.Int32", "UnityEngine.RenderTextureFormat"});
    wapi.rt_create = il2::find_method(rt, "Create", 0);
    wapi.rt_release = il2::find_method(rt, "Release", 0);
    if (sysinfo) wapi.supports_format = il2::find_method(sysinfo, "SupportsRenderTextureFormat", 1);
    wapi.shader_find = il2::find_method_params(shader, "Find", {"System.String"});
    wapi.mat_ctor = il2::find_method_params(material, ".ctor", {"UnityEngine.Shader"});
    wapi.mat_set_queue = il2::find_method(material, "set_renderQueue", 1);
    wapi.mat_set_main = il2::find_method(material, "set_mainTexture", 1);
    wapi.mat_get_main = il2::find_method(material, "get_mainTexture", 0);
    wapi.create_primitive = il2::find_method(go, "CreatePrimitive", 1);
    wapi.r_set_shared = il2::find_method(renderer, "set_sharedMaterial", 1);
    wapi.r_set_shadows = il2::find_method(renderer, "set_shadowCastingMode", 1);
    wapi.r_set_receive = il2::find_method(renderer, "set_receiveShadows", 1);
    wapi.screen_w = il2::find_method(screen, "get_width", 0);
    wapi.screen_h = il2::find_method(screen, "get_height", 0);
    if (settings) {
        wapi.fog_get = il2::find_method(settings, "get_fog", 0);
        wapi.fog_set = il2::find_method(settings, "set_fog", 1);
    }
    wapi.mesh_readable = il2::find_method(mesh, "get_isReadable", 0);
    wapi.mesh_vertices = il2::find_method(mesh, "get_vertices", 0);
    wapi.mesh_triangles = il2::find_method_params(mesh, "GetTriangles", {"System.Int32"});
    wapi.mesh_submeshes = il2::find_method(mesh, "get_subMeshCount", 0);
    wapi.tf_to_local = il2::find_method(tf, "get_worldToLocalMatrix", 0);
    wapi.keep_on_load = il2::find_method(object, "DontDestroyOnLoad", 1);
    wapi.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    {  // RenderTextureFormat.RGB111110Float where supported (SI), else ARGBHalf; PrimitiveType.Quad
        int64_t v = 0;
        if (Il2CppClass* f = il2::find_class(kCore, "UnityEngine", "RenderTextureFormat")) {
            if (il2::enum_value(f, "ARGBHalf", v)) wapi.fmt_hdr = static_cast<int32_t>(v);
            int64_t small_float = 0;
            if (il2::enum_value(f, "RGB111110Float", small_float) && wapi.supports_format) {
                int32_t fv = static_cast<int32_t>(small_float);
                void* a[] = {&fv};
                bool ok = false;
                if (il2::invoke_value(wapi.supports_format, nullptr, ok, a) && ok) wapi.fmt_hdr = fv;
            }
        }
        if (Il2CppClass* p = il2::find_class(kCore, "UnityEngine", "PrimitiveType"))
            if (il2::enum_value(p, "Quad", v)) wapi.prim_quad = static_cast<int32_t>(v);
    }
    wapi.ok = wapi.cam_render && wapi.cam_copy && wapi.cam_set_target && wapi.cam_set_depth && wapi.cam_get_mask &&
              wapi.cam_set_mask && wapi.cam_get_fov && wapi.cam_set_fov && wapi.cam_set_aspect && wapi.cam_set_near &&
              wapi.cam_set_far && wapi.cam_get_far && wapi.cam_get_proj && wapi.cam_set_proj && wapi.cam_get_w2c &&
              wapi.rt_ctor && wapi.rt_create && wapi.shader_find && wapi.mat_ctor && wapi.mat_set_queue && wapi.mat_set_main &&
              wapi.create_primitive && wapi.r_set_shared && wapi.screen_w && wapi.screen_h && wapi.mesh_readable &&
              wapi.mesh_vertices && wapi.mesh_triangles && wapi.mesh_submeshes && wapi.tf_to_local && api.tf_to_world &&
              api.mesh_bounds && api.mesh_filter_mesh && api.r_material && api.r_materials && api.go_set_layer && api.go_layer &&
              api.tf_set_local_scale && api.set_name;
    wapi.fog_ok = wapi.fog_get && wapi.fog_set;
    // the cull eye: RenderObjectInstance's camera fields and list, the frustum calls, PositionIsOnscreen
    if (Il2CppClass* roi = il2::find_class(kGame, "", "RenderObjectInstance")) {
        wapi.roi_list = il2::il2cpp_class_get_field_from_name(roi, "s_RenderObjectInstanceList");
        wapi.roi_cam_tf = il2::field_offset(roi, "m_Camera");
        wapi.roi_cam = il2::field_offset(roi, "m_CameraComponent");
        wapi.roi_frustum_type = il2::field_offset_quiet(roi, "m_FrustumType");
    }
    wapi.cull_ok = wapi.roi_list && wapi.roi_cam_tf >= 0 && wapi.roi_cam >= 0;
    // the snow box: WeatherParticleManager.m_Common (CommonVars: m_CameraMat, m_CameraInvMat, m_CamPos)
    Il2CppClass* wpm = il2::find_class(kGame, "TLD.WeatherParticle", "WeatherParticleManager");
    Il2CppClass* common = il2::find_class(kGame, "TLD.WeatherParticle", "CommonVars");
    if (wpm && common) {
        const ptrdiff_t c = il2::field_offset(wpm, "m_Common");
        const ptrdiff_t mat = il2::field_offset(common, "m_CameraMat"), inv = il2::field_offset(common, "m_CameraInvMat"),
                        pos = il2::field_offset(common, "m_CamPos");
        if (c >= 0 && mat >= 0x10 && inv >= 0x10 && pos >= 0x10) {  // (a struct's field offsets count its box's header)
            wapi.snow_cam_mat = c + mat - 0x10;
            wapi.snow_cam_inv = c + inv - 0x10;
            wapi.snow_cam_pos = c + pos - 0x10;
            wapi.snow_ok = true;
        }
    }
    if (!wapi.ok) {
        logx::warn("interiors: windows: some of Unity's camera/texture calls are missing; no window views");
        return;
    }
    // --- hooks ---
    if (Il2CppClass* fps = il2::find_class(kGame, "", "vp_FPSCamera"))
        hook("vp_FPSCamera.LateUpdate", il2::find_method(fps, "LateUpdate", 0), &fps_late_detour, &g_fps_late);
    if (!g_fps_late) {
        logx::warn("interiors: windows: vp_FPSCamera.LateUpdate can't be hooked; no window views");
        wapi.ok = false;
        return;
    }
    if (Il2CppClass* gm = il2::find_class(kGame, "", "GameManager")) {
        hook("GameManager.DoExitToMainMenu", il2::find_method(gm, "DoExitToMainMenu", 0), &exit_to_menu_detour, &g_exit_to_menu);
        hook("GameManager.LoadMainMenu", il2::find_method(gm, "LoadMainMenu", 0), &load_menu_detour, &g_load_menu);
    }
    if (wapi.cull_ok)
        if (Il2CppClass* mcf = il2::find_class(kGame, "", "MainCameraFrustrum")) {
            hook("MainCameraFrustrum.RecalculateFrustumPlanes(Camera)",
                 il2::find_method_params(mcf, "RecalculateFrustumPlanes", {"UnityEngine.Camera"}), &frustum_planes1_detour,
                 &g_frustum_planes1);
            hook("MainCameraFrustrum.RecalculateFrustumPlanes(Camera, Plane[]&, float)",
                 il2::find_method(mcf, "RecalculateFrustumPlanes", 3), &frustum_planes3_detour, &g_frustum_planes3);
            hook("MainCameraFrustrum.RecalculateFrustumCorners", il2::find_method(mcf, "RecalculateFrustumCorners", 3),
                 &frustum_corners_detour, &g_frustum_corners);
        }
    if (wapi.cull_ok)
        if (Il2CppClass* utils = il2::find_class(kGame, "", "Utils"))
            hook("Utils.PositionIsOnscreen", il2::find_method_params(utils, "PositionIsOnscreen", {"UnityEngine.Vector3", "System.Single"}),
                 &onscreen_detour, &g_onscreen);
    if (wapi.snow_ok && wpm) {
        hook("WeatherParticleManager.ScheduleFallingSnowParticleJobs", il2::find_method(wpm, "ScheduleFallingSnowParticleJobs", 0),
             &falling_jobs_detour, &g_falling_jobs);
        hook("WeatherParticleManager.ScheduleBlowingSnowParticleJobs", il2::find_method(wpm, "ScheduleBlowingSnowParticleJobs", 0),
             &blowing_jobs_detour, &g_blowing_jobs);
    }
    if (wapi.fog_ok) {
        hook("Camera.FireOnPreCull", il2::find_method(camera, "FireOnPreCull", 1), &fire_pre_cull_detour, &g_fire_pre_cull);
        hook("Camera.FireOnPostRender", il2::find_method(camera, "FireOnPostRender", 1), &fire_post_render_detour, &g_fire_post_render);
        wapi.fog_ok = g_fire_pre_cull && g_fire_post_render;
    }
    if (Il2CppClass* utils = il2::find_class(kGame, "", "Utils"))  // TLDCoop_DLC: the indoor look
        hook("Utils.SetAmbientLightScaled", il2::find_method(utils, "SetAmbientLightScaled", 2), &set_ambient_detour, &g_set_ambient);
    if (Il2CppClass* ilm = il2::find_class(kGame, "", "InteriorLightingManager"))
        hook("InteriorLightingManager.Update", il2::find_method(ilm, "Update", 0), &ilm_update_detour, &g_ilm_update);
    resolve_fog_pass();
    resolve_doors();
    logx::info("interiors: windows: ready (stand-in eye %s, snow box %s, sheet fog %s; %zu building(s) with offline window data, "
               "%zu hand row(s))",
               wapi.cull_ok && g_frustum_planes1 ? "yes" : "NO", wapi.snow_ok && g_falling_jobs ? "yes" : "NO",
               wapi.fog_ok ? "yes" : "NO", sizeof(kShellSpots) / sizeof(kShellSpots[0]), sizeof(kWinRows) / sizeof(kWinRows[0]));
}
