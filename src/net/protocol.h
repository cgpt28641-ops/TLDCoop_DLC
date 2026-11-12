#pragma once
// Wire protocol shared by the mod (winhttp.dll) and the test bot (tldbot.exe).
// One message per UDP datagram, little-endian, fixed-size strings (zero padded).
//
//   header:  u32 magic "TLDC" | u16 protocol version | u8 message type
// Up to 4 players: the host and 3 friends, each linked to the host only (the host relays what the friends need of each
// other: their states, who's there, and the world changes, as its own).
//
//   Hello    client->host  game_version[24] name[32] u32 nonce (new each time the client starts connecting: a known
//                          address saying hello with another nonce is a new connection, the old one is gone),
//                          content[256] (the client's installed optional content, i.e. DLC: net/content.h)
//   Welcome  host->client  name[32] u8 player (the id the host gave the client: 1..3; the host is 0)
//   Wait     host->client  name[32]: the answer to a Hello while the host decides whether to let them in (a host that
//                          asks: Config.ask_to_join); they keep saying hello until a Welcome or a Reject
//   Reject   host->client  u8 reason, detail[64]; TurnedAway also ends a connection that is up (the host kicked them).
//                          MissingContent: detail is the host's DLC ids the client lacks (net/content.h)
//   Ping     either        f64 sender time (ms)
//   Pong     either        f64 echoed sender time
//   State    either        u32 seq, f64 capture time (sender clock, ms), u8 valid, scene[48],
//                          f32 x y z yaw pitch, f32 normalized_time, i32 day, u8 flags (kStateStruggling...), u8 player
//                          (whose state: 0 the host's, else a friend's the host passes on; a client sends 0),
//                          u8 sleep_vote (in bed or waiting: their SleepChoice, net/sleep_vote.h),
//                          held[40] (the gear in their hands, e.g. "GEAR_Torch"; "" empty hands), u8 held_kind
//                          (net::HeldKind, net/held.h), u8 held_flags (kHeldLit, kHeldAiming, kHeldRaised, kHeldHigh)
//                          ... then (TLDCoop_DLC) place[48]: the Seamless Interiors building they're inside ("" outdoors)
//   Bye      either        (empty)
//   Profile  client->host  name[32] body[32] u8 ready (sent on connect, every 2 s and on a change; body is an id
//                          like "wolf"; ready: READY in the lobby, or playing)
//   Roster   host->client  u8 n, then per player: u8 id, u32 link, u8 ready, name[32], body[32]: everyone in the
//                          game, the host first (every 2 s and when someone comes, goes or changes). link: which
//                          connection of theirs (another one in the same id is someone new); the host's ready: it
//                          pressed START in the lobby, or is playing
//   World    host->client  u32 seq, u8 valid, f32 normalized_time, i32 day, f32 day_length_s, u8 time_lapse,
//                          weather_set[64], f32 weather_progress, i32 weather_previous, i32 weather_stage,
//                          i32 wind_strength, f32 wind_angle, u16 n + n bytes weather_set_state, region[48]
//                          (TLD's last outdoor scene: where an interior the host is in lets out), world_id[64] (which
//                          game: save slot, TLD's game id, random seed) (about 1 Hz)
//   Animals  host->client  u32 frame, f64 capture time, u8 part, u8 parts, scene[48], u8 count, then per animal:
//                          u32 id, u8 kind, u8 mode, u8 flags, f32 x y z yaw pitch roll hp, u8 layers +
//                          (i32 state, f32 time) each, u16 n + n bytes animator parameter values (10 Hz; a frame
//                          may span parts)
//   AnimalSounds host->client  u8 count, then per sound: u32 animal id, u8 emitter, u8 op, u32 event, u32 playing id,
//                          i32 action, i32 fade ms, i32 curve (as soon as the host's game plays them)
//   Attack   host->client  u32 animal, u8 kind, u8 type, f32 damage, i32 damage source, i32 options: one of the
//                          host's animals attacks the client (a fight, damage, or wound events)
//   AttackResult client->host  u32 animal, u8 outcome, f32 hp, f32 damage, u8 flags, f32 bleed minutes: how the
//                          client's fight with it ended and what it did to the animal
//   Reliable either        u32 seq, u16 n + n bytes: another message (with its own header), resent until acked
//   Ack      either        u32 seq: a Reliable arrived
//   Hit      client->host  one of the client's projectiles hit the host's animal (rifle, revolver, arrow, flare
//                          round): u32 animal, u8 weapon, u8 body part, u8 flags, collider[24], f32 damage,
//                          f32 bleed minutes, f32 hit point xyz (world), f32 hit point xyz (collider space)
//   Thrown   client->host  a stone, flare, torch or noise maker the client threw landed: u32 animal it hit (0 none),
//                          u8 item, u8 flare type, f32 xyz where it landed, f32 xyz contact point
//   Carcasses host->client the host's dead animals as carcasses: u8 count, then per carcass u32 animal,
//                          prefab[48], f32 xyz, f32 quaternion xyzw, i8 damage side, i64 meat units, i32 hide,
//                          i32 gut, f32 condition, f32 percent frozen, u8 flags, f32 seconds dead, u32 harvest ack
//                          (about 2 Hz)
//   Harvest  client->host  u32 animal, u32 seq, i64 meat units taken, i32 hide taken, i32 gut taken, u8 flags
//   Quartered client->host u32 animal: the client quartered the carcass (it's gone)
//   Chunk    either        u32 transfer, u16 index, u16 count, u16 n + n bytes: one part of a message too big for one
//                          datagram (sent through Reliable; the receiver handles the message once all parts are in)
//   ItemPut  either        scene[48], u32 seq, u8 flags, pdid (u16 n + n), prefab (u16 n + n), f32 xyz, f32 xyzw,
//                          u32 n + n bytes serialized gear: a loose item in the sender's world appeared or came to
//                          rest somewhere new (TLD's own save record for it: GearItem.CreateSaveData)
//   ItemGone either        scene[48], u32 seq, u8 reason, pdid (u16 n + n): a loose item left the sender's world
//                          (picked up...). seq: the sender's change counter, so a late older change loses
//   ItemList host->client  scene[48], u32 n, then n u64 pdid hashes: every loose item in the host's scene
//   ItemWant client->host  scene[48], u32 n, then n u64 pdid hashes: send these as ItemPut
//   ContainerPut either    scene[48], u32 seq, u8 flags, container id (u16 n + n), u32 n + n bytes: a container's
//                          contents changed in the sender's world (TLD's own save text for it: Container.Serialize),
//                          or the host's answer to ContainerWant (flags 1); flags 2: the host has nothing for it
//   ContainerWant client->host scene[48], container id (u16 n + n): the client is opening this container
//   ObjectPut either       scene[48], u32 seq, then one object: u8 kind, u8 done, f32 refresh, id (u16 n + n):
//                          something in the sender's world was broken down (kind 1, BreakDown) or harvested (kind 2,
//                          Harvestable; refresh: the game hour it grows back, TLD's m_RefreshTime), or (done 0) is back;
//                          a placement (kind 5) or a dead player's body (kind 6) also carries u32 n + n bytes of text
//   ObjectList host->client scene[48], u32 n, then n objects: everything broken down or harvested in the host's scene,
//                          then u32 n + n bytes: which of TLD's random spawns are there (RandomSpawnObjectManager.Serialize())
//   FirePut  either        scene[48], u32 seq, u8 flags, fire id (u16 n + n), u32 n + n bytes: a fire changed in the
//                          sender's world (lit, fed, embers, out...): TLD's own save record for it (one entry of
//                          FireManager.Serialize's m_SerializedFires); flags 1: it's gone (a campfire taken apart)
//   FireList host->client  scene[48], u32 n + n bytes: all the host's fires there (FireManager.Serialize())
//   WaterPut either        scene[48], u32 seq, water source id (u16 n + n), u32 n + n bytes: a water source (a toilet,
//                          a tank) changed in the sender's world: its WaterSource.Serialize() (TLDCoop_DLC)
//   ContainerLock either   scene[48], container id (u16 n + n), u8 op, u8 owner: one player at a time in a container
//                          (net/container_locks.h). Client->host: op Take (search or open it; again while busy) or
//                          Release; owner unused. Host->client: op State, owner the player in it (kNoOwner: free).
//                          (TLDCoop_DLC)
//   BloodMark either       scene[48], u8 kind, f32 x y z, f32 a b, i32 count: blood a bleeding player left in the
//                          sender's world (TLD's BloodTrail: kind 0 a drop at x y z, 1 a stain (a b: its scale's range),
//                          2 a cluster of `count` drops); the others' games leave the same (TLDCoop_DLC)
//   Features host->client  u32 bits (kFeature*), f32 tree snow (0-100): what the host's game has on that every game
//                          follows (Ambient Lights, Dynamic Trees, Seamless Interiors) and how much snow its trees carry, on connect,
//                          when it changes and every few seconds (TLDCoop_DLC)
//   SprayMark either       scene[48], decal id (u16 n + n), u8 gone, then (not gone) decal name (u16 n + n), f32 pos
//                          xyz, normal xyz, scale xyz, yaw, colour rgba, i32 projectile, u8 indoors: a spray paint
//                          marking put on or taken off a wall in the sender's world (it's on TLD's map too); the
//                          others' games paint or wipe the same (TLDCoop_DLC)
//   WaterList host->client scene[48], u32 n, then n times id (u16 n + n), u32 n + n bytes: every water source the host
//                          has there, when a friend comes into its scene (TLDCoop_DLC)
//   CaughtUp client->host  u32 n: once linked (again), after the n world changes the client made while the link was
//                          down (kept, sent first) are all acked; the host sends its lists of the world after this
//   GameMode host->client  u32 n + n bytes: the host's experience mode (ExperienceModeManager.Serialize()), i32 seed:
//                          its game's random seed (SceneTransitionData.m_GameRandomSeed), on connect and when either
//                          changes; the client plays at both while linked (its own save keeps its own)
//   Sleep    either        u8 op, u32 sleep id, u8 choice, f32 hours, u8 end, u8 who, u8 why: sleeping together
//                          (milestone 10 item 12). Start (host): everyone is in (in bed or waiting): sleep or pass
//                          time `choice`, `hours` of it.
//                          End (host): it is over (end Done, or Woken: player `who` woke, `why`). Woke (a friend):
//                          our part of sleep `id` ended early (`why`), or we weren't in bed for its Start
//   WildFrame either       (v44, [wildlife] ownership=1) one game's own animals, 10 Hz, as Animals but from any owner:
//                          u8 owner, u32 frame, f64 capture time, u8 part, u8 parts, scene[48], u8 count, then per
//                          animal u32 id, u16 epoch, u8 kind, u8 mode, u8 flags, u8 target, f32 x y z yaw pitch roll
//                          hp, f32 bleed elapsed, f32 bleed limit, (v45) u32 spawn region, u8 layers + (i32 state, f32 time) each, u16 n + n
//                          bytes animator parameters. A friend sends its own to the host, the host passes them on.
//   WildCtl  either        (v44) u8 op: Controllers (host: per scene the game that lets TLD spawn, and whether the
//                          registry has nothing there), Release (host -> owner: hand animal id to player `to`),
//                          Take (the state of the animal for its new owner: the old owner -> host -> new owner, or
//                          the host's registry when the old owner is gone; TLD's own BaseAi.Serialize() inside),
//                          Refuse (new owner -> host: it couldn't take it); v45: Gone (owner -> host: TLD removed
//                          it), Kill (host -> the scene's spawning game: an animal of one of its spawn regions died or
//                          is gone), Regions (spawn regions' respawn state: the spawning game -> host, host -> the
//                          next spawning game), Keep (the host's registry in its save, world_keep only)
//   WildSounds either      (v44) as AnimalSounds, from any owner (the host passes a friend's on)
//   WildHit  host->client  (v44) u8 op (1 hit, 2 thrown), u8 who (the shooter), then a Hit or Thrown body: a shot on
//                          an animal another game runs, passed on to its owner

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace net {

constexpr uint32_t kMagic = 0x43444C54;  // bytes "TLDC"
constexpr uint16_t kProtocolVersion = 51;  // v4: World. v5: Animals. v6: sounds. v7: attacks, State flags. v8: hits,
                                           // carcasses, harvests, fuller fight results, reliable messages. v9: chunked
                                           // messages, world items. v10: containers. v11: broken down / harvested.
                                           // v12: fires. v13: the host's region in World, random spawns in ObjectList.
                                           // v14: CaughtUp (the client's changes while the link was down). v15: GameMode.
                                           // v16: up to 3 friends (Welcome's player id, State's player, Roster).
                                           // v17: the lobby (ready in Profile and Roster), link in Roster,
                                           // TLD's placements in ObjectList, the game seed in GameMode,
                                           // Wait and TurnedAway (the host lets each friend in).
                                           // v18: the host refuses a take of an item someone else took first
                                           // (ItemGone NotYours, ItemPut kItemNotYours)
                                           // v19: a friend's carcass harvests and quarters made while the link
                                           // is down go once it's back; the host counts each harvest once (seq)
                                           // v20: locks and safes (ObjectKind Lock, Safe)
                                           // v21: takes of one item at the same moment go to a coin toss
                                           // (ItemGone LostToss)
                                           // v22: decorations a player moves, picks up or puts down (ObjectKind
                                           // Placement)
                                           // v23: death: a dead player's body (ObjectKind Body), State's
                                           // kStateDead
                                           // v24: one-of-a-kind items (net/uniques.h) are never shared: not in
                                           // ItemPut / ItemGone / the lists, nor in a ContainerPut's text
                                           // v25: sleeping together: State's kStateInBed, kStateWaiting and
                                           // sleep_vote, Sleep
                                           // v26: what a player holds: State's held, held_kind, held_flags
                                           // v27: handing an item to a friend (Give), State's kStateEating
                                           // (the food in held), kStateBed with bed, kStateCrouched, and the
                                           // sleep list's REST
                                           // v28 (TLDCoop_DLC): Hello's content (installed DLC), Reject's
                                           // MissingContent
                                           // v29 (TLDCoop_DLC): water sources (toilets, tanks): WaterPut,
                                           // WaterList
                                           // v30 (TLDCoop_DLC): one player at a time in a container:
                                           // ContainerLock
                                           // v31 (TLDCoop_DLC): sleep votes 1 HOUR and 2 HOURS (waiting)
                                           // v32 (TLDCoop_DLC): gestures (State's emote, emote_seq); held
                                           // kind Match, held flag kHeldIgniting
                                           // v33 (TLDCoop_DLC): players' blood (BloodMark)
                                           // v34 (TLDCoop_DLC): spray paint markings (SprayMark); State's
                                           // condition and afflictions
                                           // v35 (TLDCoop_DLC): the host's game features (Features: Ambient
                                           // Lights on or off)
                                           // v36 (TLDCoop_DLC): Features: Dynamic Trees, the host's snow on trees
                                           // v37 (TLDCoop_DLC): a sleep vote for 30 minutes (SleepChoice 9)
                                           // v38 (TLDCoop_DLC): Report (a bug report: every game packs its logs)
                                           // v39 (TLDCoop_DLC): Chat (a line someone typed)
                                           // v40 (TLDCoop_DLC): AnimalHandover (a friend's animals back to the host)
                                           // v41 (TLDCoop_DLC): Seamless Interiors in co-op: State's place (the
                                           // building a player is inside), Rooms (which rooms each game has built)
                                           // v42 (TLDCoop_DLC): RoomPlace (furniture moved, taken or put down in a
                                           // Seamless Interiors room)
                                           // v43 (TLDCoop_DLC): no new message; the rooms are turned to their buildings
                                           // (Stage 2): a player inside stands elsewhere in the world than in v42
                                           // v44 (TLDCoop_DLC): wildlife ownership (each animal run by one game, the
                                           // host the arbiter): WildFrame, WildCtl, WildSounds, WildHit; Features'
                                           // kFeatureWildOwnership
                                           // v45 (TLDCoop_DLC): wildlife stages 2/3: AnimalRec's spawn region,
                                           // WildCtl Gone / Kill / Regions, Take's kTakeMoved / kTakeBody and hours
                                           // v46 (TLDCoop_DLC): Controllers' per_player (more animals per player)
                                           // v47 (TLDCoop_DLC): ids up to 200 bytes (kPdidMax); Shot (a flare round seen
                                           // by the others); Controllers' loot per_player
                                           // v50 (TLDCoop_DLC): ObjectKind::Rope (a climbing rope put down on an anchor)
                                           // v51 (TLDCoop_DLC): ItemPutMsg.owner (who put a pot / food on a fire:
                                           // only they can take it, only their game cooks it)
constexpr size_t kMaxPlayers = 4;  // the host and up to 3 friends
constexpr size_t kMaxPacket = 1200;  // fits one UDP datagram on any path
constexpr size_t kMaxMessage = 256 * 1024;  // a reliable message may be this big (sent as Chunks)

constexpr size_t kVersionLen = 24;
constexpr size_t kNameLen = 32;
constexpr size_t kSceneLen = 48;
constexpr size_t kWorldIdLen = 64;
constexpr size_t kDetailLen = 64;
constexpr size_t kBodyLen = 32;
constexpr size_t kHeldLen = 40;
constexpr size_t kWeatherSetLen = 64;
constexpr size_t kWeatherStateMax = 900;
constexpr size_t kMaxAnimalLayers = 4;
constexpr size_t kMaxAnimalParams = 256;  // bytes of animator parameter values per animal

enum class Msg : uint8_t {
    Hello = 1, Welcome = 2, Reject = 3, Ping = 4, Pong = 5, State = 6, Bye = 7, Profile = 8, World = 9, Animals = 10, AnimalSounds = 11, Attack = 12, AttackResult = 13,
    Reliable = 14, Ack = 15, Hit = 16, Thrown = 17, Carcasses = 18, Harvest = 19, Quartered = 20, Chunk = 21,
    ItemPut = 22, ItemGone = 23, ItemList = 24, ItemWant = 25, ContainerPut = 26, ContainerWant = 27, ObjectPut = 28,
    ObjectList = 29, FirePut = 30, FireList = 31, CaughtUp = 32, GameMode = 33, Roster = 34, Wait = 35, Sleep = 36,
    Give = 37, WaterPut = 38, WaterList = 39, ContainerLock = 40,
    BloodMark = 41, SprayMark = 42, Features = 43, Report = 44, Chat = 45, AnimalHandover = 46,
    Rooms = 47, RoomPlace = 48, WildFrame = 49, WildCtl = 50, WildSounds = 51, WildHit = 52, FlareShot = 53,
    LogsWant = 54, LogsFile = 55,
    HouseLights = 56,  // TLDCoop_DLC (v49): House Lights' switch in a scene: u8 on, scene[64] (either way; the host passes it on)
    Automat = 57,      // TLDCoop_DLC: the slot machine (test builds): u8 op, scene blob, then the op's fields (src/slot_machine.cpp)
    Summon = 58,       // TLDCoop_DLC: host -> a friend: come to me (F1 teleportplayer): scene[64], x, y, z, yaw (src/coop_summon.cpp)
    Train = 59,        // TLDCoop_DLC: Iron Road's steam train: the driver's (or a rider's, or the host's idle) frame (src/coop_train.cpp)
    TrainParts = 60,   // TLDCoop_DLC: the train's parts (condition, boiler) and tanks (src/coop_train.cpp)
    TrackElements = 61, // TLDCoop_DLC: Iron Road's track switches, turntables, towers: guid + TLD's own state text
    IslandBox = 62     // TLDCoop_DLC: Iron Road's boat (island transit) exchange box: u8 from, scene[64], TLD's own save text
};
enum class RejectReason : uint8_t { Protocol = 1, GameVersion = 2, Full = 3, TurnedAway = 4, MissingContent = 5 };

struct StateMsg {
    uint32_t seq = 0;
    double t_ms = 0;     // when the sender captured this state, on the sender's own monotonic clock
    bool valid = false;  // false while the sender is in a menu or loading
    std::string scene;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
    float roll = 0;  // not sent in State (players don't roll); animals use it locally for their tilt
    float normalized_time = 0;
    int32_t day = 0;
    uint8_t flags = 0;  // kStateStruggling, kStateDead, kStateInBed, kStateWaiting, kStateEating, kStateBed,
                        // kStateCrouched
    uint8_t player = 0;  // whose: 0 the host, 1..3 a friend
    uint8_t sleep_vote = 0;  // in bed: the hours they vote for (net::SleepChoice); 0 otherwise
    std::string held;         // the gear prefab in their hands ("" empty hands)
    uint8_t held_kind = 0;    // net::HeldKind (net/held.h)
    uint8_t held_flags = 0;   // kHeldLit, kHeldAiming, kHeldRaised, kHeldHigh
    float bed[7] = {0, 0, 0, 0, 0, 0, 1};  // kStateBed: where their body lies in bed (TLD's Bed.m_BodyPlacementTransform:
                                           // world x y z, rotation x y z w)
    uint8_t emote = 0;      // TLDCoop_DLC: the gesture they make right now (net::Emote; net/emotes.h), 0 none
    uint8_t emote_seq = 0;  // TLDCoop_DLC: changes with each gesture they start
    uint8_t condition = 100;   // TLDCoop_DLC: their condition, 0-100 %
    uint32_t afflictions = 0;  // TLDCoop_DLC: net::Affliction bits (net/afflictions.h)
    // TLDCoop_DLC: the Seamless Interiors building they're inside ("" outdoors). With Seamless Interiors a building's
    // room stands in the region scene, parked 80 m above the building: same scene is no longer same place.
    std::string place;
    uint8_t activity = 0;  // TLDCoop_DLC: what their body is busy with (net::Activity), 0 nothing special
};
// TLDCoop_DLC: what a player's body is busy with, shown on their avatar with motion capture (avatar.cpp). Appended to
// State: older builds stop reading before it.
enum Activity : uint8_t {
    ActNone = 0,
    ActClimb = 1,      // on a rope
    ActKneel = 2,      // harvesting, cooking, feeding a fire, a rope (deploying/taking)
    ActFire = 3,       // lighting a fire
    ActChop = 4,       // breaking something down
    ActSearch = 5,     // in a container
    ActWork = 6,       // crafting, repairing
    ActSit = 7,        // a seat, a snow shelter, ice fishing
    ActAir = 8,        // jumping, falling
    ActCarry = 9,      // carrying a big load (a body)
    ActCount = 10,
};
constexpr uint8_t kStateStruggling = 1;  // in a fight with an animal (TLD's PlayerStruggle)
constexpr uint8_t kStateDead = 2;        // dead, choosing their next survivor (their body lies where they died)
constexpr uint8_t kStateInBed = 4;       // in through co-op's sleep or pass time screen (in real time, or sleeping or
                                         // passing time with everyone)
constexpr uint8_t kStateWaiting = 8;     // with kStateInBed: in to pass time, not in bed
constexpr uint8_t kStateEating = 16;     // eating or drinking (TLD's progress bar): held is the food
constexpr uint8_t kStateBed = 32;        // with kStateInBed: lying in a bed, `bed` says where (not set while waiting)
constexpr uint8_t kStateCrouched = 64;   // crouching (PlayerManager.PlayerIsCrouched)

// The host's world, which the client follows: clock, weather and wind (milestone 5). Enum values are TLD's own
// (WeatherStage, WindStrength).
struct WorldMsg {
    uint32_t seq = 0;
    bool valid = false;            // false while the host is in a menu or loading
    float normalized_time = 0;     // TimeOfDay.GetNormalizedTime(): 0 = midnight, 0.5 = noon
    int32_t day = 0;               // TimeOfDay.GetDayNumber()
    float day_length_s = 0;        // real seconds per game day, to extrapolate the clock between messages
    bool time_lapse = false;       // the host is sleeping or passing time
    std::string weather_set;       // the active WeatherSetData's name; "" when there is none
    std::string weather_set_state; // WeatherSetData.Serialize(): the set's rolled stage lengths and progress
    float weather_progress = 0;    // WeatherSetData.GetProgressFrac()
    int32_t weather_previous = 11; // WeatherTransition.m_PreviousWeatherSetType (11 = Undefined)
    int32_t weather_stage = 11;    // Weather.GetWeatherStage()
    int32_t wind_strength = 0;     // Wind.GetStrength()
    float wind_angle = 0;          // Wind.GetWindAngle_Base(), degrees
    std::string region;            // SceneTransitionData.m_LastOutdoorScene: the host's region (in an interior, where it
                                   // lets out); "" from an older recording
    std::string world_id;          // game::save_id() of the host's game: which world this is
    // TLDCoop_DLC: AuroraManager.m_NormalizedActive in the host's game (how far the aurora is on: the lamps, radios and
    // fences it powers follow it); -1 unknown (an older host: sent last, read only when there).
    float aurora = -1;
    double received_ms = 0;        // receiver's now_ms() on arrival; not sent
};

// The host's animals, which the client mirrors (milestone 7). kind = AiSubType, mode = AiMode.
struct AnimalLayer {
    int32_t state = 0;  // AnimatorStateInfo.fullPathHash
    float time = 0;     // AnimatorStateInfo.normalizedTime
};
struct AnimalRec {
    uint32_t id = 0;  // the host's own numbering, stable while the animal exists
    uint8_t kind = 0, mode = 0, flags = 0;  // flags: kAnimalDead
    float x = 0, y = 0, z = 0, yaw = 0, hp = 0;
    float pitch = 0, roll = 0;  // the body's tilt along the ground, degrees (-180..180)
    std::vector<AnimalLayer> layers;
    // Every non-trigger animator parameter, in the controller's order: float / int as 4 bytes, bool as 1. Both
    // games load the same controller, so the order is the same on both sides.
    std::string params;
    // v44, WildFrame only (wildlife ownership): which handover of the animal this is from (a late record of an older
    // owner loses), the player it is after (kNoPlayer none), and its bleeding (BaseAi.m_ElapsedBleedingOutMinutes,
    // m_DeathAfterBleeingOutMinutes).
    uint16_t epoch = 0;
    uint8_t target = 0xFF;
    float bleed_elapsed = 0, bleed_limit = 0;
    // v45: the spawn region it belongs to (a hash of where the region is, the same in every game; 0 unknown), kept
    // with the animal from owner to owner.
    uint32_t region = 0;
};
constexpr uint8_t kAnimalDead = 1;
constexpr uint8_t kAnimalAurora = 2;  // TLDCoop_DLC: an aurora animal (its own model)
constexpr uint8_t kAnimalBleeding = 4;  // v44: BaseAi.m_BleedingOut (WildFrame)
constexpr uint8_t kAnimalWounded = 8;   // v44: BaseAi.m_Wounded (WildFrame)
// TLDCoop_DLC (v50): which wolf: a timberwolf (AiTimberwolf, the packs) or a starving one (AiWolfStarving); neither:
// an ordinary wolf. Every game shows and takes over the same one (Shatsca 06.11: "a snow wolf turned into a normal one").
constexpr uint8_t kAnimalTimberwolf = 16;
constexpr uint8_t kAnimalStarving = 32;
inline int animal_variant(uint8_t flags) { return (flags & kAnimalTimberwolf) ? 1 : (flags & kAnimalStarving) ? 2 : 0; }
inline uint8_t variant_flags(int variant) { return variant == 1 ? kAnimalTimberwolf : variant == 2 ? kAnimalStarving : 0; }
constexpr uint8_t kNoPlayer = 0xFF;
struct AnimalsMsg {
    uint32_t frame = 0;
    double t_ms = 0;  // when the host captured the frame, on its own clock
    uint8_t part = 0, parts = 1;
    std::string scene;
    std::vector<AnimalRec> animals;
    double received_ms = 0;  // receiver's now_ms() on arrival; not sent
};

// A sound one of the host's animals made (Wwise), replayed on the client's copy of it.
enum class SoundOp : uint8_t { Post = 1, StopPlaying = 2, Action = 3 };
struct AnimalSound {
    uint32_t animal = 0;
    uint8_t emitter = 0;  // which of the animal's Wwise sound objects (AkGameObj, in hierarchy order)
    SoundOp op = SoundOp::Post;
    uint32_t event = 0;     // Wwise event ID (Post, Action)
    uint32_t playing = 0;   // the host's playing ID (Post: the one it got; StopPlaying / Action: the one to stop)
    int32_t action = 0;     // AkActionOnEventType (Action)
    int32_t fade_ms = 0;    // StopPlaying / Action
    int32_t curve = 0;      // AkCurveInterpolation (StopPlaying / Action)
};
constexpr size_t kAnimalSoundSize = 4 + 1 + 1 + 4 + 4 + 4 + 4 + 4;

// One of the host's animals attacks the client (milestone 7c). The host never applies these to its own player.
enum class AttackType : uint8_t {
    Struggle = 1,  // the animal jumps them: TLD's fight (PlayerStruggle) runs on the client's game
    Damage = 2,    // direct damage (AiTarget.ApplyDamage): damage + source
    Wounds = 3,    // the animal's wound events (DamageEventTable.MaybeApplyAttack): bleeding, sprains, clothing; options
};
struct AttackMsg {
    uint32_t animal = 0;
    uint8_t kind = 0;
    AttackType type = AttackType::Struggle;
    float damage = 0;
    int32_t source = 0;   // DamageSource
    int32_t options = 0;  // DamageEventTable.ApplyAttackOpts
    uint8_t player = 0;   // host: the friend it's for (not sent; 0 = the first friend)
};
// How TLD ended the fight: the animal fled (MakePartnerFlee: tap bar full, flee roll, low health), died (a weapon
// took its health), or walked off (Wander: the fight timed out and knocked the player out, or the player died).
enum class FightOutcome : uint8_t { Fled = 1, Died = 2, NoFight = 3, WalkedOff = 4 };
constexpr uint8_t kFightBleeding = 1;  // AttackResultMsg.flags: the fight left it bleeding out
struct AttackResultMsg {
    uint32_t animal = 0;
    FightOutcome outcome = FightOutcome::NoFight;
    float hp = 0;             // the stand-in's health after the fight (for the log; the host applies damage)
    float damage = 0;         // health the fight took from it (TLD only takes any when the player has a weapon)
    uint8_t flags = 0;        // kFightBleeding
    float bleed_minutes = 0;  // game minutes until it bleeds out, when kFightBleeding
    uint8_t player = 0;       // host: the friend whose fight it was (not sent; set on arrival)
};

// A packet received through the reliable channel or as one of the newer message types, handed to the game thread
// whole (header included) to parse with the read_* functions below.
struct Packet {
    Msg type = Msg::Hello;
    uint8_t from = 0;  // host: the friend it came from (1..3); client: 0 (everything comes from the host)
    uint32_t link = 0; // which connection of theirs (a new one each time they link: their counters may start over)
    std::vector<uint8_t> data;
    bool reliable = false;    // TLDCoop_DLC: it came through the Reliable channel (not sent; never trimmed lightly)
    double received_ms = 0;   // TLDCoop_DLC: when it came (now_ms; not sent)
};

// One of the client's projectiles hit one of the host's animals (its copy, on the client). The client works the
// damage out with its own skills, as TLD does for the shooter; the host applies it to the real animal.
enum class Weapon : uint8_t { Arrow = 1, HardenedArrow = 2, FlareGun = 3, Revolver = 4, Rifle = 5 };  // WeaponSource
constexpr size_t kColliderLen = 24;
constexpr uint8_t kHitCritical = 1;  // HitMsg.flags: the shooter's kill chance came up (the host decides if it counts)
constexpr uint8_t kHitStuck = 2;     // the projectile stuck in it
struct HitMsg {
    uint32_t animal = 0;
    Weapon weapon = Weapon::Rifle;
    uint8_t body_part = 0;  // LocalizedDamage.m_BodyPart
    uint8_t flags = 0;
    std::string collider;   // the hit collider's name, e.g. "capsule_chest"
    float damage = 0;
    float bleed_minutes = 0;
    float world[3] = {};    // hit point
    float local[3] = {};    // hit point in the collider's space (the host's animal may stand a little elsewhere)
};

// A thrown item landed: stones can stun small game, and all of them can scare animals off.
enum class ThrownItem : uint8_t { Stone = 1, Flare = 2, Torch = 3, NoiseMaker = 4 };
struct ThrownMsg {
    uint32_t animal = 0;  // the animal it hit, 0 for none
    ThrownItem item = ThrownItem::Stone;
    uint8_t flare_type = 0;  // FlareItem.m_Type: 0 red, 1 blue
    float pos[3] = {};       // where the item is
    float contact[3] = {};   // where it touched the animal
};

// The host's dead animals, as carcasses the client can harvest (its own copy, kept to the host's numbers).
constexpr size_t kPrefabLen = 48;
constexpr uint8_t kCarcassFrozen = 1, kCarcassRavaged = 2, kCarcassHarvested = 4, kCarcassBonusTaken = 8;
struct CarcassRec {
    uint32_t animal = 0;
    std::string prefab;  // the animal's GameObject name, e.g. "WILDLIFE_Wolf"
    float pos[3] = {};
    float rot[4] = {0, 0, 0, 1};
    int8_t damage_side = 0;  // BaseAi.m_LastDamageSide: which way it fell
    int64_t meat_units = 0;  // ItemWeight units, 1 kg = 1e9
    int32_t hide = 0, gut = 0;
    float condition = 0, percent_frozen = 0;
    uint8_t flags = 0;
    float seconds_dead = 0;    // BaseAi.m_TimeInDeadMode: the death animation takes 5 s
    uint32_t harvest_ack = 0;  // the newest client Harvest seq applied to it
};
constexpr size_t kCarcassSize = 4 + kPrefabLen + 12 + 16 + 1 + 8 + 4 + 4 + 4 + 4 + 1 + 4 + 4;

// The client took meat, hide or guts from a carcass (TLD gave it the items); the host takes the same off its own.
constexpr uint8_t kHarvestSetHarvested = 1, kHarvestBonusTaken = 2;
struct HarvestMsg {
    uint32_t animal = 0;
    uint32_t seq = 0;  // the client's harvest number: never 0, starts somewhere new each game run (net/harvests.h)
    int64_t meat_units = 0;
    int32_t hide = 0, gut = 0;
    uint8_t flags = 0;
};

// Builds one message. `max` is kMaxPacket for anything sent as one datagram; a reliable message may be built up to
// kMaxMessage and is then sent in Chunks.
class Writer {
public:
    explicit Writer(Msg type, size_t max = kMaxPacket) : max_(max) {
        buf_.reserve(max < kMaxPacket ? max : kMaxPacket);
        put(kMagic);
        put(kProtocolVersion);
        put(static_cast<uint8_t>(type));
    }

    template <typename T>
    void put(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        append(&value, sizeof(T));
    }

    void put_str(const std::string& s, size_t fixed) {
        if (!room(fixed)) return;
        const size_t at = buf_.size();
        buf_.resize(at + fixed, 0);
        memcpy(buf_.data() + at, s.data(), s.size() < fixed ? s.size() : fixed - 1);
    }

    // u16 length + bytes; an over-long blob is sent empty (callers check fits_blob first).
    void put_blob(const std::string& s, size_t max) {
        const uint16_t n = static_cast<uint16_t>(s.size() <= max && s.size() <= 0xFFFF ? s.size() : 0);
        put(n);
        append(s.data(), n);
    }

    // u32 length + bytes, for text that can outgrow a u16 (serialized gear and containers).
    void put_long_blob(const std::string& s) {
        put(static_cast<uint32_t>(s.size()));
        append(s.data(), s.size());
    }

    void put_raw(const void* p, size_t n) { append(p, n); }  // TLDCoop_DLC: a received body passed on as it came

    const uint8_t* data() const { return buf_.data(); }
    size_t size() const { return buf_.size(); }
    bool ok() const { return !overflow_; }

private:
    bool room(size_t n) {
        if (overflow_ || buf_.size() + n > max_) overflow_ = true;
        return !overflow_;
    }
    void append(const void* p, size_t n) {
        if (!room(n)) return;
        const auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }

    std::vector<uint8_t> buf_;
    size_t max_;
    bool overflow_ = false;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    template <typename T>
    T get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (pos_ + sizeof(T) > size_) {
            bad_ = true;
            return value;
        }
        memcpy(&value, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return value;
    }

    std::string get_str(size_t fixed) {
        if (pos_ + fixed > size_) {
            bad_ = true;
            return {};
        }
        auto p = reinterpret_cast<const char*>(data_ + pos_);
        pos_ += fixed;
        return std::string(p, strnlen(p, fixed));
    }

    std::string get_blob(size_t max) {
        const uint16_t n = get<uint16_t>();
        if (bad_ || n > max || pos_ + n > size_) {
            bad_ = true;
            return {};
        }
        auto p = reinterpret_cast<const char*>(data_ + pos_);
        pos_ += n;
        return std::string(p, n);
    }

    std::string get_long_blob(size_t max) {
        const uint32_t n = get<uint32_t>();
        if (bad_ || n > max || pos_ + n > size_) {
            bad_ = true;
            return {};
        }
        auto p = reinterpret_cast<const char*>(data_ + pos_);
        pos_ += n;
        return std::string(p, n);
    }

    bool ok() const { return !bad_; }
    size_t left() const { return bad_ ? 0 : size_ - pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool bad_ = false;
};

inline void write_state(Writer& w, const StateMsg& s) {
    w.put(s.seq);
    w.put(s.t_ms);
    w.put(static_cast<uint8_t>(s.valid ? 1 : 0));
    w.put_str(s.scene, kSceneLen);
    w.put(s.x);
    w.put(s.y);
    w.put(s.z);
    w.put(s.yaw);
    w.put(s.pitch);
    w.put(s.normalized_time);
    w.put(s.day);
    w.put(s.flags);
    w.put(s.player);
    w.put(s.sleep_vote);
    w.put_str(s.held, kHeldLen);
    w.put(s.held_kind);
    w.put(s.held_flags);
    for (float v : s.bed) w.put(v);
    w.put(s.emote);
    w.put(s.emote_seq);
    w.put(s.condition);
    w.put(s.afflictions);
    w.put_str(s.place, kSceneLen);
    w.put(s.activity);
}

inline bool read_state(Reader& r, StateMsg& s) {
    s.seq = r.get<uint32_t>();
    s.t_ms = r.get<double>();
    s.valid = r.get<uint8_t>() != 0;
    s.scene = r.get_str(kSceneLen);
    s.x = r.get<float>();
    s.y = r.get<float>();
    s.z = r.get<float>();
    s.yaw = r.get<float>();
    s.pitch = r.get<float>();
    s.normalized_time = r.get<float>();
    s.day = r.get<int32_t>();
    s.flags = r.get<uint8_t>();
    s.player = r.get<uint8_t>();
    s.sleep_vote = r.get<uint8_t>();
    s.held = r.get_str(kHeldLen);
    s.held_kind = r.get<uint8_t>();
    s.held_flags = r.get<uint8_t>();
    for (float& v : s.bed) v = r.get<float>();
    s.emote = r.get<uint8_t>();
    s.emote_seq = r.get<uint8_t>();
    s.condition = r.get<uint8_t>();
    s.afflictions = r.get<uint32_t>();
    s.place = r.get_str(kSceneLen);
    s.activity = r.left() >= 1 ? r.get<uint8_t>() : 0;  // TLDCoop_DLC: older builds don't send it
    if (s.activity >= ActCount) s.activity = 0;
    return r.ok();
}

// Bytes one animal takes in an Animals message.
inline size_t animal_size(const AnimalRec& a) { return 4 + 3 + 28 + 1 + a.layers.size() * 8 + 2 + a.params.size(); }
constexpr size_t kAnimalsHeader = 7 + 4 + 8 + 2 + kSceneLen + 1;

inline void write_animals(Writer& w, const AnimalsMsg& m) {
    w.put(m.frame);
    w.put(m.t_ms);
    w.put(m.part);
    w.put(m.parts);
    w.put_str(m.scene, kSceneLen);
    w.put(static_cast<uint8_t>(m.animals.size()));
    for (const AnimalRec& a : m.animals) {
        w.put(a.id);
        w.put(a.kind);
        w.put(a.mode);
        w.put(a.flags);
        w.put(a.x);
        w.put(a.y);
        w.put(a.z);
        w.put(a.yaw);
        w.put(a.pitch);
        w.put(a.roll);
        w.put(a.hp);
        w.put(static_cast<uint8_t>(a.layers.size()));
        for (const AnimalLayer& l : a.layers) {
            w.put(l.state);
            w.put(l.time);
        }
        w.put_blob(a.params, kMaxAnimalParams);
    }
}

inline bool read_animals(Reader& r, AnimalsMsg& m) {
    m.frame = r.get<uint32_t>();
    m.t_ms = r.get<double>();
    m.part = r.get<uint8_t>();
    m.parts = r.get<uint8_t>();
    m.scene = r.get_str(kSceneLen);
    const uint8_t count = r.get<uint8_t>();
    m.animals.clear();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        AnimalRec a;
        a.id = r.get<uint32_t>();
        a.kind = r.get<uint8_t>();
        a.mode = r.get<uint8_t>();
        a.flags = r.get<uint8_t>();
        a.x = r.get<float>();
        a.y = r.get<float>();
        a.z = r.get<float>();
        a.yaw = r.get<float>();
        a.pitch = r.get<float>();
        a.roll = r.get<float>();
        a.hp = r.get<float>();
        const uint8_t layers = r.get<uint8_t>();
        if (layers > kMaxAnimalLayers) return false;
        for (uint8_t l = 0; l < layers; ++l) {
            AnimalLayer layer;
            layer.state = r.get<int32_t>();
            layer.time = r.get<float>();
            a.layers.push_back(layer);
        }
        a.params = r.get_blob(kMaxAnimalParams);
        m.animals.push_back(std::move(a));
    }
    return r.ok();
}

inline void write_sounds(Writer& w, const std::vector<AnimalSound>& sounds) {
    w.put(static_cast<uint8_t>(sounds.size()));
    for (const AnimalSound& s : sounds) {
        w.put(s.animal);
        w.put(s.emitter);
        w.put(static_cast<uint8_t>(s.op));
        w.put(s.event);
        w.put(s.playing);
        w.put(s.action);
        w.put(s.fade_ms);
        w.put(s.curve);
    }
}

inline bool read_sounds(Reader& r, std::vector<AnimalSound>& out) {
    const uint8_t count = r.get<uint8_t>();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        AnimalSound s;
        s.animal = r.get<uint32_t>();
        s.emitter = r.get<uint8_t>();
        s.op = static_cast<SoundOp>(r.get<uint8_t>());
        s.event = r.get<uint32_t>();
        s.playing = r.get<uint32_t>();
        s.action = r.get<int32_t>();
        s.fade_ms = r.get<int32_t>();
        s.curve = r.get<int32_t>();
        if (r.ok()) out.push_back(s);  // TLDCoop_DLC: a cut-off record isn't one
    }
    return r.ok();
}

