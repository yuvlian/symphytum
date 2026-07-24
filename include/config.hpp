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
    std::wstring server;          // redirect target
    std::wstring asset_server;    // redirect target for asset-related urls
    int log_level = 1;
};

extern Config g;

// Load from symphytum.ini next to this DLL. Returns false on failure.
// Reads all keys (server, asset_server, log_level).
bool load();

}  // namespace symphytum::config
