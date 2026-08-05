// Method-pointer hook implementation.
#include "hook.hpp"
#include "log.hpp"
#include <windows.h>
#include <cstring>

namespace symphytum::hook {

namespace {
// MethodInfo layout we depend on: methodPointer at offset 0.
// (Matches the real Il2CppMethodInfo for Unity 2021+; offset 0 is `methodPointer`,
//  an Il2CppMethodPointer / void*.)
constexpr size_t kMethodPointerOffset = 0;

void* write_method_ptr(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr) {
    if (!method) return nullptr;
    void* slot = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(method) + kMethodPointerOffset);
    void* old;
    // The il2cpp metadata pages are typically read-only; VirtualProtect to RW for the write.
    DWORD old_prot = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_prot)) {
        SYM_LOG("hook", "VirtualProtect(RW) failed: {}", GetLastError());
        return nullptr;
    }
    std::memcpy(&old, slot, sizeof(void*));
    std::memcpy(slot, &new_ptr, sizeof(void*));
    DWORD dummy = 0;
    VirtualProtect(slot, sizeof(void*), old_prot, &dummy);
    return old;
}
}  // namespace

void* install(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr) {
    if (!method || !new_ptr) return nullptr;
    void* old = write_method_ptr(method, new_ptr);
    if (!old) return nullptr;
    return old;
}

// x86-64 instruction length decoder. Handles the common opcodes il2cpp emits
// in method prologues. Returns the instruction length at `p`, or 0 if unknown.
// This is NOT a full disassembler — just enough to find instruction boundaries
// so the inline-patch trampoline doesn't split an instruction.

// Length of ModRM byte + SIB + displacement (NOT including opcode or immediate).
// `p` points to the ModRM byte.
size_t modrm_len(const uint8_t* p) {
    uint8_t modrm = p[0];
    uint8_t mod = modrm >> 6;
    uint8_t rm = modrm & 7;
    if (mod == 3) return 1;  // register-register: just ModRM
    size_t len = 1;           // ModRM byte itself
    if (rm == 4) {            // SIB byte follows
        len += 1;             // SIB
        uint8_t sib = p[1];
        if (mod == 0 && (sib & 7) == 5) len += 4;  // disp32 when base=rbp, mod=0
    }
    if (mod == 0) {
        if (rm == 5) len += 4;  // rip-relative disp32
    } else if (mod == 1) {
        len += 1;               // disp8
    } else {                    // mod == 2
        len += 4;               // disp32
    }
    return len;
}

// If the instruction at `p` uses RIP-relative addressing, return the offset of
// the disp32 within the instruction, so the trampoline can rebase it. Returns
// -1 otherwise. Mirrors the ModRM walk in modrm_len().
static int modrm_rip_disp_offset(const uint8_t* m) {
    uint8_t mod = m[0] >> 6;
    uint8_t rm = m[0] & 7;
    if (mod != 0) return -1;  // mod 1/2 use a base register, mod 3 is register
    if (rm == 5) return 1;    // [rip + disp32]
    if (rm == 4) {            // SIB: [base + index*scale + disp]
        uint8_t sib = m[1];
        if ((sib & 7) == 5) return 2;  // base=rbp with mod=0 => disp32 follows SIB
    }
    return -1;
}

