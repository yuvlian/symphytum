#include "init.hpp"
#include "config.hpp"
#include "il2cpp_names.hpp"
#include "il2cpp.hpp"
#include "log.hpp"
#include "patches/request.hpp"
#include "patches/ssl.hpp"
#include "patches/crypto.hpp"
#include <windows.h>

namespace symphytum {

namespace {

// Wait for GameAssembly.dll to be loaded AND il2cpp to be initialized.
// We poll il2cpp_domain_get() until it returns non-null; that's the il2cpp
// "ready" signal we need before resolving any class/method.
bool wait_for_il2cpp_ready() {
    SYM_LOG("init", "waiting for GameAssembly.dll + il2cpp names...");
    bool logged_ga = false;
    for (int i = 0; i < 600; ++i) {  // up to ~60s
        if (!GetModuleHandleW(L"GameAssembly.dll")) {
            if (i == 0 || (i % 50 == 0))  // heartbeat every ~5s
                SYM_LOG("init", "poll {}: GameAssembly.dll not yet loaded", i);
            Sleep(100);
            continue;
        }
        if (!logged_ga) {
            SYM_LOG("init", "GameAssembly.dll loaded (after {} polls)", i);
            logged_ga = true;
        }
        if (!il2cpp_names::g_map.empty()) return true;
        if (il2cpp_names::resolve()) return true;
        SYM_LOG("init", "resolve() failed on poll {}", i);
        Sleep(100);
    }
    SYM_LOG("init", "timed out waiting for il2cpp names (GameAssembly.dll {})",
            GetModuleHandleW(L"GameAssembly.dll") ? "present" : "absent");
    return false;
}

// Wait until the il2cpp domain has at least one assembly (il2cpp_init done).
bool wait_for_domain_ready() {
    SYM_LOG("init", "waiting for il2cpp domain...");
    for (int i = 0; i < 600; ++i) {
        size_t n = 0;
        il2cpp::Il2CppAssembly** asms = il2cpp::domain_get_assemblies(&n);
        if (asms && n > 0) return true;
        Sleep(100);
    }
    SYM_LOG("init", "timed out waiting for il2cpp domain");
    return false;
}

}  // namespace

bool init_all() {
    // Initialize logging at default level first so config::load() can log.
    log_init(1);
    if (!symphytum::config::load()) {
        SYM_LOG_ERROR("init", "config load failed");
        return false;
    }
    // Apply the configured log level.
    g_log_level = symphytum::config::g.log_level;
    SYM_LOG("init", "symphytum starting");

    if (!wait_for_il2cpp_ready()) {
        SYM_LOG("init", "il2cpp name resolution failed");
        return false;
    }
    SYM_LOG("init", "il2cpp names resolved");

    // Initialize the il2cpp bridge (resolves GameAssembly.dll + exports)
    // BEFORE we try to query the domain. wait_for_domain_ready() calls
    // il2cpp_domain_get_assemblies, which needs the bridge initialized.
    if (!il2cpp::init()) {
        SYM_LOG("init", "il2cpp bridge init failed");
        return false;
    }

    if (!wait_for_domain_ready()) {
        SYM_LOG("init", "il2cpp domain never populated");
        return false;
    }
    SYM_LOG("init", "il2cpp domain ready");

    // Attach this thread to il2cpp so our runtime_invoke calls are valid.
    if (il2cpp::Il2CppDomain* d = il2cpp::domain_get()) {
        il2cpp::thread_attach(d);
    }

    // Install patches if enabled
    if (symphytum::config::g.enable_patches) {
        if (symphytum::config::g.redirect_game_requests || symphytum::config::g.redirect_asset_requests) {
            symphytum::patches::install_request();
        }
        if (symphytum::config::g.disable_cert_pinning || symphytum::config::g.use_custom_root_cert) {
            symphytum::patches::install_ssl();
        }
        if (symphytum::config::g.disable_encryption) {
            symphytum::patches::install_crypto();
        }
    } else {
        SYM_LOG("init", "patches are globally disabled by config");
    }

    SYM_LOG("init", "all patches installed");
    return true;
}

}  // namespace symphytum
