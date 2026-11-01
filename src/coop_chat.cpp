#include "coop_chat.h"

#include <windows.h>

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "config.h"
#include "coop_hud.h"
#include "il2cpp_api.h"
#include "log.h"
#include "net/protocol.h"
#include "net_runtime.h"

namespace coop_chat {
namespace {

constexpr size_t kMaxChars = 120;
constexpr double kLineMs = 15000;      // a line stays this long
constexpr size_t kShownLines = 6;      // at most this many at once (while typing: kTypingLines)
constexpr size_t kTypingLines = 8;
constexpr double kAskTimeoutMs = 120000;

struct Line {
    std::string text;
    double at = 0;
};

std::mutex g_mutex;          // the window's messages and the frame
bool g_typing = false;
std::wstring g_text;
bool g_submit = false, g_cancel = false;
bool g_enter_held = false;  // the ENTER that opened the line: its character isn't a send (it sent empty lines)
WNDPROC g_old_proc = nullptr;
HWND g_window = nullptr;

std::deque<Line> g_lines;
std::string g_prompt;
std::function<void(const std::string&)> g_ask;  // a question being answered (else it's chat)
double g_ask_since = 0;
bool g_enter_down = true;  // (as if held: the ENTER that started a game isn't ours)
bool g_ignoring = false;   // we set TLD's s_IgnoreInput
FieldInfo* g_ignore_input = nullptr;   // static InputManager.s_IgnoreInput
FieldInfo* g_input_context = nullptr;  // static InputManager.m_CurrentContext (a TLD screen holding the input)
std::string g_name = "Player";

double now_ms() { return static_cast<double>(GetTickCount64()); }

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// The game window's messages: while typing, characters go into the line and keys don't reach the game.
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    {
        std::lock_guard lock(g_mutex);
        if (g_typing) {
            switch (msg) {
                case WM_CHAR: {
                    static bool told = false;
                    if (!told) {
                        told = true;
                        logx::info("chat: the first typed character came in (WM_CHAR)");
                    }
                    const wchar_t c = static_cast<wchar_t>(wp);
                    if (c == L'\r') { if (!g_enter_held) g_submit = true; }
                    else if (c == 27) g_cancel = true;
                    else if (c == 8) { if (!g_text.empty()) g_text.pop_back(); }
                    else if (c >= 32 && g_text.size() < kMaxChars) g_text += c;
                    return 0;
                }
                case WM_KEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYDOWN:
                case WM_SYSKEYUP:
                    if (wp == VK_ESCAPE && msg == WM_KEYDOWN) g_cancel = true;
                    if (wp == VK_RETURN && msg == WM_KEYUP) g_enter_held = false;
                    return 0;  // (TranslateMessage already made the WM_CHAR)
                default:
                    break;
            }
        }
    }
    return CallWindowProcW(g_old_proc, hwnd, msg, wp, lp);
}

BOOL CALLBACK find_window(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[64];
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"UnityWndClass") != 0) return TRUE;
    *reinterpret_cast<HWND*>(out) = hwnd;
    return FALSE;
}

void hook_window() {
    if (g_old_proc) return;
    HWND w = nullptr;
    EnumWindows(find_window, reinterpret_cast<LPARAM>(&w));
    if (!w) return;
    g_window = w;
    g_old_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(w, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc)));
    logx::info("chat: %s", g_old_proc ? "listening to the game window's keys (ENTER opens the chat)" : "couldn't hook the game window");
}

bool game_focused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

void set_ignore(bool on) {
    if (!g_ignore_input) return;
    bool v = on;
    il2::il2cpp_field_static_set_value(g_ignore_input, &v);
}

bool tld_screen_open() {
    Il2CppObject* ctx = nullptr;
    if (g_input_context) il2::il2cpp_field_static_get_value(g_input_context, &ctx);
    return ctx != nullptr;
}

void add_line(const std::string& text) {
    g_lines.push_back({text, now_ms()});
    while (g_lines.size() > 20) g_lines.pop_front();
}

void open(const std::string& prompt) {
    std::lock_guard lock(g_mutex);
    g_typing = true;
    g_text.clear();
    g_submit = g_cancel = false;
    g_enter_held = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    g_prompt = prompt;
}

void close() {
    std::lock_guard lock(g_mutex);
    g_typing = false;
    g_text.clear();
}