static int rip_rel_disp_offset(const uint8_t* p) {
    if (!p) return -1;
    // Skip legacy prefixes.
    size_t i = 0;
    for (;;) {
        uint8_t b = p[i];
        if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||
            b == 0x64 || b == 0x65 || b == 0x66 || b == 0x67 ||
            b == 0xF0 || b == 0xF2 || b == 0xF3) {
            i++;
        } else break;
    }
    if (p[i] >= 0x40 && p[i] <= 0x4F) i++;  // REX
    uint8_t op = p[i];
    int modrm_off = -1;
    if (op == 0x0F) {
        uint8_t op2 = p[i + 1];
        bool has_modrm = !(op2 == 0x05 || op2 == 0x0B || op2 == 0x0E || op2 == 0x77 ||
                           (op2 >= 0x80 && op2 <= 0x8F) || (op2 >= 0xC8 && op2 <= 0xCF));
        if (has_modrm) modrm_off = static_cast<int>(i) + 2;
    } else if (op <= 0x3D) {
        if (op == 0x06 || op == 0x07 || op == 0x0E || op == 0x16 || op == 0x17 ||
            op == 0x1E || op == 0x1F || op == 0x27 || op == 0x2F || op == 0x37 || op == 0x3F)
            return -1;  // invalid in 64-bit mode
        uint8_t low = op & 7;
        if (low < 4) modrm_off = static_cast<int>(i) + 1;  // group with ModRM
    } else if (op == 0x63 || (op >= 0x84 && op <= 0x8B) || op == 0x8D || op == 0x8F ||
               op == 0x69 || op == 0x6B || op == 0x80 || op == 0x81 || op == 0x83 ||
               op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0xC7 ||
               (op >= 0xD0 && op <= 0xD3) || (op >= 0xD8 && op <= 0xDF) ||
               op == 0xF6 || op == 0xF7 || op == 0xFE || op == 0xFF) {
        modrm_off = static_cast<int>(i) + 1;
    }
    if (modrm_off < 0) return -1;
    int disp = modrm_rip_disp_offset(p + modrm_off);
    return disp < 0 ? -1 : modrm_off + disp;
}

