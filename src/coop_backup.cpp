#include "coop_backup.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <thread>
#include <vector>

#include "coop_notices.h"
#include "il2cpp_api.h"
#include "log.h"
#include "paths.h"

namespace coop_backup {
namespace {

namespace fs = std::filesystem;
constexpr const char* kGame = "Assembly-CSharp.dll";

FieldInfo* g_path_survival = nullptr;  // static string PersistentDataPath.m_PathSurvival
int g_keep = 5;
std::string g_last_scene;
bool g_done_this_load = false;

fs::path survival_folder() {
    if (!g_path_survival) return {};
    Il2CppString* text = nullptr;
    il2::il2cpp_field_static_get_value(g_path_survival, &text);
    if (!text) return {};
    const std::string utf8 = il2::to_utf8(text);
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), n);
    wide.resize(wcslen(wide.c_str()));
    return fs::path(wide);
}

std::wstring stamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t s[32];
    swprintf(s, 32, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return s;
}

// The copy, then the oldest ones beyond what's kept. On a thread of its own: the load goes on meanwhile.
void back_up(fs::path from, fs::path root, int keep) {
    std::error_code ec;
    const fs::path to = root / stamp();
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        logx::warn("backup: copying %ls to %ls failed: %s", from.c_str(), to.c_str(), ec.message().c_str());
        return;
    }
    std::uintmax_t bytes = 0;
    for (const auto& e : fs::recursive_directory_iterator(to, ec))
        if (e.is_regular_file(ec)) bytes += e.file_size(ec);
    std::vector<fs::path> all;
    for (const auto& e : fs::directory_iterator(root, ec))
        if (e.is_directory(ec)) all.push_back(e.path());
    std::sort(all.begin(), all.end());  // (named by date: oldest first)
    int removed = 0;
    while (static_cast<int>(all.size()) > keep) {
        fs::remove_all(all.front(), ec);
        all.erase(all.begin());
        ++removed;
    }
    logx::info("backup: the saves (%.1f MB) copied to %ls; %d kept, %d old one(s) removed", bytes / 1048576.0,
               to.c_str(), static_cast<int>(all.size()), removed);
}

}  // namespace

bool init() {
    Il2CppClass* data_path = il2::find_class(kGame, "", "PersistentDataPath");
    g_path_survival = data_path ? il2::il2cpp_class_get_field_from_name(data_path, "m_PathSurvival") : nullptr;
    logx::info("backup: %s", g_path_survival ? "ready (the saves are copied before each co-op game)"
                                             : "UNAVAILABLE (TLD's save folder not found)");
    return g_path_survival != nullptr;
}

void set_count(int keep) { g_keep = std::clamp(keep, 0, 50); }

void on_frame(const std::string& scene, bool coop_on) {
    const bool from_menu = g_last_scene.empty() || g_last_scene == "MainMenu" || g_last_scene == "Boot";
    if (scene == "MainMenu") g_done_this_load = false;
    // Leaving the main menu for a world, co-op on: once per load.
    if (from_menu && !scene.empty() && scene != "MainMenu" && scene != "Boot" && scene != g_last_scene && coop_on &&
        !g_done_this_load && g_keep > 0) {
        g_done_this_load = true;
        const fs::path from = survival_folder();
        std::error_code ec;
        if (!from.empty() && fs::exists(from, ec)) {
            std::thread(back_up, from, paths::mod_dir() / L"backups", g_keep).detach();
            coop_notices::tell("Saves backed up");
        } else {
            logx::warn("backup: TLD's save folder isn't there; nothing copied");
        }
    }
    g_last_scene = scene;
}

}  // namespace coop_backup
