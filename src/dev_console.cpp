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

bool g_done = false;
int g_tries = 0;
uintptr_t g_console = 0;

// coop.ini [devconsole]: enabled (1), key (F1..F12).
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

bool make() {
    const char* addr = "Unity.Addressables.dll";
    Il2CppClass* ref_class = il2::find_class(addr, "UnityEngine.AddressableAssets", "AssetReference");
    Il2CppClass* handle_class =
        il2::find_class("Unity.ResourceManager.dll", "UnityEngine.ResourceManagement.AsyncOperations", "AsyncOperationHandle");
    Il2CppClass* console = il2::find_class("Assembly-CSharp.dll", "", "uConsole");
    if (!ref_class || !handle_class || !console) {
        logx::warn("devconsole: TLD's classes for it are missing (AssetReference %s, AsyncOperationHandle %s, uConsole %s)",
                   ref_class ? "ok" : "no", handle_class ? "ok" : "no", console ? "ok" : "no");
        return true;  // nothing to retry
    }
    // Already there (made before, or TLD's own)?
    FieldInfo* instance = il2::il2cpp_class_get_field_from_name(console, "m_Instance");
    Il2CppObject* existing = nullptr;
    if (instance) il2::il2cpp_field_static_get_value(instance, &existing);
    Il2CppObject* made = existing;
    if (!made) {
        const MethodInfo* ctor = il2::find_method_params(ref_class, ".ctor", {"System.String"});
        const MethodInfo* inst = il2::find_method_params(ref_class, "InstantiateAsync",
                                                         {"UnityEngine.Transform", "System.Boolean"});
        const MethodInfo* wait = il2::find_method(handle_class, "WaitForCompletion", 0);
        if (!ctor || !inst || !wait) {
            logx::warn("devconsole: TLD's calls for it are missing");
            return true;
        }
        Il2CppObject* ref = il2::il2cpp_object_new(ref_class);
        void* ca[] = {il2::il2cpp_string_new(kConsoleGuid)};
        il2::invoke(ctor, ref, ca);
        bool world = false;
        void* ia[] = {nullptr, &world};
        // AsyncOperationHandle<GameObject>, boxed; the plain AsyncOperationHandle has the same fields, so its
        // (compiled) WaitForCompletion runs on it.
        Il2CppObject* handle = il2::invoke(il2::virtual_method(ref, inst), ref, ia);
        Il2CppObject* go = handle ? il2::invoke(wait, handle) : nullptr;
        if (!go) {
            logx::warn("devconsole: TLD's console didn't load (try %d)", g_tries);
            return g_tries >= 3;
        }
        if (instance) il2::il2cpp_field_static_get_value(instance, &made);
        if (!made) {
            logx::warn("devconsole: TLD's console loaded, but no uConsole came with it");
            return true;
        }
    }
    int key = kKeyF1;
    settings(key);
    const ptrdiff_t activate = il2::field_offset_quiet(console, "m_Activate");
    if (activate >= 0) *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(made) + activate) = key;
    g_console = il2::il2cpp_gchandle_new(made, false);
    logx::info("devconsole: TLD's developer console is in (F%d opens it; 'help' lists its commands)%s", key - kKeyF1 + 1,
               existing ? " (it was there already)" : "");
    return true;
}

}  // namespace

void on_frame(const std::string& scene, bool in_game) {
    if (g_done) return;
    // Once TLD's own start is through (its Addressables are up): at the main menu or in a game.
    if (scene != "MainMenu" && !in_game) return;
    int key = 0;
    if (!settings(key)) {
        g_done = true;
        logx::info("devconsole: off (coop.ini [devconsole] enabled=0)");
        return;
    }
    ++g_tries;
    g_done = make();
}

}  // namespace dev_console