size_t insn_len(const uint8_t* p) {
    if (!p) return 0;
    // Skip legacy prefixes.
    size_t i = 0;
    for (;;) {
        uint8_t b = p[i];
        if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E ||  // seg override
            b == 0x64 || b == 0x65 ||                            // fs/gs
            b == 0x66 || b == 0x67 ||                            // op/addr size
            b == 0xF0 ||                                        // lock
            b == 0xF2 || b == 0xF3) {                           // repne/rep
            i++;
        } else break;
    }
    // REX prefix.
    bool rex_w = false;
    if (p[i] >= 0x40 && p[i] <= 0x4F) {
        rex_w = (p[i] & 0x08) != 0;
        i++;
    }
    uint8_t op = p[i];

    // 0x0F two-byte opcodes.
    if (op == 0x0F) {
        uint8_t op2 = p[i + 1];
        // No-ModRM 0F opcodes: syscall(05), ud2(0B), emms(77), jcc(80-8F), bswap(C8-CF)
        if (op2 == 0x05 || op2 == 0x0B || op2 == 0x0E || op2 == 0x77) return i + 2;
        if (op2 >= 0x80 && op2 <= 0x8F) return i + 6;  // jcc rel32: 0F + op2 + rel32
        if (op2 >= 0xC8 && op2 <= 0xCF) return i + 2;  // bswap r32/r64
        // All other 0F opcodes have ModRM. A few add imm8.
        bool has_imm8 = (op2 >= 0x70 && op2 <= 0x73) || op2 == 0xA4 || op2 == 0xAC ||
                        op2 == 0xBA || op2 == 0xC2 || op2 == 0xC4 || op2 == 0xC5 ||
                        op2 == 0xC6;
        return i + 2 + modrm_len(p + i + 2) + (has_imm8 ? 1 : 0);
    }

    // 0x00-0x3D: arithmetic groups (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP).
    // Each group: +0..+3 = ModRM, +4 = AL/imm8, +5 = eAX/imm32.
    if (op <= 0x3D) {
        // Skip opcodes invalid in 64-bit mode.
        if (op == 0x06 || op == 0x07 || op == 0x0E || op == 0x16 || op == 0x17 ||
            op == 0x1E || op == 0x1F || op == 0x27 || op == 0x2F || op == 0x37 || op == 0x3F)
            return 0;
        uint8_t low = op & 7;
        if (low < 4) return i + 1 + modrm_len(p + i + 1);
        if (low == 4) return i + 2;  // AL, imm8
        return i + 5;                // eAX, imm32 (sign-extended in 64-bit)
    }
    // 0x50-0x5F: push/pop r (1 byte).
    if (op >= 0x50 && op <= 0x5F) return i + 1;
    // 0x63: movsxd (ModRM).
    if (op == 0x63) return i + 1 + modrm_len(p + i + 1);
    // 0x68: push imm32. 0x6A: push imm8.
    if (op == 0x68) return i + 5;
    if (op == 0x6A) return i + 2;
    // 0x69: imul r, r/m, imm32. 0x6B: imul r, r/m, imm8.
    if (op == 0x69) return i + 1 + modrm_len(p + i + 1) + 4;
    if (op == 0x6B) return i + 1 + modrm_len(p + i + 1) + 1;
    // 0x70-0x7F: jcc rel8 (2 bytes).
    if (op >= 0x70 && op <= 0x7F) return i + 2;
    // 0x80: r/m8, imm8. 0x81: r/m32, imm32. 0x83: r/m32, imm8.
    if (op == 0x80) return i + 1 + modrm_len(p + i + 1) + 1;
    if (op == 0x81) return i + 1 + modrm_len(p + i + 1) + 4;
    if (op == 0x83) return i + 1 + modrm_len(p + i + 1) + 1;
    // 0x84-0x87: test/xchg (ModRM).
    // 0x88-0x8B: mov (ModRM). 0x8D: lea (ModRM). 0x8F: pop r/m (ModRM).
    if ((op >= 0x84 && op <= 0x8B) || op == 0x8D || op == 0x8F)
        return i + 1 + modrm_len(p + i + 1);
    // 0x90-0x97: nop/xchg r32 (1 byte).
    if (op >= 0x90 && op <= 0x97) return i + 1;
    // 0x98-0x9F: cbw/cwd/etc (1 byte).
    if (op >= 0x98 && op <= 0x9F) return i + 1;
    // 0xA0-0xA3: mov al/eax, moffs (8-byte address in 64-bit).
    if (op >= 0xA0 && op <= 0xA3) return i + 9;
    // 0xA4-0xA7: movs/cmps (1 byte).
    if (op >= 0xA4 && op <= 0xA7) return i + 1;
    // 0xA8: test al, imm8. 0xA9: test eax, imm32.
    if (op == 0xA8) return i + 2;
    if (op == 0xA9) return i + 5;
    // 0xAA-0xAF: stos/lods/scas (1 byte).
    if (op >= 0xAA && op <= 0xAF) return i + 1;
    // 0xB0-0xB7: mov r8, imm8 (2 bytes).
    if (op >= 0xB0 && op <= 0xB7) return i + 2;
    // 0xB8-0xBF: mov r32, imm32 (or r64, imm64 with REX.W).
    if (op >= 0xB8 && op <= 0xBF) return i + (rex_w ? 9 : 5);
    // 0xC0/0xC1: shift r/m, imm8.
    if (op == 0xC0 || op == 0xC1) return i + 1 + modrm_len(p + i + 1) + 1;
    // 0xC2: ret imm16. 0xC3: ret.
    if (op == 0xC2) return i + 3;
    if (op == 0xC3) return i + 1;
    // 0xC6: mov r/m8, imm8. 0xC7: mov r/m32, imm32.
    if (op == 0xC6) return i + 1 + modrm_len(p + i + 1) + 1;
    if (op == 0xC7) return i + 1 + modrm_len(p + i + 1) + 4;
    // 0xC8: enter (4 bytes). 0xC9: leave (1 byte).
    if (op == 0xC8) return i + 4;
    if (op == 0xC9) return i + 1;
    // 0xCA: retf imm16. 0xCB: retf. 0xCC: int3. 0xCD: int imm8. 0xCF: iretq.
    if (op == 0xCA) return i + 3;
    if (op == 0xCB || op == 0xCC || op == 0xCF) return i + 1;
    if (op == 0xCD) return i + 2;
    // 0xD0-0xD3: shift r/m, 1/CL (ModRM).
    if (op >= 0xD0 && op <= 0xD3) return i + 1 + modrm_len(p + i + 1);
    // 0xD8-0xDF: x87 FPU (ModRM) — approximate.
    if (op >= 0xD8 && op <= 0xDF) return i + 1 + modrm_len(p + i + 1);
    // 0xE0-0xE3: loop/jcxz (2 bytes).
    if (op >= 0xE0 && op <= 0xE3) return i + 2;
    // 0xE4-0xE7: in/out imm8 (2 bytes).
    if (op >= 0xE4 && op <= 0xE7) return i + 2;
    // 0xE8: call rel32. 0xE9: jmp rel32.
    if (op == 0xE8 || op == 0xE9) return i + 5;
    // 0xEB: jmp rel8 (2 bytes).
    if (op == 0xEB) return i + 2;
    // 0xEC-0xEF: in/out dx (1 byte).
    if (op >= 0xEC && op <= 0xEF) return i + 1;
    // 0xF4: hlt. 0xF5: cmc.
    if (op == 0xF4 || op == 0xF5) return i + 1;
    // 0xF6/0xF7: test/not/neg/mul/div group. /0 and /1 (test) have immediate.
    if (op == 0xF6) {
        uint8_t reg = (p[i + 1] >> 3) & 7;
        return i + 1 + modrm_len(p + i + 1) + ((reg == 0 || reg == 1) ? 1 : 0);
    }
    if (op == 0xF7) {
        uint8_t reg = (p[i + 1] >> 3) & 7;
        return i + 1 + modrm_len(p + i + 1) + ((reg == 0 || reg == 1) ? 4 : 0);
    }
    // 0xF8-0xFD: clc/stc/cli/sti/cld/std (1 byte).
    if (op >= 0xF8 && op <= 0xFD) return i + 1;
    // 0xFE: inc/dec r/m8 (ModRM).
    if (op == 0xFE) return i + 1 + modrm_len(p + i + 1);
    // 0xFF: call/jmp/push r/m (ModRM, no imm).
    if (op == 0xFF) return i + 1 + modrm_len(p + i + 1);
    return 0;  // unknown
}

