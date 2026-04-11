#include "connection.hpp"
#include "customlogger.hpp"
#include "threadpool.hpp"
#include "uniquefd.hpp"
#include <arpa/inet.h>
#include <array>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>

inline constexpr const char *PORT = "3490";
inline constexpr int BACKLOG = 10;

using AddrInfoPtr = std::unique_ptr<addrinfo, void (*)(addrinfo *)>;

// TODO: use C++ methods
static void *get_in_addr(struct sockaddr *sa) noexcept {
  if (sa->sa_family == AF_INET) {
    return &((reinterpret_cast<sockaddr_in *>(sa))->sin_addr);
  }

  return &((reinterpret_cast<sockaddr_in6 *>(sa))->sin6_addr);
}

static Socket setup_server() {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC; // Either IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  addrinfo *raw_servinfo = nullptr;
  if (int rv = getaddrinfo(nullptr, PORT, &hints, &raw_servinfo); rv != 0) {
    throw std::runtime_error(std::string("gai: ") + gai_strerror(rv));
  }
  AddrInfoPtr servinfo(raw_servinfo, freeaddrinfo);

  Socket server_sock{-1}; // RAII Socket
  addrinfo *p = servinfo.get();

  for (; p != nullptr; p = p->ai_next) {
    std::array<char, INET6_ADDRSTRLEN> ipstr{};
    inet_ntop(p->ai_family, get_in_addr(p->ai_addr), ipstr.data(),
              ipstr.size());
    LOG_INFO("binding to ", ipstr.data());

    int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == -1) {
      LOG_ERRNO("socket creation failed");
      continue;
    }
    server_sock.reset(s);

    const int yes = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
        -1) {
      throw std::system_error(errno, std::system_category(), "setsockopt");
    }

    if (bind(server_sock, p->ai_addr, p->ai_addrlen) == -1) {
      LOG_ERRNO("bind");
      server_sock.reset(-1);
      continue;
    }
    break;
  }

  if (p == nullptr) {
    throw std::runtime_error("failed to bind");
  }

  if (listen(server_sock, BACKLOG) == -1) {
    throw std::system_error(errno, std::system_category(), "listen");
  }
  return server_sock;
}

[[noreturn]]
static void server_loop(Socket server_fd, ThreadPool &thread_pool) {
  while (true) {
    sockaddr_storage their_addr{};
    socklen_t sin_size = sizeof their_addr;
    int new_fd =
        accept(server_fd, reinterpret_cast<sockaddr *>(&their_addr), &sin_size);

    if (new_fd == -1) {
      LOG_ERRNO("accept");
      continue;
    }

    // `ThreadPool` can't submit move-only functions (`Socket` is move-only), so
    // `shared_ptr` for `Socket` is required
    auto accept_sock = std::make_shared<Socket>(new_fd);

    // Get client's IP
    std::array<char, INET6_ADDRSTRLEN> ip{};
    inet_ntop(their_addr.ss_family,
              get_in_addr(reinterpret_cast<sockaddr *>(&their_addr)), ip.data(),
              ip.size());
    LOG_INFO("got connection from ", ip.data());

    thread_pool.submit([accept_sock, ip = std::string(ip.data())]() mutable {
      handle_client(std::move(*accept_sock), std::move(ip));
    });
  }
}

int main() {
  try {
    Socket server_fd = setup_server();
    ThreadPool thread_pool{std::thread::hardware_concurrency()};

    LOG_INFO("waiting connections...");

    server_loop(std::move(server_fd), thread_pool);
  } catch (const std::exception &e) {
    LOG_ERROR("Exception caught: ", e.what());
    return -1;
  }
}
