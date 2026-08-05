// Method-pointer hook: replaces MethodInfo->methodPointer with a new function.
//
// il2cpp-compiled methods are dispatched via MethodInfo->methodPointer. Overwriting
// that field (offset 0 of MethodInfo) with our own function pointer redirects all
// calls to the method — the il2cpp equivalent of Harmony's Prefix/Postfix, without
// any native-code patching or trampoline allocation.
//
// Our hook receives the original methodPointer (so it can call through to the
// original implementation) and the MethodInfo* (some il2cpp call signatures pass
// it as a trailing arg; we forward it when needed).
//
// This sidesteps every fragile piece of inline hooking: no instruction-length
// decoder, no trampoline allocation, no W^X fiddling. The cost: the hook only
// fires on il2cpp-dispatched calls (not direct native calls), which is fine
// because all our targets are managed methods invoked through il2cpp.

#pragma once
#include "il2cpp.hpp"
#include <cstdint>

namespace symphytum::hook {

// Install a method-pointer hook. Returns the original methodPointer (pass to
// the hook if it needs to call the original implementation), or nullptr on
// failure. `method` must be a non-null Il2CppMethod*.
//
// `new_ptr` is the replacement function. Its signature must match the il2cpp
// calling convention: first arg is the instance (if instance method), followed
// by the method's parameters, with an extra `MethodInfo*` trailing arg added
// Install an inline hook: patches the native code at methodPointer with a
// JMP to `new_ptr`. Use this when methodPointer overwrite alone doesn't
// intercept calls (il2cpp managed-to-managed calls use baked-in direct
// addresses, not MethodInfo->methodPointer indirection). Also overwrites
// methodPointer for runtime_invoke paths. No trampoline — our hooks are
// prefix-only and don't call the original.
void* install_inline(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr);

// Install a bare entry-point jump: overwrites methodPointer AND patches the
// first 12 bytes of the native body with `mov rax, imm64; jmp rax`. No
// trampoline, no prologue relocation, works on ANY prologue shape — use this
// for hooks that never call the original (the displaced bytes are dead).
void* install_jump(::symphytum::il2cpp::Il2CppMethod* method, void* new_ptr);

 // Uninstall (restore original). `original` is the value returned by install().

}  // namespace symphytum::hook