inline void write_attack(Writer& w, const AttackMsg& a) {
    w.put(a.animal);
    w.put(a.kind);
    w.put(static_cast<uint8_t>(a.type));
    w.put(a.damage);
    w.put(a.source);
    w.put(a.options);
}

inline bool read_attack(Reader& r, AttackMsg& a) {
    a.animal = r.get<uint32_t>();
    a.kind = r.get<uint8_t>();
    a.type = static_cast<AttackType>(r.get<uint8_t>());
    a.damage = r.get<float>();
    a.source = r.get<int32_t>();
    a.options = r.get<int32_t>();
    return r.ok();
}

inline void write_attack_result(Writer& w, const AttackResultMsg& a) {
    w.put(a.animal);
    w.put(static_cast<uint8_t>(a.outcome));
    w.put(a.hp);
    w.put(a.damage);
    w.put(a.flags);
    w.put(a.bleed_minutes);
}

inline bool read_attack_result(Reader& r, AttackResultMsg& a) {
    a.animal = r.get<uint32_t>();
    a.outcome = static_cast<FightOutcome>(r.get<uint8_t>());
    a.hp = r.get<float>();
    a.damage = r.get<float>();
    a.flags = r.get<uint8_t>();
    a.bleed_minutes = r.get<float>();
    return r.ok();
}

