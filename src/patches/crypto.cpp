#include "patches/crypto.hpp"
#include "il2cpp.hpp"
#include "config.hpp"
#include "log.hpp"
#include "hook.hpp"
#include <windows.h>
#include <cstring>

namespace symphytum::patches {

namespace {

// DefaultMarshallerFactory hooks
typedef il2cpp::Il2CppArray* (*Encrypt_t)(il2cpp::Il2CppObject* self, il2cpp::Il2CppArray* bytes, il2cpp::Il2CppArray* key, il2cpp::Il2CppArray* iv, void* method);
Encrypt_t g_orig_Encrypt = nullptr;

il2cpp::Il2CppArray* hook_Encrypt(il2cpp::Il2CppObject* self, il2cpp::Il2CppArray* bytes, il2cpp::Il2CppArray* key, il2cpp::Il2CppArray* iv, void* method) {
    if (symphytum::config::g.disable_encryption) {
        SYM_LOG("crypto", "Encrypt bypassed (returning input bytes directly)");
        return bytes;
    }
    if (g_orig_Encrypt) {
        return g_orig_Encrypt(self, bytes, key, iv, method);
    }
    return bytes;
}

typedef il2cpp::Il2CppArray* (*Decrypt_t)(il2cpp::Il2CppObject* self, il2cpp::Il2CppArray* bytes, int32_t offset, int32_t length, il2cpp::Il2CppArray* key, il2cpp::Il2CppArray* iv, void* method);
Decrypt_t g_orig_Decrypt = nullptr;

il2cpp::Il2CppArray* hook_Decrypt(il2cpp::Il2CppObject* self, il2cpp::Il2CppArray* bytes, int32_t offset, int32_t length, il2cpp::Il2CppArray* key, il2cpp::Il2CppArray* iv, void* method) {
    if (symphytum::config::g.disable_encryption) {
        SYM_LOG("crypto", "Decrypt bypassed (copying offset={} len={})", offset, length);
        if (!bytes) {
            SYM_LOG_ERROR("crypto", "Decrypt: input bytes is nullptr");
            return nullptr;
        }
        il2cpp::Il2CppClass* byte_klass = il2cpp::find_class("System", "Byte");
        if (!byte_klass) {
            SYM_LOG_ERROR("crypto", "System.Byte class not found");
            return bytes;
        }
        il2cpp::Il2CppArray* res = il2cpp::array_new(byte_klass, static_cast<uintptr_t>(length));
        if (!res) {
            SYM_LOG_ERROR("crypto", "Failed to allocate Decrypt result array");
            return bytes;
        }
        uint8_t* src = reinterpret_cast<uint8_t*>(bytes) + 32 + offset;
        uint8_t* dst = reinterpret_cast<uint8_t*>(res) + 32;
        std::memcpy(dst, src, static_cast<size_t>(length));
        return res;
    }
    if (g_orig_Decrypt) {
        return g_orig_Decrypt(self, bytes, offset, length, key, iv, method);
    }
    return bytes;
}

// DefaultMarshallerFactory.Header hooks (when nested Header struct is used)

typedef void (*Header_ctor_1_t)(void* self, bool isCompress, il2cpp::Il2CppArray* key, void* method);
Header_ctor_1_t g_orig_Header_ctor_1 = nullptr;

void hook_Header_ctor_1(void* self, bool isCompress, il2cpp::Il2CppArray* key, void* method) {
    if (symphytum::config::g.disable_encryption) {
        il2cpp::Il2CppClass* byte_klass = il2cpp::find_class("System", "Byte");
        if (byte_klass) {
            il2cpp::Il2CppArray* empty_arr = il2cpp::array_new(byte_klass, 0);
            *reinterpret_cast<il2cpp::Il2CppArray**>(self) = empty_arr;
            return;
        }
    }
    if (g_orig_Header_ctor_1) {
        g_orig_Header_ctor_1(self, isCompress, key, method);
    }
}

typedef bool (*get_IsCompress_t)(void* self, void* method);
get_IsCompress_t g_orig_get_IsCompress = nullptr;

bool hook_get_IsCompress(void* self, void* method) {
    if (symphytum::config::g.disable_encryption) {
        return false;
    }
    if (g_orig_get_IsCompress) {
        return g_orig_get_IsCompress(self, method);
    }
    return false;
}

typedef int32_t (*get_KeyLength_t)(void* self, void* method);
get_KeyLength_t g_orig_get_KeyLength = nullptr;

int32_t hook_get_KeyLength(void* self, void* method) {
    if (symphytum::config::g.disable_encryption) {
        return 0;
    }
    if (g_orig_get_KeyLength) {
        return g_orig_get_KeyLength(self, method);
    }
    return 0;
}

typedef int32_t (*get_Size_t)(void* self, void* method);
get_Size_t g_orig_get_Size = nullptr;

int32_t hook_get_Size(void* self, void* method) {
    if (symphytum::config::g.disable_encryption) {
        return 0;
    }
    if (g_orig_get_Size) {
        return g_orig_get_Size(self, method);
    }
    return 0;
}

typedef int32_t (*get_DataSize_t)(void* self, void* method);
get_DataSize_t g_orig_get_DataSize = nullptr;

int32_t hook_get_DataSize(void* self, void* method) {
    if (symphytum::config::g.disable_encryption) {
        if (self) {
            il2cpp::Il2CppArray* header_arr = *reinterpret_cast<il2cpp::Il2CppArray**>(self);
            if (header_arr) {
                uint32_t len = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(header_arr) + 24);
                return static_cast<int32_t>(len);
            }
        }
        return 0;
    }
    if (g_orig_get_DataSize) {
        return g_orig_get_DataSize(self, method);
    }
    return 0;
}

} // namespace

