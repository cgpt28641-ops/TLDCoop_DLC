// TLDCoop_DLC: Seamless Interiors, Stage 2: doors that show the other side (door portals). SI never had them: its door
// fades to black, "meant to go again once the doors show the other side" (PortalPatches.cs). Built on the window views
// (seamless_windows.inc). Included by seamless_interiors.cpp (inside its anonymous namespace), after seamless_windows.inc.
//
// THE PAIRS. A room's doors are paired once with its building's (when the room is ready and the player is within 40 m):
// every door of the region that takes the player into the room (choose() says Enter) is paired with the room door that
// stands where the game lands the player (entry_point, within 6 m), which must take the player out (choose() says Exit).
// Each door's opening is measured from its own box (a BoxCollider, else a mesh, of the LoadScene's object or below it,
// through its transform: upright, 0.55-3.2 m wide, 1.6-3.6 m high, at most 0.6 m thick); its face is the side the
// player stands on (toward where the game puts the player leaving through the other door), the view's plane 6 cm in
// front of it (the door leaf, handle and frame are behind the plane: no door is hidden, the view covers it). The two
// openings must be about the same size (0.6 m wide, 0.7 m high) or the door keeps the fade. The pair's turn takes the
// building door's outward side to the room door's inward side; a point p before one door maps to the other as
// c_other + turn(p - c_this), which puts the eye BEHIND the other door, looking through it.
//
// THE VIEWS. Each frame (in the reconciler, tick_windows) the doors on screen within 15 m on the player's side, nearest
// and biggest first, get at most two views: from outside, a camera at the mapped eye renders the room's private layer by
// hand (the room is held as for the window views: switched on, colliders off, on layer 3/4/30, the building whose door is
// looked through held first, also with windows=0) into a quad over the doorway; from inside, an enabled camera at the
// mapped eye renders the world (the player's mask without the quads' and rooms' layers, the player's fog pass and shared
// buffers, the stand-in cull eye) into a quad over the room's door. The near plane is the far doorway's plane, so the far
// door's leaf and the wall around it are never in the picture. Friends in a held room are on its layer (avatar.cpp), so
// they show through the door from outside; from inside the world is drawn as it is, friends included.
//
// THE CROSSING. Using a door whose view was drawn in the last quarter second (LoadScene.Activate, as before) moves the
// player at once, with no fade: the pose relative to the near door, mapped through the pair (lateral offset, height,
// camera yaw + the pair's turn, pitch kept), lands 0.6 m past the far door's plane; walking into the door (door_walk=1:
// within 0.5 m of its plane, moving toward it at 0.4 m/s or more, inside its width, unlocked and usable) lands the same
// distance past the far door as the player stood before the near one (at least 0.35 m). The move is in the Update phase
// (the door's Activate, or GameManager.Update for a walk), so the camera that renders the frame already stands on the far
// side (TeleportPlayer sets the transform and the camera angles at once: 2.58 code); the motor's velocity is turned with
// the player. Everything else is the door's usual path: enter()/leave() (rooms opened/closed, inside state, audio
// occlusion at the threshold, fall guard, snap to the ground); a landing spot without ground within 1.2 m below, behind
// something solid seen from the doorway, or (entering) outside the room's volume falls back to the game's spot (with the
// turned facing). A door with no pair, no fresh view, a busy scene or a room not ready keeps today's behaviour (the
// fade, the 'Przygotowuje budynek...' wait). door_portals=0: everything here off, the fade for every door.

constexpr float kDoorRange = 15.0f;          // views only for doors this near
constexpr int kDoorSlots = 2;                // views drawn per frame at most
constexpr float kDoorScanRange = 40.0f;      // a ready room's doors paired when its building is this near
constexpr float kDoorPairReach = 6.0f;       // the room door within this of the game's landing spot
constexpr float kDoorViewOut = 0.06f;        // the view's plane this far in front of the door's face
constexpr float kDoorWidthMin = 0.55f, kDoorWidthMax = 3.2f, kDoorHeightMin = 1.6f, kDoorHeightMax = 3.6f,
                kDoorThickMax = 0.6f;
constexpr float kDoorPairWidthDiff = 0.6f, kDoorPairHeightDiff = 0.7f;
constexpr double kDoorFreshMs = 250;         // a door's view drawn this lately counts as showing
constexpr float kDoorStepIn = 0.6f;          // used by hand: landed this far past the far door's plane
// TLDCoop_DLC: 0.9 m (the player's body, 0.5 m round outdoors, stops at the door before its center comes within
// 0.5 m of the view's plane: only a sprint got through, an overloaded player never); pushing toward the door counts as
// walking into it (pressed against it the body doesn't move)
constexpr float kDoorWalkReach = 0.9f, kDoorWalkSpeed = 0.25f, kDoorWalkMinDepth = 0.35f;
constexpr double kDoorWalkCooldownMs = 750;
constexpr float kDoorLandDrop = 1.2f, kDoorLandRise = 0.6f;

struct DoorApi {
    bool ok = false;
    Il2CppObject* t_box = nullptr;
    const MethodInfo* box_center = nullptr, *box_size = nullptr;
    const MethodInfo* is_locked = nullptr, *can_interact = nullptr;
    const MethodInfo* gm_fps_player = nullptr;
    ptrdiff_t player_controller = -1, motor_throttle = -1;
} dapi;

