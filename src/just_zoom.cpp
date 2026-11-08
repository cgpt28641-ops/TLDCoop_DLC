#include "just_zoom.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "coop_chat.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/emotes.h"
#include "paths.h"

namespace just_zoom {
namespace {

constexpr float kPitchMin = -85, kPitchMax = 85;
constexpr float kZoomMin = 1.1f, kZoomMax = 20;
constexpr float kRad = 3.14159265358979f / 180.0f;

struct Vec2 { float x, y; };

struct Settings {
    bool enabled = true;
    int key = 'Z';
    int wheel_step = 15, speed = 12, smooth = 25;
} cfg;

struct Api {
    bool ok = false;
    const MethodInfo* get_camera = nullptr;          // static GameManager.GetVpFPSCamera()
    ptrdiff_t cam_camera = -1;                        // vp_FPSCamera.m_Camera (UnityEngine.Camera)
    ptrdiff_t render_fov = -1, mouse_sens = -1;       // m_RenderingFieldOfView, m_MouseSensitivity
    ptrdiff_t yaw = -1, pitch = -1, cur_yaw = -1, target_yaw = -1, cur_pitch = -1, target_pitch = -1;
    const MethodInfo* get_sens = nullptr, *set_sens = nullptr;    // MouseSensitivity
    const MethodInfo* get_angle = nullptr, *set_angle = nullptr;  // Angle (Vector2: yaw, pitch)
    const MethodInfo* get_fov = nullptr, *set_fov = nullptr;      // Camera.fieldOfView
    const MethodInfo* scroll = nullptr;                           // static Input.mouseScrollDelta
    const MethodInfo* delta_time = nullptr;                       // static Time.deltaTime
} api;

// JustZoom's state
float zoom = 1, zoom_target = 2.5f, base_fov = 60, base_sens = 0;
bool has_base_sens = false, need_sync = true;
float target_yaw = 0, target_pitch = 0, smooth_yaw = 0, smooth_pitch = 0;
Vec2 last_written{};
bool logged_first = false;

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) { return *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off); }
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) { *reinterpret_cast<T*>(reinterpret_cast<char*>(o) + off) = v; }

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float delta_angle(float a, float b) {  // Mathf.DeltaAngle
    float d = std::fmod(b - a, 360.0f);
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    return d;
}
float repeat360(float v) { return v - std::floor(v / 360.0f) * 360.0f; }

bool game_in_front() {
    DWORD pid = 0;
    if (HWND w = GetForegroundWindow()) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

void load_settings() {
    const auto ini = paths::instance_dir() / L"coop.ini";
    cfg.enabled = GetPrivateProfileIntW(L"zoom", L"enabled", 1, ini.c_str()) != 0;
    wchar_t v[32];
    GetPrivateProfileStringW(L"zoom", L"key", L"Z", v, 32, ini.c_str());
    std::string key;
    for (const wchar_t* p = v; *p; ++p) key += static_cast<char>(*p);
    cfg.key = net::virtual_key(key);
    if (!cfg.key) cfg.key = 'Z';
    cfg.wheel_step = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"zoom", L"wheel_step", 15, ini.c_str())), 5, 50);
    cfg.speed = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"zoom", L"speed", 12, ini.c_str())), 1, 30);
    cfg.smooth = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"zoom", L"smooth", 25, ini.c_str())), 1, 40);
}

float sensitivity(Il2CppObject* cam) {
    float s = 0;
    if (api.get_sens) il2::invoke_value(api.get_sens, cam, s);
    return s;
}
void set_sensitivity(Il2CppObject* cam, float value) {
    void* a[] = {&value};
    if (api.set_sens) il2::invoke(api.set_sens, cam, a);
    if (api.mouse_sens >= 0) put<float>(cam, api.mouse_sens, value);
}
void set_fov(Il2CppObject* cam, Il2CppObject* camera, float fov) {
    void* a[] = {&fov};
    il2::invoke(api.set_fov, camera, a);
    if (api.render_fov >= 0) put<float>(cam, api.render_fov, fov);
}
void set_all_angles(Il2CppObject* cam, float y, float p) {
    Vec2 v{y, p};
    void* a[] = {&v};
    il2::invoke(api.set_angle, cam, a);
    for (ptrdiff_t off : {api.yaw, api.cur_yaw, api.target_yaw})
        if (off >= 0) put<float>(cam, off, y);
    for (ptrdiff_t off : {api.pitch, api.cur_pitch, api.target_pitch})
        if (off >= 0) put<float>(cam, off, p);
}
void sync(const Vec2& a) {
    target_yaw = smooth_yaw = a.x;
    target_pitch = smooth_pitch = a.y;
    last_written = a;
    need_sync = false;
}

