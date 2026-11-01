#include "coop_update.h"

#include <windows.h>
#include <wininet.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "coop_notices.h"
#include "log.h"
#include "paths.h"

#if __has_include("update_secret.inc")
#include "update_secret.inc"  // (CI writes it: kSecret/kSecretLen/kSecretKey, the token xor'ed; kBuild, our commit)
#else
namespace {
constexpr unsigned char kSecret[] = {0};
constexpr size_t kSecretLen = 0;
constexpr unsigned char kSecretKey = 0;
constexpr const char* kBuild = "";
}  // namespace
#endif

namespace coop_update {
namespace {

namespace fs = std::filesystem;
constexpr const char* kReleaseUrl = "https://api.github.com/repos/cgpt28641-ops/TLDCoop_DLC/releases/tags/dll-latest";

std::mutex g_mutex;
std::string g_message;
std::atomic<bool> g_started{false};
bool g_told_in_game = false;
double g_menu_since = 0;

std::string token() {
    std::string t;
    for (size_t i = 0; i < kSecretLen; ++i) t += static_cast<char>(kSecret[i] ^ static_cast<unsigned char>(kSecretKey + i * 7));
    return t;
}

fs::path game_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

// One HTTPS GET with these headers. No redirect followed: `location` gets it. Body into `out`. The HTTP status.
DWORD get(HINTERNET net, const std::string& url, const std::string& headers, std::string& out, std::string& location) {
    out.clear();
    location.clear();
    HINTERNET req = InternetOpenUrlA(net, url.c_str(), headers.empty() ? nullptr : headers.c_str(),
                                     headers.empty() ? 0 : static_cast<DWORD>(-1),
                                     INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_AUTO_REDIRECT | INTERNET_FLAG_RELOAD |
                                         INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES,
                                     0);
    if (!req) return 0;
    DWORD status = 0, len = sizeof(status);
    HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);
    if (status == 301 || status == 302 || status == 307) {
        char loc[4096];
        DWORD ll = sizeof(loc);
        if (HttpQueryInfoA(req, HTTP_QUERY_LOCATION, loc, &ll, nullptr)) location.assign(loc, ll);
    } else {
        char buf[65536];
        DWORD got = 0;
        while (InternetReadFile(req, buf, sizeof(buf), &got) && got) out.append(buf, got);
    }
    InternetCloseHandle(req);
    return status;
}

// "key": "value" from `from` on (GitHub's release JSON, scanned plainly).
std::string json_str(const std::string& j, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    const size_t k = j.find("\"" + key + "\"", from);
    if (k == std::string::npos) return {};
    const size_t q = j.find('"', j.find(':', k) + 1);
    if (q == std::string::npos) return {};
    std::string v;
    for (size_t i = q + 1; i < j.size() && j[i] != '"'; ++i) {
        if (j[i] == '\\' && i + 1 < j.size()) ++i;
        v += j[i];
    }
    if (at) *at = k;
    return v;
}

void tell(const std::string& m) {
    std::lock_guard lock(g_mutex);
    g_message = m;
}

void run() {
    HINTERNET net = InternetOpenA("TLDCoop-update", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!net) {
        logx::warn("update: no internet handle (%lu)", GetLastError());
        return;
    }
    const std::string auth = "Authorization: Bearer " + token() + "\r\nUser-Agent: TLDCoop\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    std::string body, loc;
    DWORD st = get(net, kReleaseUrl, auth + "Accept: application/vnd.github+json\r\n", body, loc);
    if (st != 200) {
        logx::warn("update: couldn't ask GitHub for the latest build (HTTP %lu)", st);
        InternetCloseHandle(net);
        return;
    }
    // the notes say "Only winhttp.dll, build <sha> (run <id>)"
    const std::string notes = json_str(body, "body");
    const size_t b = notes.find("build ");
    const std::string latest = b == std::string::npos ? std::string() : notes.substr(b + 6, notes.find_first_of(" )", b + 6) - b - 6);
    std::string asset_url;
    for (size_t from = body.find("\"assets\""), at = 0; from != std::string::npos;) {
        const std::string name = json_str(body, "name", from, &at);
        if (name.empty()) break;
        if (name == "winhttp.dll") {
            asset_url = json_str(body, "url", body.rfind("\"url\"", at));
            break;
        }
        from = at + 6;
    }
    if (latest.empty() || asset_url.empty()) {
        logx::warn("update: the latest build's notes or file weren't found");
        InternetCloseHandle(net);
        return;
    }
    if (latest == kBuild) {
        logx::info("update: we are the latest build (%s)", kBuild);
        InternetCloseHandle(net);
        return;
    }
    logx::info("update: build %s is out (ours %s); downloading it", latest.c_str(), kBuild);
    std::string dll;
    st = get(net, asset_url, auth + "Accept: application/octet-stream\r\n", dll, loc);
    if (st >= 300 && st < 400 && !loc.empty()) st = get(net, loc, "User-Agent: TLDCoop\r\n", dll, loc);  // (signed URL)
    InternetCloseHandle(net);
    if (st != 200 || dll.size() < 100000 || dll[0] != 'M' || dll[1] != 'Z') {
        logx::warn("update: the download failed (HTTP %lu, %zu bytes)", st, dll.size());
        return;
    }
    const fs::path dir = game_dir(), ours = dir / L"winhttp.dll", fresh = dir / L"winhttp.dll.new", old = dir / L"winhttp.dll.old";
    if (FILE* f = _wfopen(fresh.c_str(), L"wb")) {
        fwrite(dll.data(), 1, dll.size(), f);
        fclose(f);
    } else {
        logx::warn("update: couldn't write winhttp.dll.new");
        return;
    }
    DeleteFileW(old.c_str());
    if (!MoveFileExW(ours.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        logx::warn("update: couldn't move ours aside (%lu); the new one waits as winhttp.dll.new", GetLastError());
        return;
    }
    if (!MoveFileExW(fresh.c_str(), ours.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        logx::warn("update: couldn't put the new one in place (%lu); ours back", GetLastError());
        MoveFileExW(old.c_str(), ours.c_str(), 0);
        return;
    }
    logx::info("update: build %s is in place (%zu bytes); it runs from the next start", latest.c_str(), dll.size());
    tell("Pobrano nową wersję moda (" + latest + "). Uruchom grę ponownie.");
}

}  // namespace

void init() {
    DeleteFileW((game_dir() / L"winhttp.dll.old").c_str());  // the one the last update replaced
    logx::info("update: build %s%s", *kBuild ? kBuild : "(local, no build id)",
               kSecretLen ? "" : "; no update token built in: no self-update");
}

std::string message() {
    std::lock_guard lock(g_mutex);
    return g_message;
}

void on_frame(const std::string& scene, bool in_game) {
    if (in_game && !g_told_in_game) {
        if (const std::string m = message(); !m.empty()) {
            coop_notices::tell(m);
            g_told_in_game = true;
        }
    }
    if (!kSecretLen || !*kBuild || g_started) return;
    if (scene != "MainMenu") {
        g_menu_since = 0;
        return;
    }
    const double now = static_cast<double>(GetTickCount64());
    if (!g_menu_since) g_menu_since = now;
    if (now - g_menu_since < 3000) return;
    g_started = true;
    if (!GetPrivateProfileIntW(L"update", L"enabled", 1, (paths::instance_dir() / L"coop.ini").c_str())) {
        logx::info("update: off (coop.ini [update] enabled=0)");
        return;
    }
    std::thread(run).detach();
}

}  // namespace coop_update