constexpr size_t kHeaderSize = 4 + 2 + 1;  // TLDCoop_DLC: magic, version, type

// A Reader past a received packet's header (magic, version, type), for the read_* functions.
inline Reader packet_body(const Packet& p) {
    Reader r(p.data.data(), p.data.size());
    r.get<uint32_t>();
    r.get<uint16_t>();
    r.get<uint8_t>();
    return r;
}

inline void put_floats(Writer& w, const float* v, int n) {
    for (int i = 0; i < n; ++i) w.put(v[i]);
}
inline void get_floats(Reader& r, float* v, int n) {
    for (int i = 0; i < n; ++i) v[i] = r.get<float>();
}

inline void write_hit(Writer& w, const HitMsg& h) {
    w.put(h.animal);
    w.put(static_cast<uint8_t>(h.weapon));
    w.put(h.body_part);
    w.put(h.flags);
    w.put_str(h.collider, kColliderLen);
    w.put(h.damage);
    w.put(h.bleed_minutes);
    put_floats(w, h.world, 3);
    put_floats(w, h.local, 3);
}

inline bool read_hit(Reader& r, HitMsg& h) {
    h.animal = r.get<uint32_t>();
    h.weapon = static_cast<Weapon>(r.get<uint8_t>());
    h.body_part = r.get<uint8_t>();
    h.flags = r.get<uint8_t>();
    h.collider = r.get_str(kColliderLen);
    h.damage = r.get<float>();
    h.bleed_minutes = r.get<float>();
    get_floats(r, h.world, 3);
    get_floats(r, h.local, 3);
    return r.ok();
}