void update_zoom(Il2CppObject* cam, Il2CppObject* camera, bool hold, float dt) {
    // TLDCoop_DLC (logs 08.11: "Camera.get_fieldOfView threw NullReferenceException" while a game loaded): a camera
    // already destroyed (its native part gone) isn't touched
    if (!camera || !*reinterpret_cast<void**>(reinterpret_cast<char*>(camera) + 0x10)) return;
    if (!hold && zoom <= 1.005f) {
        if (zoom != 1) {
            zoom = 1;
            set_fov(cam, camera, base_fov);
            if (has_base_sens) set_sensitivity(cam, base_sens);
        } else {
            il2::invoke_value(api.get_fov, camera, base_fov);
            base_sens = sensitivity(cam);
            has_base_sens = true;
        }
        return;
    }
    if (hold && api.scroll) {
        Vec2 s{};
        il2::invoke_value(api.scroll, nullptr, s);
        if (s.y != 0) {
            const float step = 1 + cfg.wheel_step / 100.0f;
            zoom_target = std::clamp(zoom_target * std::pow(step, s.y), kZoomMin, kZoomMax);
        }
    }
    const float target = hold ? zoom_target : 1;
    zoom = lerp(zoom, target, 1 - std::exp(-cfg.speed * dt));
    const float half = base_fov * 0.5f * kRad;
    set_fov(cam, camera, 2 * std::atan(std::tan(half) / zoom) / kRad);
    if (zoom <= 1.01f) {  // (JustZoom: !IsZoomed) the sensitivity follows back as it zooms out
        if (!has_base_sens) {
            base_sens = sensitivity(cam);
            has_base_sens = true;
        }
        set_sensitivity(cam, base_sens / std::max(1.0f, zoom));
    }
    if (!logged_first) {
        logged_first = true;
        logx::info("zoom: zooming (first time this session; base field of view %.0f)", base_fov);
    }
}

void smooth_look(Il2CppObject* cam, float dt) {
    Vec2 a{};
    if (!il2::invoke_value(api.get_angle, cam, a)) return;
    if (need_sync) {
        sync(a);
        return;
    }
    float d_yaw = delta_angle(last_written.x, a.x), d_pitch = a.y - last_written.y;
    if (std::fabs(d_yaw) > 90 || std::fabs(d_pitch) > 90) {
        sync(a);
        return;
    }
    const float z = std::max(1.0f, zoom);
    target_yaw += d_yaw / z;
    target_pitch = std::clamp(target_pitch + d_pitch / z, kPitchMin, kPitchMax);
    const float k = 1 - std::exp(-(41 - cfg.smooth) * dt);
    smooth_yaw = lerp(smooth_yaw, target_yaw, k);
    smooth_pitch = lerp(smooth_pitch, target_pitch, k);
    const float y = repeat360(smooth_yaw), p = std::clamp(smooth_pitch, kPitchMin, kPitchMax);
    set_all_angles(cam, y, p);
    last_written = {y, p};
}

}  // namespace

void init() {
    load_settings();
    Il2CppClass* gm = il2::find_class("Assembly-CSharp.dll", "", "GameManager");
    Il2CppClass* fps = il2::find_class("Assembly-CSharp.dll", "", "vp_FPSCamera");
    Il2CppClass* camera = il2::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    Il2CppClass* input = il2::find_class("UnityEngine.InputLegacyModule.dll", "UnityEngine", "Input");
    Il2CppClass* time = il2::find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Time");
    if (gm) api.get_camera = il2::find_method_quiet(gm, "GetVpFPSCamera", 0);
    if (fps) {
        api.cam_camera = il2::field_offset_quiet(fps, "m_Camera");
        api.render_fov = il2::field_offset_quiet(fps, "m_RenderingFieldOfView");
        api.mouse_sens = il2::field_offset_quiet(fps, "m_MouseSensitivity");
        api.yaw = il2::field_offset_quiet(fps, "m_Yaw");
        api.pitch = il2::field_offset_quiet(fps, "m_Pitch");
        api.cur_yaw = il2::field_offset_quiet(fps, "m_CurrentYaw");
        api.target_yaw = il2::field_offset_quiet(fps, "m_TargetYaw");
        api.cur_pitch = il2::field_offset_quiet(fps, "m_CurrentPitch");
        api.target_pitch = il2::field_offset_quiet(fps, "m_TargetPitch");
        api.get_sens = il2::find_method_quiet(fps, "get_MouseSensitivity", 0);
        api.set_sens = il2::find_method_quiet(fps, "set_MouseSensitivity", 1);
        api.get_angle = il2::find_method_quiet(fps, "get_Angle", 0);
        api.set_angle = il2::find_method_quiet(fps, "set_Angle", 1);
    }
    if (camera) {
        api.get_fov = il2::find_method_quiet(camera, "get_fieldOfView", 0);
        api.set_fov = il2::find_method_quiet(camera, "set_fieldOfView", 1);
    }
    if (input) api.scroll = il2::find_method_quiet(input, "get_mouseScrollDelta", 0);
    if (time) api.delta_time = il2::find_method_quiet(time, "get_deltaTime", 0);
    api.ok = api.get_camera && api.cam_camera >= 0 && api.get_angle && api.set_angle && api.get_fov && api.set_fov &&
             api.delta_time;
    logx::info("zoom: %s", !cfg.enabled ? "off (coop.ini [zoom] enabled=0)"
                           : api.ok ? "ready (hold the zoom key, the mouse wheel zooms more or less)"
                                    : "unavailable (TLD's calls missing)");
}

void reset() {
    zoom = 1;
    has_base_sens = false;
    need_sync = true;
}

void late_update(bool in_game) {
    if (!cfg.enabled || !api.ok) return;
    Il2CppObject* cam = in_game ? il2::invoke(api.get_camera, nullptr) : nullptr;
    Il2CppObject* camera = cam ? get<Il2CppObject*>(cam, api.cam_camera) : nullptr;
    if (!cam || !camera) {
        zoom = 1;
        return;
    }
    // TLDCoop_DLC: not while a chat line is typed (Z is a letter of it)
    const bool hold = game_in_front() && !coop_chat::typing() && (GetAsyncKeyState(cfg.key) & 0x8000) != 0;
    float dt = 0;
    il2::invoke_value(api.delta_time, nullptr, dt);
    update_zoom(cam, camera, hold, dt);
    if (zoom > 1.01f)
        smooth_look(cam, dt);
    else
        need_sync = true;
}

}  // namespace just_zoom