struct DoorFace {
    uintptr_t door = 0;  // the LoadScene
    V3 c, n, r;          // the view's plane (6 cm in front of the face), toward the player's side, right
    float w = 0, h = 0, thick = 0;
    // TLDCoop_DLC: the door leaf swung open while the pair is open (it stood behind the view, so an open door looked
    // like a hole: "drzwi znikają po otwarciu"); where it stood, to put it back
    uintptr_t leaf = 0;
    V3 leaf_pos;
    Quat leaf_rot;
};
struct DoorPair {
    DoorFace out, in;    // out: the building's door (seen from outdoors); in: the room's (seen from the room)
    float turn = 0;      // degrees about up: out-space to in-space (out.n -> -in.n)
    bool exit_main = true;  // the pair a room door leaving takes (several building doors may land at one room door)
    double drawn_out = -1e18, drawn_in = -1e18;  // the last time each side's view was drawn
    bool was_out = false, was_in = false;        // drawn last frame (hysteresis)
    // TLDCoop_DLC: the door stands closed (the game's own door, no view) until it's clicked open; open, the far side
    // shows and walking in goes through; it closes on another click, or once the player is 3 m from it for a moment.
    bool open = false;
    double away_since = -1;
};
struct RoomDoors {
    const Config* cfg = nullptr;
    uintptr_t master = 0, shell = 0;  // what it was scanned with (gone or changed: scanned again)
    std::vector<DoorPair> pairs;
};
struct DoorPick {
    size_t room, pair;
    bool inside;
    float priority;
};
struct Doors {
    std::vector<RoomDoors> rooms;
    std::vector<DoorPick> picks;
    std::vector<Slot> out_slots, in_slots;
    std::vector<uintptr_t> quads, quad_rs;
    bool any_in_camera = false;
    const Config* want = nullptr;  // the building last held for its door (kept a while: no switching its room on and off)
    double want_until = 0;
    uint64_t frames = 0, drawn_total = 0;
    double next_report = 0;
    // walking through
    bool have_last = false;
    V3 last_pos;
    double last_ms = 0, walk_after = 0;
} g_doors;

// --- small things ----------------------------------------------------------------------------------------------------------

// The door leaf of a face: the renderer under its LoadScene about as wide as the opening (0.4-1.1x), at least 60 % as
// high, thin (< 25 cm across it). Swung ~85 degrees about its edge on the face's left, toward the player's side (in
// front of the view, so it shows), or put back where it stood.
int g_swing_logged = 0;
void swing(DoorFace& f, bool open) {
    if (!open) {
        if (!f.leaf) return;
        if (Il2CppObject* t = target(f.leaf); alive(t)) {
            set_position(t, f.leaf_pos);
            Quat q = f.leaf_rot;
            void* a[] = {&q};
            il2::invoke(api.tf_set_rotation, t, a);
        }
        drop(f.leaf);
        return;
    }
    if (f.leaf || !api.tf_set_rotation || !api.r_bounds) return;
    Il2CppObject* door = target(f.door);
    Il2CppObject* go = alive(door) ? go_of(door) : nullptr;
    if (!go) return;
    Il2CppObject* best = nullptr;
    float best_w = 0;
    V3 best_c{};
    Il2CppObject* door_t = tf_of(door);
    each_in(go, api.t_renderer, [&](Il2CppObject* r) {
        if (tf_of(r) == door_t) return;  // (the door's own transform: its click box and where it is would move too)
        UBounds b;
        if (!il2::invoke_value(api.r_bounds, r, b)) return;
        const float w = 2 * (std::fabs(b.extents.x * f.r.x) + std::fabs(b.extents.z * f.r.z));
        const float d = 2 * (std::fabs(b.extents.x * f.n.x) + std::fabs(b.extents.z * f.n.z));
        const float h = 2 * b.extents.y;
        if (w < 0.4f * f.w || w > 1.1f * f.w || h < 0.6f * f.h || d > 0.25f || w <= best_w) return;
        best = r, best_w = w, best_c = b.center;
    });
    Il2CppObject* t = best ? tf_of(best) : nullptr;
    if (!alive(t)) {
        if (g_swing_logged++ < 5) logx::info("interiors: doors: no door leaf found to swing open (the view shows the far side)");
        return;
    }
    f.leaf = hold(t);
    f.leaf_pos = position(t);
    f.leaf_rot = rotation(t);
    const V3 hinge = best_c - f.r * (best_w * 0.5f);
    Quat q = axis_angle(85.0f, kUp);
    if (w_dot(rotate(q, f.r), f.n) < 0) q = axis_angle(-85.0f, kUp);
    set_position(t, hinge + rotate(q, f.leaf_pos - hinge));
    Quat nr = mul(q, f.leaf_rot);
    void* a[] = {&nr};
    il2::invoke(api.tf_set_rotation, t, a);
    if (g_swing_logged++ < 5) logx::info("interiors: doors: %s swung open", name_of(go_of(best)).c_str());
}
void swing_pair(DoorPair& p) {
    swing(p.out, p.open);
    swing(p.in, p.open);
}

void drop_room_doors(RoomDoors& rd) {
    drop(rd.master);
    drop(rd.shell);
    for (DoorPair& p : rd.pairs) {
        swing(p.out, false), swing(p.in, false);
        drop(p.out.door), drop(p.in.door);
    }
    rd.pairs.clear();
}
V3 map_through(const DoorFace& a, const DoorFace& b, float turn, V3 p) { return b.c + rotate(axis_angle(turn, kUp), p - a.c); }
V3 turn_dir(float turn, V3 v) { return rotate(axis_angle(turn, kUp), v); }
HeldRoom* held_for(const Config* cfg) {
    for (HeldRoom& h : g_oi.rooms)
        if (h.cfg == cfg) return &h;
    return nullptr;
}

