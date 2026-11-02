#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace logx {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;

void write(const char* level, const char* fmt, va_list args) {
    char message[2048];
    vsnprintf(message, sizeof(message), fmt, args);

    SYSTEMTIME t;
    GetLocalTime(&t);

    std::lock_guard lock(g_mutex);
    if (!g_file) return;
    fprintf(g_file, "[%02u:%02u:%02u.%03u] [%s] [t%lu] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
            level, GetCurrentThreadId(), message);
    fflush(g_file);  // keep the log useful if the game crashes
}

// TLDCoop_DLC: on a stack overflow, which code the overflowed stack is full of: the return addresses on it into the
// game's and our modules, counted (a loop shows as a few addresses repeated hundreds of times). Static storage: the
// stack is all but gone.
struct StackHit {
    uintptr_t addr;
    uint32_t count;
};
StackHit g_hits[512];
const char* module_of(uintptr_t a, uintptr_t& base) {
    static const char* const kNames[] = {"winhttp.dll", "GameAssembly.dll", "UnityPlayer.dll"};
    for (const char* n : kNames) {
        HMODULE m = GetModuleHandleA(n);
        if (!m) continue;
        const auto b = reinterpret_cast<uintptr_t>(m);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(b + dos->e_lfanew);
        if (a > b + 0x1000 && a < b + nt->OptionalHeader.SizeOfImage) {
            base = b;
            return n;
        }
    }
    return nullptr;
}
bool after_call(uintptr_t a) {
    const auto* p = reinterpret_cast<const unsigned char*>(a);
    return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-2] == 0xFF && (p[-1] & 0xF8) == 0xD0) ||
           (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10);
}
void log_stack_loop(FILE* f, const CONTEXT* ctx) {
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    uintptr_t at = ctx->Rsp & ~uintptr_t{7};
    const uintptr_t top = reinterpret_cast<uintptr_t>(tib->StackBase);
    size_t used = 0;
    for (int n = 0; at + 8 <= top && n < 200000; at += 8, ++n) {
        const uintptr_t v = *reinterpret_cast<const uintptr_t*>(at);
        uintptr_t base = 0;
        if (!module_of(v, base) || !after_call(v)) continue;
        size_t i = 0;
        while (i < used && g_hits[i].addr != v) ++i;
        if (i < used) {
            ++g_hits[i].count;
        } else if (used < sizeof(g_hits) / sizeof(g_hits[0])) {
            g_hits[used++] = {v, 1};
        }
    }
    for (int k = 0; k < 16; ++k) {
        size_t best = used;
        for (size_t i = 0; i < used; ++i)
            if (g_hits[i].count && (best == used || g_hits[i].count > g_hits[best].count)) best = i;
        if (best == used) break;
        uintptr_t base = 0;
        const char* name = module_of(g_hits[best].addr, base);
        fprintf(f, "    on the stack x%u: %s+0x%llx\n", g_hits[best].count, name ? name : "?",
                static_cast<unsigned long long>(g_hits[best].addr - base));
        g_hits[best].count = 0;
    }
}

// TLDCoop_DLC: a fatal fault (access violation, stack overflow...) goes into the log with the module and offset
// it happened at, so a crash on someone's PC says where it was. Only logged, never handled: the game's own crash
// handler still runs. Faults the game handles itself also pass here, hence only the first few are written.
LONG CALLBACK fault_logger(EXCEPTION_POINTERS* info) {
    static volatile LONG logged = 0;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN */)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&logged) > 12) return EXCEPTION_CONTINUE_SEARCH;

    void* at = info->ExceptionRecord->ExceptionAddress;
    char module[MAX_PATH] = "?";
    uintptr_t offset = reinterpret_cast<uintptr_t>(at);
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(at), &mod) && mod) {
        GetModuleFileNameA(mod, module, sizeof(module));
        offset -= reinterpret_cast<uintptr_t>(mod);
    }
    const char* name = strrchr(module, '\\');
    name = name ? name + 1 : module;
    uintptr_t target = 0;
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        target = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);

    // No blocking: the fault may have happened while this thread held the log.
    if (!g_mutex.try_lock()) return EXCEPTION_CONTINUE_SEARCH;
    if (g_file) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(g_file, "[%02u:%02u:%02u.%03u] [FAULT] [t%lu] 0x%08lX at %s+0x%llx (address 0x%llx)\n", t.wHour,
                t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), code, name,
                static_cast<unsigned long long>(offset), static_cast<unsigned long long>(target));
        fflush(g_file);
        if (code == EXCEPTION_STACK_OVERFLOW) {
            log_stack_loop(g_file, info->ContextRecord);
            fflush(g_file);
        }
    }
    g_mutex.unlock();
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void open(const std::filesystem::path& file) {
    std::lock_guard lock(g_mutex);
    if (g_file) return;
    // TLDCoop_DLC: the last two sessions' logs are kept (coop-prev.log, coop-prev2.log). A game that crashed is
    // usually started again at once, and its log, the one that shows the crash, was gone.
    std::filesystem::path prev = file, prev2 = file;
    prev.replace_filename(file.stem().wstring() + L"-prev" + file.extension().wstring());
    prev2.replace_filename(file.stem().wstring() + L"-prev2" + file.extension().wstring());
    MoveFileExW(prev.c_str(), prev2.c_str(), MOVEFILE_REPLACE_EXISTING);
    MoveFileExW(file.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING);
    g_file = _wfsopen(file.c_str(), L"w", _SH_DENYWR);
    AddVectoredExceptionHandler(0, fault_logger);
}

void info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write("info", fmt, args);
    va_end(args);
}

void warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write("WARN", fmt, args);
    va_end(args);
}

void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write("ERROR", fmt, args);
    va_end(args);
}

}  // namespace logx