// Allocate a page of executable memory for trampolines. We keep a single
// page and bump-allocate trampolines from it.
//
// The page is placed within ±2GB of GameAssembly.dll so that RIP-relative
// disp32 operands in displaced prologues can be rebased to reach the module
// (see install_inline). VirtualAlloc allocates *at* the requested address or
// fails, so we walk a few candidate addresses around the module.
uint8_t* g_trampoline_page = nullptr;
size_t g_trampoline_offset = 0;
uint8_t* alloc_trampoline(size_t size) {
    if (!g_trampoline_page) {
        HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
        uintptr_t base = ga ? reinterpret_cast<uintptr_t>(ga) : 0;
        uintptr_t hints[] = {
            base ? base - 0x10000000u : 0,  // 256 MB below the module
            base ? base - 0x08000000u : 0,  // 128 MB below
            base ? base + 0x20000000u : 0,  // 512 MB above
            base ? base - 0x20000000u : 0,  // 512 MB below
            0,                              // no hint (last resort)
        };
        for (uintptr_t h : hints) {
            void* p = VirtualAlloc(reinterpret_cast<LPVOID>(h), 4096,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) {
                g_trampoline_page = static_cast<uint8_t*>(p);
                break;
            }
        }
        if (!g_trampoline_page) {
            SYM_LOG("hook", "trampoline VirtualAlloc failed: {}", GetLastError());
            return nullptr;
        }
        SYM_LOG_DEBUG("hook", "trampoline page at {} (module {})",
                      static_cast<void*>(g_trampoline_page), reinterpret_cast<void*>(base));
        g_trampoline_offset = 0;
    }
    if (g_trampoline_offset + size > 4096) return nullptr;
    uint8_t* p = g_trampoline_page + g_trampoline_offset;
    g_trampoline_offset += size;
    return p;
}

