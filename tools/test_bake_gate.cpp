// Unit test for the CreateChartData autoplay-bake gate locator (bake_gate.hpp).
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "patches/bake_gate.hpp"

using symphytum::patches::bake_gate::find_jz;

static int failures = 0;

static void expect(const char* name, const uint8_t* buf, size_t n, uint32_t off,
                   size_t want) {
    size_t got = find_jz(buf, n, off);
    if (got != want) {
        ++failures;
        std::printf("FAIL %-32s got=0x%zx want=0x%zx\n", name, got, want);
    } else {
        std::printf("ok   %s -> 0x%zx\n", name, got);
    }
}

int main() {
    // 1. Real IDB gate bytes: 41 80 7F 58 00 | 0F 84 06 01 00 00 | 49 8B 57 18 ...
    //    cmp byte ptr [r15+58h], 0 ; jz +0x106
    const uint8_t t1[] = {0x41, 0x80, 0x7F, 0x58, 0x00,
                          0x0F, 0x84, 0x06, 0x01, 0x00, 0x00,
                          0x49, 0x8B, 0x57, 0x18, 0x48};
    expect("IDB cmp disp8 gate", t1, sizeof(t1), 0x58, 5);

    // 2. Same but without REX.B (base = rdi, e.g.).
    const uint8_t t2[] = {0x80, 0x7F, 0x58, 0x00,
                          0x0F, 0x84, 0x06, 0x01, 0x00, 0x00};
    expect("cmp disp8 no-rex", t2, sizeof(t2), 0x58, 4);

    // 3. disp32 form: 80 B9 58 00 00 00 00 | 0F 84 ...
    const uint8_t t3[] = {0x80, 0xB9, 0x58, 0x00, 0x00, 0x00, 0x00,
                          0x0F, 0x84, 0x10, 0x00, 0x00, 0x00};
    expect("cmp disp32 gate", t3, sizeof(t3), 0x58, 7);

    // 4. movzx + test form: 41 0F B6 47 58 | 84 C0 | 0F 84 ...
    const uint8_t t4[] = {0x41, 0x0F, 0xB6, 0x47, 0x58,
                          0x84, 0xC0,
                          0x0F, 0x84, 0x20, 0x00, 0x00, 0x00};
    expect("movzx/test gate", t4, sizeof(t4), 0x58, 7);

    // 5. movzx disp32 form: 0F B6 87 58 00 00 00 | 84 C0 | 0F 84 ...
    const uint8_t t5[] = {0x0F, 0xB6, 0x87, 0x58, 0x00, 0x00, 0x00,
                          0x84, 0xC0,
                          0x0F, 0x84, 0x20, 0x00, 0x00, 0x00};
    expect("movzx disp32 gate", t5, sizeof(t5), 0x58, 9);

    // 6. Different field offset must NOT match (field moved).
    expect("wrong field off", t1, sizeof(t1), 0x5C, SIZE_MAX);

    // 6b. mov al, byte ptr [reg+off] ; test al, al ; jz (form 3)
    const uint8_t t6b[] = {0x8A, 0x47, 0x58,
                           0x84, 0xC0,
                           0x0F, 0x84, 0x20, 0x00, 0x00, 0x00};
    expect("mov al gate", t6b, sizeof(t6b), 0x58, 5);

    // 7. cmp with imm8 != 0 must not match.
    const uint8_t t7[] = {0x80, 0x7F, 0x58, 0x01,
                          0x0F, 0x84, 0x06, 0x01, 0x00, 0x00};
    expect("cmp imm!=0", t7, sizeof(t7), 0x58, SIZE_MAX);

    // 8. jz not following (different branch: jnz).
    const uint8_t t8[] = {0x80, 0x7F, 0x58, 0x00,
                          0x0F, 0x85, 0x06, 0x01, 0x00, 0x00};
    expect("jnz not jz", t8, sizeof(t8), 0x58, SIZE_MAX);

    // 9. Noisy prefix: random bytes before the gate must not confuse the scan.
    const uint8_t t9[] = {0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x89, 0x74, 0x24, 0x58,
                          0x41, 0x80, 0x7F, 0x58, 0x00,
                          0x0F, 0x84, 0x06, 0x01, 0x00, 0x00};
    expect("gate after prologue bytes", t9, sizeof(t9), 0x58, 15);

    // 10. find_auto_judge_imm with the real bake-block bytes (from the IDB):
    //     gate jz, then `cmp [rdi+0x18], 0xa; jne; xor edx,edx; call; jmp;
    //     mov edx, 0x64; call` — the 0x64 must be found.
    const uint8_t t10[] = {
        0x41, 0x80, 0x7F, 0x58, 0x00, 0x0F, 0x84, 0x06, 0x01, 0x00, 0x00,  // gate
        0x89, 0x44, 0x24, 0x58,                                               // mov [rsp+58h],eax
        0x83, 0x7F, 0x18, 0x0A,                                               // cmp [rdi+18h],0xa
        0x75, 0x09,                                                           // jne +9
        0x33, 0xD2,                                                           // xor edx,edx
        0xE8, 0x4F, 0x7E, 0xF1, 0xFC,                                         // call (damage tuple)
        0xEB, 0x0A,                                                           // jmp +10
        0xBA, 0x64, 0x00, 0x00, 0x00,                                         // mov edx, 0x64  << the imm
        0xE8, 0x43, 0x7E, 0xF1, 0xFC,                                         // call (auto tuple)
    };
    size_t imm = symphytum::patches::bake_gate::find_auto_judge_imm(t10, sizeof(t10), 5);
    if (imm != 31) { ++failures; std::printf("FAIL find_auto_judge_imm got=0x%zx want=0x1f\n", imm); }
    else std::printf("ok   find_auto_judge_imm -> 0x%zx\n", imm);

    // 11. REX-prefixed variant (mov r8d, 0x64): 41 B8 64 00 00 00
    const uint8_t t11[] = {0x41, 0xB8, 0x64, 0x00, 0x00, 0x00};
    size_t imm2 = symphytum::patches::bake_gate::find_auto_judge_imm(t11, sizeof(t11), 0);
    if (imm2 != 2) { ++failures; std::printf("FAIL find_auto_judge_imm rex got=0x%zx want=0x2\n", imm2); }
    else std::printf("ok   find_auto_judge_imm rex -> 0x%zx\n", imm2);

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
