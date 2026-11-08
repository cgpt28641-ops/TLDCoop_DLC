#include "coop_train.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"

namespace coop_train {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;  // UnityEngine.Object.m_CachedPtr: null once destroyed
constexpr int kNobody = -1;
constexpr double kSendMs = 66;          // a frame while we drive or ride
constexpr double kIdleSendMs = 3000;    // the host's train while nobody drives
constexpr double kOwnerLostMs = 2500;   // no frame from the driver this long: nobody drives
constexpr double kRiderLostMs = 1200;
constexpr float kSnapMetres = 4.0f;     // further off than this: put there at once

enum Flags : uint8_t { kOwner = 1, kInside = 2, kRelease = 4, kIdle = 8 };

struct V3 {
    float x, y, z;
};

struct Api {
    Il2CppClass* train_class = nullptr;
    Il2CppObject* train_type = nullptr;
    Il2CppObject* track_type = nullptr;
    const MethodInfo* active_train = nullptr;   // static SteamTrain.get_ActiveSteamTrain()
    const MethodInfo* inside = nullptr;         // static SteamTrain.get_IsInsideSteamTrain()
    const MethodInfo* engine = nullptr;         // SteamTrain.get_Engine()
    const MethodInfo* update_controls = nullptr;  // SteamTrain.UpdateControls()
    const MethodInfo* update_engine = nullptr;  // SteamTrainEngine.UpdateEngine()
    const MethodInfo* engine_state = nullptr;   // SteamTrainEngine.get_CurrentState()
    const MethodInfo* guid = nullptr;           // TrainCar.get_GUID()
    const MethodInfo* controller = nullptr;     // TrainCar.get_SplineController()
    const MethodInfo* get_spline = nullptr;     // SplineController.get_Spline()
    const MethodInfo* set_spline = nullptr;     // SplineController.set_Spline(CurvySpline)
    const MethodInfo* get_abs = nullptr;        // CurvyController.get_AbsolutePosition()
    const MethodInfo* set_abs = nullptr;        // CurvyController.set_AbsolutePosition(float)
    const MethodInfo* get_dir = nullptr;        // CurvyController.get_MovementDirection()
    const MethodInfo* set_dir = nullptr;        // CurvyController.set_MovementDirection(MovementDirection)
    const MethodInfo* track_guid = nullptr;     // SteamTrainTrackSpline.get_GUID()
    const MethodInfo* track_spline = nullptr;   // SteamTrainTrackSpline.get_TrackSpline()
    const MethodInfo* game_object = nullptr;    // Component.get_gameObject()
    const MethodInfo* transform = nullptr;      // Component.get_transform()
    const MethodInfo* get_component = nullptr;  // GameObject.GetComponent(Type)
    const MethodInfo* find_objects = nullptr;   // static Object.FindObjectsOfType(Type)
    const MethodInfo* player_tf = nullptr;      // static GameManager.GetPlayerTransform()
    const MethodInfo* position = nullptr;       // Transform.get_position
    const MethodInfo* euler = nullptr;          // Transform.get_eulerAngles
    const MethodInfo* to_local = nullptr;       // Transform.InverseTransformPoint(Vector3)
    const MethodInfo* to_world = nullptr;       // Transform.TransformPoint(Vector3)
    ptrdiff_t fuel_source = -1, water_source = -1;  // SteamTrain.m_FuelSource, m_WaterSource (GenericLiquidContainer)
    ptrdiff_t liquid_volume = -1;               // GenericLiquidContainer.m_CurrentVolume (ItemLiquidVolume: int64)
    std::array<const MethodInfo*, 4> part_get{};  // SteamTrain.get_BrakeShoe, get_Boiler, get_CowCatcher, get_Wheels
    const MethodInfo* serialize_component = nullptr;    // SteamTrainComponent.SerializeComponent()
    const MethodInfo* deserialize_component = nullptr;  // SteamTrainComponent.DeserializeComponent(state)
    const MethodInfo* boiler_on = nullptr;      // SteamTrainBoiler.get_BoilerIsOn()
    ptrdiff_t cs_condition = -1, cs_data = -1;  // SteamTrainComponentState.m_Condition, m_ComponentDataSerialized
    FieldInfo* track_elements = nullptr;        // static TrackElementManager.s_TrackElements (List<ITrackElement>)
    ptrdiff_t list_items = -1, list_size = -1;  // List<T>._items, _size
    ptrdiff_t st_velocity = -1, st_sign = -1, st_pressure = -1, st_throttle = -1, st_brakes = -1;  // EngineState
} api;

bool g_ready = false;

bool alive(Il2CppObject* o) {
    return o && *reinterpret_cast<void**>(reinterpret_cast<char*>(o) + kCachedPtr) != nullptr;
}

template <typename T>
T get(Il2CppObject* o, ptrdiff_t off) {
    T v{};
    if (o && off >= 0) memcpy(&v, reinterpret_cast<char*>(o) + off, sizeof(T));
    return v;
}
template <typename T>
void put(Il2CppObject* o, ptrdiff_t off, T v) {
    if (o && off >= 0) memcpy(reinterpret_cast<char*>(o) + off, &v, sizeof(T));
}

double now_ms() {
    static const double per_ms = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / per_ms;
}

std::string text(Il2CppString* s) { return s ? il2::to_utf8(s) : std::string(); }

// --- what a frame says ----------------------------------------------------------------------------------------------

struct Frame {
    uint8_t from = 0, flags = 0;
    std::string scene, guid, spline;
    float pos = 0;
    uint8_t dir = 0;
    float velocity = 0, sign = 1, pressure = 0;
    int32_t throttle = 0;
    uint8_t brakes = 0;
    int64_t fuel = -1, water = -1;
    V3 local{};
    float local_yaw = 0;
    double at = 0;  // when it came (ours)
};

void write(net::Writer& w, const Frame& f) {
    w.put<uint8_t>(f.from);
    w.put<uint8_t>(f.flags);
    w.put_blob(f.scene, 64);
    w.put_blob(f.guid, 64);
    w.put_blob(f.spline, 64);
    w.put<float>(f.pos);
    w.put<uint8_t>(f.dir);
    w.put<float>(f.velocity);
    w.put<float>(f.sign);
    w.put<float>(f.pressure);
    w.put<int32_t>(f.throttle);
    w.put<uint8_t>(f.brakes);
    w.put<int64_t>(f.fuel);
    w.put<int64_t>(f.water);
    w.put<float>(f.local.x);
    w.put<float>(f.local.y);
    w.put<float>(f.local.z);
    w.put<float>(f.local_yaw);
}

bool read(net::Reader& r, Frame& f) {
    f.from = r.get<uint8_t>();
    f.flags = r.get<uint8_t>();
    f.scene = r.get_blob(64);
    f.guid = r.get_blob(64);
    f.spline = r.get_blob(64);
    f.pos = r.get<float>();
    f.dir = r.get<uint8_t>();
    f.velocity = r.get<float>();
    f.sign = r.get<float>();
    f.pressure = r.get<float>();
    f.throttle = r.get<int32_t>();
    f.brakes = r.get<uint8_t>();
    f.fuel = r.get<int64_t>();
    f.water = r.get<int64_t>();
    f.local.x = r.get<float>();
    f.local.y = r.get<float>();
    f.local.z = r.get<float>();
    f.local_yaw = r.get<float>();
    return r.ok() && f.from < net::kMaxPlayers;
}

// --- state ----------------------------------------------------------------------------------------------------------

std::string g_scene;
bool g_in_game = false;
uint8_t g_self = 0;
Il2CppObject* g_train = nullptr;   // ours in this scene (gchandle-less: checked with alive() each frame)
std::string g_guid;
double g_next_find = 0;
int g_owner = kNobody;             // who drives the train in our scene
double g_owner_seen = 0;
Frame g_last;                      // the driver's newest frame
double g_last_rate_pos = 0;        // for the speed along the spline the frames show
float g_rate = 0;                  // metres a second along the spline (the frames' own, signed)
bool g_was_inside = false;
double g_next_send = 0;
std::array<Frame, net::kMaxPlayers> g_riders{};  // anyone inside: where they stand in the cab
std::array<bool, net::kMaxPlayers> g_riding{};
Il2CppObject* g_spline_seen = nullptr;  // the spline whose GUID we last looked up
std::string g_spline_guid;
int g_snaps = 0, g_switches = 0;
Frame g_pending_idle;                    // a release or idle frame for a train not (yet) found here
bool g_has_pending = false;

bool hosting() {
    const auto role = netrt::role();
    return role && *role == net::Role::Host;
}

Il2CppObject* find_train() {
    Il2CppObject* t = api.active_train ? il2::invoke(api.active_train, nullptr) : nullptr;
    if (alive(t)) return t;
    const double now = now_ms();
    if (now < g_next_find || !api.find_objects || !api.train_type) return nullptr;
    g_next_find = now + 2000;
    void* args[] = {api.train_type};
    Il2CppObject* arr = il2::invoke(api.find_objects, nullptr, args);
    if (!arr || il2::array_length(arr) == 0) return nullptr;
    return il2::array_object_at(arr, 0);
}

Il2CppObject* controller() { return g_train ? il2::invoke(api.controller, g_train) : nullptr; }

std::string spline_guid(Il2CppObject* ctl) {
    Il2CppObject* spline = ctl ? il2::invoke(api.get_spline, ctl) : nullptr;
    if (!alive(spline)) return {};
    if (spline == g_spline_seen) return g_spline_guid;
    g_spline_seen = spline;
    g_spline_guid.clear();
    Il2CppObject* go = il2::invoke(api.game_object, spline);
    void* args[] = {api.track_type};
    Il2CppObject* track = go ? il2::invoke(api.get_component, go, args) : nullptr;
    if (alive(track)) g_spline_guid = text(reinterpret_cast<Il2CppString*>(il2::invoke(api.track_guid, track)));
    return g_spline_guid;
}

Il2CppObject* spline_by_guid(const std::string& guid) {
    if (guid.empty() || !api.find_objects || !api.track_type) return nullptr;
    void* args[] = {api.track_type};
    Il2CppObject* arr = il2::invoke(api.find_objects, nullptr, args);
    for (size_t i = 0, n = arr ? il2::array_length(arr) : 0; i < n; ++i) {
        Il2CppObject* t = il2::array_object_at(arr, i);
        if (alive(t) && text(reinterpret_cast<Il2CppString*>(il2::invoke(api.track_guid, t))) == guid)
            return il2::invoke(api.track_spline, t);
    }
    return nullptr;
}

Il2CppObject* engine_state() {
    Il2CppObject* engine = g_train ? il2::invoke(api.engine, g_train) : nullptr;
    return alive(engine) ? il2::invoke(api.engine_state, engine) : nullptr;
}

Il2CppObject* liquid(ptrdiff_t field) {
    Il2CppObject* c = get<Il2CppObject*>(g_train, field);
    return alive(c) ? c : nullptr;
}

bool inside_train() {
    bool in = false;
    return api.inside && il2::invoke_value(api.inside, nullptr, in) && in;
}

float yaw_of(Il2CppObject* tf) {
    V3 e{};
    il2::invoke_value(api.euler, tf, e);
    return e.y;
}

Il2CppObject* train_tf() { return g_train ? il2::invoke(api.transform, g_train) : nullptr; }

// Our train as a frame.
Frame snapshot(uint8_t flags) {
    Frame f;
    f.from = g_self;
    f.flags = flags;
    f.scene = g_scene;
    f.guid = g_guid;
    Il2CppObject* ctl = controller();
    if (ctl) {
        f.spline = spline_guid(ctl);
        il2::invoke_value(api.get_abs, ctl, f.pos);
        int32_t dir = 0;
        il2::invoke_value(api.get_dir, ctl, dir);
        f.dir = static_cast<uint8_t>(dir);
    }
    if (Il2CppObject* st = engine_state()) {
        f.velocity = get<float>(st, api.st_velocity);
        f.sign = get<float>(st, api.st_sign);
        f.pressure = get<float>(st, api.st_pressure);
        f.throttle = get<int32_t>(st, api.st_throttle);
        f.brakes = get<bool>(st, api.st_brakes) ? 1 : 0;
    }
    if (Il2CppObject* c = liquid(api.fuel_source)) f.fuel = get<int64_t>(c, api.liquid_volume);
    if (Il2CppObject* c = liquid(api.water_source)) f.water = get<int64_t>(c, api.liquid_volume);
    if (flags & kInside) {
        Il2CppObject* me = il2::invoke(api.player_tf, nullptr);
        Il2CppObject* tf = train_tf();
        V3 p{};
        if (me && tf && il2::invoke_value(api.position, me, p)) {
            void* a[] = {&p};
            il2::invoke_value(api.to_local, tf, f.local, a);
            f.local_yaw = yaw_of(me) - yaw_of(tf);
        }
    }
    return f;
}

void send(const Frame& f, bool reliable) {
    net::Writer w(net::Msg::Train, 320);
    write(w, f);
    netrt::send_packet(w, reliable);
}

bool driven_by_other() { return g_owner != kNobody && g_owner != g_self; }

// The driver's train on ours: the same spline, place and direction, the engine as theirs.
void follow(double now, float dt) {
    Il2CppObject* ctl = controller();
    if (!ctl || g_last.guid != g_guid) return;
    if (!g_last.spline.empty() && spline_guid(ctl) != g_last.spline) {
        Il2CppObject* s = spline_by_guid(g_last.spline);
        if (alive(s)) {
            void* a[] = {s};
            il2::invoke(api.set_spline, ctl, a);
            g_spline_seen = nullptr;
            if (g_switches++ < 40) logx::info("train: onto the driver's track %s", g_last.spline.c_str());
        }
    }
    int32_t dir = g_last.dir;
    void* da[] = {&dir};
    il2::invoke(api.set_dir, ctl, da);
    const float ahead = g_rate * static_cast<float>(std::min(now - g_last.at, 500.0) / 1000.0);
    float want = g_last.pos + ahead, have = 0;
    il2::invoke_value(api.get_abs, ctl, have);
    const float err = want - have;
    if (std::fabs(err) > kSnapMetres) {
        if (g_snaps++ < 40) logx::info("train: %.1f m off the driver's: put there", err);
    } else {
        want = have + err * std::min(1.0f, dt * 4.0f);
    }
    void* pa[] = {&want};
    il2::invoke(api.set_abs, ctl, pa);
}

void apply_engine(Il2CppObject* st) {
    if (!st) return;
    put<float>(st, api.st_velocity, g_last.velocity);
    put<float>(st, api.st_sign, g_last.sign);
    put<float>(st, api.st_pressure, g_last.pressure);
    put<int32_t>(st, api.st_throttle, g_last.throttle);
    put<bool>(st, api.st_brakes, g_last.brakes != 0);
}

void apply_liquids(const Frame& f) {
    if (Il2CppObject* c = liquid(api.fuel_source); c && f.fuel >= 0) put<int64_t>(c, api.liquid_volume, f.fuel);
    if (Il2CppObject* c = liquid(api.water_source); c && f.water >= 0) put<int64_t>(c, api.liquid_volume, f.water);
}

// A train standing where nobody drives it: put ours there if it's more than a metre off.
void place(const Frame& f) {
    Il2CppObject* ctl = controller();
    if (!ctl || f.guid != g_guid) return;
    float have = 0;
    il2::invoke_value(api.get_abs, ctl, have);
    const bool other_spline = !f.spline.empty() && spline_guid(ctl) != f.spline;
    if (!other_spline && std::fabs(have - f.pos) < 1.0f) return;
    if (other_spline) {
        Il2CppObject* s = spline_by_guid(f.spline);
        if (!alive(s)) return;
        void* a[] = {s};
        il2::invoke(api.set_spline, ctl, a);
        g_spline_seen = nullptr;
    }
    int32_t dir = f.dir;
    void* da[] = {&dir};
    il2::invoke(api.set_dir, ctl, da);
    float pos = f.pos;
    void* pa[] = {&pos};
    il2::invoke(api.set_abs, ctl, pa);
    if (Il2CppObject* st = engine_state()) {
        put<float>(st, api.st_velocity, 0.0f);
        put<int32_t>(st, api.st_throttle, 0);
    }
    logx::info("train: put where %s left it (%.1f m along%s)", f.flags & kIdle ? "the host has it" : "its driver",
               f.pos, other_spline ? ", another track" : "");
}

void receive(double now) {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Train, in);
    const bool host = hosting();
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        Frame f;
        if (!read(r, f)) continue;
        if (host) {  // pass it on to the others
            net::Writer w(net::Msg::Train, 320);
            write(w, f);
            netrt::send_packet_except(p.from, w, p.reliable);
            f.from = p.from;  // (a friend can only speak for themselves)
        }
        if (f.from == g_self) continue;
        f.at = now;
        g_riders[f.from] = f;
        g_riding[f.from] = (f.flags & kInside) && !(f.flags & kRelease);
        if (f.scene != g_scene) continue;
        if (f.flags & kOwner) {
            if (g_owner == g_self && f.from > g_self) continue;  // both took it: the lower id drives
            if (g_owner == g_self) logx::info("train: player %u drives as well; theirs wins (lower id)", f.from);
            if (g_owner != f.from) logx::info("train: player %u drives the train", f.from);
            g_owner = f.from;
            g_owner_seen = now;
            if (g_last.at > 0 && g_last.from == f.from && g_last.spline == f.spline && now > g_last.at)
                g_rate = (f.pos - g_last.pos) / static_cast<float>(std::max(now - g_last.at, 10.0) / 1000.0);
            else
                g_rate = 0;
            g_last = f;
            apply_liquids(f);
        } else if (f.flags & (kRelease | kIdle)) {
            if (f.flags & kRelease && g_owner == f.from) {
                logx::info("train: player %u left the cab: nobody drives", f.from);
                g_owner = kNobody;
                g_rate = 0;
            }
            if (g_owner != kNobody) continue;
            if (g_train) place(f);
            else g_pending_idle = f, g_has_pending = true;
        }
    }
}


