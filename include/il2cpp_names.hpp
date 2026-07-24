// il2cpp name resolver.
//
// Uses the embedded generated table directly (from generated/il2cpp_map.generated.h).

#pragma once
#include <string>
#include <unordered_map>

namespace symphytum::il2cpp_names {

// canonical il2cpp name -> obfuscated GameAssembly.dll export name.
// as of right now this game doesnt obfuscate them so we can just mirror.
extern std::unordered_map<std::string, std::string> g_map;

// Resolve the map at runtime. Returns true on success.
bool resolve();

// Lookup; returns "" if not present.
const std::string& lookup(const char* canonical);

}  // namespace symphytum::il2cpp_names
