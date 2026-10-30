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
    // `enabled` counts for everyone (the CO-OP setup screen's AMBIENT LIGHTS row); the levels are each player's own.
    bool ambient_lights = true;
    float ambient_intensity = 1.1f;    // the window lights' brightness (0 = off, 1 = the mod's own)
    float ambient_range = 1.0f;        // how far they reach
    int ambient_night = 1;             // 0 game default, 1 the mod's, 2 brighter nights, 3 endless day
    float ambient_ambience = 0.8f;     // TLD's own indoor ambient light (1 = TLD's)
    float ambient_fill = 0.7f;         // TLD's fill lights (1 = TLD's)
    float ambient_fill_color = 0.5f;   // how coloured TLD's coloured fill lights stay (1 = TLD's)
    float ambient_aurora_intensity = 0.8f;
    float ambient_aurora_saturation = 1.4f;
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