// --- the train's parts and the track (item 10, step 2) --------------------------------------------------------------
// The engine's parts (brake shoe, boiler, cow catcher, wheels: their condition, the boiler lit or not and its heat) and
// the tanks. The one in charge (the driver; nobody driving: the host) sends them when they change and every 5 s; the
// others put theirs so. Someone else's own doing (a repair, lighting the firebox, filling a tank) goes to the one in
// charge at once. The track (switches, turntables, water and oil towers: TrackElementManager's list) is everyone's:
// a change goes out within a second, and the host sends the whole list every 15 s.

constexpr int kParts = 4;
const char* const kPartNames[kParts] = {"brake shoe", "boiler", "cow catcher", "wheels"};
constexpr int64_t kLiquidStep = 50000000;  // 0.05 l (ItemLiquidVolume: 1e-9 l): a refill, not a tank's drain

struct Part {
    bool valid = false;
    float condition = 0;
    bool on = false;
    std::string data;
};
std::array<Part, kParts> g_known{};
int64_t g_fuel_known = -1, g_water_known = -1;
double g_next_parts = 0, g_parts_full_at = 0;
std::string g_parts_guid;
int g_parts_logged = 0;

bool in_charge() { return g_owner == g_self || (g_owner == kNobody && hosting()); }

Il2CppObject* part_of(int i) {
    if (!g_train || !api.part_get[i]) return nullptr;
    Il2CppObject* c = il2::invoke(api.part_get[i], g_train);
    return alive(c) ? c : nullptr;
}