inline void write_thrown(Writer& w, const ThrownMsg& t) {
    w.put(t.animal);
    w.put(static_cast<uint8_t>(t.item));
    w.put(t.flare_type);
    put_floats(w, t.pos, 3);
    put_floats(w, t.contact, 3);
}

inline bool read_thrown(Reader& r, ThrownMsg& t) {
    t.animal = r.get<uint32_t>();
    t.item = static_cast<ThrownItem>(r.get<uint8_t>());
    t.flare_type = r.get<uint8_t>();
    get_floats(r, t.pos, 3);
    get_floats(r, t.contact, 3);
    return r.ok();
}

inline void write_carcasses(Writer& w, const std::vector<CarcassRec>& list) {
    w.put(static_cast<uint8_t>(list.size()));
    for (const CarcassRec& c : list) {
        w.put(c.animal);
        w.put_str(c.prefab, kPrefabLen);
        put_floats(w, c.pos, 3);
        put_floats(w, c.rot, 4);
        w.put(c.damage_side);
        w.put(c.meat_units);
        w.put(c.hide);
        w.put(c.gut);
        w.put(c.condition);
        w.put(c.percent_frozen);
        w.put(c.flags);
        w.put(c.seconds_dead);
        w.put(c.harvest_ack);
    }
}

inline bool read_carcasses(Reader& r, std::vector<CarcassRec>& out) {
    const uint8_t count = r.get<uint8_t>();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        CarcassRec c;
        c.animal = r.get<uint32_t>();
        c.prefab = r.get_str(kPrefabLen);
        get_floats(r, c.pos, 3);
        get_floats(r, c.rot, 4);
        c.damage_side = r.get<int8_t>();
        c.meat_units = r.get<int64_t>();
        c.hide = r.get<int32_t>();
        c.gut = r.get<int32_t>();
        c.condition = r.get<float>();
        c.percent_frozen = r.get<float>();
        c.flags = r.get<uint8_t>();
        c.seconds_dead = r.get<float>();
        c.harvest_ack = r.get<uint32_t>();
        if (r.ok()) out.push_back(std::move(c));  // TLDCoop_DLC: a cut-off record isn't one
    }
    return r.ok();
}

inline void write_harvest(Writer& w, const HarvestMsg& h) {
    w.put(h.animal);
    w.put(h.seq);
    w.put(h.meat_units);
    w.put(h.hide);
    w.put(h.gut);
    w.put(h.flags);
}

inline bool read_harvest(Reader& r, HarvestMsg& h) {
    h.animal = r.get<uint32_t>();
    h.seq = r.get<uint32_t>();
    h.meat_units = r.get<int64_t>();
    h.hide = r.get<int32_t>();
    h.gut = r.get<int32_t>();
    h.flags = r.get<uint8_t>();
    return r.ok();
}

