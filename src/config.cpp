#include "config.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "log.h"
#include "paths.h"

namespace config {
namespace {

constexpr const char* kDefaultIni =
    "; TLDCoop settings. Restart the game after editing.\n"
    "[net]\n"
    "; Co-op starts when you pick CO-OP after NEW, RESUME or LOAD in the Survival menu; until then nothing goes online.\n"
    "; autostart=1 starts it at launch with the settings below instead (for testing).\n"
    "autostart=0\n"
    "\n"
    "; The role the CO-OP setup screen starts on (it saves your last choice here):\n"
    "; host = the other player joins you (your world, save, time and weather)\n"
    "; join = you join a host\n"
    "mode=off\n"
    "\n"
    "; steam = join codes through Steam (no IPs, no port forwarding); the co-op menu sets this up\n"
    "; local = UDP by IP, for testing on this PC (tldbot) or a LAN\n"
    "transport=steam\n"
    "\n"
    "; steam: code = your host code (your friend enters it); join_code = the code you were given\n"
    "code=\n"
    "join_code=\n"
    "\n"
    "; local only. bind_ip=0.0.0.0 to accept LAN players; host_ip is where 'join' connects.\n"
    "bind_ip=127.0.0.1\n"
    "host_ip=127.0.0.1\n"
    "port=27015\n"
    "\n"
    "name=Player\n"
    "\n"
    "; Hosting: each friend who joins asks first, and you LET IN or TURN AWAY (in the lobby, or while you play).\n"
    "; ask_to_join=0 lets anyone with your code straight in.\n"
    "ask_to_join=1\n"
    "\n"
    "; Hosting: each friend must have every DLC (optional content) you have installed, because your world can use\n"
    "; any of it; one who doesn't is told which DLC they lack. dlc_check=0 lets them in anyway (at your own risk:\n"
    "; DLC regions and items won't work for them).\n"
    "dlc_check=1\n"
    "\n"
    "; Before each co-op game your saves are copied to TLDCoop\\backups; this many copies are kept (0: none)\n"
    "backups=5\n"
    "\n"
    "; How the other player sees you (they need TLDCoop\\tldcoop_bodies.bundle; without it: a capsule):\n"
    ";   mackenzie_indoor, astrid, molly\n"
    "body=mackenzie_indoor\n"
    "\n"
    "; Shaders for the other player's story body: own (default) or tld (the game's own; experimental)\n"
    "body_shader=own\n"
    "\n"
    "[struggle]\n"
    "; In co-op, how long TLD's weapon picker (fists, knife, hatchet...) stays up when an animal jumps you, as a\n"
    "; fraction of the game's own time: 0.5 = half (harder), 1 = unchanged\n"
    "picker_scale=0.5\n"
    "\n"
    "[hud]\n"
    "; Finding each other, each switch your own (the co-op setup screen sets these too):\n"
    "; names = friends' names over their heads, and an arrow at the screen's edge for those out of sight\n"
    "; radar = a small round map of the area around you in a corner, with your friends on it\n"
    "; chart = your map charts itself where you walk, as charcoal mapping does (your own map only)\n"
    "names=1\n"
    "radar=1\n"
    "chart=1\n"
    "\n"
    "[colors]\n"
    "; Each player has a colour on the radar, on TLD's map and over their head (by their number: the same in every\n"
    "; game). Your own pick for a friend, by their name: red, orange, yellow, green, teal, blue, purple, pink, white,\n"
    "; grey, black or #RRGGBB, e.g.\n"
    "; Shatsca=blue\n"
    "\n"
    "[ambient]\n"
    "; Ambient Lights (built in, after the mod by Xpazeman / Thekillergreece, MIT): daylight through each interior's\n"
    "; windows that follows the time of day and the weather. In co-op the HOST decides for everyone (the CO-OP setup\n"
    "; screen's MODS button); playing alone, enabled= below. The levels are your own (1 = as the mod made them).\n"
    "enabled=1\n"
    "intensity=1.1\n"
    "range=1\n"
    "; night: 0 = the game's dark nights, 1 = the mod's, 2 = brighter nights, 3 = endless day\n"
    "night=1\n"
    "; TLD's own indoor lighting: ambience (overall glow), fill lights, how coloured its coloured fill lights stay\n"
    "ambience=0.8\n"
    "fill=0.7\n"
    "fill_color=0.5\n"
    "aurora_intensity=0.8\n"
    "aurora_saturation=1.4\n"
    "\n"
    "[trees]\n"
    "; Dynamic Trees (built in, by Fuar, The Illusion and Jods, with their permission): snow on the trees builds up\n"
    "; in snowy, windy weather and goes in clear, windy weather. In co-op the HOST decides for everyone (MODS).\n"
    "enabled=1\n"
    "\n"
    "[interiors]\n"
    "; Seamless Interiors (built in, by karsontr13, with his permission): a building's door takes you in and out\n"
    "; without a loading screen. Being finished: off by default. In co-op the HOST decides for everyone (MODS).\n"
    "enabled=0\n"
    "\n"
    "[death]\n"
    "; When you die, this many percent of your ordinary items are lost at random (story, mission and undroppable\n"
    "; items never); the rest stay in your body for anyone to take back. 0 = keep everything.\n"
    "lose_percent=70\n"
    "\n"
    "[report]\n"
    "; A bug report with one key: the moment is marked in the log and coop.log, coop-prev.log, coop-prev2.log and\n"
    "; coop.ini go into TLDCoop\\reports\\<time>_<name>.zip. all=1: in co-op every player's game makes its own report\n"
    "; at the same moment. Nothing is sent anywhere. key: F1..F12, A..Z, 0..9, NUM0..NUM9, off\n"
    "key=F11\n"
    "all=1\n"
    "\n"
    "[zoom]\n"
    "; JustZoom (built in, by WillWunderhorn, with his permission): hold the key to zoom in; the mouse wheel (while\n"
    "; holding it) zooms more or less. wheel_step 5..50 (how much a wheel notch zooms), speed 1..30 (how fast it\n"
    "; zooms), smooth 1..40 (how smoothly the view turns while zoomed). Your own: the others aren't affected.\n"
    "enabled=1\n"
    "key=Z\n"
    "wheel_step=15\n"
    "speed=12\n"
    "smooth=25\n"
    "\n"
    "[emotes]\n"
    "; Gestures the others see you make (a line on your HUD says which). Keys: F1..F12, A..Z, 0..9, NUM0..NUM9, off\n"
    "wave=F5\n"
    "point=F6\n"
    "come=F7\n"
    "stop=F8\n"
    "yes=F3\n"
    "no=F4\n";

std::string read(const std::filesystem::path& ini, const wchar_t* key, const std::string& fallback,
                 const wchar_t* section = L"net") {
    wchar_t value[256];
    std::wstring wide_fallback(fallback.begin(), fallback.end());
    GetPrivateProfileStringW(section, key, wide_fallback.c_str(), value, static_cast<DWORD>(std::size(value)),
                             ini.c_str());
    int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// TLDCoop_DLC: a section's name=value lines (comments left out), in UTF-8.
std::vector<std::pair<std::string, std::string>> read_section(const std::filesystem::path& ini, const wchar_t* section) {
    std::vector<std::pair<std::string, std::string>> out;
    std::wstring text(16384, L'\0');
    const DWORD n = GetPrivateProfileSectionW(section, text.data(), static_cast<DWORD>(text.size()), ini.c_str());
    auto utf8 = [](const std::wstring& w) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
        return s;
    };
    for (size_t i = 0; i < n && text[i];) {
        const std::wstring line(text.c_str() + i);
        i += line.size() + 1;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || line[0] == L';' || line[0] == L'#') continue;
        out.emplace_back(utf8(line.substr(0, eq)), utf8(line.substr(eq + 1)));
    }
    return out;
}

}  // namespace