Part read_part(int i) {
    Part p;
    Il2CppObject* c = part_of(i);
    Il2CppObject* st = c ? il2::invoke(api.serialize_component, c) : nullptr;
    if (!st) return p;
    p.valid = true;
    p.condition = get<float>(st, api.cs_condition);
    p.data = text(get<Il2CppString*>(st, api.cs_data));
    if (i == 1 && api.boiler_on) il2::invoke_value(api.boiler_on, c, p.on);
    return p;
}

void write_part(int i, const Part& p) {
    Il2CppObject* c = part_of(i);
    Il2CppObject* st = c ? il2::invoke(api.serialize_component, c) : nullptr;  // a state to fill in
    if (!st) return;
    put<float>(st, api.cs_condition, p.condition);
    il2::store_ref(st, api.cs_data, p.data.empty() ? nullptr : il2::il2cpp_string_new(p.data.c_str()));
    void* a[] = {st};
    il2::invoke(api.deserialize_component, c, a);
}

int64_t tank(ptrdiff_t field) {
    Il2CppObject* c = liquid(field);
    return c ? get<int64_t>(c, api.liquid_volume) : -1;
}

void send_parts(const std::array<Part, kParts>& parts, int64_t fuel, int64_t water, bool full) {
    net::Writer w(net::Msg::TrainParts, 32768);
    w.put<uint8_t>(g_self);
    w.put<uint8_t>(full ? 1 : 0);
    w.put_blob(g_scene, 64);
    w.put_blob(g_guid, 64);
    w.put<int64_t>(fuel);
    w.put<int64_t>(water);
    w.put<uint8_t>(kParts);
    for (const Part& p : parts) {
        w.put<uint8_t>(p.valid ? 1 : 0);
        w.put<float>(p.condition);
        w.put<uint8_t>(p.on ? 1 : 0);
        w.put_blob(p.data, 6000);
    }
    netrt::send_packet(w, true);
}