// True if the displaced prologue captures rsp into a register or writes rsp.
// Such prologues cannot be relocated: the trampoline executes them with the
// trampoline's own rsp, so the resumed code (e.g. IL2CPP's shrink-wrap
// `mov rax, rsp; mov [rax+10h], rbx; ...`) dereferences the wrong stack.
static bool prologue_captures_rsp(const uint8_t* p, size_t n) {
    for (size_t i = 0; i + 2 < n; ++i) {
        // REX.W prefix then opcode.
        if (p[i] != 0x48) continue;
        uint8_t op = p[i + 1];
        uint8_t b2 = p[i + 2];
        if (op == 0x8B && b2 == 0xC4) return true;      // mov rax, rsp
        if (op == 0x89) {
            if (b2 == 0xE0) return true;                // mov rax, rsp
            if (b2 == 0xC4) return true;                // mov rsp, rax
            if (b2 == 0xE4) return true;                // mov rsp, rsp (no-op, odd but rsp-write)
        }
        if (op == 0x8D && (b2 & 7) == 4 && i + 3 < n && (p[i + 3] & 7) == 4)
            return true;  // lea reg, [rsp+disp] (ModRM rm=4 -> SIB, SIB base=4 -> rsp)
    }
    return false;
}

// Install an inline hook at the native code body. Overwrites methodPointer
// (offset 0) for runtime_invoke paths AND patches the native code body with
// a jump to our stub, intercepting direct managed-to-managed calls.
// Returns the trampoline address (which runs the displaced prologue then jumps
// to original+N) so hooks that need the original can call through. If the
// trampoline can't be built safely (instruction boundaries don't align), falls
// back to methodPointer-only and returns the original methodPointer.
void* install_inline(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr) {
    if (!method || !new_ptr) return nullptr;
    void* old_method_ptr = write_method_ptr(method, new_ptr);
    if (!old_method_ptr) return nullptr;

    auto* orig = static_cast<uint8_t*>(old_method_ptr);

    // Prefer a 5-byte relative jump (E9 rel32) if the stub is within ±2GB.
    // This reduces the number of displaced bytes the trampoline must cover.
    intptr_t rel = reinterpret_cast<intptr_t>(new_ptr) -
                   (reinterpret_cast<intptr_t>(orig) + 5);
    bool use_rel32 = (rel >= INT32_MIN && rel <= INT32_MAX);
    size_t patch_size = use_rel32 ? 5 : 12;

    // Walk instruction boundaries until we've covered at least patch_size.
    size_t copied = 0;
    while (copied < patch_size) {
        size_t il = insn_len(orig + copied);
        if (il == 0) break;
        copied += il;
    }
    if (copied < patch_size) {
        // Can't build a safe trampoline — don't inline-patch. methodPointer
        // was already overwritten, so runtime_invoke paths are still hooked.
        SYM_LOG_DEBUG("hook", "inline hook skipped (only {} bytes decoded, need {}): {}",
                copied, patch_size, static_cast<void*>(method));
        return old_method_ptr;
    }
    if (prologue_captures_rsp(orig, copied)) {
        // Relocating this prologue would break the function (see above).
        // Don't inline-patch; fall back to methodPointer-only.
        SYM_LOG("hook", "inline hook skipped (prologue captures rsp, un-relocatable): {}",
                static_cast<void*>(method));
        return old_method_ptr;
    }

    // Trampoline: <copied bytes> + <12-byte abs jump to orig+copied>.
    size_t tramp_size = copied + 12;
    uint8_t* tramp = alloc_trampoline(tramp_size);
    if (!tramp) {
        SYM_LOG("hook", "trampoline alloc failed for {}", static_cast<void*>(method));
        return old_method_ptr;
    }
    std::memcpy(tramp, orig, copied);
    // Rebase RIP-relative displacements in the relocated prologue: the disp32
    // is relative to the end of the instruction, and the instruction now sits
    // in the trampoline. Without this, any `[rip+disp32]` in the displaced
    // bytes reads from the wrong address (IL2CPP prologues commonly start
    // with `movzx eax, [rip+<metadata-init-flag>]`, which would fault).
    // Requires the trampoline to be within ±2GB of the module (alloc_trampoline
    // parks the page there); if a displacement cannot be rebased, bail to the
    // methodPointer-only fallback rather than leave a broken trampoline.
    for (size_t off = 0; off < copied;) {
        int disp_off = rip_rel_disp_offset(orig + off);
        if (disp_off >= 0 && disp_off + 4 <= static_cast<int>(copied - off)) {
            intptr_t delta = (orig + off) - (tramp + off);
            if (delta < INT32_MIN || delta > INT32_MAX) {
                SYM_LOG_DEBUG("hook", "rip-relative operand too far to rebase, skipping inline hook");
                return old_method_ptr;
            }
            int32_t* disp = reinterpret_cast<int32_t*>(tramp + off + static_cast<size_t>(disp_off));
            *disp += static_cast<int32_t>(delta);
        }
        size_t il = insn_len(orig + off);
        if (il == 0) break;
        off += il;
    }
    tramp[copied + 0] = 0x48;
    tramp[copied + 1] = 0xB8;
    uintptr_t target = reinterpret_cast<uintptr_t>(orig) + copied;
    std::memcpy(tramp + copied + 2, &target, 8);
    tramp[copied + 10] = 0xFF;
    tramp[copied + 11] = 0xE0;

    // Inline-patch the native code body.
    uint8_t jump[12];
    if (use_rel32) {
        jump[0] = 0xE9;
        int32_t r = static_cast<int32_t>(rel);
        std::memcpy(jump + 1, &r, 4);
    } else {
        jump[0] = 0x48;  // REX.W
        jump[1] = 0xB8;  // mov rax, imm64
        std::memcpy(jump + 2, &new_ptr, 8);
        jump[10] = 0xFF;  // jmp rax
        jump[11] = 0xE0;
    }
    DWORD old_prot = 0;
    if (!VirtualProtect(old_method_ptr, patch_size, PAGE_EXECUTE_READWRITE, &old_prot)) {
        SYM_LOG("hook", "inline VirtualProtect(RWX) failed: {}", GetLastError());
        return old_method_ptr;
    }
    std::memcpy(old_method_ptr, jump, patch_size);
    DWORD dummy = 0;
    VirtualProtect(old_method_ptr, patch_size, old_prot, &dummy);
    FlushInstructionCache(GetCurrentProcess(), old_method_ptr, patch_size);
    SYM_LOG_DEBUG("hook", "inline hooked {} at {} -> {} ({}-byte patch, trampoline={}, copied={})",
             static_cast<void*>(method), old_method_ptr, new_ptr,
             patch_size, static_cast<void*>(tramp), copied);
    return tramp;
}