// Loose items lying in the world (milestone 8). TLD's persistent id (ObjectGuid, "PDID") names an item in both games.
constexpr size_t kPdidMax = 200;  // v47: 64 before (a Seamless Interiors room's container id, "si:<room>|g:<furniture guid>|<name>|<spot>", is longer)
constexpr size_t kGearPrefabMax = 64;
constexpr size_t kGearTextMax = 64 * 1024;  // one serialized GearItem
constexpr uint8_t kItemAnswer = 1;          // ItemPutMsg.flags: an answer to ItemWant (the host's list), not a change
constexpr uint8_t kItemNotYours = 2;        // host -> the taker: your take is refused (the host has it in its hands):
                                            // out of your pack, and it lies here again (the host's record and spot)
struct ItemPutMsg {
    std::string scene;
    uint32_t seq = 0;  // the sender's change counter (reliable messages can arrive out of order)
    uint8_t flags = 0;
    std::string pdid;
    std::string prefab;  // GearSaveData.m_PrefabName, e.g. "GEAR_Hatchet"
    float pos[3] = {};
    float rot[4] = {0, 0, 0, 1};
    std::string gear;    // GearSaveData.m_SearializedGear: everything TLD saves about the item (its pose included)
    // TLDCoop_DLC (v51, Karol 11.11): a pot on a fire (or the food in it): the player who put it there (only they can
    // take it, only their game cooks it); kNoPotOwner for anything else.
    uint8_t owner = 0xFF;
};
constexpr uint8_t kNoPotOwner = 0xFF;
// NotYours: host -> the taker: your take is refused (someone else's came first): out of your pack. LostToss: the same,
// someone took it at the same moment and won the coin toss.
enum class ItemGoneReason : uint8_t { Taken = 1, Destroyed = 2, NotInHostWorld = 3, NotYours = 4, LostToss = 5 };
struct ItemGoneMsg {
    std::string scene;
    uint32_t seq = 0;
    ItemGoneReason reason = ItemGoneReason::Taken;
    std::string pdid;
};
// ItemList (host: everything loose in its scene) and ItemWant (client: send me these), as 64-bit hashes of the pdids.
struct ItemHashesMsg {
    std::string scene;
    std::vector<uint64_t> hashes;
};

inline uint64_t pdid_hash(const std::string& pdid) {  // FNV-1a
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : pdid) h = (h ^ c) * 1099511628211ull;
    return h;
}

inline void write_item_put(Writer& w, const ItemPutMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    w.put(m.flags);
    w.put_blob(m.pdid, kPdidMax);
    w.put_blob(m.prefab, kGearPrefabMax);
    put_floats(w, m.pos, 3);
    put_floats(w, m.rot, 4);
    w.put_long_blob(m.gear);
    w.put(m.owner);
}

// `kept`: a record from the host's world file (world_keep), which may have been written before v51: no owner byte
// then. (12.11: read strictly, every item record kept before the update failed to load in the first v51 build.)
inline bool read_item_put(Reader& r, ItemPutMsg& m, bool kept = false) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    m.flags = r.get<uint8_t>();
    m.pdid = r.get_blob(kPdidMax);
    m.prefab = r.get_blob(kGearPrefabMax);
    get_floats(r, m.pos, 3);
    get_floats(r, m.rot, 4);
    m.gear = r.get_long_blob(kGearTextMax);
    if (kept && r.ok() && r.left() == 0) {
        m.owner = kNoPotOwner;
        return true;
    }
    m.owner = r.get<uint8_t>();  // (v51)
    return r.ok();
}

inline void write_item_gone(Writer& w, const ItemGoneMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    w.put(static_cast<uint8_t>(m.reason));
    w.put_blob(m.pdid, kPdidMax);
}

inline bool read_item_gone(Reader& r, ItemGoneMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    m.reason = static_cast<ItemGoneReason>(r.get<uint8_t>());
    m.pdid = r.get_blob(kPdidMax);
    return r.ok();
}

inline void write_item_hashes(Writer& w, const ItemHashesMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(static_cast<uint32_t>(m.hashes.size()));
    for (uint64_t h : m.hashes) w.put(h);
}

inline bool read_item_hashes(Reader& r, ItemHashesMsg& m) {
    m.scene = r.get_str(kSceneLen);
    const uint32_t n = r.get<uint32_t>();
    if (!r.ok() || n > kMaxMessage / 8) return false;
    m.hashes.resize(n);
    for (uint32_t i = 0; i < n; ++i) m.hashes[i] = r.get<uint64_t>();
    return r.ok();
}

// Containers (milestone 8b), named by TLD's persistent id for them (or "@name|x|y|z" in cm when they have none).
constexpr size_t kContainerTextMax = 200 * 1024;  // one Container.Serialize()
constexpr uint8_t kContainerAnswer = 1;           // ContainerPutMsg.flags: the host's answer to ContainerWant
constexpr uint8_t kContainerUnknown = 2;          // with kContainerAnswer: the host has no contents for it (keep yours)
struct ContainerPutMsg {
    std::string scene;
    uint32_t seq = 0;  // the sender's change counter (reliable messages can arrive out of order)
    uint8_t flags = 0;
    std::string id;
    std::string text;  // Container.Serialize(): everything TLD saves about the container and what's in it
};
struct ContainerWantMsg {
    std::string scene;
    std::string id;
};

inline void write_container_put(Writer& w, const ContainerPutMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    w.put(m.flags);
    w.put_blob(m.id, kPdidMax);
    w.put_long_blob(m.text);
}

inline bool read_container_put(Reader& r, ContainerPutMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    m.flags = r.get<uint8_t>();
    m.id = r.get_blob(kPdidMax);
    m.text = r.get_long_blob(kContainerTextMax);
    return r.ok();
}

inline void write_container_want(Writer& w, const ContainerWantMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put_blob(m.id, kPdidMax);
}

inline bool read_container_want(Reader& r, ContainerWantMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.id = r.get_blob(kPdidMax);
    return r.ok();
}

// Things in the world that are broken down or harvested (milestone 8c); locks and safes, placements (decorations,
// movable containers) a player moved, and dead players' bodies (milestone 10). Named by TLD's persistent id for them
// (or "@name|x|y|z" in cm when they have none).
enum class ObjectKind : uint8_t { BreakDown = 1, Harvestable = 2, Lock = 3, Safe = 4, Placement = 5, Body = 6, Repair = 7, Door = 8, Rope = 9 };
constexpr size_t kPlacementEntryMax = 16 * 1024;  // one placement's record
constexpr size_t kBodyTextMax = 192 * 1024;       // one body: who, its TLD record and what it holds
inline bool has_text(ObjectKind k) { return k == ObjectKind::Placement || k == ObjectKind::Body; }
struct ObjectState {
    ObjectKind kind = ObjectKind::BreakDown;
    uint8_t done = 0;      // broken down / harvested; a lock: its LockState (0 locked, 1 unlocked, 2 forced); a safe:
                           // cracked; a placement: its PlacementState (0 there, 1 gone, 2 carried, 3 hidden)
    float refresh = 0;     // Harvestable.m_RefreshTime: the game hour it grows back (0: never)
    std::string id;
    std::string text;      // a placement: its record (PlaceableInfoSaveData, as TLD's save writes it); a body: whose,
                           // its record (CustomSpawnedItemSaveData) and its contents (Container.Serialize); sent for
                           // those only
};
struct ObjectPutMsg {
    std::string scene;
    uint32_t seq = 0;  // the sender's change counter (reliable messages can arrive out of order)
    ObjectState object;
};
constexpr size_t kSpawnTextMax = 200 * 1024;      // RandomSpawnObjectManager.Serialize()
constexpr size_t kPlacementTextMax = 200 * 1024;  // PlaceableManager.SerializeScenePlacements(), as TLD's save writes it
struct ObjectListMsg {
    std::string scene;
    std::vector<ObjectState> objects;
    std::string spawns;      // RandomSpawnObjectManager.Serialize(): which of each random set is there
    std::string placements;  // the scene's placements: which movable containers, corpses... are where, or gone
};

inline void write_object_state(Writer& w, const ObjectState& s) {
    w.put(static_cast<uint8_t>(s.kind));
    w.put(s.done);
    w.put(s.refresh);
    w.put_blob(s.id, kPdidMax);
    if (has_text(s.kind)) w.put_long_blob(s.text);
}

inline bool read_object_state(Reader& r, ObjectState& s) {
    const uint8_t kind = r.get<uint8_t>();
    s.kind = static_cast<ObjectKind>(kind);
    s.done = r.get<uint8_t>();
    s.refresh = r.get<float>();
    s.id = r.get_blob(kPdidMax);
    if (has_text(s.kind)) s.text = r.get_long_blob(s.kind == ObjectKind::Body ? kBodyTextMax : kPlacementEntryMax);
    return r.ok() && kind >= 1 && kind <= 9;  // TLDCoop_DLC: 7 Repair (a safehouse's furniture repaired), 8 Door (an
                                              // interior's door, closet or locker open or shut: OpenClose), 9 Rope (v50:
                                              // a climbing rope down on a RopeAnchorPoint, or taken)
}

inline void write_object_put(Writer& w, const ObjectPutMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    write_object_state(w, m.object);
}

inline bool read_object_put(Reader& r, ObjectPutMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    return read_object_state(r, m.object) && r.ok();
}

inline void write_object_list(Writer& w, const ObjectListMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(static_cast<uint32_t>(m.objects.size()));
    for (const ObjectState& s : m.objects) write_object_state(w, s);
    w.put_long_blob(m.spawns);
    w.put_long_blob(m.placements);
}

inline bool read_object_list(Reader& r, ObjectListMsg& m) {
    m.scene = r.get_str(kSceneLen);
    const uint32_t n = r.get<uint32_t>();
    if (!r.ok() || n > kMaxMessage / 8) return false;
    m.objects.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        if (!read_object_state(r, m.objects[i])) return false;
    m.spawns = r.get_long_blob(kSpawnTextMax);
    m.placements = r.get_long_blob(kPlacementTextMax);
    return r.ok();
}

// Fires (milestone 8d), named by TLD's persistent id for them.
constexpr size_t kFireTextMax = 64 * 1024;    // one fire's save record
constexpr size_t kFireListMax = 240 * 1024;   // FireManager.Serialize()
constexpr uint8_t kFireGone = 1;              // FirePutMsg.flags: the fire is gone
struct FirePutMsg {
    std::string scene;
    uint32_t seq = 0;  // the sender's change counter (reliable messages can arrive out of order)
    uint8_t flags = 0;
    std::string id;
    std::string text;  // {"m_Position":..., "m_PrefabName":..., "m_SearializedFire":..., "m_Guid":...}
};
struct FireListMsg {
    std::string scene;
    std::string text;  // FireManager.Serialize()
};

inline void write_fire_put(Writer& w, const FirePutMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    w.put(m.flags);
    w.put_blob(m.id, kPdidMax);
    w.put_long_blob(m.text);
}

inline bool read_fire_put(Reader& r, FirePutMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    m.flags = r.get<uint8_t>();
    m.id = r.get_blob(kPdidMax);
    m.text = r.get_long_blob(kFireTextMax);
    return r.ok();
}

inline void write_fire_list(Writer& w, const FireListMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put_long_blob(m.text);
}

inline bool read_fire_list(Reader& r, FireListMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.text = r.get_long_blob(kFireListMax);
    return r.ok();
}

// One player at a time in a container (TLDCoop_DLC; net/container_locks.h).
enum class LockOp : uint8_t { Release = 0, Take = 1, State = 2 };
struct ContainerLockMsg {
    std::string scene;
    std::string id;
    LockOp op = LockOp::Take;
    uint8_t owner = 0xFF;
};

inline void write_container_lock(Writer& w, const ContainerLockMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put_blob(m.id, kPdidMax);
    w.put(static_cast<uint8_t>(m.op));
    w.put(m.owner);
}

inline bool read_container_lock(Reader& r, ContainerLockMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.id = r.get_blob(kPdidMax);
    const uint8_t op = r.get<uint8_t>();
    m.owner = r.get<uint8_t>();
    if (op > static_cast<uint8_t>(LockOp::State)) return false;
    m.op = static_cast<LockOp>(op);
    return r.ok();
}