void parts_tick(double now) {
    if (!g_train || !api.serialize_component || now < g_next_parts) return;
    g_next_parts = now + 1000;
    if (g_parts_guid != g_guid) {  // a new train (or scene): what it has now is what we know
        g_parts_guid = g_guid;
        for (int i = 0; i < kParts; ++i) g_known[i] = read_part(i);
        g_fuel_known = tank(api.fuel_source);
        g_water_known = tank(api.water_source);
        g_parts_full_at = 0;
    }
    std::array<Part, kParts> now_parts;
    for (int i = 0; i < kParts; ++i) now_parts[i] = read_part(i);
    const int64_t fuel = tank(api.fuel_source), water = tank(api.water_source);
    if (in_charge()) {
        bool changed = fuel != g_fuel_known || water != g_water_known;
        for (int i = 0; i < kParts; ++i)
            changed = changed || std::fabs(now_parts[i].condition - g_known[i].condition) > 0.05f ||
                      now_parts[i].on != g_known[i].on || now_parts[i].data != g_known[i].data;
        const bool full = now >= g_parts_full_at;
        if (!changed && !full) return;
        if (!netrt::status().players.empty()) send_parts(now_parts, fuel, water, true);
        g_known = now_parts;
        g_fuel_known = fuel, g_water_known = water;
        g_parts_full_at = now + 5000;
        return;
    }
    // Not in charge: only what we did ourselves goes out.
    std::string why;
    for (int i = 0; i < kParts; ++i) {
        if (!now_parts[i].valid || !g_known[i].valid) continue;
        if (now_parts[i].condition > g_known[i].condition + 0.5f) why += std::string(" ") + kPartNames[i] + " repaired";
        if (now_parts[i].on != g_known[i].on) why += now_parts[i].on ? " boiler lit" : " boiler out";
    }
    if (fuel > g_fuel_known + kLiquidStep) why += " oil added";
    if (water > g_water_known + kLiquidStep) why += " water added";
    if (why.empty()) return;
    if (g_parts_logged++ < 40) logx::info("train: our own doing to the train:%s: sent", why.c_str());
    send_parts(now_parts, fuel, water, false);
    g_known = now_parts;
    g_fuel_known = fuel, g_water_known = water;
}

