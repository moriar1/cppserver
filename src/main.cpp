#include "customlogger.hpp"
#include "httphandler.hpp"
#include "socket.hpp"
#include "threadpool.hpp"
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

  if (server_sock.listen(BACKLOG) == -1) {
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
        server_fd.accept(reinterpret_cast<sockaddr *>(&their_addr), &sin_size);

    if (new_fd == -1) {
      LOG_ERRNO("accept");
      continue;
    }

    // `ThreadPool` can't submit move-only functions (`Socket` is move-only), so
    // `shared_ptr` for `Socket` is required
    auto accept_sock = std::make_shared<Socket>(new_fd);

    std::array<char, INET6_ADDRSTRLEN> ip = get_ip(their_addr);
    LOG_INFO("got connection from ", ip.data());

    thread_pool.submit([accept_sock, ip = std::string(ip.data())]() mutable {
      http::handle_client(std::move(*accept_sock), std::move(ip));
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