// Players' blood (TLDCoop_DLC): what TLD's BloodTrail leaves on the ground for a bleeding player.
enum class BloodKind : uint8_t { Drop = 0, Stain = 1, Cluster = 2 };
struct BloodMarkMsg {
    std::string scene;
    BloodKind kind = BloodKind::Drop;
    float x = 0, y = 0, z = 0;
    float a = 0, b = 0;  // Stain: its scale's range (min, max)
    int32_t count = 0;   // Cluster: how many drops
};

inline void write_blood_mark(Writer& w, const BloodMarkMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(static_cast<uint8_t>(m.kind));
    w.put(m.x);
    w.put(m.y);
    w.put(m.z);
    w.put(m.a);
    w.put(m.b);
    w.put(m.count);
}

inline bool read_blood_mark(Reader& r, BloodMarkMsg& m) {
    m.scene = r.get_str(kSceneLen);
    const uint8_t kind = r.get<uint8_t>();
    m.x = r.get<float>();
    m.y = r.get<float>();
    m.z = r.get<float>();
    m.a = r.get<float>();
    m.b = r.get<float>();
    m.count = r.get<int32_t>();
    if (kind > static_cast<uint8_t>(BloodKind::Cluster)) return false;
    m.kind = static_cast<BloodKind>(kind);
    return r.ok() && std::isfinite(m.x) && std::isfinite(m.y) && std::isfinite(m.z) && m.count >= 0 && m.count <= 64;
}

// Spray paint markings (TLDCoop_DLC): TLD's DynamicDecalsManager decals of type SprayPaint, by their id.
constexpr size_t kDecalNameMax = 96;
struct SprayMarkMsg {
    std::string scene;
    std::string id;
    bool gone = false;
    std::string name;
    float pos[3] = {}, normal[3] = {}, scale[3] = {};
    float yaw = 0;
    float colour[4] = {1, 1, 1, 1};
    int32_t projectile = 0;
    bool indoors = false;
};

inline void write_spray_mark(Writer& w, const SprayMarkMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put_blob(m.id, kPdidMax);
    w.put(static_cast<uint8_t>(m.gone ? 1 : 0));
    if (m.gone) return;
    w.put_blob(m.name, kDecalNameMax);
    for (float v : m.pos) w.put(v);
    for (float v : m.normal) w.put(v);
    for (float v : m.scale) w.put(v);
    w.put(m.yaw);
    for (float v : m.colour) w.put(v);
    w.put(m.projectile);
    w.put(static_cast<uint8_t>(m.indoors ? 1 : 0));
}

inline bool read_spray_mark(Reader& r, SprayMarkMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.id = r.get_blob(kPdidMax);
    m.gone = r.get<uint8_t>() != 0;
    if (m.gone) return r.ok() && !m.id.empty();
    m.name = r.get_blob(kDecalNameMax);
    for (float& v : m.pos) v = r.get<float>();
    for (float& v : m.normal) v = r.get<float>();
    for (float& v : m.scale) v = r.get<float>();
    m.yaw = r.get<float>();
    for (float& v : m.colour) v = r.get<float>();
    m.projectile = r.get<int32_t>();
    m.indoors = r.get<uint8_t>() != 0;
    for (float v : m.pos)
        if (!std::isfinite(v)) return false;
    return r.ok() && !m.id.empty();
}

// The host's game features (TLDCoop_DLC): what every game in the session follows the host on.
constexpr uint32_t kFeatureAmbientLights = 1;  // Ambient Lights (built-in port of the MelonLoader mod)
constexpr uint32_t kFeatureDynamicTrees = 2;   // Dynamic Trees (built in, with its authors' permission)
constexpr uint32_t kFeatureAdaptiveArsenal = 4;  // (was Adaptive Arsenal: removed; the bit stays unused)
constexpr uint32_t kFeatureSeamlessInteriors = 8;  // Seamless Interiors (built in, with its author's permission)
constexpr uint32_t kFeatureHouseLights = 32;   // (v49) House Lights (built in, with its author's permission)
constexpr uint32_t kFeatureFoodTweaker = 64;   // (v50) Food Tweaker (built in: realistic calories and cooking times)
constexpr uint32_t kFeatureWildOwnership = 16;  // (v44) [wildlife] ownership=1: each animal is run by one game
inline void write_features(Writer& w, uint32_t bits, float tree_snow) {
    w.put<uint32_t>(bits);
    w.put<float>(tree_snow);
}
inline bool read_features(Reader& r, uint32_t& bits, float& tree_snow) {
    bits = r.get<uint32_t>();
    tree_snow = r.get<float>();
    return r.ok() && std::isfinite(tree_snow);
}

// Report (TLDCoop_DLC), either way (the host passes a friend's on): someone pressed the bug report key; every game
// marks its log with the same id and packs its logs.
inline void write_report(Writer& w, uint32_t id, const std::string& who) {
    w.put<uint32_t>(id);
    w.put_blob(who, 64);
}
inline bool read_report(Reader& r, uint32_t& id, std::string& who) {
    id = r.get<uint32_t>();
    who = r.get_blob(64);
    return r.ok() && id != 0;
}

// TLDCoop_DLC: "everyone's logs" (Ctrl + the report key): `who` asks; every other game packs its logs and sends the zip
// back to `who` in parts (the host passes both on).
constexpr size_t kLogsPart = 192 * 1024;
constexpr size_t kLogsFileMax = 16 * 1024 * 1024;
struct LogsFileMsg {
    uint32_t id = 0;
    std::string to, from, name;  // who asked, whose logs, the zip's file name
    uint16_t part = 0, parts = 0;
    std::string bytes;
};
inline void write_logs_file(Writer& w, const LogsFileMsg& m) {
    w.put<uint32_t>(m.id);
    w.put_blob(m.to, 64);
    w.put_blob(m.from, 64);
    w.put_blob(m.name, 160);
    w.put<uint16_t>(m.part);
    w.put<uint16_t>(m.parts);
    w.put_long_blob(m.bytes);
}
inline bool read_logs_file(Reader& r, LogsFileMsg& m) {
    m.id = r.get<uint32_t>();
    m.to = r.get_blob(64);
    m.from = r.get_blob(64);
    m.name = r.get_blob(160);
    m.part = r.get<uint16_t>();
    m.parts = r.get<uint16_t>();
    m.bytes = r.get_long_blob(kLogsPart);
    return r.ok() && m.parts > 0 && m.part < m.parts && m.parts <= kLogsFileMax / kLogsPart + 1;
}

// Chat (TLDCoop_DLC), either way (the host passes a friend's on): who said it and what (UTF-8).
inline void write_chat(Writer& w, const std::string& who, const std::string& text) {
    w.put_blob(who, 64);
    w.put_blob(text, 512);
}
inline bool read_chat(Reader& r, std::string& who, std::string& text) {
    who = r.get_blob(64);
    text = r.get_blob(512);
    return r.ok() && !text.empty();
}

// AnimalHandover (TLDCoop_DLC), friend -> host: the host came back into a scene the friend's game ran the animals of
// while it was away; where they are, so the host's own (just loaded) take their places.
struct HandoverAnimal {
    uint8_t kind = 0;
    float pos[3] = {}, yaw = 0, hp = 0;
};
constexpr size_t kMaxHandover = 64;
inline void write_animal_handover(Writer& w, const std::string& scene, const std::vector<HandoverAnimal>& list) {
    w.put_str(scene, kSceneLen);
    const size_t n = list.size() < kMaxHandover ? list.size() : kMaxHandover;
    w.put(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; ++i) {
        w.put(list[i].kind);
        put_floats(w, list[i].pos, 3);
        w.put(list[i].yaw);
        w.put(list[i].hp);
    }
}
inline bool read_animal_handover(Reader& r, std::string& scene, std::vector<HandoverAnimal>& list) {
    scene = r.get_str(kSceneLen);
    const uint8_t n = r.get<uint8_t>();
    list.clear();
    for (uint8_t i = 0; i < n && r.ok(); ++i) {
        HandoverAnimal a;
        a.kind = r.get<uint8_t>();
        get_floats(r, a.pos, 3);
        a.yaw = r.get<float>();
        a.hp = r.get<float>();
        list.push_back(a);
    }
    return r.ok();
}

// Rooms (TLDCoop_DLC, v41), either way: the Seamless Interiors rooms built and ready in the sender's game, in its
// region. The host's list says which rooms a friend may build (the host's game is the authority on what is in them);
// a friend's list tells the host when to send its world lists again (a room of the friend's just came up).
constexpr size_t kRoomsMax = 200;
constexpr size_t kRoomIdMax = 64;
struct RoomsMsg {
    std::string region;
    std::vector<std::string> ids;
};

inline void write_rooms(Writer& w, const RoomsMsg& m) {
    w.put_str(m.region, kSceneLen);
    const uint8_t n = static_cast<uint8_t>(m.ids.size() < kRoomsMax ? m.ids.size() : kRoomsMax);
    w.put(n);
    for (uint8_t i = 0; i < n; ++i) w.put_blob(m.ids[i], kRoomIdMax);
}

inline bool read_rooms(Reader& r, RoomsMsg& m) {
    m.region = r.get_str(kSceneLen);
    const uint8_t n = r.get<uint8_t>();
    if (!r.ok() || n > kRoomsMax) return false;
    m.ids.clear();
    for (uint8_t i = 0; i < n && r.ok(); ++i) m.ids.push_back(r.get_blob(kRoomIdMax));
    return r.ok();
}

// RoomPlace (TLDCoop_DLC, v42), either way: furniture and decorations of a Seamless Interiors room (its placeables,
// which TLD's PlaceableManager doesn't hold) as one player's game has them: moved, put away (gone: taken into a pack or
// carried off) or put down. Keyed by the building and the placeable's id (stable in every game). Room-local transform.
// A friend's goes to the host, which applies it, keeps it with its room files and passes it on; the host's goes to the
// friends. `spawned`: TLD's record of a placeable the player brought into the room (PlaceableSaveData, room-local), for
// a game that hasn't got it; "" for the room's own furniture.
constexpr size_t kRoomPlaceGuidMax = 64;
constexpr size_t kRoomPlaceSpawnedMax = 16 * 1024;
struct RoomPlaceMsg {
    std::string region;
    std::string building;
    std::string guid;
    uint8_t gone = 0;    // 1: not in the room any more (picked up / taken)
    uint8_t active = 1;  // its GameObject switched on
    float pos[3] = {};
    float rot[4] = {0, 0, 0, 1};
    float scale[3] = {1, 1, 1};
    std::string spawned;
};

inline void write_room_place(Writer& w, const RoomPlaceMsg& m) {
    w.put_str(m.region, kSceneLen);
    w.put_blob(m.building, kRoomIdMax);
    w.put_blob(m.guid, kRoomPlaceGuidMax);
    w.put(m.gone);
    w.put(m.active);
    for (float v : m.pos) w.put(v);
    for (float v : m.rot) w.put(v);
    for (float v : m.scale) w.put(v);
    w.put_blob(m.spawned, kRoomPlaceSpawnedMax);
}

inline bool read_room_place(Reader& r, RoomPlaceMsg& m) {
    m.region = r.get_str(kSceneLen);
    m.building = r.get_blob(kRoomIdMax);
    m.guid = r.get_blob(kRoomPlaceGuidMax);
    m.gone = r.get<uint8_t>();
    m.active = r.get<uint8_t>();
    for (float& v : m.pos) v = r.get<float>();
    for (float& v : m.rot) v = r.get<float>();
    for (float& v : m.scale) v = r.get<float>();
    m.spawned = r.get_blob(kRoomPlaceSpawnedMax);
    return r.ok();
}

// Water sources (TLDCoop_DLC): toilets, tanks... TLD's WaterSource, named by its persistent id (or its position when it
// has none), with TLD's own save text for it (WaterSource.Serialize(): the litres in it, their quality).
constexpr size_t kWaterTextMax = 4 * 1024;  // one source's WaterSource.Serialize()
constexpr size_t kWaterListMax = 512;       // sources in one WaterList
struct WaterPutMsg {
    std::string scene;
    uint32_t seq = 0;  // the sender's change counter
    std::string id;
    std::string text;
};
struct WaterListMsg {
    std::string scene;
    std::vector<std::pair<std::string, std::string>> sources;  // id, text
};

