// Locator for the autoplay-bake gate inside MusicScoreController::CreateChartData.
//
// The compiler emits (base register varies, disp8 or disp32):
//     cmp byte ptr [reg+_isAuto], 0      ; 80 7?/B? <disp> 00
//     jz  skip_bake                      ; 0F 84 rel32
// or:
//     movzx eax, byte ptr [reg+_isAuto]  ; 0F B6 ?/B? <disp>
//     test al, al                        ; 84 C0
//     jz  skip_bake                      ; 0F 84 rel32
//
// NOP-ing the jz makes the bake unconditional, so the Auto judge results are
// always pre-baked into the notes at chart build time (real-autoplay behavior
// without starting the live in autoplay mode).
//
// Pure byte logic — deliberately dependency-free so it can be unit-tested.

#pragma once
#include <cstdint>
#include <cstddef>

namespace symphytum::patches::bake_gate {

// Scan `body[0..n)` for the bake-gate `jz rel32` (0F 84 ..). Returns its
// offset within `body`, or SIZE_MAX if not found. `field_off` is the _isAuto
// field offset used to match the displacement.
inline size_t find_jz(const uint8_t* body, size_t n, uint32_t field_off) {
    if (!body || n < 8) return SIZE_MAX;
    for (size_t i = 0; i + 8 <= n; ++i) {
        size_t k = i;
        if (body[k] >= 0x40 && body[k] <= 0x4F) k++;  // optional REX
        if (k + 5 >= n) break;

        // Form 1: cmp byte ptr [reg+off], imm8  (opcode 80, /7, mod 01/10, no SIB)
        if (body[k] == 0x80) {
            uint8_t m = body[k + 1];
            uint8_t mod = m >> 6, rm = m & 7;
            if ((m & 0x38) == 0x38 && mod != 3 && rm != 4) {
                size_t jz_off = 0;
                if (mod == 1) {  // disp8: 80 7X <off8> <imm8>
                    if (body[k + 2] == static_cast<uint8_t>(field_off) &&
                        body[k + 3] == 0) jz_off = k + 4;
                    else continue;
                } else {         // disp32: 80 BX <off32> <imm8>
                    if (body[k + 2] == static_cast<uint8_t>(field_off) &&
                        body[k + 3] == 0 && body[k + 4] == 0 && body[k + 5] == 0 &&
                        body[k + 6] == 0) jz_off = k + 7;
                    else continue;
                }
                if (jz_off + 6 <= n && body[jz_off] == 0x0F && body[jz_off + 1] == 0x84)
                    return jz_off;
            }
        }
        // Form 2: movzx eax, byte ptr [reg+off] ; test al, al ; jz
        if (body[k] == 0x0F && body[k + 1] == 0xB6) {
            uint8_t m = body[k + 2];
            uint8_t mod = m >> 6, rm = m & 7;
            if ((m & 0x38) == 0 && mod != 3 && rm != 4) {
                size_t test_off = 0;
                if (mod == 1) {  // disp8
                    if (body[k + 3] == static_cast<uint8_t>(field_off)) test_off = k + 4;
                    else continue;
                } else {         // disp32
                    if (body[k + 3] == static_cast<uint8_t>(field_off) &&
                        body[k + 4] == 0 && body[k + 5] == 0 && body[k + 6] == 0)
                        test_off = k + 7;
                    else continue;
                }
                if (test_off + 8 <= n && body[test_off] == 0x84 && body[test_off + 1] == 0xC0 &&
                    body[test_off + 2] == 0x0F && body[test_off + 3] == 0x84)
                    return test_off + 2;
            }
        }
        // Form 3: mov al, byte ptr [reg+off] ; test al, al ; jz
        if (body[k] == 0x8A) {
            uint8_t m = body[k + 1];
            uint8_t mod = m >> 6, rm = m & 7;
            if ((m & 0x38) == 0 && mod != 3 && rm != 4) {
                size_t test_off = 0;
                if (mod == 1) {  // disp8: 8A 4X <off8>
                    if (body[k + 2] == static_cast<uint8_t>(field_off)) test_off = k + 3;
                    else continue;
                } else {         // disp32: 8A 8X <off32>
                    if (body[k + 2] == static_cast<uint8_t>(field_off) &&
                        body[k + 3] == 0 && body[k + 4] == 0 && body[k + 5] == 0)
                        test_off = k + 6;
                    else continue;
                }
                if (test_off + 8 <= n && body[test_off] == 0x84 && body[test_off + 1] == 0xC0 &&
                    body[test_off + 2] == 0x0F && body[test_off + 3] == 0x84)
                    return test_off + 2;
            }
        }
    }
    return SIZE_MAX;
}

// Find the `mov r32, 0x64` (Auto=100 judge immediate) in the bake block that
// follows the gate jz. The judge type baked into every note comes from this
// single immediate; flipping it to 0x06 makes the bake produce PerfectPlus.
// Returns the offset of the 0x64 byte within `body`, or SIZE_MAX.
inline size_t find_auto_judge_imm(const uint8_t* body, size_t n, size_t gate_jz) {
    size_t end = gate_jz + 0x200;
    if (end > n) end = n;
    for (size_t i = gate_jz; i + 5 <= end; ++i) {
        size_t k = i;
        if (body[k] >= 0x40 && body[k] <= 0x4F) k++;  // REX
        if (body[k] >= 0xB8 && body[k] <= 0xBF) {     // mov r32, imm32
            if (body[k + 1] == 0x64 && body[k + 2] == 0 &&
                body[k + 3] == 0 && body[k + 4] == 0)
                return k + 1;
        }
    }
    return SIZE_MAX;
}

}  // namespace symphytum::patches::bake_gate