void send_chat(const std::string& text) {
    net::Writer w(net::Msg::Chat, 1024);
    net::write_chat(w, g_name, text);
    netrt::send_packet(w, true);
}

}  // namespace

void init() {
    if (Il2CppClass* im = il2::find_class("Assembly-CSharp.dll", "", "InputManager")) {
        g_ignore_input = il2::il2cpp_class_get_field_from_name(im, "s_IgnoreInput");
        g_input_context = il2::il2cpp_class_get_field_from_name(im, "m_CurrentContext");
    }
    g_name = config::load().name;
}

bool typing() { return g_typing; }

void ask(const std::string& prompt, std::function<void(const std::string&)> done) {
    hook_window();
    if (!g_old_proc) {  // no window to type in: no description
        if (done) done("");
        return;
    }
    if (g_ask) g_ask("");  // (an older question goes unanswered)
    g_ask = std::move(done);
    g_ask_since = now_ms();
    open(prompt);
}

void on_frame(bool in_game) {
    hook_window();
    const double now = now_ms();
    const bool connected = netrt::status().connected;

    // ENTER opens the chat (in a co-op game, nothing of TLD's open, not already typing)
    const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    if (enter && !g_enter_down && !g_typing) {
        // (each reason it doesn't open is logged: "chat doesn't work" with nothing else to go on)
        Il2CppObject* ctx = nullptr;
        if (g_input_context) il2::il2cpp_field_static_get_value(g_input_context, &ctx);
        const char* why = !in_game ? "not in the game" : !connected ? "not connected (co-op off or the link is down)"
                        : !game_focused() ? "the game window isn't in front" : !g_old_proc ? "the game window isn't hooked"
                        : nullptr;
        if (why) {
            logx::info("chat: ENTER, but no chat: %s", why);
        } else if (ctx) {
            logx::info("chat: ENTER, but no chat: TLD's %s has the input", il2::class_name_of(ctx).c_str());
        } else {
            open("CZAT:");
            logx::info("chat: ENTER: the chat line is open");
        }
    }
    g_enter_down = enter;

    // what was typed
    bool submit = false, cancel = false;
    std::wstring text;
    {
        std::lock_guard lock(g_mutex);
        submit = g_submit;
        cancel = g_cancel;
        g_submit = g_cancel = false;
        text = g_text;
    }
    if (g_typing && g_ask && now - g_ask_since > kAskTimeoutMs) cancel = true;
    if (g_typing && !in_game && !g_ask) cancel = true;
    if (submit || cancel) {
        const std::string said = submit ? utf8(text) : std::string();
        logx::info("chat: the line %s (%zu chars)%s", submit ? "sent" : "dropped", said.size(), g_ask ? " (an answer)" : "");
        close();
        if (g_ask) {
            auto done = std::move(g_ask);
            g_ask = nullptr;
            done(said);
        } else if (!said.empty() && connected) {
            send_chat(said);
            add_line(g_name + ": " + said);
            logx::info("chat: we said: %s", said.c_str());
        }
        g_enter_down = true;  // (the ENTER that sent it doesn't open it again)
    }

    // TLD's input off while typing (the player doesn't walk or open things with the letters)
    if (g_typing != g_ignoring) {
        set_ignore(g_typing);
        g_ignoring = g_typing;
    } else if (g_typing) {
        set_ignore(true);
    }

    // what the others say (the host passes each friend's on)
    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Chat, in);
    const auto role = netrt::role();
    const bool host = role && *role == net::Role::Host;
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        std::string who, said;
        if (!net::read_chat(r, who, said)) continue;
        if (host) {
            net::Writer w(net::Msg::Chat, 1024);
            net::write_chat(w, who, said);
            netrt::send_packet_except(p.from, w, true);
        }
        add_line(who + ": " + said);
        logx::info("chat: %s: %s", who.c_str(), said.c_str());
    }

    // the box
    std::vector<std::string> shown;
    const size_t most = g_typing ? kTypingLines : kShownLines;
    for (auto it = g_lines.rbegin(); it != g_lines.rend() && shown.size() < most; ++it)
        if (g_typing || now - it->at < kLineMs) shown.insert(shown.begin(), it->text);
    coop_hud::set_chat(in_game ? shown : std::vector<std::string>{}, g_typing && in_game, g_prompt, utf8(text));
}

}  // namespace coop_chat
