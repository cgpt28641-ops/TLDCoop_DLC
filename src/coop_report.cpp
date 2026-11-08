#include "coop_report.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <deque>
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

struct Done {
    std::string made;        // for the notice
    fs::path zip;            // the zip made ("" if only a folder)
    uint32_t want_id = 0;    // TLDCoop_DLC: "POBIERZ LOGI": who asked for it (sent there once packed)
    std::string want_to;
};
std::mutex g_done_mutex;
std::vector<Done> g_done;  // what the packing thread made, for a notice on the main thread
// TLDCoop_DLC: parts of our logs' zip waiting to go out, one at a time
std::deque<net::LogsFileMsg> g_paced;
double g_paced_ms = 0;
constexpr double kLogsPaceMs = 1500;  // (20:10: 192 KB every 300 ms pushed the rtt to 3.7 s, containers "no answer")
// TLDCoop_DLC: zips coming in for our "POBIERZ LOGI"
struct Incoming {
    std::string from, name;
    std::vector<std::string> parts;
    size_t got = 0;
};
std::map<std::string, Incoming> g_incoming;  // by id/from/name
uint32_t g_collect_id = 0;
std::string g_collect_dir;  // TLDCoop\reports\<time>_logi_wszystkich

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

void pack(std::string stamp, std::string description, uint32_t want_id = 0, std::string want_to = {},
          std::string subdir = {}) {
    std::error_code ec;
    const fs::path base = paths::instance_dir();
    const fs::path reports = subdir.empty() ? base / L"reports" : base / L"reports" / fs::path(subdir);
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
    const std::string where = subdir.empty() ? "TLDCoop\\reports\\" : "TLDCoop\\reports\\" + subdir + "\\";
    Done d;
    d.want_id = want_id;
    d.want_to = want_to;
    if (run_tar(dir, zip)) {
        fs::remove_all(dir, ec);
        d.made = where + stamp + ".zip";
        d.zip = zip;
    } else {
        d.made = where + stamp + " (a folder: no zip could be made)";
    }
    logx::info("report: %d file(s) packed: %s", copied, d.made.c_str());
    std::lock_guard lock(g_done_mutex);
    g_done.push_back(std::move(d));
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
        std::thread(pack, std::string(stamp), std::string(), 0u, std::string(), std::string()).detach();
        return;
    }
    // TLDCoop_DLC: outside a game (no HUD for the box: it ate the keys unseen for 2 minutes), or with a chat line
    // being typed (kept as it is): the logs at once, no description.
    if (!in_game || coop_chat::typing()) {
        coop_notices::tell("Bug report: saving the logs...");
        std::thread(pack, std::string(stamp), std::string(), 0u, std::string(), std::string()).detach();
        return;
    }
    // ours: a line about what happened first (the box opens; ENTER saves, ESC skips), then the logs are packed
    const std::string st = stamp;
    coop_chat::ask("OPISZ BŁĄD (ENTER - zapisz, ESC - pomiń):", [st, id](const std::string& description) {
        if (!description.empty())
            logx::info("==================== REPORT #%08x: what happened: %s ====================", id, description.c_str());
        coop_notices::tell("Bug report: saving the logs...");
        std::thread(pack, st, description, 0u, std::string(), std::string()).detach();
    });
}

std::string stamp_now(const char* tail) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[96];
    snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u_%02u-%02u-%02u_%s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond, tail);
    return stamp;
}

// TLDCoop_DLC: who asks for logs and who they go to: the name and the player's number (04.11 23:09: everyone's coop.ini
// came back from the zip with name=Player, so "the one who asked" was everyone, and each game skipped the request
// as its own: no logs came)
std::string me_key() { return g_name + "#" + std::to_string(netrt::status().self_id); }
std::string shown(const std::string& key) {
    const size_t hash = key.rfind('#');
    return hash == std::string::npos ? key : key.substr(0, hash);
}

// TLDCoop_DLC: a zip of our logs, to the player who asked, in parts (the host passes them on).
void send_zip(const Done& d) {
    FILE* in = _wfsopen(d.zip.c_str(), L"rb", _SH_DENYNO);
    if (!in) return;
    std::string bytes;
    char buf[64 * 1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0 && bytes.size() <= net::kLogsFileMax) bytes.append(buf, n);
    fclose(in);
    if (bytes.size() > net::kLogsFileMax) {
        logx::warn("report: our logs' zip is too big to send (%zu bytes)", bytes.size());
        return;
    }
    net::LogsFileMsg m;
    m.id = d.want_id;
    m.to = d.want_to;
    m.from = g_name;
    m.name = d.zip.filename().string();
    if (m.name.empty() || m.name.size() > 150) m.name = "logs.zip";
    m.parts = static_cast<uint16_t>(std::max<size_t>(1, (bytes.size() + net::kLogsPart - 1) / net::kLogsPart));
    // TLDCoop_DLC: one part every kLogsPaceMs (send_paced), so the game's own changes go out between them
    for (uint16_t i = 0; i < m.parts; ++i) {
        m.part = i;
        m.bytes = bytes.substr(static_cast<size_t>(i) * net::kLogsPart, net::kLogsPart);
        g_paced.push_back(m);
    }
    logx::info("report: our logs (%zu bytes, %u part(s)) going to %s, one part every %u ms", bytes.size(), m.parts,
               shown(d.want_to).c_str(), static_cast<unsigned>(kLogsPaceMs));
}

