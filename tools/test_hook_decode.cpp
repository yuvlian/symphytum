// Standalone sanity test for the hook.cpp x86-64 prologue decoder and the
// RIP-relative displacement locator used by the trampoline rebase.
#include <cstdio>
#include <cstdint>
#include <cstddef>

// Pull in the decoder helpers directly (they are plain functions in the
// symphytum::hook namespace; hook.cpp itself has no main).
#include "../src/hook.cpp"

using symphytum::hook::insn_len;
using symphytum::hook::rip_rel_disp_offset;

static int failures = 0;

static void check(const char* name, const uint8_t* bytes, size_t n,
                  size_t expect_len, int expect_disp_off) {
    size_t len = insn_len(bytes);
    int disp = rip_rel_disp_offset(bytes);
    bool ok = len == expect_len && disp == expect_disp_off;
    if (!ok) {
        ++failures;
        std::printf("FAIL %-28s len=%zu (want %zu) disp=%d (want %d)\n",
                    name, len, expect_len, disp, expect_disp_off);
    } else {
        std::printf("ok   %s (len=%zu, disp=%d)\n", name, len, disp);
    }
    (void)n;
}

int main() {
    // movzx eax, byte ptr [rip+disp32]   <- IL2CPP metadata-flag prologue
    const uint8_t t1[] = {0x0F, 0xB6, 0x05, 0x34, 0x12, 0x00, 0x00};
    check("movzx eax,[rip+disp32]", t1, 7, 7, 3);

    // cmp byte ptr [rip+disp32], imm8
    const uint8_t t2[] = {0x80, 0x3D, 0x34, 0x12, 0x00, 0x00, 0x00};
    check("cmp byte [rip+disp32],0", t2, 7, 7, 2);

    // mov rax, qword ptr [rip+disp32] (REX.W)
    const uint8_t t3[] = {0x48, 0x8B, 0x05, 0x34, 0x12, 0x00, 0x00};
    check("mov rax,[rip+disp32]", t3, 7, 7, 3);

    // mov rcx, qword ptr [rip+disp32] (REX.W, /r=1 -> ModRM 0x0D)
    const uint8_t t4[] = {0x48, 0x8B, 0x0D, 0x34, 0x12, 0x00, 0x00};
    check("mov rcx,[rip+disp32]", t4, 7, 7, 3);

    // mov rax, qword ptr [rbx]  -- no displacement, not rip-relative
    const uint8_t t5[] = {0x48, 0x8B, 0x03};
    check("mov rax,[rbx]", t5, 3, 3, -1);

    // add rsp, 0x20 -- immediate form, no modrm displacement
    const uint8_t t6[] = {0x48, 0x83, 0xC4, 0x20};
    check("add rsp,0x20", t6, 4, 4, -1);

    // push rbp
    const uint8_t t7[] = {0x55};
    check("push rbp", t7, 1, 1, -1);

    // movzx ecx, byte ptr [rsp+8]  (mod=1 -> base reg, not rip-relative)
    const uint8_t t8[] = {0x0F, 0xB6, 0x4C, 0x24, 0x08};
    check("movzx ecx,[rsp+8]", t8, 5, 5, -1);

    // mov eax, [rip+disp32] (no REX)
    const uint8_t t9[] = {0x8B, 0x05, 0x34, 0x12, 0x00, 0x00};
    check("mov eax,[rip+disp32]", t9, 6, 6, 2);

    // lea rax, [rip+disp32]
    const uint8_t t10[] = {0x48, 0x8D, 0x05, 0x34, 0x12, 0x00, 0x00};
    check("lea rax,[rip+disp32]", t10, 7, 7, 3);

    // jmp rel32 -- no modrm
    const uint8_t t11[] = {0xE9, 0x00, 0x00, 0x00, 0x00};
    check("jmp rel32", t11, 5, 5, -1);

    // call rel32
    const uint8_t t12[] = {0xE8, 0x00, 0x00, 0x00, 0x00};
    check("call rel32", t12, 5, 5, -1);

    // movzx eax,[rbp+idx*1+disp32] -- mod=10 SIB with base reg (not rip-relative)
    const uint8_t t13[] = {0x0F, 0xB6, 0x84, 0x2D, 0x34, 0x12, 0x00, 0x00};
    check("movzx eax,[rbp+idx+disp32]", t13, 8, 8, -1);  // base reg, not rip

    // 64-bit immediate move (REX.W mov r64, imm64) used by the 12-byte patch
    const uint8_t t14[] = {0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    check("mov rax,imm64", t14, 10, 10, -1);

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
