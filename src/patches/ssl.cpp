#include "patches/ssl.hpp"
#include "il2cpp.hpp"
#include "config.hpp"
#include "log.hpp"
#include "hook.hpp"
#include <windows.h>
#include <string>

namespace symphytum::patches {

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

#pragma pack(push, 1)
struct NullableBool {
    bool value;
    bool has_value;
};
#pragma pack(pop)

typedef il2cpp::Il2CppString* (*GetSSLRootCertificates_t)(il2cpp::Il2CppObject* self, void* method);
GetSSLRootCertificates_t g_orig_GetSSLRootCertificates = nullptr;

il2cpp::Il2CppString* hook_GetSSLRootCertificates(il2cpp::Il2CppObject* self, void* method) {
    if (symphytum::config::g.use_custom_root_cert) {
        std::string cert = narrow(symphytum::config::g.custom_root_cert);
        static bool logged = false;
        if (!logged) {
            SYM_LOG("ssl", "Returning custom root cert (len={})", cert.size());
            logged = true;
        }
        return il2cpp::string_new(cert.c_str());
    }
    // if (symphytum::config::g.disable_cert_pinning) {
    //     SYM_LOG("ssl", "Bypassing cert pinning (returning empty string)");
    //     return nullptr;
    // }
    if (g_orig_GetSSLRootCertificates) {
        return g_orig_GetSSLRootCertificates(self, method);
    }
    return nullptr;
}

typedef void (*Initialize_t)(il2cpp::Il2CppObject* self, void* ctx, il2cpp::Il2CppObject* settings, void* method);
Initialize_t g_orig_Initialize = nullptr;

void hook_Initialize(il2cpp::Il2CppObject* self, void* ctx, il2cpp::Il2CppObject* settings, void* method) {
    if (symphytum::config::g.disable_cert_pinning && settings) {
        il2cpp::Il2CppClass* settings_class = il2cpp::object_get_class(settings);
        il2cpp::Il2CppMethod* set_skip = il2cpp::class_get_method_from_name(settings_class, "set_SkipCertificateVerification", 1);
        if (set_skip) {
            NullableBool val = { true, true };
            void* args[1] = { &val };
            void* exc = nullptr;
            il2cpp::runtime_invoke(set_skip, settings, args, &exc);
            if (exc) {
                SYM_LOG_ERROR("ssl", "Exception setting SkipCertificateVerification");
            } else {
                SYM_LOG("ssl", "Set SkipCertificateVerification to true");
            }
        } else {
            SYM_LOG_ERROR("ssl", "set_SkipCertificateVerification method not found");
        }
    }
    if (g_orig_Initialize) {
        g_orig_Initialize(self, ctx, settings, method);
    }
}

} // namespace

bool install_ssl() {
    bool ok = true;

    il2cpp::Il2CppClass* api_class = il2cpp::find_class("Vision.Common.Network", "Api");
    if (api_class) {
        il2cpp::Il2CppMethod* get_certs = il2cpp::class_get_method_from_name(api_class, "GetSSLRootCertificates", 0);
        if (get_certs) {
            void* old = symphytum::hook::install_inline(get_certs, reinterpret_cast<void*>(&hook_GetSSLRootCertificates));
            if (old) {
                g_orig_GetSSLRootCertificates = reinterpret_cast<GetSSLRootCertificates_t>(old);
                SYM_LOG("ssl", "hooked Api.GetSSLRootCertificates");
            } else {
                SYM_LOG_ERROR("ssl", "failed to hook Api.GetSSLRootCertificates");
                ok = false;
            }
        } else {
            SYM_LOG_ERROR("ssl", "method Api.GetSSLRootCertificates not found");
            ok = false;
        }
    } else {
        SYM_LOG_ERROR("ssl", "class Vision.Common.Network.Api not found");
        ok = false;
    }

    il2cpp::Il2CppClass* handler_class = il2cpp::find_class("Cysharp.Net.Http", "NativeHttpHandlerCore");
    if (handler_class) {
        il2cpp::Il2CppMethod* init_method = il2cpp::class_get_method_from_name(handler_class, "Initialize", 2);
        if (init_method) {
            void* old = symphytum::hook::install_inline(init_method, reinterpret_cast<void*>(&hook_Initialize));
            if (old) {
                g_orig_Initialize = reinterpret_cast<Initialize_t>(old);
                SYM_LOG("ssl", "hooked NativeHttpHandlerCore.Initialize");
            } else {
                SYM_LOG_ERROR("ssl", "failed to hook NativeHttpHandlerCore.Initialize");
                ok = false;
            }
        } else {
            SYM_LOG_ERROR("ssl", "method NativeHttpHandlerCore.Initialize not found");
            ok = false;
        }
    } else {
        SYM_LOG_ERROR("ssl", "class NativeHttpHandlerCore not found");
        ok = false;
    }

    return ok;
}

} // namespace symphytum::patches
