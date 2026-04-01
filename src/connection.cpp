#include "connection.hpp"
#include "customlogger.hpp"
#include "socket.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <sys/socket.h>

static constexpr size_t MAXDATASIZE = 4096;
static constexpr time_t TIMEOUT = 10;

void handle_client(Socket sock, std::string ip) {
  // Set timeout for connection
  const struct timeval time = {TIMEOUT, 0};
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time)) != 0) {
    LOG_ERRNO("failed set rcv timout");
  }
  if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time)) != 0) {
    LOG_ERRNO("failed set snd timout");
  }

  // Recieve requests (HTTP headers)
  ssize_t nbytes = 0;
  size_t total_nbytes = 0;
  std::array<char, MAXDATASIZE> buf{};
  while (true) {
    size_t spaceleft = buf.size() - total_nbytes;
    nbytes = recv(sock, &buf[total_nbytes], spaceleft, 0);

    if (nbytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        LOG_INFO(ip, " timeout");
      } else {
        LOG_ERRNO("recv");
      }
      return;
    }

    if (nbytes == 0) {
      LOG_INFO(ip, " disconnected");
      break;
    }

    total_nbytes += static_cast<size_t>(nbytes);
    if (std::string_view(buf.data(), total_nbytes).find("\r\n\r\n") !=
        std::string::npos) { // found end of headers
      break;
    }

    // Too long headers => 431
    if (total_nbytes >= buf.size()) {
      // TODO: send_431(sock);
      break;
    }
  }
  std::cout << buf.data();
}