void save(const Settings& s) {
    const auto ini = paths::instance_dir() / L"coop.ini";
    auto put = [&](const wchar_t* key, const std::string& value) {
        int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        std::wstring wide(static_cast<size_t>(n > 0 ? n : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), n);
        WritePrivateProfileStringW(L"net", key, wide.c_str(), ini.c_str());
    };
    put(L"mode", s.mode);
    put(L"transport", s.transport);
    put(L"code", s.code);
    put(L"join_code", s.join_code);
    put(L"host_ip", s.host_ip);
    put(L"name", s.name);
    put(L"body", s.body);
    put(L"voice", s.voice);
    WritePrivateProfileStringW(L"hud", L"names", s.hud_names ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hud", L"radar", s.hud_radar ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"hud", L"chart", s.hud_chart ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"ambient", L"enabled", s.ambient_lights ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"trees", L"enabled", s.dynamic_trees ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"interiors", L"enabled", s.seamless_interiors ? L"1" : L"0", ini.c_str());
    logx::info("settings saved: mode=%s transport=%s code=%s join_code=%s name=%s body=%s", s.mode.c_str(),
               s.transport.c_str(), s.code.c_str(), s.join_code.c_str(), s.name.c_str(), s.body.c_str());
}

Settings load() {
    const auto ini = paths::instance_dir() / L"coop.ini";
    if (!std::filesystem::exists(ini)) {
        std::ofstream(ini, std::ios::binary) << kDefaultIni;
        logx::info("wrote default %ls", ini.c_str());
    }

    Settings s;
    s.autostart = GetPrivateProfileIntW(L"net", L"autostart", 0, ini.c_str()) != 0;
    s.mode = read(ini, L"mode", s.mode);
    s.transport = read(ini, L"transport", s.transport);
    s.code = read(ini, L"code", s.code);
    s.join_code = read(ini, L"join_code", s.join_code);
    s.bind_ip = read(ini, L"bind_ip", s.bind_ip);
    s.host_ip = read(ini, L"host_ip", s.host_ip);
    s.port = static_cast<uint16_t>(GetPrivateProfileIntW(L"net", L"port", s.port, ini.c_str()));
    s.name = read(ini, L"name", s.name);
    s.body = read(ini, L"body", s.body);
    s.voice = read(ini, L"voice", s.voice);
    s.ask_to_join = GetPrivateProfileIntW(L"net", L"ask_to_join", 1, ini.c_str()) != 0;
    s.dlc_check = GetPrivateProfileIntW(L"net", L"dlc_check", 1, ini.c_str()) != 0;
    s.backups = static_cast<int>(GetPrivateProfileIntW(L"net", L"backups", 5, ini.c_str()));
    s.body_shader = read(ini, L"body_shader", s.body_shader);
    const std::string picker = read(ini, L"picker_scale", "", L"struggle");
    if (!picker.empty()) s.picker_scale = static_cast<float>(atof(picker.c_str()));
    s.hud_names = GetPrivateProfileIntW(L"hud", L"names", 1, ini.c_str()) != 0;
    s.hud_radar = GetPrivateProfileIntW(L"hud", L"radar", 1, ini.c_str()) != 0;
    s.hud_chart = GetPrivateProfileIntW(L"hud", L"chart", 1, ini.c_str()) != 0;
    s.hud_colours = read_section(ini, L"colors");  // TLDCoop_DLC
    s.ambient_lights = GetPrivateProfileIntW(L"ambient", L"enabled", 1, ini.c_str()) != 0;
    s.dynamic_trees = GetPrivateProfileIntW(L"trees", L"enabled", 1, ini.c_str()) != 0;
    s.seamless_interiors = GetPrivateProfileIntW(L"interiors", L"enabled", 0, ini.c_str()) != 0;
    s.death_lose_percent = static_cast<int>(GetPrivateProfileIntW(L"death", L"lose_percent", 70, ini.c_str()));
    auto level = [&](const wchar_t* key, float& v) {
        const std::string t = read(ini, key, "", L"ambient");
        if (!t.empty()) v = static_cast<float>(atof(t.c_str()));
    };
    level(L"intensity", s.ambient_intensity);
    level(L"range", s.ambient_range);
    s.ambient_night = static_cast<int>(GetPrivateProfileIntW(L"ambient", L"night", 1, ini.c_str()));
    level(L"ambience", s.ambient_ambience);
    level(L"fill", s.ambient_fill);
    level(L"fill_color", s.ambient_fill_color);
    level(L"aurora_intensity", s.ambient_aurora_intensity);
    level(L"aurora_saturation", s.ambient_aurora_saturation);
    s.emote_keys = read_section(ini, L"emotes");
    s.test_resume = read(ini, L"resume", "", L"test");
    s.test_nosave = GetPrivateProfileIntW(L"test", L"nosave", 0, ini.c_str()) != 0;
    s.test_god = GetPrivateProfileIntW(L"test", L"god", 0, ini.c_str()) != 0;
    s.test_own_saves = read(ini, L"own_saves", "", L"test");
    s.test_lobby = GetPrivateProfileIntW(L"test", L"lobby", 0, ini.c_str()) != 0;
    s.test_window = read(ini, L"window", "", L"test");
    if (!s.test_resume.empty() || s.test_nosave || s.test_god || !s.test_own_saves.empty())
        logx::info("settings: TEST resume=%s nosave=%d god=%d own_saves=%s lobby=%d window=%s", s.test_resume.c_str(),
                   s.test_nosave ? 1 : 0, s.test_god ? 1 : 0, s.test_own_saves.c_str(), s.test_lobby ? 1 : 0,
                   s.test_window.c_str());
    logx::info("settings: mode=%s transport=%s code=%s join_code=%s bind=%s host=%s port=%u name=%s body=%s "
               "hud names=%d radar=%d chart=%d",
               s.mode.c_str(), s.transport.c_str(), s.code.c_str(), s.join_code.c_str(), s.bind_ip.c_str(),
               s.host_ip.c_str(), s.port, s.name.c_str(), s.body.c_str(), s.hud_names ? 1 : 0, s.hud_radar ? 1 : 0,
               s.hud_chart ? 1 : 0);
    return s;
}

}  // namespace config
