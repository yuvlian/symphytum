// DllMain: spawn the init thread on attach. We never block the loader.
//
// Early hook: we also patch the host exe's IAT to neuter ExitProcess before the
// game's main thread runs. LimbusCompany.exe's anti-cheat calls ExitProcess(53)
// early in native startup when it detects injection; making ExitProcess a no-op
// keeps the process alive so our il2cpp-level patches can install later.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "init.hpp"
#include "log.hpp"
#include <cstdio>
#include <cstring>

namespace symphytum {
namespace early_hook {

// Our replacement for ExitProcess: log and return without exiting. The caller
// (Environment.Exit / native anti-cheat) expects ExitProcess to not return, so
// the thread that called it will continue with whatever happens after the call
// site — typically unwinding back into managed code, which is fine for our
// purposes (we just need the process to stay alive).
VOID WINAPI stub_ExitProcess(UINT uExitCode) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "[Symphytum:early] ExitProcess(%u) BLOCKED", uExitCode);
    log_raw(buf);
    // Intentionally do not call the real ExitProcess. Return to caller.
    // The anti-cheat's thread keeps running; in practice it unwinds harmlessly
    // because the code path after ExitProcess is unreachable in well-formed
    // binaries, but staying alive is what matters.
}

// Walk the in-memory import directory of `module` and replace every IAT entry
// that points at `target_proc` with `new_proc`. Returns the count replaced.
// This works for both exe and dll modules that statically import the function.
size_t patch_iat(HMODULE module, void* target_proc, void* new_proc) {
    if (!module || !target_proc || !new_proc) return 0;
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.Size) return 0;
    auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    size_t count = 0;
    for (; imp->Name; ++imp) {
        auto thunk_ref = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; thunk_ref->u1.Function; ++thunk_ref) {
            void** slot = reinterpret_cast<void**>(&thunk_ref->u1.Function);
            if (*slot != target_proc) continue;
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
            *slot = new_proc;
            DWORD dummy = 0;
            VirtualProtect(slot, sizeof(void*), old, &dummy);
            ++count;
        }
    }
    return count;
}

void install_early_hooks() {
    // Resolve the real ExitProcess so we can match IAT entries against it.
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return;
    void* real_exit = reinterpret_cast<void*>(GetProcAddress(k32, "ExitProcess"));
    if (!real_exit) return;
    // Patch the host exe's IAT. GetModuleHandleW(nullptr) returns the main
    // exe's base in the context of the injected process.
    HMODULE exe = GetModuleHandleW(nullptr);
    size_t n = patch_iat(exe, real_exit, reinterpret_cast<void*>(&stub_ExitProcess));
    // Also patch our own DLL's IAT in case we inadvertently call it.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&install_early_hooks), &self);
    if (self) n += patch_iat(self, real_exit, reinterpret_cast<void*>(&stub_ExitProcess));
    log_raw(n > 0
        ? "[Symphytum:early] ExitProcess IAT hooked (patched entries)"
        : "[Symphytum:early] ExitProcess not in IAT (no patch needed or anti-cheat imports dynamically)");
}

}  // namespace early_hook
}  // namespace symphytum

namespace {
// Top-level crash handler: if init_all() crashes, log it instead of dying
// silently. Clang (zig cc) doesn't support __try/__except, so we use
// SetUnhandledExceptionFilter.
LONG WINAPI symphytum_crash_filter(EXCEPTION_POINTERS* ep) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "[Symphytum:init] init_all() CRASHED (code 0x%08lX at %p)",
                  static_cast<unsigned long>(ep ? ep->ExceptionRecord->ExceptionCode : 0),
                  ep ? static_cast<void*>(ep->ExceptionRecord->ExceptionAddress) : nullptr);
    symphytum::log_raw(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI init_thread(LPVOID) {
    SetUnhandledExceptionFilter(&symphytum_crash_filter);
    symphytum::init_all();
    return 0;
}
}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hInst);
        // Install the early ExitProcess IAT hook before anything else runs —
        // the anti-cheat's native exit call can fire before the main thread
        // finishes its first instruction, and definitely before il2cpp is ready.
        symphytum::early_hook::install_early_hooks();
        HANDLE h = CreateThread(nullptr, 0, &init_thread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
