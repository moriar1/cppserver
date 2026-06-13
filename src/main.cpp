#include "config.hpp"
#include "customlogger.hpp"
#include "httphandler.hpp"
#include "socket.hpp"
#include "threadpool.hpp"
#include <arpa/inet.h>
#include <array>
#include <csignal>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>

using AddrInfoPtr = std::unique_ptr<addrinfo, void (*)(addrinfo *)>;

struct Pipe {
  UniqueFd read;
  UniqueFd write;

  static Pipe create() {
    int fds[2]; // NOLINT
    if (pipe(&fds[0]) < 0) {
      throw std::system_error(errno, std::system_category(), "pipe");
    }
    return {UniqueFd(fds[0]), UniqueFd(fds[1])};
  }
};

// Access static pipe only using this function
static Pipe &get_pipe() {
  static Pipe instance = Pipe::create();
  return instance;
}

static void fatalsig(int __attribute__((unused)) signum) {
  write(get_pipe().write.get(), "f", 1); // interrupt `select()` waiting
}

static std::array<char, INET6_ADDRSTRLEN> get_ip(const sockaddr *sa) {
  const void *addr_ptr = nullptr;
  if (sa->sa_family == AF_INET) {
    addr_ptr = &reinterpret_cast<const sockaddr_in *>(sa)->sin_addr;
  } else if (sa->sa_family == AF_INET6) {
    addr_ptr = &reinterpret_cast<const sockaddr_in6 *>(sa)->sin6_addr;
  } else {
    throw std::runtime_error("unsupported sa_family"); // unlikely
  }

  std::array<char, INET6_ADDRSTRLEN> ipstr{};
  inet_ntop(sa->sa_family, addr_ptr, ipstr.data(), ipstr.size());
  return ipstr;
}

static std::array<char, INET6_ADDRSTRLEN> get_ip(const addrinfo *ai) {
  return get_ip(ai->ai_addr);
}

static std::array<char, INET6_ADDRSTRLEN> get_ip(const sockaddr_storage &ss) {
  return get_ip(reinterpret_cast<const sockaddr *>(&ss));
}

static Socket setup_server(const Config &cfg) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo *raw_servinfo = nullptr;
  if (int rv = getaddrinfo(nullptr, std::to_string(cfg.port).data(), &hints,
                           &raw_servinfo);
      rv != 0) {
    throw std::runtime_error(std::string("gai: ") + gai_strerror(rv));
  }
  AddrInfoPtr servinfo(raw_servinfo, freeaddrinfo);

  Socket server_sock{-1}; // RAII Socket
  addrinfo *p = servinfo.get();

  for (; p != nullptr; p = p->ai_next) {
    std::array<char, INET6_ADDRSTRLEN> ipstr = get_ip(p);
    LOG_INFO("binding to ", ipstr.data());

    int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == -1) {
      LOG_ERRNO("socket creation failed");
      continue;
    }
    server_sock.reset(s);

    const int yes = 1;
    if (server_sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
        -1) {
      throw std::system_error(errno, std::system_category(), "setsockopt");
    }

    if (server_sock.bind(p->ai_addr, p->ai_addrlen) == -1) {
      LOG_ERRNO("bind");
      server_sock.reset(-1);
      continue;
    }
    break;
  }

  if (p == nullptr) {
    throw std::runtime_error("failed to bind");
  }

  if (server_sock.listen(cfg.backlog) == -1) {
    throw std::system_error(errno, std::system_category(), "listen");
  }
  return server_sock;
}

static void server_loop(Socket server_sock, ThreadPool &thread_pool,
                        std::chrono::seconds timeout) {
  while (true) {
    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(get_pipe().read.get(), &readset);
    FD_SET(server_sock.get(), &readset);

    int max_fd = 1 + std::max(get_pipe().read.get(), server_sock.get());
    if (select(max_fd, &readset, nullptr, nullptr, nullptr) == -1) {
      if (errno == EINTR) {
        continue; // interrupted by signal
      }
      LOG_ERRNO("select");
      break;
    }

    if (FD_ISSET(get_pipe().read.get(), &readset)) {
      char dummy{};
      read(get_pipe().read.get(), &dummy, 1);
      LOG_INFO("shutdown singal recvieved");
      break;
    }

    if (FD_ISSET(server_sock.get(), &readset)) {
      sockaddr_storage their_addr{};
      socklen_t sin_size = sizeof their_addr;
      int new_fd = server_sock.accept(reinterpret_cast<sockaddr *>(&their_addr),
                                      &sin_size);

      if (new_fd == -1) {
        LOG_ERRNO("accept");
        continue;
      }

      // `ThreadPool` can't submit move-only functions (`Socket` is move-only),
      // so `shared_ptr` for `Socket` is required
      auto accept_sock = std::make_shared<Socket>(new_fd);

      std::array<char, INET6_ADDRSTRLEN> ip = get_ip(their_addr);
      LOG_INFO(ip.data(), " got connection");

      thread_pool.submit([=, ip = std::string(ip.data())]() mutable {
        http::handle_client(std::move(*accept_sock), std::move(ip), timeout);
      });
    }
  }
}

int main(int argc, char *argv[]) {
  try {
    std::vector<std::string_view> args(argv, argv + argc); // NOLINT
    auto config = parse(args);
    if (!config) {
      return 0;
    }
    const Config &cfg = config.value();

    Socket server_fd = setup_server(cfg);
    ThreadPool thread_pool{cfg.workers, cfg.queue_size};
    [[maybe_unused]] auto &p = get_pipe(); // static pipe init for signals

    // Set signal handlers
    struct sigaction action{};
    action.sa_handler = fatalsig;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    // Ignore SIGPIPE
    action.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &action, nullptr);

    LOG_INFO("waiting connections on port ", cfg.port, "...");
    server_loop(std::move(server_fd), thread_pool, cfg.timeout);
  } catch (const std::exception &e) {
    LOG_ERROR("Exception caught: ", e.what());
    return -1;
  }
}