void parts_receive() {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::TrainParts, in);
    const bool host = hosting();
    for (const net::Packet& p : in) {
        if (host) {
            net::Writer w(net::Msg::TrainParts, 32768);
            w.put_raw(p.data.data() + net::kHeaderSize, p.data.size() - net::kHeaderSize);
            netrt::send_packet_except(p.from, w, true);
        }
        net::Reader r = net::packet_body(p);
        const uint8_t from = r.get<uint8_t>();
        const bool full = r.get<uint8_t>() != 0;
        const std::string scene = r.get_blob(64), guid = r.get_blob(64);
        const int64_t fuel = r.get<int64_t>(), water = r.get<int64_t>();
        const uint8_t n = r.get<uint8_t>();
        std::array<Part, kParts> parts;
        for (int i = 0; i < n && i < 16; ++i) {
            Part x;
            x.valid = r.get<uint8_t>() != 0;
            x.condition = r.get<float>();
            x.on = r.get<uint8_t>() != 0;
            x.data = r.get_blob(6000);
            if (i < kParts) parts[i] = x;
        }
        if (!r.ok() || (host ? p.from : from) == g_self || scene != g_scene || guid != g_guid || !g_train) continue;
        // A full state from someone not in charge here (an old driver) or our own doing over the one in charge's:
        // the one in charge only takes others' own doings.
        if (full && in_charge()) continue;
        for (int i = 0; i < kParts; ++i)
            if (parts[i].valid) write_part(i, parts[i]);
        if (Il2CppObject* c = liquid(api.fuel_source); c && fuel >= 0) put<int64_t>(c, api.liquid_volume, fuel);
        if (Il2CppObject* c = liquid(api.water_source); c && water >= 0) put<int64_t>(c, api.liquid_volume, water);
        for (int i = 0; i < kParts; ++i) g_known[i] = read_part(i);
        g_fuel_known = tank(api.fuel_source), g_water_known = tank(api.water_source);
        if (!full && g_parts_logged++ < 40) logx::info("train: a friend's doing to the train taken in");
    }
}

// The track's switches, turntables and towers.
struct Element {
    Il2CppObject* obj = nullptr;
    const MethodInfo* guid = nullptr;
    const MethodInfo* serialize = nullptr;
    const MethodInfo* deserialize = nullptr;
};
std::vector<Element> g_elements;
std::vector<std::pair<std::string, std::string>> g_element_known;  // guid, data
double g_next_elements = 0, g_elements_full_at = 0;
std::string g_elements_scene;
int g_elements_logged = 0;

const MethodInfo* method_of(Il2CppObject* obj, const char* name, int args) {
    for (Il2CppClass* k = obj ? il2::il2cpp_object_get_class(obj) : nullptr; k; k = il2::il2cpp_class_get_parent(k))
        if (const MethodInfo* m = il2::find_method_quiet(k, name, args)) return m;
    return nullptr;
}

void collect_elements() {
    g_elements.clear();
    if (!api.track_elements) return;
    Il2CppObject* list = nullptr;
    il2::il2cpp_field_static_get_value(api.track_elements, &list);
    if (!list || api.list_items < 0 || api.list_size < 0) return;
    Il2CppObject* items = get<Il2CppObject*>(list, api.list_items);
    const int32_t size = get<int32_t>(list, api.list_size);
    for (int32_t i = 0; items && i < size && i < static_cast<int32_t>(il2::array_length(items)); ++i) {
        Element e;
        e.obj = il2::array_object_at(items, i);
        if (!alive(e.obj)) continue;
        e.guid = method_of(e.obj, "get_GUID", 0);
        e.serialize = method_of(e.obj, "Serialize", 0);
        e.deserialize = method_of(e.obj, "Deserialize", 1);
        if (e.guid && e.serialize && e.deserialize) g_elements.push_back(e);
    }
}