// --- measuring a door ----------------------------------------------------------------------------------------------------

// A box (local center and size on a transform) as a doorway: upright, door-sized. f.c/f.n are the box's middle and its
// thin level axis (sign not chosen yet).
bool door_box(Il2CppObject* tf, V3 center, V3 size, DoorFace& f) {
    M4 m;
    if (!alive(tf) || !il2::invoke_value(api.tf_to_world, tf, m)) return false;
    const V3 mid = m4_point(m, center);
    const V3 ax[3] = {m4_dir(m, {size.x, 0, 0}), m4_dir(m, {0, size.y, 0}), m4_dir(m, {0, 0, size.z})};
    int up = 0;
    float best = -1;
    for (int i = 0; i < 3; ++i) {
        const float l = w_len(ax[i]);
        const float upness = l > 1e-4f ? std::fabs(ax[i].y) / l : 0;
        if (upness > best) best = upness, up = i;
    }
    if (best < 0.85f) return false;
    const int a = (up + 1) % 3, b = (up + 2) % 3;
    const float la = w_len(ax[a]), lb = w_len(ax[b]);
    const int thin = la < lb ? a : b;
    f.w = std::max(la, lb);
    f.thick = std::min(la, lb);
    f.h = w_len(ax[up]);
    f.n = w_flat(ax[thin]);
    f.c = mid;
    return f.w >= kDoorWidthMin && f.w <= kDoorWidthMax && f.h >= kDoorHeightMin && f.h <= kDoorHeightMax &&
           f.thick <= kDoorThickMax && f.thick <= f.w * 0.5f;
}
// The opening of a door (its LoadScene's object or below it): the biggest door-sized box of a BoxCollider, else of a mesh.
// `toward`: a point on the player's side (where the game puts the player, coming through the other door).
bool measure_door(Il2CppObject* door, V3 toward, V3 toward_else, DoorFace& out, std::string& why) {
    Il2CppObject* go = go_of(door);
    const V3 at = position(door_tf(door));
    DoorFace best;
    float best_area = 0;
    bool from_collider = false;
    auto consider = [&](Il2CppObject* tf, V3 c, V3 s, bool collider) {
        DoorFace f;
        if (!door_box(tf, c, s, f) || dist(f.c, at) > 3.0f) return;
        const float area = f.w * f.h;
        if ((collider && !from_collider) || (collider == from_collider && area > best_area)) {
            best = f, best_area = area, from_collider = collider;
        }
    };
    if (dapi.t_box)
        each_in(go, dapi.t_box, [&](Il2CppObject* box) {
            V3 c, s;
            if (il2::invoke_value(dapi.box_center, box, c) && il2::invoke_value(dapi.box_size, box, s)) consider(tf_of(box), c, s, true);
        });
    if (!from_collider)
        each_in(go, api.t_mesh_filter, [&](Il2CppObject* filter) {
            Il2CppObject* mesh = il2::invoke(api.mesh_filter_mesh, filter);
            UBounds b;
            if (alive(mesh) && il2::invoke_value(api.mesh_bounds, mesh, b)) consider(tf_of(filter), b.center, b.extents * 2.0f, false);
        });
    if (best_area <= 0) {
        why = "no door-sized box on it";
        return false;
    }
    float side = w_dot(toward - best.c, best.n);
    if (std::fabs(side) < 0.15f) side = w_dot(toward_else - best.c, best.n);
    if (std::fabs(side) < 0.05f) {
        why = "which side the player stands on is unclear";
        return false;
    }
    if (side < 0) best.n = best.n * -1.0f;
    best.c = best.c + best.n * (best.thick * 0.5f + kDoorViewOut);
    best.r = w_norm(w_cross(kUp, best.n * -1.0f));
    best.door = hold(door);
    out = best;
    return true;
}

// --- pairing a room's doors (once per room, when it is ready and near) ------------------------------------------------------

