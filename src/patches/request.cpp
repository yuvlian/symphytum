#include "patches/request.hpp"
#include "il2cpp.hpp"
#include "il2cpp_names.hpp"
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

bool url_host_scheme(const std::string& url, std::string& scheme, std::string& host) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return false;
    scheme = url.substr(0, pos);
    size_t p = pos + 3;
    size_t slash = url.find('/', p);
    host = url.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
    return true;
}

std::string url_replace_host_scheme(const std::string& original, const std::string& target) {
    std::string t_scheme, t_host;
    if (!url_host_scheme(target, t_scheme, t_host)) return original;
    auto pos = original.find("://");
    if (pos == std::string::npos) return original;
    size_t p = pos + 3;
    size_t slash = original.find('/', p);
    std::string rest = (slash == std::string::npos) ? "/" : original.substr(slash);
    return t_scheme + "://" + t_host + rest;
}

il2cpp::Il2CppObject* create_uri(const std::string& url_str) {
    il2cpp::Il2CppClass* uri_class = il2cpp::find_class("System", "Uri");
    if (!uri_class) {
        SYM_LOG_ERROR("request", "System.Uri class not found");
        return nullptr;
    }
    il2cpp::Il2CppMethod* uri_ctor = il2cpp::class_get_method_from_name(uri_class, ".ctor", 1);
    if (!uri_ctor) {
        SYM_LOG_ERROR("request", "System.Uri constructor (.ctor/1) not found");
        return nullptr;
    }
    il2cpp::Il2CppObject* uri_obj = (il2cpp::Il2CppObject*)il2cpp::object_new(uri_class);
    if (!uri_obj) {
        SYM_LOG_ERROR("request", "Failed to allocate System.Uri object");
        return nullptr;
    }
    il2cpp::Il2CppString* cs_str = il2cpp::string_new(url_str.c_str());
    void* args[1] = { cs_str };
    void* exc = nullptr;
    il2cpp::runtime_invoke(uri_ctor, uri_obj, args, &exc);
    if (exc) {
        SYM_LOG_ERROR("request", "Exception in System.Uri constructor");
        return nullptr;
    }
    return uri_obj;
}

typedef void (*set_RequestUri_t)(il2cpp::Il2CppObject* self, il2cpp::Il2CppObject* uri_obj);
set_RequestUri_t g_orig_set_RequestUri = nullptr;

void hook_set_RequestUri(il2cpp::Il2CppObject* self, il2cpp::Il2CppObject* uri_obj) {
    if (uri_obj) {
        il2cpp::Il2CppClass* uri_class = il2cpp::object_get_class(uri_obj);
        il2cpp::Il2CppMethod* to_string_method = il2cpp::class_get_method_from_name(uri_class, "ToString", 0);
        if (to_string_method) {
            il2cpp::Il2CppString* uri_str = (il2cpp::Il2CppString*)il2cpp::runtime_invoke(to_string_method, uri_obj, nullptr, nullptr);
            if (uri_str) {
                std::string full_url = il2cpp::string_to_utf8(uri_str);
                SYM_LOG("request", "Original URL: {}", full_url);

                std::string target_server = narrow(symphytum::config::g.server);
                if (full_url.find("asset") != std::string::npos) {
                    target_server = narrow(symphytum::config::g.asset_server);
                }

                std::string new_url = url_replace_host_scheme(full_url, target_server);
                SYM_LOG("request", "Rewritten URL: {}", new_url);

                il2cpp::Il2CppObject* new_uri_obj = create_uri(new_url);
                if (new_uri_obj) {
                    uri_obj = new_uri_obj;
                }
            }
        }
    }

    if (g_orig_set_RequestUri) {
        g_orig_set_RequestUri(self, uri_obj);
    }
}

}  // namespace

bool install_request() {
    il2cpp::Il2CppClass* k = il2cpp::find_class("System.Net.Http", "HttpRequestMessage");
    if (!k) {
        SYM_LOG_ERROR("request", "class System.Net.Http.HttpRequestMessage not found");
        return false;
    }
    il2cpp::Il2CppMethod* m = il2cpp::class_get_method_from_name(k, "set_RequestUri", 1);
    if (!m) {
        SYM_LOG_ERROR("request", "method HttpRequestMessage.set_RequestUri not found");
        return false;
    }
    void* old = symphytum::hook::install_inline(m, reinterpret_cast<void*>(&hook_set_RequestUri));
    if (old) {
        g_orig_set_RequestUri = reinterpret_cast<set_RequestUri_t>(old);
        SYM_LOG("request", "hooked HttpRequestMessage.set_RequestUri");
        return true;
    }
    SYM_LOG_ERROR("request", "failed to hook HttpRequestMessage.set_RequestUri");
    return false;
}

}  // namespace symphytum::patches
