// Debug logging via console + file + OutputDebugStringW. No deps.
//
// Log levels (controlled by log_level in symphytum.ini):
//   0 = quiet  — only SYM_LOG_ERROR
//   1 = info   — SYM_LOG + SYM_LOG_ERROR (default)
//   2 = debug  — SYM_LOG_DEBUG + SYM_LOG + SYM_LOG_ERROR
//
// C++23: the public API is a type-safe std::format-based variadic template.
// This removes the old snprintf variadic forwarder and its -Wformat-security
// warning entirely — arguments are checked at compile time by std::format.
#pragma once
#include <string>
#include <format>

namespace symphytum {

void log_init(int level = 1, const wchar_t* log_name = L"symphytum.log");
void log_raw(const char* msg);
void log_raw(const wchar_t* msg);

// Current log level (set by log_init). 0=quiet, 1=info, 2=debug.
extern int g_log_level;

// Type-safe formatted log. Tag is a short literal ("scan", "guard", ...).
// Usage:  SYM_LOG("scan", "resolved {} names", count);
template <typename... Args>
inline void log_info(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    if (g_log_level < 1) return;
    std::string msg = std::format("[Symphytum:{}] ", tag);
    msg += std::format(fmt, std::forward<Args>(args)...);
    log_raw(msg.c_str());
}

// Debug-level log. Only emitted at log_level >= 2.
template <typename... Args>
inline void log_debug(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    if (g_log_level < 2) return;
    std::string msg = std::format("[Symphytum:{}] ", tag);
    msg += std::format(fmt, std::forward<Args>(args)...);
    log_raw(msg.c_str());
}
// Error-level log. Always emitted regardless of level.
template <typename... Args>
inline void log_error(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format("[Symphytum:{}] ", tag);
    msg += std::format(fmt, std::forward<Args>(args)...);
    log_raw(msg.c_str());
}

// Override-level log. Always emitted regardless of level — use for important
// one-shot messages like config summaries and dump start/finish that should
// appear even at log_level=0.
template <typename... Args>
inline void log_override(std::string_view tag, std::format_string<Args...> fmt, Args&&... args) {
    std::string msg = std::format("[Symphytum:{}] ", tag);
    msg += std::format(fmt, std::forward<Args>(args)...);
    log_raw(msg.c_str());
}

}  // namespace symphytum

// Macros keep call sites terse. `tag` is a string literal so std::format_string's
// compile-time format check fires at every SYM_LOG site.
#define SYM_LOG(tag, ...)       ::symphytum::log_info(tag, __VA_ARGS__)
#define SYM_LOG_DEBUG(tag, ...) ::symphytum::log_debug(tag, __VA_ARGS__)
#define SYM_LOG_ERROR(tag, ...)    ::symphytum::log_error(tag, __VA_ARGS__)
#define SYM_LOG_OVERRIDE(tag, ...) ::symphytum::log_override(tag, __VA_ARGS__)
