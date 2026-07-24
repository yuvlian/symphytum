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

}  // namespace

bool load() {
    std::wstring dir = dll_dir();
    std::wstring ini = dir + L"symphytum.ini";

    // Write a default ini if none exists so the user can see the keys.
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const wchar_t* def =
            L"[symphytum]\r\n"
            L"; redirect target (must include scheme + trailing slash)\r\n"
            L"server=http://127.0.0.1:3000/\r\n"
            L"; redirect target for asset requests\r\n"
            L"asset_server=http://127.0.0.1:3000/\r\n"
            L"; 0 = only error logs, 1 = error & info, 2 = error & info & debug\r\n"
            L"log_level=1\r\n";
        HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            WriteFile(h, def, static_cast<DWORD>(wcslen(def) * sizeof(wchar_t)), &wr, nullptr);
            CloseHandle(h);
        }
    }

    g.server       = read_ini(ini.c_str(), L"symphytum", L"server", L"http://127.0.0.1:3000/");
    g.asset_server = read_ini(ini.c_str(), L"symphytum", L"asset_server", L"http://127.0.0.1:3000/");
    g.log_level    = static_cast<int>(GetPrivateProfileIntW(L"symphytum", L"log_level", 1, ini.c_str()));

    SYM_LOG_OVERRIDE("config", "server={}", narrow(g.server));
    SYM_LOG_OVERRIDE("config", "asset_server={}", narrow(g.asset_server));
    return true;
}

}  // namespace symphytum::config