void scan_room_doors(Instance& in, RoomDoors& rd) {
    rd.cfg = in.cfg;
    rd.master = hold(master_of(in));
    rd.shell = hold(live(in.shell));
    std::vector<Il2CppObject*> room_doors, outer;
    each_in(master_of(in), api.t_load_scene, [&](Il2CppObject* d) {
        const DoorChoice c = choose(d);
        if (c.way == DoorWay::Exit && c.in == &in) room_doors.push_back(d);
    });
    Il2CppObject* master_tf = live(in.master_tf);
    each_listed(static_object(g_door_list), [&](Il2CppObject* d, int) {
        if (!alive(d) || child_of(door_tf(d), master_tf)) return;
        const DoorChoice c = choose(d);
        if (c.way == DoorWay::Enter && c.in == &in) outer.push_back(d);
    });
    V3 shell_mid = in.cfg->fallback;
    if (Box b; shell_mesh_box(in, b)) shell_mid = (b.min + b.max) * 0.5f;
    const V3 room_mid = in.have_trigger ? to_world(in, in.trig_center) : in.master_pos;
    int refused = 0;
    for (Il2CppObject* s : outer) {
        const V3 entry = entry_point(in, s);
        Il2CppObject* best = nullptr;
        float best_d = kDoorPairReach;
        for (Il2CppObject* r : room_doors) {
            const float d = dist(position(door_tf(r)), entry);
            if (d < best_d) best_d = d, best = r;
        }
        const std::string sname = name_of(go_of(s));
        if (!best) {
            logx::info("interiors: doors: %s: '%s' keeps the fade (no door of the room within %.0f m of where it lands)", in.cfg->id,
                       sname.c_str(), kDoorPairReach);
            ++refused;
            continue;
        }
        DoorPair p;
        std::string why;
        const V3 exit_spot = exit_point(in, best);
        const V3 sp = position(door_tf(s));
        if (!measure_door(s, exit_spot, sp + (sp - shell_mid), p.out, why)) {
            logx::info("interiors: doors: %s: '%s' keeps the fade (the building's door: %s)", in.cfg->id, sname.c_str(), why.c_str());
            ++refused;
            continue;
        }
        if (!measure_door(best, entry, room_mid, p.in, why)) {
            logx::info("interiors: doors: %s: '%s' keeps the fade (the room's door '%s': %s)", in.cfg->id, sname.c_str(),
                       name_of(go_of(best)).c_str(), why.c_str());
            drop(p.out.door);
            ++refused;
            continue;
        }
        if (std::fabs(p.out.w - p.in.w) > kDoorPairWidthDiff || std::fabs(p.out.h - p.in.h) > kDoorPairHeightDiff) {
            logx::info("interiors: doors: %s: '%s' keeps the fade (its opening %.2f x %.2f m, the room's %.2f x %.2f m)", in.cfg->id,
                       sname.c_str(), p.out.w, p.out.h, p.in.w, p.in.h);
            drop(p.out.door), drop(p.in.door);
            ++refused;
            continue;
        }
        p.turn = w_signed_angle(p.out.n, p.in.n * -1.0f);
        logx::info("interiors: doors: %s: '%s' (%.2f x %.2f m) paired with the room's '%s' (%.2f x %.2f m), turned %.1f deg",
                   in.cfg->id, sname.c_str(), p.out.w, p.out.h, name_of(go_of(best)).c_str(), p.in.w, p.in.h, p.turn);
        rd.pairs.push_back(p);
    }
    // a room door several building doors land at: leaving takes the one nearest where the game puts the player
    for (size_t i = 0; i < rd.pairs.size(); ++i) {
        Il2CppObject* rdoor = live(rd.pairs[i].in.door);
        if (!rdoor) continue;
        const V3 spot = exit_point(in, rdoor);
        size_t best = i;
        float best_d = 1e30f;
        for (size_t j = 0; j < rd.pairs.size(); ++j)
            if (live(rd.pairs[j].in.door) == rdoor) {
                const float d = dist(rd.pairs[j].out.c, spot);
                if (d < best_d) best_d = d, best = j;
            }
        rd.pairs[i].exit_main = best == i;
    }
    logx::info("interiors: doors: %s: %zu door(s) show the other side, %d keep the fade (%zu building door(s), %zu room door(s))",
               in.cfg->id, rd.pairs.size(), refused, outer.size(), room_doors.size());
}

// --- the reconciler's part ------------------------------------------------------------------------------------------------

void free_door_quads() {
    for (uintptr_t& q : g_doors.quads) {
        if (Il2CppObject* go = live(q)) destroy(go);
        drop(q);
    }
    for (uintptr_t& r : g_doors.quad_rs) drop(r);
    g_doors.quads.clear();
    g_doors.quad_rs.clear();
}
bool doors_standing() { return !g_doors.out_slots.empty() || !g_doors.in_slots.empty() || !g_doors.quads.empty(); }
void teardown_doors(const char* why) {
    g_doors.picks.clear();
    if (!doors_standing()) return;
    for (Slot& s : g_doors.out_slots) free_slot(s);
    for (Slot& s : g_doors.in_slots) free_slot(s);
    g_doors.out_slots.clear();
    g_doors.in_slots.clear();
    free_slot_fx(kDoorFxBase, kDoorFxBase + kDoorSlots);
    free_door_quads();
    g_doors.any_in_camera = false;
    if (g_doors.frames)
        logx::info("interiors: doors: views taken down (%s; %llu frame(s), %.2f views a frame on average)", why,
                   static_cast<unsigned long long>(g_doors.frames),
                   static_cast<double>(g_doors.drawn_total) / static_cast<double>(g_doors.frames));
    g_doors.frames = g_doors.drawn_total = 0;
}
void forget_doors_region() {
    teardown_doors("another region");
    for (RoomDoors& rd : g_doors.rooms) drop_room_doors(rd);
    g_doors.rooms.clear();
    g_doors.want = nullptr;
}

