#include "coop_report.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <random>
#include <share.h>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "coop_chat.h"
#include "coop_notices.h"
#include "log.h"
#include "net/emotes.h"
#include "net/protocol.h"
#include "net_runtime.h"
#include "paths.h"

namespace coop_report {
namespace {

namespace fs = std::filesystem;

constexpr double kRepeatMs = 10000;  // another press (or another player's) this soon makes no second report

int g_key = VK_F11;
bool g_all = true;
bool g_down = false;
double g_last_ms = -1e9;
uint32_t g_last_id = 0;
std::string g_name = "Player";

std::mutex g_done_mutex;
std::vector<std::string> g_done;  // what the packing thread made, for a notice on the main thread

double now_ms() { return static_cast<double>(GetTickCount64()); }

bool game_in_front() {
    DWORD pid = 0;
    if (HWND w = GetForegroundWindow()) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

std::string safe(const std::string& s) {
    std::string out;
    for (char c : s) out += (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "Player" : out.substr(0, 32);
}

// A copy of a file the game may still be writing (our log: open for writing, others may read).
bool copy_shared(const fs::path& from, const fs::path& to) {
    FILE* in = _wfsopen(from.c_str(), L"rb", _SH_DENYNO);
    if (!in) return false;
    FILE* out = _wfsopen(to.c_str(), L"wb", _SH_DENYWR);
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

// TLDCoop_DLC: coop.ini without the Steam codes (code=, join_code=): a report gets passed around.
bool copy_ini_blanked(const fs::path& from, const fs::path& to) {
    FILE* in = _wfsopen(from.c_str(), L"rb", _SH_DENYNO);
    if (!in) return false;
    std::string text;
    char buf[16 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) text.append(buf, n);
    fclose(in);
    if (text.size() >= 2 && text[0] == '\xFF' && text[1] == '\xFE') return false;  // (UTF-16: left out, not copied raw)
    std::string out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end + 1;
        std::string line = text.substr(at, end - at);
        at = end;
        size_t k = line.find_first_not_of(" \t");
        const size_t eq = line.find('=');
        if (k != std::string::npos && eq != std::string::npos && eq > k) {
            std::string key = line.substr(k, eq - k);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            for (char& c : key) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (key == "code" || key == "join_code") {
                const bool crlf = line.size() >= 2 && line.compare(line.size() - 2, 2, "\r\n") == 0;
                line = line.substr(0, eq + 1) + (crlf ? "\r\n" : (line.back() == '\n' ? "\n" : ""));
            }
        }
        out += line;
    }
    FILE* o = _wfsopen(to.c_str(), L"wb", _SH_DENYWR);
    if (!o) return false;
    fwrite(out.data(), 1, out.size(), o);
    fclose(o);
    return true;
}

// tar.exe (Windows 10 1803 and later) makes a zip with -a and a .zip name.
bool run_tar(const fs::path& dir, const fs::path& zip) {
    wchar_t system[MAX_PATH];
    if (!GetSystemDirectoryW(system, MAX_PATH)) return false;
    const fs::path tar = fs::path(system) / L"tar.exe";
    if (!fs::exists(tar)) return false;
    std::wstring cmd = L"\"" + tar.wstring() + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" + dir.wstring() + L"\" .";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 && fs::exists(zip);
}

void pack(std::string stamp, std::string description) {
    std::error_code ec;
    const fs::path base = paths::instance_dir();
    const fs::path reports = base / L"reports";
    const fs::path dir = reports / fs::path(stamp);
    fs::create_directories(dir, ec);
    int copied = 0;
    for (const wchar_t* f : {L"coop.log", L"coop-prev.log", L"coop-prev2.log"})
        if (fs::exists(base / f, ec) && copy_shared(base / f, dir / f)) ++copied;
    if (fs::exists(base / L"coop.ini", ec) && copy_ini_blanked(base / L"coop.ini", dir / L"coop.ini")) ++copied;
    if (!description.empty()) {  // what the player wrote about it
        if (FILE* out = _wfsopen((dir / L"opis.txt").c_str(), L"wb", _SH_DENYWR)) {
            fwrite("\xEF\xBB\xBF", 1, 3, out);  // (UTF-8 for Notepad)
            fwrite(description.data(), 1, description.size(), out);
            fclose(out);
            ++copied;
        }
    }
    const fs::path zip = reports / fs::path(stamp + ".zip");
    std::string made;
    if (run_tar(dir, zip)) {
        fs::remove_all(dir, ec);
        made = "TLDCoop\\reports\\" + stamp + ".zip";
    } else {
        made = "TLDCoop\\reports\\" + stamp + " (a folder: no zip could be made)";
    }
    logx::info("report: %d file(s) packed: %s", copied, made.c_str());
    std::lock_guard lock(g_done_mutex);
    g_done.push_back(made);
}

void make(uint32_t id, const std::string& who, bool ours, bool in_game) {
    const double now = now_ms();
    if (id == g_last_id || now - g_last_ms < kRepeatMs) return;
    g_last_id = id;
    g_last_ms = now;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[96];
    snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u_%02u-%02u-%02u_%s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond, safe(g_name).c_str());
    logx::info("==================== REPORT #%08x: %s pressed the report key%s ====================", id,
               ours ? "we" : who.c_str(), ours ? "" : " (their report, made here too)");
    if (!ours) {
        coop_notices::tell(who + " reported a bug: saving our logs too...");
        std::thread(pack, std::string(stamp), std::string()).detach();
        return;
    }
    // TLDCoop_DLC: outside a game (no HUD for the box: it ate the keys unseen for 2 minutes), or with a chat line
    // being typed (kept as it is): the logs at once, no description.
    if (!in_game || coop_chat::typing()) {
        coop_notices::tell("Bug report: saving the logs...");
        std::thread(pack, std::string(stamp), std::string()).detach();
        return;
    }
    // ours: a line about what happened first (the box opens; ENTER saves, ESC skips), then the logs are packed
    const std::string st = stamp;
    coop_chat::ask("OPISZ BŁĄD (ENTER - zapisz, ESC - pomiń):", [st, id](const std::string& description) {
        if (!description.empty())
            logx::info("==================== REPORT #%08x: what happened: %s ====================", id, description.c_str());
        coop_notices::tell("Bug report: saving the logs...");
        std::thread(pack, st, description).detach();
    });
}

void send(uint32_t id, const std::string& who, int except = -1) {
    net::Writer w(net::Msg::Report, 128);
    net::write_report(w, id, who);
    if (except >= 0)
        netrt::send_packet_except(static_cast<uint8_t>(except), w, true);
    else
        netrt::send_packet(w, true);
}

}  // namespace

void init() {
    const auto ini = paths::instance_dir() / L"coop.ini";
    wchar_t v[32];
    GetPrivateProfileStringW(L"report", L"key", L"F11", v, 32, ini.c_str());
    std::string key;
    for (const wchar_t* p = v; *p; ++p) key += static_cast<char>(*p);
    g_key = net::virtual_key(key);
    g_all = GetPrivateProfileIntW(L"report", L"all", 1, ini.c_str()) != 0;
    g_name = config::load().name;
    logx::info("report: %s%s", g_key ? ("the report key is " + key).c_str() : "no report key ([report] key=off)",
               g_all ? "; in co-op everyone's game makes a report with it" : "");
}

void on_frame(bool in_game) {
    const bool down = g_key && game_in_front() && (GetAsyncKeyState(g_key) & 0x8000) != 0;
    if (down && !g_down && now_ms() - g_last_ms >= kRepeatMs) {
        std::random_device rd;
        const uint32_t id = rd() | 1;
        make(id, g_name, true, in_game);
        if (g_all && netrt::status().connected) send(id, g_name);
    }
    g_down = down;

    static std::vector<net::Packet> in;
    in.clear();
    netrt::drain_packets(net::Msg::Report, in);
    const auto role = netrt::role();
    const bool host = role && *role == net::Role::Host;
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        uint32_t id = 0;
        std::string who;
        if (!net::read_report(r, id, who)) continue;
        if (host) send(id, who, p.from);  // the others hear it too
        if (g_all) make(id, who, false, in_game);
    }

    std::vector<std::string> done;
    {
        std::lock_guard lock(g_done_mutex);
        done.swap(g_done);
    }
    for (const std::string& d : done) coop_notices::tell("Bug report saved: " + d);
}

}  // namespace coop_report
