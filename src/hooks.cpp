#include "hooks.h"

#include <MinHook.h>

#include <mutex>
#include <unordered_map>

#include "log.h"

namespace hooks {

bool init() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        logx::error("MH_Initialize failed: %s", MH_StatusToString(s));
        return false;
    }
    return true;
}

bool install(const char* label, void* target, void* detour, void** original) {
    if (!target) {
        logx::error("hook %s: target is null", label);
        return false;
    }
    // TLDCoop_DLC: two parts of the mod on the same game method (InteriorLightingManager.Update: Ambient Lights and
    // Seamless Interiors; GameManager.LoadMainMenu: items and windows): the second used to fail (MH_ERROR_ALREADY_CREATED)
    // and its part never ran. Now it goes on top of the first: the game calls the second, whose "original" is the first.
    static std::mutex mu;
    static std::unordered_map<void*, void*> top;  // game method -> the detour the game now calls
    std::lock_guard<std::mutex> lock(mu);
    void* at = target;
    if (const auto it = top.find(target); it != top.end()) {
        at = it->second;
        logx::info("hook %s: the game's method is hooked already; ours goes on top of it", label);
    }
    MH_STATUS s = MH_CreateHook(at, detour, original);
    if (s == MH_OK) s = MH_EnableHook(at);
    if (s == MH_OK) top[target] = detour;
    if (s != MH_OK) {
        logx::error("hook %s at %p failed: %s", label, target, MH_StatusToString(s));
        return false;
    }
    logx::info("hooked %s at %p", label, target);
    return true;
}

bool install_method(const char* label, const MethodInfo* method, void* detour, void** original) {
    void* code = il2::method_pointer(method);
    if (!code) {
        logx::error("hook %s: method has no compiled code", label);
        return false;
    }
    size_t shared = il2::code_share_count(code);
    if (shared > 1) {
        logx::error("hook %s: code at rva 0x%llx is shared by %zu methods (folded), refusing", label,
                    static_cast<unsigned long long>(il2::rva(code)), shared);
        return false;
    }
    logx::info("hook %s: rva 0x%llx", label, static_cast<unsigned long long>(il2::rva(code)));
    return install(label, code, detour, original);
}

}  // namespace hooks