// Which doors get a view this frame (and which building is held for its door). Before the held rooms are chosen.
void doors_pick(Instance* inside, V3 eye) {
    g_doors.picks.clear();
    g_door_want = nullptr;
    if (!g_door_portals || !dapi.ok) {
        if (!g_doors.rooms.empty()) forget_doors_region();
        return;
    }
    // rooms gone, closed for good or rebuilt: their pairs go
    for (size_t i = g_doors.rooms.size(); i-- > 0;) {
        RoomDoors& rd = g_doors.rooms[i];
        Instance* in = instance_of(rd.cfg);
        if (in && in_room(in) && master_of(*in) && live(rd.master) == master_of(*in) && live(rd.shell) == live(in->shell)) continue;
        drop_room_doors(rd);
        g_doors.rooms.erase(g_doors.rooms.begin() + static_cast<ptrdiff_t>(i));
    }
    // one room paired a frame: the one the player is in, else the nearest ready building within 40 m
    {
        Instance* next = nullptr;
        float next_d = kDoorScanRange;
        for (Instance& in : g_instances) {
            if (!in_room(&in) || !in.hydrated || !master_of(in) || !live(in.shell)) continue;
            bool have = false;
            for (const RoomDoors& rd : g_doors.rooms) have = have || rd.cfg == in.cfg;
            if (have) continue;
            const float d = &in == inside ? 0.0f : shell_distance(in, eye);
            if (d < next_d) next_d = d, next = &in;
        }
        if (next) {
            g_doors.rooms.emplace_back();
            scan_room_doors(*next, g_doors.rooms.back());
        }
    }
    if (!inside && g_inside) return;  // a sub-interior: no doors shown
    for (size_t i = 0; i < g_doors.rooms.size(); ++i) {
        RoomDoors& rd = g_doors.rooms[i];
        Instance* in = instance_of(rd.cfg);
        if (!in) continue;
        const bool here = inside == in;
        if (inside && !here) continue;
        for (size_t j = 0; j < rd.pairs.size(); ++j) {
            const DoorPair& p = rd.pairs[j];
            if (here && !p.exit_main) continue;
            if (!p.open) continue;  // (closed: the door itself shows)
            const DoorFace& a = here ? p.in : p.out;
            Il2CppObject* door = live(a.door);
            if (!door || !active_in_tree(go_of(door))) continue;
            const V3 to = eye - a.c;
            const float d = w_len(to);
            const float front = w_dot(to, a.n);
            if (d > kDoorRange || front <= kMinEyeDistance) continue;
            const V3 pa = a.c - a.r * (a.w * 0.5f) - kUp * (a.h * 0.5f);
            float u0, u1, v0, v1;
            if (!visible_subrect(pa, a.r, kUp, a.w, a.h, u0, u1, v0, v1)) continue;
            float pr = (a.w * a.h) * (front / std::max(d, 0.1f)) / std::max(d * d, 0.25f);
            if (here ? p.was_in : p.was_out) pr *= kHysteresis;
            g_doors.picks.push_back({i, j, here, pr});
        }
    }
    std::stable_sort(g_doors.picks.begin(), g_doors.picks.end(),
                     [](const DoorPick& a, const DoorPick& b) { return a.priority > b.priority; });
    if (g_doors.picks.size() > static_cast<size_t>(kDoorSlots)) g_doors.picks.resize(static_cast<size_t>(kDoorSlots));
    for (const DoorPick& pk : g_doors.picks)
        if (!pk.inside) {
            g_door_want = g_doors.rooms[pk.room].cfg;
            break;
        }
    // the building looked at a moment ago stays held for 5 s while the player is near (looking away and back again
    // doesn't switch the whole room off and on)
    const double now = now_ms();
    if (g_door_want) {
        g_doors.want = g_door_want, g_doors.want_until = now + 5000;
    } else if (!inside && g_doors.want && now < g_doors.want_until) {
        Instance* in = instance_of(g_doors.want);
        if (in && in_room(in) && shell_distance(*in, eye) <= kDoorRange + 10) g_door_want = g_doors.want;
    }
}