void uninstall(::symphytum::il2cpp::Il2CppMethod* method, void* original) {
    if (!method || !original) return;
    write_method_ptr(method, original);
}

void* install_jump(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr) {
    if (!method || !new_ptr) return nullptr;
    void* old = write_method_ptr(method, new_ptr);
    if (!old) return nullptr;
    auto* orig = static_cast<uint8_t*>(old);
    // mov rax, imm64; jmp rax — absolute, no ±2GB constraint, and no
    // instruction-boundary analysis needed (the patched bytes are dead; the
    // original is never resumed through them).
    uint8_t jump[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    std::memcpy(jump + 2, &new_ptr, 8);
    DWORD old_prot = 0;
    if (!VirtualProtect(orig, 12, PAGE_EXECUTE_READWRITE, &old_prot)) {
        SYM_LOG("hook", "install_jump VirtualProtect failed: {}", GetLastError());
        return old;
    }
    std::memcpy(orig, jump, 12);
    DWORD dummy = 0;
    VirtualProtect(orig, 12, old_prot, &dummy);
    FlushInstructionCache(GetCurrentProcess(), orig, 12);
    SYM_LOG_DEBUG("hook", "entry-jumped {} at {} -> {}", static_cast<void*>(method),
                  old, new_ptr);
    return old;
}

}  // namespace symphytum::hook