std::vector<std::pair<std::string, std::string>> read_elements() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const Element& e : g_elements) {
        if (!alive(e.obj)) continue;
        out.emplace_back(text(reinterpret_cast<Il2CppString*>(il2::invoke(e.guid, e.obj))),
                         text(reinterpret_cast<Il2CppString*>(il2::invoke(e.serialize, e.obj))));
    }
    return out;
}

void send_elements(const std::vector<std::pair<std::string, std::string>>& list) {
    if (list.empty() || netrt::status().players.empty()) return;
    net::Writer w(net::Msg::TrackElements, 65536);
    w.put<uint8_t>(g_self);
    w.put_blob(g_scene, 64);
    w.put<uint16_t>(static_cast<uint16_t>(list.size()));
    for (const auto& [guid, data] : list) {
        w.put_blob(guid, 128);
        w.put_blob(data, 4000);
    }
    netrt::send_packet(w, true);
}

void elements_tick(double now) {
    if (!api.track_elements || now < g_next_elements) return;
    g_next_elements = now + 1000;
    if (g_elements_scene != g_scene) {
        g_elements_scene = g_scene;
        g_element_known.clear();
        g_elements_full_at = now + 3000;
    }
    collect_elements();
    if (g_elements.empty()) return;
    const auto current = read_elements();
    std::vector<std::pair<std::string, std::string>> changed;
    for (const auto& c : current) {
        auto it = std::find_if(g_element_known.begin(), g_element_known.end(),
                               [&](const auto& k) { return k.first == c.first; });
        if (it == g_element_known.end()) {
            g_element_known.push_back(c);  // first seen: what the scene loaded with
        } else if (it->second != c.second) {
            it->second = c.second;
            changed.push_back(c);
        }
    }
    if (!changed.empty()) {
        if (g_elements_logged++ < 60)
            logx::info("train: %zu track part(s) changed here (%s ...): sent", changed.size(), changed[0].first.c_str());
        send_elements(changed);
    }
    if (hosting() && now >= g_elements_full_at) {
        send_elements(current);
        g_elements_full_at = now + 15000;
    }
}

void elements_receive() {
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::TrackElements, in);
    const bool host = hosting();
    for (const net::Packet& p : in) {
        if (host) {
            net::Writer w(net::Msg::TrackElements, 65536);
            w.put_raw(p.data.data() + net::kHeaderSize, p.data.size() - net::kHeaderSize);
            netrt::send_packet_except(p.from, w, true);
        }
        net::Reader r = net::packet_body(p);
        const uint8_t from = r.get<uint8_t>();
        const std::string scene = r.get_blob(64);
        const uint16_t n = r.get<uint16_t>();
        if (!r.ok() || (host ? p.from : from) == g_self || scene != g_scene) continue;
        if (g_elements.empty()) collect_elements();
        int applied = 0;
        for (uint16_t i = 0; i < n && r.ok(); ++i) {
            const std::string guid = r.get_blob(128), data = r.get_blob(4000);
            if (!r.ok()) break;
            auto it = std::find_if(g_element_known.begin(), g_element_known.end(),
                                   [&](const auto& k) { return k.first == guid; });
            if (it != g_element_known.end() && it->second == data) continue;
            for (const Element& e : g_elements) {
                if (!alive(e.obj) || text(reinterpret_cast<Il2CppString*>(il2::invoke(e.guid, e.obj))) != guid) continue;
                const std::string mine = text(reinterpret_cast<Il2CppString*>(il2::invoke(e.serialize, e.obj)));
                if (mine != data) {
                    void* a[] = {il2::il2cpp_string_new(data.c_str())};
                    il2::invoke(e.deserialize, e.obj, a);
                    ++applied;
                }
                const std::string now_data = text(reinterpret_cast<Il2CppString*>(il2::invoke(e.serialize, e.obj)));
                if (it != g_element_known.end()) it->second = now_data;
                else g_element_known.emplace_back(guid, now_data);
                break;
            }
        }
        if (applied && g_elements_logged++ < 60) logx::info("train: %d track part(s) set as player %u has them", applied,
                                                            host ? p.from : from);
    }
}

// --- hooks ----------------------------------------------------------------------------------------------------------

using VoidFn = void (*)(Il2CppObject* self, const MethodInfo* method);
VoidFn g_update_controls = nullptr, g_update_engine = nullptr;

void update_controls_detour(Il2CppObject* self, const MethodInfo* method) {
    if (g_ready && self == g_train && driven_by_other()) return;  // a passenger: the driver's levers
    g_update_controls(self, method);
}

void update_engine_detour(Il2CppObject* self, const MethodInfo* method) {
    if (g_ready && g_train && driven_by_other()) {
        Il2CppObject* engine = il2::invoke(api.engine, g_train);
        if (self == engine) {
            apply_engine(il2::invoke(api.engine_state, self));
            return;
        }
    }
    g_update_engine(self, method);
}

}  // namespace

