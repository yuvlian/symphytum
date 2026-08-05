// End-to-end test of hook::install_inline: a fake native function whose
// prologue starts with `movzx eax, [rip+disp32]` (the IL2CPP metadata-flag
// pattern), hooked through a fabricated MethodInfo. Verifies the trampoline
// (displaced prologue + RIP-relative rebase) executes correctly and that the
// relocated instruction reaches the same flag byte.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <windows.h>

#include "../src/hook.cpp"

using symphytum::hook::install_inline;

struct FakeMethodInfo {
    void* methodPointer;  // offset 0, all install_inline reads
    void* pad[15];
};

static int (*g_orig)(void*, void*) = nullptr;
static int g_calls = 0;
static int __attribute__((noinline)) hook_stub(void* self, void* method) {
    ++g_calls;
    return g_orig(self, method);
}

int main() {
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!page) { std::printf("VirtualAlloc failed\n"); return 1; }
    uint8_t* f = static_cast<uint8_t*>(page);
    uint8_t* flag = f + 0x200;  // 512 bytes into the same page
    *flag = 0;

    // movzx eax, byte ptr [rip+d1]      ; 7 bytes, rip-relative
    // test  al, al
    // jnz   +7                          ; skip the write on second call
    // mov   byte ptr [rip+d2], 1        ; 7 bytes, rip-relative
    // mov   eax, 0x2A
    // ret
    f[0] = 0x0F; f[1] = 0xB6; f[2] = 0x05;
    int32_t d1 = static_cast<int32_t>(flag - (f + 7));
    std::memcpy(f + 3, &d1, 4);
    f[7] = 0x84; f[8] = 0xC0;
    f[9] = 0x75; f[10] = 0x07;
    f[11] = 0xC6; f[12] = 0x05;
    int32_t d2 = static_cast<int32_t>(flag - (f + 18));
    std::memcpy(f + 13, &d2, 4);
    f[17] = 0x01;
    f[18] = 0xB8; std::memcpy(f + 19, "\x2A\x00\x00\x00", 4);
    f[23] = 0xC3;

    FakeMethodInfo mi{};
    mi.methodPointer = f;
    void* tramp = install_inline(&mi, reinterpret_cast<void*>(&hook_stub));
    if (!tramp) { std::printf("install_inline returned null\n"); return 1; }
    g_orig = reinterpret_cast<int (*)(void*, void*)>(tramp);

    auto fn = reinterpret_cast<int (*)(void*, void*)>(f);
    int r1 = fn(nullptr, nullptr);
    int r2 = fn(nullptr, nullptr);

    bool ok = r1 == 42 && r2 == 42 && g_calls == 2 && *flag == 1 &&
              mi.methodPointer == reinterpret_cast<void*>(&hook_stub);
    std::printf("r1=%d r2=%d calls=%d flag=%d methodPointer=%s tramp=%p\n",
                r1, r2, g_calls, *flag,
                mi.methodPointer == reinterpret_cast<void*>(&hook_stub) ? "hooked" : "UNHOOKED",
                tramp);
    std::printf(ok ? "END-TO-END PASS\n" : "END-TO-END FAIL\n");
    if (!ok) return 1;

    // --- Negative: shrink-wrap prologue (`mov rax, rsp`) must be REJECTED. ---
    // The entry must not be patched and the stub must never fire.
    uint8_t* f2 = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!f2) { std::printf("second VirtualAlloc failed\n"); return 1; }
    uint8_t* p = f2;
    p[0] = 0x48; p[1] = 0x8B; p[2] = 0xC4;      // mov rax, rsp
    p[3] = 0x48; p[4] = 0x89; p[5] = 0x58; p[6] = 0x10;  // mov [rax+10h], rbx
    p[7] = 0xB8; std::memcpy(p + 8, "\x2A\x00\x00\x00", 4);  // mov eax, 0x2A
    p[12] = 0xC3;                                   // ret

    FakeMethodInfo mi2{};
    mi2.methodPointer = f2;
    int calls_before = g_calls;
    void* t2 = install_inline(&mi2, reinterpret_cast<void*>(&hook_stub));
    bool rejected = (t2 == f2);                      // methodPointer-only fallback
    bool entry_untouched = f2[0] == 0x48 && f2[1] == 0x8B && f2[2] == 0xC4;
    int r3 = reinterpret_cast<int (*)(void*, void*)>(f2)(nullptr, nullptr);
    bool stub_not_called = (g_calls == calls_before);
    bool ok2 = rejected && entry_untouched && r3 == 42 && stub_not_called;
    std::printf("rsp-capture: rejected=%d entry_untouched=%d r3=%d stub_called=%d\n",
                rejected, entry_untouched, r3, !stub_not_called);
    std::printf(ok2 ? "RSP-CAPTURE REJECT PASS\n" : "RSP-CAPTURE REJECT FAIL\n");
    return ok2 ? 0 : 1;
}
