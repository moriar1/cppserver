#include "connection.hpp"
#include "socket.hpp"
#include <array>
#include <string>
#include <sys/socket.h>

static constexpr size_t MAXDATASIZE = 4096;

void handle_client(Socket sock, std::string ip) {
  long numbytes = 0;
  std::array<char, MAXDATASIZE> buf{};
  while (true) {
    numbytes = recv(sock, buf.data(), buf.size(), 0);
    if (numbytes == -1) {
      LOG_ERRNO("recv");
      break;
    }
    if (numbytes == 0) {
      LOG_INFO(ip, " disconnected");
      break;
    }
    if (send(sock, buf.data(), static_cast<size_t>(numbytes), 0) == -1) {
      LOG_ERRNO("send");
    } else {
      LOG_INFO("echoed to ", ip);
    }
  }
  // TODO: handle_http_request(sock, buf); or write HttpHandler class
}