inline void write_water_put(Writer& w, const WaterPutMsg& m) {
    w.put_str(m.scene, kSceneLen);
    w.put(m.seq);
    w.put_blob(m.id, kPdidMax);
    w.put_long_blob(m.text);
}

inline bool read_water_put(Reader& r, WaterPutMsg& m) {
    m.scene = r.get_str(kSceneLen);
    m.seq = r.get<uint32_t>();
    m.id = r.get_blob(kPdidMax);
    m.text = r.get_long_blob(kWaterTextMax);
    return r.ok();
}

inline void write_water_list(Writer& w, const WaterListMsg& m) {
    w.put_str(m.scene, kSceneLen);
    const uint32_t n = static_cast<uint32_t>(m.sources.size() < kWaterListMax ? m.sources.size() : kWaterListMax);
    w.put(n);
    for (uint32_t i = 0; i < n; ++i) {
        w.put_blob(m.sources[i].first, kPdidMax);
        w.put_long_blob(m.sources[i].second);
    }
}

inline bool read_water_list(Reader& r, WaterListMsg& m) {
    m.scene = r.get_str(kSceneLen);
    const uint32_t n = r.get<uint32_t>();
    if (!r.ok() || n > kWaterListMax) return false;
    m.sources.clear();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
        std::string id = r.get_blob(kPdidMax);
        std::string text = r.get_long_blob(kWaterTextMax);
        m.sources.emplace_back(std::move(id), std::move(text));
    }
    return r.ok();
}

inline void write_world(Writer& w, const WorldMsg& m) {
    w.put(m.seq);
    w.put(static_cast<uint8_t>(m.valid ? 1 : 0));
    w.put(m.normalized_time);
    w.put(m.day);
    w.put(m.day_length_s);
    w.put(static_cast<uint8_t>(m.time_lapse ? 1 : 0));
    w.put_str(m.weather_set, kWeatherSetLen);
    w.put(m.weather_progress);
    w.put(m.weather_previous);
    w.put(m.weather_stage);
    w.put(m.wind_strength);
    w.put(m.wind_angle);
    w.put_blob(m.weather_set_state, kWeatherStateMax);
    w.put_str(m.region, kSceneLen);
    w.put_str(m.world_id, kWorldIdLen);
    w.put(m.aurora);
}

inline bool read_world(Reader& r, WorldMsg& m) {
    m.seq = r.get<uint32_t>();
    m.valid = r.get<uint8_t>() != 0;
    m.normalized_time = r.get<float>();
    m.day = r.get<int32_t>();
    m.day_length_s = r.get<float>();
    m.time_lapse = r.get<uint8_t>() != 0;
    m.weather_set = r.get_str(kWeatherSetLen);
    m.weather_progress = r.get<float>();
    m.weather_previous = r.get<int32_t>();
    m.weather_stage = r.get<int32_t>();
    m.wind_strength = r.get<int32_t>();
    m.wind_angle = r.get<float>();
    m.weather_set_state = r.get_blob(kWeatherStateMax);
    if (r.left() >= kSceneLen + kWorldIdLen) {  // tldbot's recordings from before v13 end before these
        m.region = r.get_str(kSceneLen);
        m.world_id = r.get_str(kWorldIdLen);
    }
    m.aurora = r.left() >= sizeof(float) ? r.get<float>() : -1.0f;
    return r.ok();
}

// Everyone in the game (Roster), the host first.
struct RosterEntry {
    uint8_t id = 0;     // 0 the host, 1..3 the friends
    uint32_t link = 0;  // the host's serial for their connection (0 for the host itself)
    bool ready = false;
    std::string name, body;
};
inline void write_roster(Writer& w, const std::vector<RosterEntry>& players) {
    w.put(static_cast<uint8_t>(players.size()));
    for (const RosterEntry& p : players) {
        w.put(p.id);
        w.put(p.link);
        w.put(static_cast<uint8_t>(p.ready ? 1 : 0));
        w.put_str(p.name, kNameLen);
        w.put_str(p.body, kBodyLen);
    }
}
inline bool read_roster(Reader& r, std::vector<RosterEntry>& out) {
    const auto n = r.get<uint8_t>();
    if (!r.ok() || n > kMaxPlayers) return false;
    out.clear();
    for (uint8_t i = 0; i < n; ++i) {
        RosterEntry p;
        p.id = r.get<uint8_t>();
        p.link = r.get<uint32_t>();
        p.ready = r.get<uint8_t>() != 0;
        p.name = r.get_str(kNameLen);
        p.body = r.get_str(kBodyLen);
        out.push_back(std::move(p));
    }
    return r.ok();
}

// The host's experience mode: ExperienceModeManager.Serialize(), e.g. {"m_CurrentModeType":"Stalker",...}, and its
// game's random seed (what TLD's rolls for spawn chances, container contents and gear condition start from).
constexpr size_t kGameModeMax = 8 * 1024;
inline void write_game_mode(Writer& w, const std::string& text, int32_t seed) {
    w.put_long_blob(text);
    w.put<int32_t>(seed);
}
inline bool read_game_mode(Reader& r, std::string& text, int32_t& seed) {
    text = r.get_long_blob(kGameModeMax);
    seed = r.get<int32_t>();
    return r.ok();
}

// Sleeping together (milestone 10 item 12). The host decides from everyone's State (kStateInBed, sleep_vote) and
// starts the sleep in every game; each game sleeps it TLD's own way.
enum class SleepOp : uint8_t { Start = 1, End = 2, Woke = 3 };
constexpr uint8_t kSleepChoiceWire = 10;  // TLDCoop_DLC: net::kSleepChoices (sleep_vote.h; checked in coop_sleep)
enum class SleepEnd : uint8_t { Done = 1, Woken = 2 };
// Stopped: a player passing time stopped it (Escape, STOP).
enum class SleepWhy : uint8_t { None = 0, Animal = 1, Cold = 2, Other = 3, NotInBed = 4, Stopped = 5 };
struct SleepMsg {
    SleepOp op = SleepOp::Start;
    uint32_t id = 0;      // the host's count of shared sleeps
    uint8_t choice = 0;   // Start: the SleepChoice that won
    float hours = 0;      // Start: how long (exact: till dawn needn't be whole hours)
    SleepEnd end = SleepEnd::Done;  // End
    uint8_t who = 0;      // End Woken: who woke
    SleepWhy why = SleepWhy::None;  // End Woken, Woke
};
inline void write_sleep(Writer& w, const SleepMsg& m) {
    w.put(m.op);
    w.put(m.id);
    w.put(m.choice);
    w.put(m.hours);
    w.put(m.end);
    w.put(m.who);
    w.put(m.why);
}
inline bool read_sleep(Reader& r, SleepMsg& m) {
    m.op = r.get<SleepOp>();
    m.id = r.get<uint32_t>();
    m.choice = r.get<uint8_t>();
    m.hours = r.get<float>();
    m.end = r.get<SleepEnd>();
    m.who = r.get<uint8_t>();
    m.why = r.get<SleepWhy>();
    // TLDCoop_DLC: only what the game can act on (an op, end and reason it knows; a choice of net::SleepChoice; a
    // Start's hours a real span of a day at most)
    if (!r.ok() || m.op < SleepOp::Start || m.op > SleepOp::Woke || m.end < SleepEnd::Done || m.end > SleepEnd::Woken ||
        m.why > SleepWhy::Stopped || m.choice >= kSleepChoiceWire || !std::isfinite(m.hours))
        return false;
    return m.op != SleepOp::Start || (m.hours > 0 && m.hours <= 24);
}

// Handing an item to a friend (milestone 13, net/handover.h). Each player's pack is their own: the giver offers, the
// receiver accepts, and the giver, still holding it, takes it out of their pack and sends it (Done, TLD's save
// record for what changes hands); the receiver puts that in theirs, once. A friend's go to the host, which passes
// them on to everyone (each game shows the two avatars). Withdraw: taken back (by the giver, or the receiver walked
// off); Failed: the giver couldn't give it after all (it's gone from their pack).
enum class GiveOp : uint8_t { Offer = 1, Withdraw = 2, Accept = 3, Done = 4, Failed = 5 };
struct GiveMsg {
    GiveOp op = GiveOp::Offer;
    uint32_t id = 0;        // the giver's (net::handover_id): unique in the game
    uint8_t giver = 0, receiver = 0;
    uint8_t item = 0;       // net::HandItem: which grip the clips use
    int32_t units = 1;      // how many of it (a stack's units; 1 otherwise)
    std::string gear;       // the prefab held out (GEAR_...): the copy in the hand, the notices
    std::string prefab;     // Done: the save record's prefab (GearItemSaveDataProxy.m_PrefabName)
    std::string record;     // Done: the save record (m_SearializedGear) of what changes hands
};
inline void write_give(Writer& w, const GiveMsg& m) {
    w.put(m.op);
    w.put(m.id);
    w.put(m.giver);
    w.put(m.receiver);
    w.put(m.item);
    w.put(m.units);
    w.put_blob(m.gear, kGearPrefabMax);
    w.put_blob(m.prefab, kGearPrefabMax);
    w.put_long_blob(m.record);
}
inline bool read_give(Reader& r, GiveMsg& m) {
    m.op = r.get<GiveOp>();
    m.id = r.get<uint32_t>();
    m.giver = r.get<uint8_t>();
    m.receiver = r.get<uint8_t>();
    m.item = r.get<uint8_t>();
    m.units = r.get<int32_t>();
    m.gear = r.get_blob(kGearPrefabMax);
    m.prefab = r.get_blob(kGearPrefabMax);
    m.record = r.get_long_blob(kGearTextMax);
    return r.ok() && m.op >= GiveOp::Offer && m.op <= GiveOp::Failed;
}

// --- wildlife ownership (v44, TLDCoop_DLC) -------------------------------------------------------------------------

// WildFrame: one owner's animals (see the header). Records as AnimalRec, with epoch, target and bleeding.
struct WildFrameMsg {
    uint8_t owner = 0;
    uint32_t frame = 0;
    double t_ms = 0;  // the owner's clock
    uint8_t part = 0, parts = 1;
    std::string scene;
    std::vector<AnimalRec> animals;
    double received_ms = 0;  // not sent
};
inline size_t wild_rec_size(const AnimalRec& a) { return 4 + 2 + 4 + 36 + 4 + 1 + a.layers.size() * 8 + 2 + a.params.size(); }
constexpr size_t kWildFrameHeader = 7 + 1 + 4 + 8 + 2 + kSceneLen + 1;

inline void write_wild_rec(Writer& w, const AnimalRec& a) {
    w.put(a.id);
    w.put(a.epoch);
    w.put(a.kind);
    w.put(a.mode);
    w.put(a.flags);
    w.put(a.target);
    for (float v : {a.x, a.y, a.z, a.yaw, a.pitch, a.roll, a.hp, a.bleed_elapsed, a.bleed_limit}) w.put(v);
    w.put(a.region);
    const size_t layers = a.layers.size() < kMaxAnimalLayers ? a.layers.size() : kMaxAnimalLayers;
    w.put(static_cast<uint8_t>(layers));
    for (size_t l = 0; l < layers; ++l) {
        w.put(a.layers[l].state);
        w.put(a.layers[l].time);
    }
    w.put_blob(a.params, kMaxAnimalParams);
}

inline bool read_wild_rec(Reader& r, AnimalRec& a) {
    a.id = r.get<uint32_t>();
    a.epoch = r.get<uint16_t>();
    a.kind = r.get<uint8_t>();
    a.mode = r.get<uint8_t>();
    a.flags = r.get<uint8_t>();
    a.target = r.get<uint8_t>();
    float* f[] = {&a.x, &a.y, &a.z, &a.yaw, &a.pitch, &a.roll, &a.hp, &a.bleed_elapsed, &a.bleed_limit};
    for (float* v : f) {
        *v = r.get<float>();
        if (!std::isfinite(*v)) return false;
    }
    a.region = r.get<uint32_t>();
    const uint8_t layers = r.get<uint8_t>();
    if (layers > kMaxAnimalLayers) return false;
    a.layers.clear();
    for (uint8_t l = 0; l < layers; ++l) {
        AnimalLayer layer;
        layer.state = r.get<int32_t>();
        layer.time = r.get<float>();
        a.layers.push_back(layer);
    }
    a.params = r.get_blob(kMaxAnimalParams);
    return r.ok();
}