void send_paced() {
    if (g_paced.empty()) return;
    if (!netrt::status().connected) {
        g_paced.clear();
        return;
    }
    const double now = now_ms();
    if (now - g_paced_ms < kLogsPaceMs) return;
    g_paced_ms = now;
    net::Writer w(net::Msg::LogsFile, net::kMaxMessage);
    net::write_logs_file(w, g_paced.front());
    netrt::send_packet(w, true);
    if (g_paced.front().part + 1 == g_paced.front().parts)
        logx::info("report: the last part of our logs sent to %s", shown(g_paced.front().to).c_str());
    g_paced.pop_front();
}

void got_part(const net::LogsFileMsg& m) {
    const std::string key = std::to_string(m.id) + "/" + m.from + "/" + m.name;
    Incoming& in = g_incoming[key];
    if (in.parts.empty()) in.parts.resize(m.parts), in.from = m.from, in.name = m.name;
    if (m.part >= in.parts.size() || !in.parts[m.part].empty()) return;
    in.parts[m.part] = m.bytes;
    if (++in.got < in.parts.size()) return;
    std::error_code ec;
    const fs::path dir = paths::instance_dir() / L"reports" / fs::path(g_collect_dir.empty() ? "logi_od_innych" : g_collect_dir);
    fs::create_directories(dir, ec);
    const std::string file = safe(m.from) + "_" + safe(fs::path(m.name).stem().string()) + ".zip";
    size_t size = 0;
    if (FILE* out = _wfsopen((dir / fs::path(file)).c_str(), L"wb", _SH_DENYWR)) {
        for (const std::string& p : in.parts) fwrite(p.data(), 1, p.size(), out), size += p.size();
        fclose(out);
    }
    g_incoming.erase(key);
    logx::info("report: %s's logs came in (%zu bytes): %s", m.from.c_str(), size, file.c_str());
    coop_notices::tell("Logi od " + m.from + " zapisane (TLDCoop\\reports\\" + (g_collect_dir.empty() ? "logi_od_innych" : g_collect_dir) + ")");
}

void send(uint32_t id, const std::string& who, int except = -1) {
    net::Writer w(net::Msg::Report, 128);
    net::write_report(w, id, who);
    if (except >= 0)
        netrt::send_packet_except(static_cast<uint8_t>(except), w, true);
    else
        netrt::send_packet(w, true);
}

void send_want(uint32_t id, const std::string& who, int except = -1) {
    net::Writer w(net::Msg::LogsWant, 128);
    net::write_report(w, id, who);
    if (except >= 0)
        netrt::send_packet_except(static_cast<uint8_t>(except), w, true);
    else
        netrt::send_packet(w, true);
}

}  // namespace

void collect_all() {
    std::random_device rd;
    g_collect_id = rd() | 1;
    g_collect_dir = stamp_now("logi_wszystkich");
    g_incoming.clear();
    logx::info("==================== LOGS #%08x: we asked for everyone's logs ====================", g_collect_id);
    std::thread(pack, stamp_now(safe(g_name).c_str()), std::string(), 0u, std::string(), g_collect_dir).detach();
    if (netrt::status().connected) send_want(g_collect_id, me_key());
    coop_notices::tell("Zbieram logi wszystkich graczy...");
}

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

void set_name(const std::string& name) {
    if (!name.empty()) g_name = name;
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

    // TLDCoop_DLC: "POBIERZ LOGI": someone asks for our logs; parts of others' logs (ours if we asked)
    in.clear();
    netrt::drain_packets(net::Msg::LogsWant, in);
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        uint32_t id = 0;
        std::string who;
        if (!net::read_report(r, id, who)) continue;
        if (host) send_want(id, who, p.from);
        if (who == me_key()) continue;
        logx::info("==================== LOGS #%08x: %s asked for everyone's logs; ours packed and sent ====================", id,
                   shown(who).c_str());
        std::thread(pack, stamp_now(safe(g_name).c_str()), std::string(), id, who, std::string()).detach();
    }
    in.clear();
    netrt::drain_packets(net::Msg::LogsFile, in);
    for (const net::Packet& p : in) {
        net::Reader r = net::packet_body(p);
        net::LogsFileMsg m;
        if (!net::read_logs_file(r, m)) continue;
        if (m.to == me_key()) {
            got_part(m);
        } else if (host) {  // on to the one who asked
            net::Writer w(net::Msg::LogsFile, net::kMaxMessage);
            net::write_logs_file(w, m);
            netrt::send_packet_except(p.from, w, true);
        }
    }

    send_paced();

    std::vector<Done> done;
    {
        std::lock_guard lock(g_done_mutex);
        done.swap(g_done);
    }
    for (const Done& d : done) {
        if (d.want_id && !d.want_to.empty() && !d.zip.empty() && netrt::status().connected) {
            send_zip(d);
            coop_notices::tell(shown(d.want_to) + " pobiera logi: nasze wysłane");
        } else {
            const bool collected = !g_collect_dir.empty() && d.made.find(g_collect_dir) != std::string::npos;
            coop_notices::tell((collected ? "Nasze logi: " : "Bug report saved: ") + d.made);
        }
    }
}

}  // namespace coop_report