// The views picked: drawn (after the held rooms are settled). Returns how many views out of the room were drawn.
int doors_render(Instance* inside, V3 eye) {
    (void)inside;
    for (RoomDoors& rd : g_doors.rooms)
        for (DoorPair& p : rd.pairs) p.was_out = p.was_in = false;
    if (static_cast<int>(g_doors.quads.size()) < kDoorSlots && !g_doors.picks.empty()) {
        while (static_cast<int>(g_doors.quads.size()) < kDoorSlots) {
            Il2CppObject* q = new_quad("TLDCoop_SI_DoorView");
            g_doors.quads.push_back(hold(q));
            g_doors.quad_rs.push_back(hold(q ? component(q, api.t_renderer) : nullptr));
        }
        g_doors.out_slots.resize(static_cast<size_t>(kDoorSlots));
        g_doors.in_slots.resize(static_cast<size_t>(kDoorSlots));
    }
    const WinQuality& qy = quality();
    const double now = now_ms();
    int used = 0, used_in = 0, inside_views = 0;
    for (const DoorPick& pk : g_doors.picks) {
        if (used >= kDoorSlots || static_cast<size_t>(used) >= g_doors.quads.size()) break;
        RoomDoors& rd = g_doors.rooms[pk.room];
        DoorPair& p = rd.pairs[pk.pair];
        const DoorFace& a = pk.inside ? p.in : p.out;
        const DoorFace& b = pk.inside ? p.out : p.in;
        const float turn = pk.inside ? -p.turn : p.turn;
        int mask;
        if (pk.inside) {
            mask = g_main.mask & ~(1 << kQuadLayer) & ~kRoomLayersMask;
        } else {
            HeldRoom* h = held_for(rd.cfg);
            if (!h || !h->borrowed) continue;  // (held from a later frame on)
            mask = 1 << h->layer;
        }
        const V3 pa = a.c - a.r * (a.w * 0.5f) - kUp * (a.h * 0.5f);
        float u0, u1, v0, v1;
        if (!visible_subrect(pa, a.r, kUp, a.w, a.h, u0, u1, v0, v1)) continue;
        const V3 qa = pa + a.r * u0 + kUp * v0, qb = pa + a.r * u1 + kUp * v0, qc = pa + a.r * u0 + kUp * v1;
        int tw, th;
        pixel_size(qa, qb, qc, std::max(qy.scale, 0.5f), tw, th);
        const int k = pk.inside ? used_in : used - used_in;
        Slot& s = pk.inside ? g_doors.in_slots[static_cast<size_t>(k)] : g_doors.out_slots[static_cast<size_t>(k)];
        char name[48];
        std::snprintf(name, sizeof(name), pk.inside ? "TLDCoop_SI_DoorOutCamera%d" : "TLDCoop_SI_DoorInCamera%d", k);
        if (!ensure_slot(s, name, pk.inside ? -70 - k : -90 - k, pk.inside ? kViewOutQueue : kViewInQueue, tw, th, mask)) continue;
        Il2CppObject* cam = live(s.cam);
        if (pk.inside) {
            if (cam) g_out_cams.insert(cam);
            share_player_buffers(static_cast<int>(kDoorFxBase) + k, cam);
            use_fog_pass(static_cast<int>(kDoorFxBase) + k, cam);
        }
        const V3 eye_far = map_through(a, b, turn, eye);
        const V3 fwd = turn_dir(turn, a.n * -1.0f), right = turn_dir(turn, a.r);
        if (!aim_camera(cam, eye_far, fwd, kUp, right, eye_far, map_through(a, b, turn, qa), map_through(a, b, turn, qb),
                        map_through(a, b, turn, qc), pk.inside ? g_main.zfar : kOutsideInFarClip))
            continue;
        if (pk.inside) {
            set_behaviour(cam, true);  // Unity draws it, later in the frame (as the window views out)
        } else {
            set_behaviour(cam, false);
            il2::invoke(wapi.cam_render, cam);  // by hand: only the room's layer
        }
        Il2CppObject* quad = live(g_doors.quads[static_cast<size_t>(used)]);
        Il2CppObject* qr = live(g_doors.quad_rs[static_cast<size_t>(used)]);
        if (!quad || !qr) continue;
        place_quad(quad, pa, a.r, kUp, a.n * -1.0f, u0, u1, v0, v1);
        void* ma[] = {live(s.mat)};
        il2::invoke(wapi.r_set_shared, qr, ma);
        if (!renderer_on(qr)) set_renderer(qr, true);
        if (pk.inside) {
            p.drawn_in = now, p.was_in = true;
            ++used_in;
            ++inside_views;
        } else {
            p.drawn_out = now, p.was_out = true;
        }
        ++used;
    }
    // what has no view this frame: its quad off, the cameras left over stop drawing
    for (size_t k = static_cast<size_t>(used); k < g_doors.quad_rs.size(); ++k)
        if (Il2CppObject* qr = live(g_doors.quad_rs[k]); qr && renderer_on(qr)) set_renderer(qr, false);
    for (size_t k = static_cast<size_t>(used_in); k < g_doors.in_slots.size(); ++k)
        if (Il2CppObject* c = live(g_doors.in_slots[k].cam); c && behaviour_on(c)) set_behaviour(c, false);
    if (!g_doors.picks.empty() || used > 0) {
        ++g_doors.frames;
        g_doors.drawn_total += static_cast<uint64_t>(used);
    }
    if (used > 0 && now >= g_doors.next_report) {
        g_doors.next_report = now + 30000;
        logx::info("interiors: doors: %d door view(s) drawn this frame (%d from inside), %zu wanted (limit %d)", used, inside_views,
                   g_doors.picks.size(), kDoorSlots);
    }
    return inside_views;
}

// --- the crossing --------------------------------------------------------------------------------------------------------

// The ground under a landing spot (non-trigger, not the player), 0.6 m above to 1.2 m below.
bool ground_near(V3 pos, V3& out) {
    const std::vector<Hit> hits = raycast_all(pos + V3{0, kDoorLandRise, 0}, {0, -1, 0}, kDoorLandRise + kDoorLandDrop, kQueryIgnore);
    const Hit* best = nullptr;
    for (const Hit& h : hits) {
        if (is_trigger(h.collider) || under_player(h.collider)) continue;
        if (!best || h.distance < best->distance) best = &h;
    }
    if (!best) return false;
    out = {pos.x, best->point.y, pos.z};
    return true;
}
// Called by enter()/leave() once the far side's colliders are up: the landing spot on the ground, nothing solid between
// the doorway and it, and (entering) inside the room.
bool landing(const Through& t, Instance* room, V3& out) {
    V3 g;
    if (!ground_near(t.pos, g)) {
        logx::info("interiors: doors: no ground under the landing spot (%.1f, %.1f, %.1f); the game's spot instead", t.pos.x, t.pos.y,
                   t.pos.z);
        return false;
    }
    const V3 chest = g + V3{0, 1.0f, 0};
    const V3 along = chest - t.door_mid;
    const float len = w_len(along);
    if (len > 0.05f) {
        const V3 dir = along * (1.0f / len);
        for (const Hit& h : raycast_all(t.door_mid, dir, len + 0.25f, kQueryIgnore)) {
            if (is_trigger(h.collider) || under_player(h.collider)) continue;
            logx::info("interiors: doors: '%s' stands between the doorway and the landing spot; the game's spot instead",
                       name_of(go_of(h.collider)).c_str());
            return false;
        }
    }
    if (room && room->have_trigger && !in_volume(*room, g, 1.0f)) {
        logx::info("interiors: doors: the landing spot is outside %s's room; the game's spot instead", room->cfg->id);
        return false;
    }
    out = g;
    return true;
}
// After the move: the motor's velocity turned with the player (the camera angles came with TeleportPlayer).
void after_through(const Through& t) {
    if (std::fabs(t.turn) < 0.5f || !dapi.gm_fps_player || dapi.player_controller < 0 || dapi.motor_throttle < 0) return;
    Il2CppObject* player = il2::invoke(dapi.gm_fps_player, nullptr);
    Il2CppObject* ctl = alive(player) ? get<Il2CppObject*>(player, dapi.player_controller) : nullptr;
    if (!alive(ctl)) return;
    put<V3>(ctl, dapi.motor_throttle, turn_dir(t.turn, get<V3>(ctl, dapi.motor_throttle)));
}

