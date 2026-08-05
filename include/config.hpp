// Config: reads symphytum.ini next to the DLL using Windows' GetPrivateProfileStringW.
//
// [symphytum]
// server = http://127.0.0.1:3000/
// asset_server = http://127.0.0.1:3000/
// log_level = 1  (0 = only error, 1 = error & info, 2 = error & info & debug)

#pragma once
#include <string>
#include <cstdint>

namespace symphytum::config {

struct Config {
    bool enable_patches = true;
    std::wstring game_server;     // redirect target for game-related urls
    bool redirect_game_requests = true;
    std::wstring asset_server;    // redirect target for asset-related urls
    bool redirect_asset_requests = true;
    bool disable_encryption = false;
    bool disable_cert_pinning = true;
    std::wstring custom_root_cert; // PEM string
    bool use_custom_root_cert = false;
    bool force_autoplay = false;     // force the in-live autoplay engine on
    bool fake_manual_result = true;  // remap Auto judges to PerfectPlus
    int log_level = 1;
};

extern Config g;

// Load from symphytum.ini next to this DLL. Returns false on failure.
// Reads all keys (server, asset_server, log_level).
bool load();

}  // namespace symphytum::config
