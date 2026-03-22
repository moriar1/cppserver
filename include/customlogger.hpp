#pragma once

#include <iostream>
#include <string_view>

namespace customlogger {

template <typename... Args>
void log_impl(std::string_view level, std::string_view func, int line,
              Args &&...args) {
  std::cerr << "[" << level << "] " << func << ":" << line << ": ";
  (std::cerr << ... << std::forward<Args>(args)) << '\n'; // fold expression
}

} // namespace customlogger

// Use macros instead of inline functions because of __FILE_NAME__ and __LINE__
#define LOG_INFO(...)                                                          \
  customlogger::log_impl("INFO", __FILE_NAME__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)                                                         \
  customlogger::log_impl("ERROR", __FILE_NAME__, __LINE__, __VA_ARGS__)
#define LOG_ERRNO(...)                                                         \
  do {                                                                         \
    int _saved_errno = errno;                                                  \
    customlogger::log_impl("ERROR", __FILE_NAME__, __LINE__, __VA_ARGS__,      \
                           ": ",                                               \
                           std::system_category().message(_saved_errno));      \
    errno = _saved_errno;                                                      \
  } while (0)

#define LOG_FATAL(...)                                                         \
  do {                                                                         \
    customlogger::log_impl("FATAL", __FILE_NAME__, __LINE__, __VA_ARGS__);     \
    std::abort();                                                              \
  } while (0)

// Debug only macro
#ifndef NDEBUG
#define LOG_DEBUG(...)                                                         \
  customlogger::log_impl("DEBUG", __FILE_NAME__, __LINE__, __VA_ARGS__)
#else
#define LOG_DEBUG(...) ((void)0)
#endif
