// il2cpp name resolver implementation.
#include "il2cpp_names.hpp"
#include "log.hpp"
#include "il2cpp_map.generated.h"
#include <string>
#include <unordered_map>

namespace symphytum::il2cpp_names {

std::unordered_map<std::string, std::string> g_map;

bool resolve() {
    g_map.clear();
    for (size_t i = 0; i < IL2CPP_MAP_COUNT; ++i) {
        g_map[IL2CPP_MAP[i].canonical] = IL2CPP_MAP[i].obfuscated;
    }
    SYM_LOG("scan", "loaded embedded il2cpp map ({} entries)", g_map.size());
    return true;
}

const std::string& lookup(const char* canonical) {
    static const std::string empty;
    auto it = g_map.find(canonical);
    return it == g_map.end() ? empty : it->second;
}

}  // namespace symphytum::il2cpp_names
