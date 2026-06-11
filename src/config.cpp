#include "config.hpp"
#include <charconv>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <type_traits>

static constexpr size_t DEFAULT_TIMEOUT = 10;
static constexpr size_t DEFAULT_QUEUE_SIZE = SIZE_MAX; // if change, edit --help
static constexpr uint16_t DEFAULT_PORT = 3490;
static constexpr int DEFAULT_BACKLOG = SOMAXCONN;
static const size_t DEFAULT_WORKERS = std::thread::hardware_concurrency()
                                          ? std::thread::hardware_concurrency()
                                          : 1;

template <typename T> static std::optional<T> to_int(std::string_view sv) {
  static_assert(std::is_integral_v<T>);
  if (sv.empty()) {
    return std::nullopt;
  }

  // to prevent `clang-tidy` warnings
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const char *last = sv.data() + sv.size();
  const char *first = sv.data();

  T val{};
  auto [ptr, ec] = std::from_chars(first, last, val);
  if (ec != std::errc() || ptr != last) {
    return std::nullopt;
  }
  return val;
}

static void print_usage_full(std::string_view program_name) {
  // clang-format off
  std::cout
      << "Usage: " << program_name
      << " [-p port] [-w workers] [-t timeout] [-q queue size] [-b backlog]\n\n"
      << "Options:\n"
      << "  -p, --port        Port to listen on (default: " << DEFAULT_PORT << ")\n"
      << "  -w, --workers     Number of worker threads (default: " << DEFAULT_WORKERS << ")\n"
      << "  -t, --timeout     Connection timeout in seconds (default: " << DEFAULT_TIMEOUT << ")\n"
      << "  -q, --queue-size  Max tasks in queue (default: unlimited)\n"
      << "  -b, --backlog     Backlog (default: " << DEFAULT_BACKLOG << ")\n";
  // clang-format on
}

// print full usage
static void print_error(std::string_view program_name, std::string_view err) {
  std::cerr
      << "err: " << err << "\n\n"
      << "Usage: " << program_name
      << " [-p port] [-w workers] [-t timeout] [-q queue size] [-b backlog]\n";
}

std::optional<Config> parse(const std::vector<std::string_view> &args) {
  uint16_t port = DEFAULT_PORT;
  size_t workers = DEFAULT_WORKERS;
  std::chrono::seconds timeout{DEFAULT_TIMEOUT};
  size_t queue_size = DEFAULT_QUEUE_SIZE;
  int backlog = DEFAULT_BACKLOG;

  for (size_t i = 1; i < args.size(); /*empty, see end of loop body*/) {
    if (args[i] == "--help" || args[i] == "-h") {
      print_usage_full(args[0]);
      return std::nullopt;
    }
    if (args[i] == "--version" || args[i] == "-v") {
      std::cout << PROJECT_VERSION << '\n';
      return std::nullopt;
    }

    // NOTE: below we always check if `args[i]` is valid argument and then check
    // if `args[i+1]` exists. If we remove `is_known_check` then when user
    // writes something like `cppserver -p 3490 -wwww` he will get `missing
    // value for parameter -wwww` instead of "Unknown parameter: -wwww"

    const bool is_known_option =
        (args[i] == "--port" || args[i] == "-p") ||
        (args[i] == "--workers" || args[i] == "-w") ||
        (args[i] == "--timeout" || args[i] == "-t") ||
        (args[i] == "--queue-size" || args[i] == "-q") ||
        (args[i] == "--backlog" || args[i] == "-b");
    if (!is_known_option) {
      print_error(args[0], "Unknown parameter: " + std::string(args[i]));
      return std::nullopt;
    }
    if (i + 1 >= args.size()) {
      print_error(args[0],
                  "missing value for parameter " + std::string(args[i]));
      return std::nullopt;
    }
    std::string_view val = args[i + 1];

    if (args[i] == "--port" || args[i] == "-p") {
      auto maybe_port = to_int<uint16_t>(val);
      if (!maybe_port || maybe_port == 0) {
        print_error(args[0], "invalid port");
        return std::nullopt;
      }
      port = maybe_port.value();
    } else if (args[i] == "--workers" || args[i] == "-w") {
      auto maybe_workers = to_int<size_t>(val);
      if (!maybe_workers || maybe_workers == 0) {
        print_error(args[0], "invalid workers");
        return std::nullopt;
      }
      workers = maybe_workers.value();
    } else if (args[i] == "--timeout" || args[i] == "-t") {
      auto maybe_timeout = to_int<int>(val);
      if (!maybe_timeout || maybe_timeout < 0) {
        print_error(args[0], "invalid timeout");
        return std::nullopt;
      }
      timeout = std::chrono::seconds(maybe_timeout.value());
    } else if (args[i] == "--queue-size" || args[i] == "-q") {
      auto maybe_queue_size = to_int<size_t>(val);
      if (!maybe_queue_size || maybe_queue_size == 0) {
        print_error(args[0], "invalid queue size");
        return std::nullopt;
      }
      queue_size = maybe_queue_size.value();
    } else if (args[i] == "--backlog" || args[i] == "-b") {
      auto maybe_backlog = to_int<int>(val);
      if (!maybe_backlog || maybe_backlog <= 0 || maybe_backlog > SOMAXCONN) {
        print_error(args[0],
                    "invalid backlog, maximum: " + std::to_string(SOMAXCONN));
        return std::nullopt;
      }
      backlog = maybe_backlog.value();
    }
    i += 2;
  }
  return Config{port, workers, timeout, queue_size, backlog};
}