DoorPair* pair_of(const DoorChoice& c, Il2CppObject* door, bool& entering) {
    if (!c.in || (c.way != DoorWay::Enter && c.way != DoorWay::Exit)) return nullptr;
    entering = c.way == DoorWay::Enter;
    for (RoomDoors& rd : g_doors.rooms) {
        if (rd.cfg != c.in->cfg) continue;
        for (DoorPair& p : rd.pairs)
            if (entering ? live(p.out.door) == door : (live(p.in.door) == door && p.exit_main)) return &p;
    }
    return nullptr;
}

bool portal_door(Il2CppObject* door, const DoorChoice& c, bool walked) {
    if (!g_door_portals || !dapi.ok || !api.ok || !wapi.ok || !door) return false;
    bool entering = false;
    DoorPair* p = pair_of(c, door, entering);
    if (!p) return false;
    const double now = now_ms();
    if (now - (entering ? p->drawn_out : p->drawn_in) > kDoorFreshMs) {
        static int told = 0;
        if (told++ < 20)
            logx::info("interiors: doors: '%s' used while its view wasn't showing; the fade instead", name_of(go_of(door)).c_str());
        return false;
    }
    if (entering ? g_inside != nullptr : g_inside != c.in) return false;  // (the player isn't on that door's side)
    const DoorFace& a = entering ? p->out : p->in;
    const DoorFace& b = entering ? p->in : p->out;
    const float turn = entering ? p->turn : -p->turn;
    Il2CppObject* ptf = player_tf();
    Il2CppObject* cam = main_camera();
    if (!alive(ptf)) return false;
    const V3 pos = position(ptf);
    const V3 rel = pos - a.c;
    const float before = w_dot(rel, a.n);  // how far in front of the near door's plane
    float across = w_dot(rel, a.r);
    const float half = std::max(0.0f, std::min(a.w, b.w) * 0.5f - 0.3f);
    across = std::clamp(across, -half, half);
    const float depth = walked ? std::max(before, kDoorWalkMinDepth) : kDoorStepIn;
    Through t;
    t.turn = turn;
    // across and height kept, the far door's plane crossed (b.n points into the far side)
    t.pos = b.c + turn_dir(turn, a.r) * across + V3{0, pos.y - a.c.y, 0} + b.n * depth;
    t.door_mid = b.c;
    t.rot = mul(axis_angle(turn, kUp), alive(cam) ? rotation(tf_of(cam)) : rotation(ptf));
    g_last_portal = now;
    if (!p->open) door_sounds(false);
    p->open = true;  // (it stays open behind the player, and closes once they've walked on)
    p->away_since = -1;
    logx::info("interiors: doors: through '%s' %s %s (%s, %.2f m before it, turned %.1f deg)", name_of(go_of(door)).c_str(),
               entering ? "into" : "out of", c.in->cfg->id, walked ? "walked in" : "used", before, turn);
    if (entering) enter(*c.in, door, &t);
    else leave(*c.in, door, &t);
    return true;
}

// From GameManager.Update: walking into a door whose view is showing takes the player through it.
bool door_usable(Il2CppObject* door) {
    if (!dapi.is_locked || !dapi.can_interact) return false;
    bool locked = true, can = false;
    if (!il2::invoke_value(dapi.is_locked, door, locked) || locked) return false;
    return il2::invoke_value(dapi.can_interact, door, can) && can;
}
void door_sound(bool close) {
    Il2CppObject* cam = il2::invoke(api.gm_main_camera, nullptr);
    Il2CppObject* cam_go = cam ? go_of(cam) : nullptr;
    if (!close && api.gui_click) il2::invoke(api.gui_click, nullptr);  // (wakes the sound pipeline up)
    if (cam_go) play_sound(close ? "Play_SndMechDoorWoodClose1" : "Play_SndMechDoorWoodOpen1", cam_go);
}

// A click on a door with a pair: closed, it opens (the far side shows in it); open, it closes (or, with walking
// through off, the click takes the player through as before). False: not one of ours (the fade, as before).
bool door_click(Il2CppObject* door, const DoorChoice& c) {
    if (!g_door_portals || !dapi.ok || !api.ok || !wapi.ok || !door) return false;
    bool entering = false;
    DoorPair* p = pair_of(c, door, entering);
    if (!p) return false;
    if (entering ? g_inside != nullptr : g_inside != c.in) return false;
    if (p->open && !g_door_walk) return false;  // (portal_door: through it)
    p->open = !p->open;
    p->away_since = -1;
    swing_pair(*p);
    door_sound(!p->open);
    logx::info("interiors: doors: '%s' (%s) %s", name_of(go_of(door)).c_str(), c.in->cfg->id,
               p->open ? "opened: the far side shows in it; walk in to go through" : "closed");
    return true;
}