inline void write_wild_frame(Writer& w, const WildFrameMsg& m) {
    w.put(m.owner);
    w.put(m.frame);
    w.put(m.t_ms);
    w.put(m.part);
    w.put(m.parts);
    w.put_str(m.scene, kSceneLen);
    w.put(static_cast<uint8_t>(m.animals.size()));
    for (const AnimalRec& a : m.animals) write_wild_rec(w, a);
}

inline bool read_wild_frame(Reader& r, WildFrameMsg& m) {
    m.owner = r.get<uint8_t>();
    m.frame = r.get<uint32_t>();
    m.t_ms = r.get<double>();
    m.part = r.get<uint8_t>();
    m.parts = r.get<uint8_t>();
    m.scene = r.get_str(kSceneLen);
    const uint8_t count = r.get<uint8_t>();
    m.animals.clear();
    for (uint8_t i = 0; i < count && r.ok(); ++i) {
        AnimalRec a;
        if (!read_wild_rec(r, a)) return false;
        m.animals.push_back(std::move(a));
    }
    return r.ok() && m.owner < kMaxPlayers;
}

// WildCtl: the host arbitrates who runs each animal.
enum class WildOp : uint8_t { Controllers = 1, Release = 2, Take = 3, Refuse = 4, Gone = 5, Kill = 6, Regions = 7, Keep = 8 };
constexpr uint8_t kTakeDead = 1;     // Take: the animal died on its owner; the host runs its carcass from here
constexpr uint8_t kTakeKept = 2;     // Take: the old owner keeps it (a fight began meanwhile); `to` is the old owner
constexpr uint8_t kTakeNoState = 4;  // Take: the old owner hasn't got it any more: the host's registry fills it in
constexpr uint8_t kTakeMoved = 8;    // v45 Take: nobody saw it for a while (hours): placed anew in its spawn region
constexpr uint8_t kTakeOld = 32;     // TLDCoop_DLC Take (with kTakeDead): an old body (kept/frozen): not a fresh kill
constexpr uint8_t kTakeBody = 16;    // v45 Take (with kTakeDead): `text` is its carcass (BodyHarvest.Serialize())
constexpr uint8_t kKillGone = 1;     // v45 Kill: it didn't die, it's just gone (TLD put it away): no respawn wait
constexpr uint8_t kKillCounted = 2;  // v45 Kill: TLD's own spawn region bookkeeping already counted it where it died
constexpr size_t kWildRegionsMax = 128;
// v45: one spawn region's respawn state, its times relative to the game hours of whoever sent it.
struct WildRegionState {
    uint32_t key = 0;
    int32_t pending = 0;        // SpawnRegion.m_NumRespawnsPending
    int32_t trapped = 0;        // m_NumTrapped
    float next_h = 0;           // m_ElapasedHoursNextRespawnAllowed - now
    float trap_reset_h = 0;     // m_HoursNextTrapReset - now
    float cooldown_h = 0;       // m_CooldownTimerHours - now
};
constexpr size_t kWildTextMax = 32 * 1024;  // BaseAi.Serialize()
constexpr size_t kWildScenesMax = 16;
struct WildSceneControl {
    std::string scene;
    uint8_t player = kNoPlayer;  // the game that lets TLD spawn there (kNoPlayer: none)
    uint8_t fresh = 0;           // 1: the registry has no animal there (that game shares its own)
};
struct WildCtlMsg {
    WildOp op = WildOp::Controllers;
    std::vector<WildSceneControl> scenes;  // Controllers
    uint8_t per_player = 0;                // Controllers (v46): % more animals for each player beyond the first
    uint32_t id = 0;                       // Release, Take, Refuse
    uint8_t to = kNoPlayer;                // Release, Take: the new owner
    uint16_t epoch = 0;                    // Release, Take, Refuse: the handover this belongs to
    uint8_t flags = 0;                     // Take: kTake*
    std::string scene;                     // Take
    AnimalRec state;                       // Take: as the old owner (or the registry) last had it
    std::string text;                      // Take: BaseAi.Serialize() ("" none); kTakeBody: BodyHarvest.Serialize()
    // v45
    double hours = 0;                      // Take kTakeMoved: game hours nobody saw it; Keep: game hours when frozen;
                                           // Regions: (host's store) its game hours when the states were reported
    uint32_t region = 0;                   // Kill: the spawn region (AnimalRec.region)
    uint8_t died_on = kNoPlayer;           // Kill: the game it died in
    uint8_t kind = 0;                      // Kill
    std::vector<WildRegionState> regions;  // Regions (scene: `scene`)
};

inline void write_wild_ctl(Writer& w, const WildCtlMsg& m) {
    w.put(static_cast<uint8_t>(m.op));
    switch (m.op) {
        case WildOp::Controllers: {
            const size_t n = m.scenes.size() < kWildScenesMax ? m.scenes.size() : kWildScenesMax;
            w.put(static_cast<uint8_t>(n));
            for (size_t i = 0; i < n; ++i) {
                w.put_str(m.scenes[i].scene, kSceneLen);
                w.put(m.scenes[i].player);
                w.put(m.scenes[i].fresh);
            }
            w.put(m.per_player);
            break;
        }
        case WildOp::Release:
            w.put(m.id);
            w.put(m.to);
            w.put(m.epoch);
            break;
        case WildOp::Take:
            w.put(m.id);
            w.put(m.to);
            w.put(m.epoch);
            w.put(m.flags);
            w.put_str(m.scene, kSceneLen);
            write_wild_rec(w, m.state);
            w.put_long_blob(m.text.size() <= kWildTextMax ? m.text : std::string());
            w.put(m.hours);
            break;
        case WildOp::Refuse:
        case WildOp::Gone:
            w.put(m.id);
            w.put(m.epoch);
            break;
        case WildOp::Kill:
            w.put(m.id);
            w.put(m.flags);
            w.put(m.died_on);
            w.put(m.kind);
            w.put(m.region);
            w.put_str(m.scene, kSceneLen);
            break;
        case WildOp::Regions: {
            w.put_str(m.scene, kSceneLen);
            w.put(m.hours);
            const size_t n = m.regions.size() < kWildRegionsMax ? m.regions.size() : kWildRegionsMax;
            w.put(static_cast<uint8_t>(n));
            for (size_t i = 0; i < n; ++i) {
                const WildRegionState& g = m.regions[i];
                w.put(g.key);
                w.put(g.pending);
                w.put(g.trapped);
                w.put(g.next_h);
                w.put(g.trap_reset_h);
                w.put(g.cooldown_h);
            }
            break;
        }
        case WildOp::Keep:
            w.put(m.id);
            w.put(m.epoch);
            w.put(m.flags);
            w.put(m.hours);
            w.put_str(m.scene, kSceneLen);
            write_wild_rec(w, m.state);
            w.put_long_blob(m.text.size() <= kWildTextMax ? m.text : std::string());
            break;
    }
}

inline bool read_wild_ctl(Reader& r, WildCtlMsg& m) {
    m.op = static_cast<WildOp>(r.get<uint8_t>());
    switch (m.op) {
        case WildOp::Controllers: {
            const uint8_t n = r.get<uint8_t>();
            if (n > kWildScenesMax) return false;
            m.scenes.clear();
            for (uint8_t i = 0; i < n && r.ok(); ++i) {
                WildSceneControl c;
                c.scene = r.get_str(kSceneLen);
                c.player = r.get<uint8_t>();
                c.fresh = r.get<uint8_t>();
                m.scenes.push_back(c);
            }
            m.per_player = r.get<uint8_t>();
            return r.ok();
        }
        case WildOp::Release:
            m.id = r.get<uint32_t>();
            m.to = r.get<uint8_t>();
            m.epoch = r.get<uint16_t>();
            return r.ok();
        case WildOp::Take:
            m.id = r.get<uint32_t>();
            m.to = r.get<uint8_t>();
            m.epoch = r.get<uint16_t>();
            m.flags = r.get<uint8_t>();
            m.scene = r.get_str(kSceneLen);
            if (!read_wild_rec(r, m.state)) return false;
            m.text = r.get_long_blob(kWildTextMax);
            m.hours = r.get<double>();
            return r.ok() && std::isfinite(m.hours);
        case WildOp::Refuse:
        case WildOp::Gone:
            m.id = r.get<uint32_t>();
            m.epoch = r.get<uint16_t>();
            return r.ok();
        case WildOp::Kill:
            m.id = r.get<uint32_t>();
            m.flags = r.get<uint8_t>();
            m.died_on = r.get<uint8_t>();
            m.kind = r.get<uint8_t>();
            m.region = r.get<uint32_t>();
            m.scene = r.get_str(kSceneLen);
            return r.ok();
        case WildOp::Regions: {
            m.scene = r.get_str(kSceneLen);
            m.hours = r.get<double>();
            const uint8_t n = r.get<uint8_t>();
            if (n > kWildRegionsMax) return false;
            m.regions.clear();
            for (uint8_t i = 0; i < n && r.ok(); ++i) {
                WildRegionState g;
                g.key = r.get<uint32_t>();
                g.pending = r.get<int32_t>();
                g.trapped = r.get<int32_t>();
                g.next_h = r.get<float>();
                g.trap_reset_h = r.get<float>();
                g.cooldown_h = r.get<float>();
                if (!std::isfinite(g.next_h) || !std::isfinite(g.trap_reset_h) || !std::isfinite(g.cooldown_h)) return false;
                m.regions.push_back(g);
            }
            return r.ok() && std::isfinite(m.hours);
        }
        case WildOp::Keep:
            m.id = r.get<uint32_t>();
            m.epoch = r.get<uint16_t>();
            m.flags = r.get<uint8_t>();
            m.hours = r.get<double>();
            m.scene = r.get_str(kSceneLen);
            if (!read_wild_rec(r, m.state)) return false;
            m.text = r.get_long_blob(kWildTextMax);
            return r.ok() && std::isfinite(m.hours);
    }
    return false;
}

// FlareShot (v47, TLDCoop_DLC): a flare-gun round someone fired, for the others to see fly and burn (no damage there).
struct FlareShotMsg {
    std::string scene;
    float pos[3] = {0, 0, 0};
    float rot[4] = {0, 0, 0, 1};
};
inline void write_flare_shot(Writer& w, const FlareShotMsg& m) {
    w.put_str(m.scene, kSceneLen);
    for (float v : m.pos) w.put(v);
    for (float v : m.rot) w.put(v);
}
inline bool read_flare_shot(Reader& r, FlareShotMsg& m) {
    m.scene = r.get_str(kSceneLen);
    for (float& v : m.pos) v = r.get<float>();
    for (float& v : m.rot) v = r.get<float>();
    for (float v : m.pos) if (!std::isfinite(v)) return false;
    for (float v : m.rot) if (!std::isfinite(v)) return false;
    return r.ok();
}

// WildHit: a hit (or a thrown item) on an animal another game runs, passed on by the host to that game.
enum class WildHitOp : uint8_t { Hit = 1, Thrown = 2 };
struct WildHitMsg {
    WildHitOp op = WildHitOp::Hit;
    uint8_t who = 0;  // the player who shot (threw)
    HitMsg hit;       // Hit
    ThrownMsg thrown; // Thrown
};
inline void write_wild_hit(Writer& w, const WildHitMsg& m) {
    w.put(static_cast<uint8_t>(m.op));
    w.put(m.who);
    if (m.op == WildHitOp::Hit) write_hit(w, m.hit);
    else write_thrown(w, m.thrown);
}
inline bool read_wild_hit(Reader& r, WildHitMsg& m) {
    m.op = static_cast<WildHitOp>(r.get<uint8_t>());
    m.who = r.get<uint8_t>();
    if (m.op == WildHitOp::Hit) return read_hit(r, m.hit) && r.ok();
    if (m.op == WildHitOp::Thrown) return read_thrown(r, m.thrown) && r.ok();
    return false;
}

}  // namespace net