bool install_crypto() {
    il2cpp::Il2CppClass* k = il2cpp::find_class("Qua.Network", "DefaultMarshallerFactory");
    if (!k) {
        SYM_LOG_ERROR("crypto", "class Qua.Network.DefaultMarshallerFactory not found");
        return false;
    }

    bool ok = true;

    // Hook Encrypt
    il2cpp::Il2CppMethod* m_encrypt = il2cpp::class_get_method_from_name(k, "Encrypt", 3);
    if (m_encrypt) {
        void* old = symphytum::hook::install_inline(m_encrypt, reinterpret_cast<void*>(&hook_Encrypt));
        if (old) {
            g_orig_Encrypt = reinterpret_cast<Encrypt_t>(old);
            SYM_LOG("crypto", "hooked DefaultMarshallerFactory.Encrypt");
        } else {
            SYM_LOG_ERROR("crypto", "failed to hook DefaultMarshallerFactory.Encrypt");
            ok = false;
        }
    } else {
        SYM_LOG_ERROR("crypto", "method DefaultMarshallerFactory.Encrypt not found");
        ok = false;
    }

    // Hook Decrypt
    il2cpp::Il2CppMethod* m_decrypt = il2cpp::class_get_method_from_name(k, "Decrypt", 5);
    if (m_decrypt) {
        void* old = symphytum::hook::install_inline(m_decrypt, reinterpret_cast<void*>(&hook_Decrypt));
        if (old) {
            g_orig_Decrypt = reinterpret_cast<Decrypt_t>(old);
            SYM_LOG("crypto", "hooked DefaultMarshallerFactory.Decrypt");
        } else {
            SYM_LOG_ERROR("crypto", "failed to hook DefaultMarshallerFactory.Decrypt");
            ok = false;
        }
    } else {
        SYM_LOG_ERROR("crypto", "method DefaultMarshallerFactory.Decrypt not found");
        ok = false;
    }

    // Hook DefaultMarshallerFactory+Header methods
    il2cpp::Il2CppClass* header_klass = nullptr;
    void* nested_iter = nullptr;
    while (il2cpp::Il2CppClass* nested = il2cpp::class_get_nested_types(k, &nested_iter)) {
        const char* name = il2cpp::class_get_name(nested);
        if (name && strcmp(name, "Header") == 0) {
            header_klass = nested;
            break;
        }
    }
    if (header_klass) {
        // .ctor(bool, byte[]) - unique RVA
        il2cpp::Il2CppMethod* m_ctor = il2cpp::class_get_method_from_name(header_klass, ".ctor", 2);
        if (m_ctor) {
            void* old = symphytum::hook::install_inline(m_ctor, reinterpret_cast<void*>(&hook_Header_ctor_1));
            if (old) {
                g_orig_Header_ctor_1 = reinterpret_cast<Header_ctor_1_t>(old);
                SYM_LOG("crypto", "hooked Header.ctor(bool, byte[])");
            } else {
                SYM_LOG_ERROR("crypto", "failed to hook Header.ctor(bool, byte[])");
                ok = false;
            }
        }

        // get_IsCompress - unique RVA
        il2cpp::Il2CppMethod* m_get_IsCompress = il2cpp::class_get_method_from_name(header_klass, "get_IsCompress", 0);
        if (m_get_IsCompress) {
            void* old = symphytum::hook::install_inline(m_get_IsCompress, reinterpret_cast<void*>(&hook_get_IsCompress));
            if (old) {
                g_orig_get_IsCompress = reinterpret_cast<get_IsCompress_t>(old);
                SYM_LOG("crypto", "hooked Header.get_IsCompress");
            } else {
                SYM_LOG_ERROR("crypto", "failed to hook Header.get_IsCompress");
                ok = false;
            }
        }

        // get_KeyLength - unique RVA
        il2cpp::Il2CppMethod* m_get_KeyLength = il2cpp::class_get_method_from_name(header_klass, "get_KeyLength", 0);
        if (m_get_KeyLength) {
            void* old = symphytum::hook::install_inline(m_get_KeyLength, reinterpret_cast<void*>(&hook_get_KeyLength));
            if (old) {
                g_orig_get_KeyLength = reinterpret_cast<get_KeyLength_t>(old);
                SYM_LOG("crypto", "hooked Header.get_KeyLength");
            } else {
                SYM_LOG_ERROR("crypto", "failed to hook Header.get_KeyLength");
                ok = false;
            }
        }

        // get_Size - unique RVA
        il2cpp::Il2CppMethod* m_get_Size = il2cpp::class_get_method_from_name(header_klass, "get_Size", 0);
        if (m_get_Size) {
            void* old = symphytum::hook::install_inline(m_get_Size, reinterpret_cast<void*>(&hook_get_Size));
            if (old) {
                g_orig_get_Size = reinterpret_cast<get_Size_t>(old);
                SYM_LOG("crypto", "hooked Header.get_Size");
            } else {
                SYM_LOG_ERROR("crypto", "failed to hook Header.get_Size");
                ok = false;
            }
        }

        // get_DataSize - unique RVA
        il2cpp::Il2CppMethod* m_get_DataSize = il2cpp::class_get_method_from_name(header_klass, "get_DataSize", 0);
        if (m_get_DataSize) {
            void* old = symphytum::hook::install_inline(m_get_DataSize, reinterpret_cast<void*>(&hook_get_DataSize));
            if (old) {
                g_orig_get_DataSize = reinterpret_cast<get_DataSize_t>(old);
                SYM_LOG("crypto", "hooked Header.get_DataSize");
            } else {
                SYM_LOG_ERROR("crypto", "failed to hook Header.get_DataSize");
                ok = false;
            }
        }
    } else {
        SYM_LOG_ERROR("crypto", "class Header not found");
        ok = false;
    }

    return ok;
}

} // namespace symphytum::patches