bool init() {
    Il2CppClass* train = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrain");
    Il2CppClass* car = il2::find_class(kGame, "TLD.SteamTrain", "TrainCar");
    Il2CppClass* engine = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrainEngine");
    Il2CppClass* track = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrainTrackSpline");
    if (!train || !car || !engine || !track) {
        logx::info("train: no Iron Road steam train in this game; nothing to do");
        return false;
    }
    Il2CppClass* state = il2::find_nested_class(engine, "EngineState");
    Il2CppClass* sc = il2::find_class("ToolBuddy.Curvy.dll", "FluffyUnderware.Curvy.Controllers", "SplineController");
    if (!sc) sc = il2::find_class(kGame, "FluffyUnderware.Curvy.Controllers", "SplineController");
    Il2CppClass* cc = il2::find_class("ToolBuddy.Curvy.dll", "FluffyUnderware.Curvy.Controllers", "CurvyController");
    if (!cc) cc = il2::find_class(kGame, "FluffyUnderware.Curvy.Controllers", "CurvyController");
    Il2CppClass* liquid = il2::find_class(kGame, "TLD.Gameplay", "GenericLiquidContainer");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    api.train_class = train;
    api.train_type = il2::type_object(train);
    api.track_type = il2::type_object(track);
    api.active_train = il2::find_method(train, "get_ActiveSteamTrain", 0);
    api.inside = il2::find_method(train, "get_IsInsideSteamTrain", 0);
    api.engine = il2::find_method(train, "get_Engine", 0);
    api.update_controls = il2::find_method(train, "UpdateControls", 0);
    api.update_engine = il2::find_method(engine, "UpdateEngine", 0);
    api.engine_state = il2::find_method(engine, "get_CurrentState", 0);
    api.guid = il2::find_method(car, "get_GUID", 0);
    api.controller = il2::find_method(car, "get_SplineController", 0);
    if (sc) {
        api.get_spline = il2::find_method(sc, "get_Spline", 0);
        api.set_spline = il2::find_method(sc, "set_Spline", 1);
    }
    if (cc) {
        api.get_abs = il2::find_method(cc, "get_AbsolutePosition", 0);
        api.set_abs = il2::find_method(cc, "set_AbsolutePosition", 1);
        api.get_dir = il2::find_method(cc, "get_MovementDirection", 0);
        api.set_dir = il2::find_method(cc, "set_MovementDirection", 1);
    }
    api.track_guid = il2::find_method(track, "get_GUID", 0);
    api.track_spline = il2::find_method(track, "get_TrackSpline", 0);
    api.game_object = il2::find_method(component, "get_gameObject", 0);
    api.transform = il2::find_method(component, "get_transform", 0);
    api.get_component = il2::find_method_params(go, "GetComponent", {"System.Type"});
    api.find_objects = il2::find_method_params(object, "FindObjectsOfType", {"System.Type"});
    api.player_tf = il2::find_method(gm, "GetPlayerTransform", 0);
    api.position = il2::find_method(tf, "get_position", 0);
    api.euler = il2::find_method(tf, "get_eulerAngles", 0);
    api.to_local = il2::find_method_params(tf, "InverseTransformPoint", {"UnityEngine.Vector3"});
    api.to_world = il2::find_method_params(tf, "TransformPoint", {"UnityEngine.Vector3"});
    api.fuel_source = il2::field_offset(train, "m_FuelSource");
    api.water_source = il2::field_offset(train, "m_WaterSource");
    if (liquid) api.liquid_volume = il2::field_offset(liquid, "m_CurrentVolume");
    if (state) {
        api.st_velocity = il2::field_offset(state, "m_Velocity");
        api.st_sign = il2::field_offset(state, "m_LastVelocitySign");
        api.st_pressure = il2::field_offset(state, "m_SteamPressure");
        api.st_throttle = il2::field_offset(state, "m_ThrottlePosition");
        api.st_brakes = il2::field_offset(state, "m_BrakesActive");
    }
    const bool ok = api.active_train && api.inside && api.engine && api.update_controls && api.update_engine &&
                    api.engine_state && api.guid && api.controller && api.get_spline && api.set_spline &&
                    api.get_abs && api.set_abs && api.get_dir && api.set_dir && api.track_guid && api.track_spline &&
                    api.game_object && api.transform && api.get_component && api.find_objects && api.player_tf &&
                    api.position && api.euler && api.to_local && api.to_world && api.st_velocity >= 0 &&
                    api.st_throttle >= 0;
    if (!ok) {
        logx::warn("train: some of the steam train's parts not found (controller %s, curvy %s, state %s); no train sync",
                   api.controller ? "ok" : "MISSING", cc && sc ? "ok" : "MISSING", state ? "ok" : "MISSING");
        return false;
    }
    if (!hooks::install_method("SteamTrain.UpdateControls", api.update_controls,
                               reinterpret_cast<void*>(&update_controls_detour),
                               reinterpret_cast<void**>(&g_update_controls)) ||
        !hooks::install_method("SteamTrainEngine.UpdateEngine", api.update_engine,
                               reinterpret_cast<void*>(&update_engine_detour),
                               reinterpret_cast<void**>(&g_update_engine)))
        return false;
    {  // step 2: the parts and the track (optional: without them the train still runs together)
        Il2CppClass* comp = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrainComponent");
        Il2CppClass* cstate = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrainComponentState");
        Il2CppClass* boiler = il2::find_class(kGame, "TLD.SteamTrain", "SteamTrainBoiler");
        Il2CppClass* manager = il2::find_class(kGame, "TLD.SteamTrain", "TrackElementManager");
        const char* getters[4] = {"get_BrakeShoe", "get_Boiler", "get_CowCatcher", "get_Wheels"};
        for (int i = 0; i < 4; ++i) api.part_get[i] = il2::find_method(train, getters[i], 0);
        if (comp) {
            api.serialize_component = il2::find_method(comp, "SerializeComponent", 0);
            api.deserialize_component = il2::find_method(comp, "DeserializeComponent", 1);
        }
        if (cstate) {
            api.cs_condition = il2::field_offset(cstate, "m_Condition");
            api.cs_data = il2::field_offset(cstate, "m_ComponentDataSerialized");
        }
        if (boiler) api.boiler_on = il2::find_method(boiler, "get_BoilerIsOn", 0);
        if (!api.serialize_component || !api.deserialize_component || api.cs_condition < 0 || api.cs_data < 0) {
            api.serialize_component = nullptr;
            logx::warn("train: the engine's parts can't be read; their state isn't shared");
        }
        if (manager) api.track_elements = il2::il2cpp_class_get_field_from_name(manager, "s_TrackElements");
        if (api.track_elements) {
            Il2CppClass* list = il2::il2cpp_class_from_type(il2::il2cpp_field_get_type(api.track_elements));
            if (list) {
                api.list_items = il2::field_offset(list, "_items");
                api.list_size = il2::field_offset(list, "_size");
            }
        }
        if (!api.track_elements || api.list_items < 0) {
            api.track_elements = nullptr;
            logx::warn("train: the track's list not found; switches and turntables aren't shared");
        }
    }
    g_ready = true;
    logx::info("train: ready (the first one into the cab drives; the others' trains follow theirs)");
    return true;
}