// Open doors close by themselves once the player has been 3 m from both sides for 1.5 s.
void tick_door_close(V3 pos, double now) {
    for (RoomDoors& rd : g_doors.rooms)
        for (DoorPair& p : rd.pairs) {
            if (!p.open) continue;
            const float d = std::min(w_len(pos - p.out.c), w_len(pos - p.in.c));
            if (d < 3.0f) {
                p.away_since = -1;
                continue;
            }
            if (p.away_since < 0) p.away_since = now;
            if (now - p.away_since < 1500) continue;
            p.open = false;
            p.away_since = -1;
            swing_pair(p);
            if (d < 15.0f) door_sound(true);
        }
}

// The player is trying to move into the door (the motor's throttle against its outward side), stopped or not.
bool pushing_toward(V3 door_out) {
    if (!dapi.gm_fps_player || dapi.player_controller < 0 || dapi.motor_throttle < 0) return false;
    Il2CppObject* player = il2::invoke(dapi.gm_fps_player, nullptr);
    Il2CppObject* ctl = alive(player) ? get<Il2CppObject*>(player, dapi.player_controller) : nullptr;
    if (!alive(ctl)) return false;
    const V3 t = get<V3>(ctl, dapi.motor_throttle);
    return w_dot(t, door_out) < -0.01f;
}
void tick_door_walk() {
    const double now = now_ms();
    Il2CppObject* ptf = player_tf();
    if (!alive(ptf)) {
        g_doors.have_last = false;
        return;
    }
    const V3 pos = position(ptf);
    tick_door_close(pos, now);
    const bool had = g_doors.have_last;
    const V3 last = g_doors.last_pos;
    const double dt = (now - g_doors.last_ms) / 1000.0;
    g_doors.last_pos = pos, g_doors.last_ms = now, g_doors.have_last = true;
    if (!had || dt <= 0.001 || dt > 0.25 || !g_door_portals || !g_door_walk || !engaged() || g_fade.busy || now < g_doors.walk_after ||
        now - g_last_portal < kDoorWalkCooldownMs)
        return;
    for (RoomDoors& rd : g_doors.rooms) {
        Instance* in = instance_of(rd.cfg);
        if (!in || !in_room(in)) continue;
        const bool from_inside = g_inside == in;
        if (g_inside && !from_inside) continue;
        for (DoorPair& p : rd.pairs) {
            if (from_inside && !p.exit_main) continue;
            if (now - (from_inside ? p.drawn_in : p.drawn_out) > kDoorFreshMs) continue;
            const DoorFace& a = from_inside ? p.in : p.out;
            const float d = w_dot(pos - a.c, a.n), was = w_dot(last - a.c, a.n);
            if (d > kDoorWalkReach || d < -0.3f) continue;
            if ((was - d) / static_cast<float>(dt) < kDoorWalkSpeed && !pushing_toward(a.n)) continue;
            if (std::fabs(w_dot(pos - a.c, a.r)) > a.w * 0.5f - 0.1f) continue;
            const float bottom = a.c.y - a.h * 0.5f;
            if (pos.y < bottom - 0.6f || pos.y > bottom + 0.8f) continue;
            Il2CppObject* door = live(a.door);
            if (!door || !active_in_tree(go_of(door)) || !door_usable(door)) continue;
            std::string busy;
            if (door_scene_busy(door, busy)) continue;
            const DoorChoice c = choose(door);
            if (c.in != in || c.way != (from_inside ? DoorWay::Exit : DoorWay::Enter)) continue;
            if (portal_door(door, c, true)) {
                g_doors.walk_after = now + kDoorWalkCooldownMs;
                return;  // (the rooms' list may have changed)
            }
        }
    }
}

void resolve_doors() {
    if (Il2CppClass* box = il2::find_class(kPhysics, "UnityEngine", "BoxCollider")) {
        dapi.t_box = il2::type_object(box);
        dapi.box_center = il2::find_method(box, "get_center", 0);
        dapi.box_size = il2::find_method(box, "get_size", 0);
        if (!dapi.box_center || !dapi.box_size) dapi.t_box = nullptr;
    }
    if (Il2CppClass* li = il2::find_class(kGame, "", "LockedInteraction")) dapi.is_locked = il2::find_method(li, "IsLocked", 0);
    if (Il2CppClass* ls = il2::find_class(kGame, "", "LoadScene")) dapi.can_interact = il2::find_method(ls, "get_CanInteract", 0);
    if (Il2CppClass* gm = il2::find_class(kGame, "", "GameManager")) dapi.gm_fps_player = il2::find_method(gm, "GetVpFPSPlayer", 0);
    if (Il2CppClass* fp = il2::find_class(kGame, "", "vp_FPSPlayer")) dapi.player_controller = il2::field_offset(fp, "Controller");
    if (Il2CppClass* fc = il2::find_class(kGame, "", "vp_FPSController")) dapi.motor_throttle = il2::field_offset(fc, "m_MotorThrottle");
    dapi.ok = api.tf_to_world && api.mesh_bounds && api.mesh_filter_mesh && api.t_load_scene && g_door_list;
    if (!dapi.ok) {
        g_door_portals = false;
        logx::warn("interiors: doors: some calls are missing (transform matrix, meshes, the doors' list); every door keeps the fade");
        return;
    }
    logx::info("interiors: doors: ready (door boxes from %s; walking through %s; velocity kept %s)",
               dapi.t_box ? "colliders and meshes" : "meshes only", dapi.is_locked && dapi.can_interact ? "possible" : "OFF",
               dapi.gm_fps_player && dapi.player_controller >= 0 && dapi.motor_throttle >= 0 ? "yes" : "no");
}
