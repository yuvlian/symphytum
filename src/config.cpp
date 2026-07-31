// Config implementation.
#include "config.hpp"
#include "log.hpp"
#include <windows.h>
#include <string>

namespace symphytum::config {

Config g;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

namespace {

std::wstring dll_dir() {
    wchar_t path[MAX_PATH] = {};
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&dll_dir), &h);
    if (!h) return L".\\";
    if (!GetModuleFileNameW(h, path, MAX_PATH)) return L".\\";
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".\\";
    return p.substr(0, slash + 1);
}

std::wstring read_ini(const wchar_t* ini_path, const wchar_t* section,
                      const wchar_t* key, const wchar_t* def) {
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(section, key, def, buf, 1024, ini_path);
    return std::wstring(buf);
}

bool read_ini_bool(const wchar_t* ini_path, const wchar_t* section,
                        const wchar_t* key, bool def) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(section, key, def ? L"true" : L"false", buf, 64, ini_path);
    std::wstring s(buf);
    for (auto& c : s) c = static_cast<wchar_t>(std::tolower(c));
    if (s == L"true" || s == L"1" || s == L"yes") return true;
    if (s == L"false" || s == L"0" || s == L"no") return false;
    return def;
}

std::wstring replace_escaped_newlines(const std::wstring& str) {
    std::wstring res;
    res.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == L'\\' && i + 1 < str.size()) {
            if (str[i+1] == L'n') {
                res += L'\n';
                ++i;
                continue;
            } else if (str[i+1] == L'r') {
                res += L'\r';
                ++i;
                continue;
            }
        }
        res += str[i];
    }
    return res;
}

}  // namespace

bool load() {
    std::wstring dir = dll_dir();
    std::wstring ini = dir + L"symphytum.ini";

    // Write a default ini if none exists so the user can see the keys.
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const wchar_t* def =
            L"[symphytum]\r\n"
            L"enable_patches=true\r\n"
            L"game_server=https://127.0.0.1:3000/\r\n"
            L"redirect_game_requests=true\r\n"
            L"asset_server=https://127.0.0.1:3000/\r\n"
            L"redirect_asset_requests=false\r\n"
            L"disable_encryption=true\r\n"
            L"disable_cert_pinning=true\r\n"
            L"use_custom_root_cert=false\r\n"
            L"; PEM string. Use literal \\n or \\r\\n for multi-line certs\r\n"
            L"custom_root_cert=\r\n"
            L"; 0 = only error logs, 1 = error & info, 2 = error & info & debug\r\n"
            L"log_level=2\r\n";
        HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            WriteFile(h, def, static_cast<DWORD>(wcslen(def) * sizeof(wchar_t)), &wr, nullptr);
            CloseHandle(h);
        }
    }

    g.enable_patches          = read_ini_bool(ini.c_str(), L"symphytum", L"enable_patches", true);
    g.game_server             = read_ini(ini.c_str(), L"symphytum", L"game_server", L"http://127.0.0.1:3000/");
    g.redirect_game_requests  = read_ini_bool(ini.c_str(), L"symphytum", L"redirect_game_requests", true);
    g.asset_server            = read_ini(ini.c_str(), L"symphytum", L"asset_server", L"http://127.0.0.1:3000/");
    g.redirect_asset_requests = read_ini_bool(ini.c_str(), L"symphytum", L"redirect_asset_requests", true);
    g.disable_encryption      = read_ini_bool(ini.c_str(), L"symphytum", L"disable_encryption", false);
    g.disable_cert_pinning    = read_ini_bool(ini.c_str(), L"symphytum", L"disable_cert_pinning", true);
    g.use_custom_root_cert    = read_ini_bool(ini.c_str(), L"symphytum", L"use_custom_root_cert", false);
    g.custom_root_cert        = replace_escaped_newlines(read_ini(ini.c_str(), L"symphytum", L"custom_root_cert", L""));
    g.log_level               = static_cast<int>(GetPrivateProfileIntW(L"symphytum", L"log_level", 1, ini.c_str()));

    SYM_LOG_OVERRIDE("config", "enable_patches={}", g.enable_patches);
    SYM_LOG_OVERRIDE("config", "game_server={}", narrow(g.game_server));
    SYM_LOG_OVERRIDE("config", "redirect_game_requests={}", g.redirect_game_requests);
    SYM_LOG_OVERRIDE("config", "asset_server={}", narrow(g.asset_server));
    SYM_LOG_OVERRIDE("config", "redirect_asset_requests={}", g.redirect_asset_requests);
    SYM_LOG_OVERRIDE("config", "disable_encryption={}", g.disable_encryption);
    SYM_LOG_OVERRIDE("config", "disable_cert_pinning={}", g.disable_cert_pinning);
    SYM_LOG_OVERRIDE("config", "use_custom_root_cert={}", g.use_custom_root_cert);
    SYM_LOG_OVERRIDE("config", "custom_root_cert_len={}", g.custom_root_cert.size());
    return true;
}

}  // namespace symphytum::config
