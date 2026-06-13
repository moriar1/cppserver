#pragma once

#include <array>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string_view>

namespace customlogger {

inline std::mutex glob_log_mut; // locks before writing in `cerr`

inline void print_time() {
  // Use fast C API instead of C++ with locales etc.
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  tm tm_buf{};
  localtime_r(&ts.tv_sec, &tm_buf);

  std::array<char, 16> str{};
  if (std::strftime(str.data(), sizeof(str), "[%H:%M:%S.", &tm_buf) != 0U) {
    auto ms = static_cast<unsigned>(ts.tv_nsec / 1'000'000);

    // add miliseconds
    str[10] = static_cast<char>('0' + (ms / 100));
    str[11] = static_cast<char>('0' + ((ms / 10) % 10));
    str[12] = static_cast<char>('0' + (ms % 10));

    str[13] = ']';
    str[14] = ' ';
    str[15] = 0;

    std::cerr << str.data();
  }
}

template <typename... Args>
void log_impl(std::string_view level, std::string_view file, int line,
              const Args &...args) {
  std::lock_guard lk(glob_log_mut);

  print_time();
  std::cerr << "[" << level << "] " << file << ":" << line << ": ";
  (std::cerr << ... << args) << '\n'; // fold expression
}

template <typename... Args>
void log_release_impl(std::string_view level, const Args &...args) {
  std::lock_guard lk(glob_log_mut);

  print_time();
  std::cerr << "[" << level << "] ";
  (std::cerr << ... << args) << '\n';
}

template <typename... Args>
constexpr void unused_helper(const Args &.../*unused*/) noexcept {}

} // namespace customlogger

// Use macros instead of inline functions because of __FILE_NAME__ and __LINE__

// clang-format off
#ifdef NO_LOGS

// We cant just leave these macros as `((void)0)` because otherwise
// we will get warnings: `-Wunused-variable` `-Wunused-exception-parameter` etc.
// Anyway these macros do not overhead cpu when `-O3` used

#define LOG_DEBUG(...) \
  do { if (false) { customlogger::unused_helper(__VA_ARGS__); } } while (0)
#define LOG_INFO(...) \
  do { if (false) { customlogger::unused_helper(__VA_ARGS__); } } while (0)
#define LOG_ERROR(...) \
  do { if (false) { customlogger::unused_helper(__VA_ARGS__); } } while (0)
#define LOG_ERRNO(...) \
  do { if (false) { customlogger::unused_helper(__VA_ARGS__); } } while (0)
#define LOG_FATAL(...) \
  do { if (false) { customlogger::unused_helper(__VA_ARGS__); } } while (0)
// clang-format on

// Debug macros
#elif !defined NDEBUG
#define LOG_DEBUG(...)                                                         \
  customlogger::log_impl("DEBUG", __FILE_NAME__, __LINE__, __VA_ARGS__)
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

#else // Release macros

#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...) customlogger::log_release_impl("INFO", __VA_ARGS__)
#define LOG_ERROR(...) customlogger::log_release_impl("ERROR", __VA_ARGS__)
#define LOG_ERRNO(...)                                                         \
  do {                                                                         \
    int _saved_errno = errno;                                                  \
    customlogger::log_release_impl(                                            \
        "ERROR", __VA_ARGS__, ": ",                                            \
        std::system_category().message(_saved_errno));                         \
    errno = _saved_errno;                                                      \
  } while (0)
#define LOG_FATAL(...)                                                         \
  do {                                                                         \
    customlogger::log_release_impl("FATAL", __VA_ARGS__);                      \
    std::abort();                                                              \
  } while (0)

#endif
