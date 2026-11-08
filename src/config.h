#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace config {

struct Settings {
    bool autostart = false;            // start co-op at launch (testing); normally it starts from NEW > CO-OP
    std::string mode = "off";          // off | host | join; the role the CO-OP setup screen starts on
    std::string transport = "steam";   // steam (join codes) | local (UDP by IP, for tldbot / LAN)
    std::string code;                  // host: the code your friend enters (made once, then kept)
    std::string join_code;             // join: the code you were given
    std::string last_host;             // TLDCoop_DLC: join: the host we last got into, and with which code
    std::string last_host_code;
    std::string bind_ip = "127.0.0.1"; // UDP: our interface; 0.0.0.0 for LAN
    std::string host_ip = "127.0.0.1"; // UDP join: the host's address
    uint16_t port = 27015;
    std::string name = "Player";
    std::string body = "mackenzie_indoor";      // how the other player sees you (see the list in coop.ini)
    std::string voice = "male";        // male | female: your survivor's voice (TLD's own Male/Female choice)
    bool ask_to_join = true;           // hosting from the menu: each friend asks to join (LET IN / TURN AWAY)
    int backups = 5;                   // TLDCoop_DLC: copies of the saves kept (coop_backup), 0 none
    bool dlc_check = true;             // hosting: a friend must have every DLC we have installed (net/content.h)
    std::string body_shader = "own";   // own | tld: shaders used to draw the other player's story body
    float picker_scale = 0.5f;         // [struggle] how long TLD's weapon picker stays up in co-op (1 = TLD's own)
    // [hud] finding each other (milestone 12), each player's own:
    bool hud_names = true;             // friends' names over their heads, an arrow at the edge for those out of sight
    bool hud_radar = true;             // the round map of the area around us, friends on it
    bool hud_chart = true;             // our own map charts itself where we walk
    // TLDCoop_DLC: [colors] a friend's colour by their name (else each player's own by their number)
    std::vector<std::pair<std::string, std::string>> hud_colours;
    // TLDCoop_DLC: [emotes] a key for each gesture (wave, point, come, stop, yes, no)
    std::vector<std::pair<std::string, std::string>> emote_keys;
    // TLDCoop_DLC: [ambient] Ambient Lights (built in, after the MelonLoader mod by Xpazeman / Thekillergreece):
    // light through each interior's windows that follows the time of day and the weather. In co-op the host's
    // `enabled` counts for everyone (the CO-OP setup screen's MODS page); the levels are each player's own.
    bool ambient_lights = true;
    // TLDCoop_DLC: [houselights] House Lights (built in, by DemonBunnyBon / Xpazeman, with permission): the light
    // switches of houses work again. enabled: the host's choice for everyone (MODS); the rest each player's own.
    bool house_lights = true;
    // TLDCoop_DLC: [food] Food Tweaker (built in): realistic calories and meat cooking times; the host's choice (MODS)
    bool food_tweaker = false;
    // TLDCoop_DLC: [sky] Better Night Sky (built in, MIT): finer stars and a phased moon; each player's own look
    bool night_sky = true;
    // TLDCoop_DLC: [fuel] drain: a kerosene lamp's fuel back into a canister (Better Fuel Management's Drain); own
    bool fuel_drain = true;
    // TLDCoop_DLC: [clothing] enabled: ClothingExpanded's 28 items (off until tested: the 06.11 build crashed a start)
    bool clothing = false;
    float hl_intensity = 2.0f;      // how bright (the mod's 0-3)
    float hl_range = 1.4f;          // how far they reach (x their own range, at most 20 m)
    bool hl_white = false;          // whiter light
    bool hl_warm = true;            // TLDCoop_DLC: every lamp the same warm white (Karol 04.11)
    bool hl_no_flicker = true;      // no aurora flicker
    int hl_cull = 50;               // lights farther than this (m) stay off
    bool hl_shadows = false;        // they cast shadows
    bool hl_audio = false;          // the lamps hum
    float hl_reach = 1.0f;          // how near the switch must be (m)
    bool hl_outside = false;        // also in the regions (street lamps...)
    float ambient_intensity = 1.1f;    // the window lights' brightness (0 = off, 1 = the mod's own)
    float ambient_range = 1.0f;        // how far they reach
    int ambient_night = 1;             // 0 game default, 1 the mod's, 2 brighter nights, 3 endless day
    float ambient_ambience = 0.8f;     // TLD's own indoor ambient light (1 = TLD's)
    float ambient_fill = 0.7f;         // TLD's fill lights (1 = TLD's)
    float ambient_fill_color = 0.5f;   // how coloured TLD's coloured fill lights stay (1 = TLD's)
    float ambient_aurora_intensity = 0.8f;
    float ambient_aurora_saturation = 1.4f;
    // TLDCoop_DLC: [trees] Dynamic Trees (built in, with its authors' permission): snow on the trees that builds up
    // in snowy, windy weather and goes in clear, windy weather. In co-op the host's choice and the host's snow.
    bool dynamic_trees = true;
    // TLDCoop_DLC: [interiors] Seamless Interiors (built in, with its author's permission): a building's door takes
    // the player in without a loading screen. Off by default while it's being finished; in co-op the host's choice.
    bool seamless_interiors = false;
    // [interiors] import_old: a building used before the rooms (its interior is in the save) gets its room built from
    // that record (items, containers, furniture...), once; 0: such a building keeps the game's door.
    bool seamless_import_old = true;
    bool seamless_reimport_changed = true;  // [interiors] reimport_changed (TLDCoop_DLC)
    // [interiors] keep_rooms (A8): finished, saved rooms of the region just left are kept (at most 6) and put back on
    // the way back instead of being built again; 0: always built again.
    bool seamless_keep_rooms = true;
    // [interiors] auto_buildings (B4): buildings with no SI config (Timberwolf Mountain, Forlorn Muskeg, the DLC's...)
    // get their rooms too, found from their doors when a region loads; 0: only SI's 126 buildings. A friend in co-op
    // follows the host (only the host's rooms are built).
    bool seamless_auto_buildings = true;
    // [interiors] fire_effects (B3): a fire burning in a room smokes from the building's chimney and lights its windows
    // from outside at night; 0: off.
    bool seamless_fire_effects = true;
    // [interiors] windows (Stage 2): the rooms' windows show the real world outside (and, from outside, the buildings'
    // windows show their rooms); 0: the painted windows. window_quality: 0 low, 1 medium, 2 high (views drawn at once,
    // their resolution, rooms seen from outside and how far).
    bool seamless_windows = true;
    int seamless_window_quality = 1;
    // [interiors] indoor_light (TLDCoop_DLC): in a room, the outdoor ambient light at this % (rooms stand in the open,
    // lit like outdoors; the game's interiors are darker). 100: as outdoors.
    int seamless_indoor_light = 0;  // (0: the game's own interior light)
    // [interiors] door_portals (Stage 2): a building's door shows its room through the doorway (and the room's door the
    // world outside), and using it moves the player through with no fade; 0: the fade as before. door_walk: walking into
    // such a door (open, its view showing) takes the player through too; 0: only by using it.
    bool seamless_door_portals = true;
    bool seamless_door_walk = true;
    // TLDCoop_DLC: [death] lose_percent: how many of the ordinary items (not story, mission or undroppable ones) a death
    // takes, at random; the rest stay in the body.
    int death_lose_percent = 70;
    // TLDCoop_DLC (Shatsca 08.11): [death] mode: "cheat" = TLD's own Cheat Death (penalty, close/far respawn, the old
    // pack left in the world) while lives last, then co-op's new survivor; "coop" = co-op's body + new survivor always.
    // lives: Cheat Death's lives in co-op (whatever the experience mode gives).
    bool death_cheat = true;
    int death_lives = 3;
    // [test] section, for automated runs only (no menu clicks):
    std::string test_resume;           // load this save slot (e.g. sandbox5) as soon as the main menu is up
    bool test_nosave = false;          // never save: a second copy of the game playing the same save
    bool test_god = false;             // god mode, kept on (nobody dies while testing)
    std::string test_own_saves;        // keep this copy's saves in its own folder, first filled with these slots
    bool test_lobby = false;           // with resume=: stop at the Survival menu (the co-op screens by dev `menu`)
    std::string test_window;           // WxH[+X+Y]: a window this size (there), whatever the player's display settings say
};

// Reads coop.ini (paths::instance_dir()), writing a commented default file first if there is none.
Settings load();

// Writes the choices the co-op menu makes (mode, host_ip, name, body, the [hud] switches) back to coop.ini, keeping its
// comments.
void save(const Settings& s);

}  // namespace config
