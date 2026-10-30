#include "coop_emotes.h"

#include <windows.h>

#include <cctype>

#include "coop_notices.h"
#include "log.h"
#include "net/emotes.h"
#include "net/session.h"
#include "net_runtime.h"

namespace coop_emotes {
namespace {

int g_keys[static_cast<int>(net::Emote::Count)] = {};
bool g_down[static_cast<int>(net::Emote::Count)] = {};
net::Emote g_emote = net::Emote::None;
uint8_t g_seq = 0;
double g_started = 0;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool game_in_front() {
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

void begin(net::Emote e) {
    g_emote = e;
    g_started = net::now_ms();
    ++g_seq;
    if (g_seq == 0) g_seq = 1;
    coop_notices::tell(net::emote_notice(e));
    logx::info("emotes: %s (gesture %u)", net::emote_key_name(e), g_seq);
}

}  // namespace

void set_keys(const std::vector<std::pair<std::string, std::string>>& from_ini) {
    std::string text;
    for (int i = 1; i < static_cast<int>(net::Emote::Count); ++i) {
        const auto e = static_cast<net::Emote>(i);
        std::string key = net::emote_default_key(e);
        for (const auto& [name, value] : from_ini)
            if (lower(name) == net::emote_key_name(e)) key = value;
        g_keys[i] = net::virtual_key(key);
        if (g_keys[i] == 0 && !key.empty() && lower(key) != "off")
            logx::warn("emotes: [emotes] %s=%s isn't a key (F1..F12, A..Z, 0..9, NUM0..NUM9, off)", net::emote_key_name(e),
                       key.c_str());
        text += std::string(text.empty() ? "" : ", ") + net::emote_key_name(e) + "=" + (g_keys[i] ? key : "off");
    }
    logx::info("emotes: keys %s", text.c_str());
}

void on_frame(bool in_game, bool can_gesture) {
    if (g_emote != net::Emote::None &&
        (net::now_ms() - g_started) / 1000.0 >= net::emote_seconds(g_emote))
        g_emote = net::Emote::None;
    const bool listen = in_game && can_gesture && netrt::status().connected && game_in_front();
    for (int i = 1; i < static_cast<int>(net::Emote::Count); ++i) {
        const bool down = g_keys[i] && (GetAsyncKeyState(g_keys[i]) & 0x8000) != 0;
        if (listen && down && !g_down[i]) begin(static_cast<net::Emote>(i));
        g_down[i] = down;
    }
}

void current(uint8_t& emote, uint8_t& seq) {
    emote = static_cast<uint8_t>(g_emote);
    seq = g_seq;
}

bool start(const std::string& name) {
    for (int i = 1; i < static_cast<int>(net::Emote::Count); ++i)
        if (lower(name) == net::emote_key_name(static_cast<net::Emote>(i))) {
            begin(static_cast<net::Emote>(i));
            return true;
        }
    return false;
}

}  // namespace coop_emotes