void on_frame(const std::string& scene, bool in_game) {
    if (!g_ready) return;
    const double now = now_ms();
    static double last = now;
    const float dt = static_cast<float>(std::clamp((now - last) / 1000.0, 0.0, 0.25));
    last = now;
    if (scene != g_scene || !in_game) {
        g_train = nullptr;
        g_guid.clear();
        g_owner = kNobody;
        g_last = Frame{};
        g_rate = 0;
        g_spline_seen = nullptr;
        g_was_inside = false;
        g_next_find = 0;
    }
    g_scene = scene;
    g_in_game = in_game;
    if (!in_game || !netrt::role()) return;
    g_self = netrt::status().self_id;

    if (!alive(g_train)) {
        g_train = find_train();
        if (alive(g_train)) {
            g_guid = text(reinterpret_cast<Il2CppString*>(il2::invoke(api.guid, g_train)));
            logx::info("train: the steam train is here (%s)", g_guid.c_str());
            if (g_has_pending && g_pending_idle.scene == g_scene) place(g_pending_idle);
            g_has_pending = false;
        } else {
            g_train = nullptr;
        }
    }
    receive(now);
    parts_receive();
    elements_receive();
    elements_tick(now);
    if (g_owner != kNobody && g_owner != g_self && now - g_owner_seen > kOwnerLostMs) {
        logx::info("train: no word from player %d for %.1f s: nobody drives", g_owner, (now - g_owner_seen) / 1000.0);
        g_owner = kNobody;
        g_rate = 0;
    }
    for (size_t i = 0; i < g_riding.size(); ++i)
        if (g_riding[i] && now - g_riders[i].at > kRiderLostMs) g_riding[i] = false;
    if (!g_train) return;

    const bool inside = inside_train();
    if (inside && !g_was_inside) {
        if (g_owner == kNobody) {
            g_owner = g_self;
            logx::info("train: we're in the cab and nobody drives: we drive");
        } else if (g_owner != g_self) {
            logx::info("train: we're in the cab; player %d drives", g_owner);
        }
    }
    if (!inside && g_was_inside) {
        if (g_owner == g_self) {
            send(snapshot(kRelease), true);
            g_owner = kNobody;
            logx::info("train: we left the cab: nobody drives");
        }
    }
    g_was_inside = inside;

    if (driven_by_other()) follow(now, dt);
    parts_tick(now);

    if (now >= g_next_send) {
        if (g_owner == g_self) {
            send(snapshot(kOwner | kInside), false);
            g_next_send = now + kSendMs;
        } else if (inside) {
            send(snapshot(kInside), false);
            g_next_send = now + kSendMs;
        } else if (g_owner == kNobody && hosting()) {
            send(snapshot(kIdle), false);
            g_next_send = now + kIdleSendMs;
        } else {
            g_next_send = now + 250;
        }
    }
}

bool ride_spot(unsigned char player, float& x, float& y, float& z, float& yaw) {
    if (!g_ready || !g_train || player >= g_riding.size() || !g_riding[player]) return false;
    const Frame& f = g_riders[player];
    if (f.scene != g_scene || f.guid != g_guid) return false;
    Il2CppObject* tf = train_tf();
    if (!tf) return false;
    V3 local = f.local, world{};
    void* a[] = {&local};
    if (!il2::invoke_value(api.to_world, tf, world, a)) return false;
    x = world.x, y = world.y, z = world.z;
    yaw = yaw_of(tf) + f.local_yaw;
    return true;
}

std::string describe() {
    if (!g_ready) return "train: not available";
    char s[256];
    float pos = 0;
    if (Il2CppObject* ctl = controller()) il2::invoke_value(api.get_abs, ctl, pos);
    snprintf(s, sizeof(s), "train: %s, driver %d (we're %u), at %.1f m on %s, rate %.2f m/s, snaps %d, switches %d",
             g_train ? g_guid.c_str() : "not here", g_owner, g_self, pos, g_spline_guid.c_str(), g_rate, g_snaps,
             g_switches);
    return s;
}

}  // namespace coop_train
