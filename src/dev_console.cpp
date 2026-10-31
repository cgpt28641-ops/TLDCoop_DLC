#include "dev_console.h"

#include <windows.h>

#include <cstdint>
#include <cwctype>
#include <string>

#include "il2cpp_api.h"
#include "log.h"
#include "paths.h"

namespace dev_console {
namespace {

// TLD's uConsole prefab: its Addressables GUID (key "uConsole", Assets/uConsole/Prefabs/uConsole.prefab).
constexpr const char* kConsoleGuid = "5e8222cec53bee74d90412e9e11caec3";
constexpr int kKeyF1 = 282;  // UnityEngine.KeyCode.F1 (F2..F12 follow)

bool g_done = false;        // nothing more to do (made and keyed, off, or given up)
bool g_requested = false;   // InstantiateAsync asked; waiting for TLD's uConsole to wake (its Awake sets m_Instance)
double g_menu_since = 0, g_requested_at = 0;
Il2CppClass* g_console_class = nullptr;
FieldInfo* g_instance = nullptr;

double now_ms() { return static_cast<double>(GetTickCount64()); }

// coop.ini [devconsole]: enabled (1), key (F1..F15).
bool settings(int& key) {
    const auto ini = paths::instance_dir() / L"coop.ini";
    if (!GetPrivateProfileIntW(L"devconsole", L"enabled", 1, ini.c_str())) return false;
    wchar_t v[16];
    GetPrivateProfileStringW(L"devconsole", L"key", L"F1", v, 16, ini.c_str());
    key = kKeyF1;
    if ((v[0] == L'F' || v[0] == L'f') && v[1]) {
        const int n = _wtoi(v + 1);
        if (n >= 1 && n <= 15) key = kKeyF1 + n - 1;
    }
    return true;
}

Il2CppObject* instance() {
    Il2CppObject* o = nullptr;
    if (g_instance) il2::il2cpp_field_static_get_value(g_instance, &o);
    return o;
}

// TLD's uConsole is there: its key set. True when done.
bool key_it(Il2CppObject* made, bool was_there) {
    int key = kKeyF1;
    settings(key);
    const ptrdiff_t activate = il2::field_offset_quiet(g_console_class, "m_Activate");
    if (activate >= 0) *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(made) + activate) = key;
    logx::info("devconsole: TLD's developer console is in (F%d opens it; 'help' lists its commands)%s", key - kKeyF1 + 1,
               was_there ? " (it was there already)" : "");
    return true;
}

// Ask Addressables for TLD's uConsole prefab, made in the scene. No waiting on it here (blocking on the handle
// crashed the game at the main menu): its Awake sets uConsole.m_Instance, which on_frame looks for.
bool request() {
    Il2CppClass* ref_class = il2::find_class("Unity.Addressables.dll", "UnityEngine.AddressableAssets", "AssetReference");
    const MethodInfo* ctor = ref_class ? il2::find_method_params(ref_class, ".ctor", {"System.String"}) : nullptr;
    const MethodInfo* inst = ref_class ? il2::find_method_params(ref_class, "InstantiateAsync",
                                                                  {"UnityEngine.Transform", "System.Boolean"})
                                       : nullptr;
    if (!ctor || !inst) {
        logx::warn("devconsole: TLD's calls for it are missing");
        return false;
    }
    Il2CppObject* ref = il2::il2cpp_object_new(ref_class);
    void* ca[] = {il2::il2cpp_string_new(kConsoleGuid)};
    il2::invoke(ctor, ref, ca);
    bool world = false;
    void* ia[] = {nullptr, &world};
    Il2CppObject* handle = il2::invoke(il2::virtual_method(ref, inst), ref, ia);
    // AsyncOperationHandle<GameObject>, boxed: its first field is the operation (null: nothing was started)
    void* op = handle ? *reinterpret_cast<void**>(reinterpret_cast<char*>(handle) + 0x10) : nullptr;
    if (!op) {
        logx::warn("devconsole: Addressables didn't start loading TLD's console");
        return false;
    }
    logx::info("devconsole: loading TLD's console");
    return true;
}

// A crash in TLD's code while asking (an access violation) caught here: no console, but the game goes on.
bool guarded(bool (*fn)(), bool* faulted) {
    __try {
        return fn();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return false;
    }
}

}  // namespace

void on_frame(const std::string& scene, bool in_game) {
    if (g_done) return;
    if (scene != "MainMenu" && !in_game) {
        g_menu_since = 0;
        return;
    }
    const double now = now_ms();
    if (!g_menu_since) g_menu_since = now;
    if (now - g_menu_since < 5000) return;  // TLD's own start first (Addressables, its menus)
    if (!g_console_class) {
        g_console_class = il2::find_class("Assembly-CSharp.dll", "", "uConsole");
        g_instance = g_console_class ? il2::il2cpp_class_get_field_from_name(g_console_class, "m_Instance") : nullptr;
        if (!g_console_class || !g_instance) {
            logx::warn("devconsole: TLD has no uConsole here");
            g_done = true;
            return;
        }
    }
    int key = 0;
    if (!settings(key)) {
        g_done = true;
        logx::info("devconsole: off (coop.ini [devconsole] enabled=0)");
        return;
    }
    if (Il2CppObject* made = instance()) {
        g_done = key_it(made, !g_requested);
        return;
    }
    if (!g_requested) {
        g_requested = true;
        g_requested_at = now;
        bool faulted = false;
        if (!guarded(&request, &faulted)) g_done = true;
        if (faulted) logx::error("devconsole: TLD faulted loading its console; no console this time (the game goes on)");
        return;
    }
    if (now - g_requested_at > 30000) {
        logx::warn("devconsole: TLD's console didn't come (30 s); no console this time");
        g_done = true;
    }
}

}  // namespace dev_console
