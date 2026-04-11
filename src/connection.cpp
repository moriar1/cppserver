#include "connection.hpp"
#include "customlogger.hpp"
#include "uniquefd.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>

using HttpStatus = unsigned;

static constexpr size_t MAXDATASIZE = 4096;
static constexpr time_t TIMEOUT = 10;

static HttpStatus handle_http_request(Socket &sock, std::string_view request);

void handle_client(Socket sock, std::string ip) {
  try {
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
    std::array<char, MAXDATASIZE> buf;
    while (true) {
      size_t spaceleft = buf.size() - total_nbytes;
      nbytes = sock.recv(&buf[total_nbytes], spaceleft, 0);

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
        LOG_INFO(ip, " failed get request headers: client disconnected");
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
    std::string_view request(buf.data(), total_nbytes);

    // Send requested file
    HttpStatus s = handle_http_request(sock, request);
    if (s == 0) {
      LOG_INFO("client ", ip,
               " error occured in `send()` or `sendfile()` call");
    } else {
      LOG_INFO("client ", ip, " status: ", s, ", closing connection...");
    }
  } catch (const std::exception &e) {
    LOG_INFO("client ", ip, " unexpected error: ", e.what());
  }
}

HttpStatus handle_http_request(Socket &sock, std::string_view request) {
  if (request.find("Host:") == std::string::npos) {
    // send_400(); // Bad request
    return 400;
  }

  std::string path = "index.html"; // TODO: parse from request

  // Prepare file
  // NOTE: may add generating HTML document if directory is requested
  UniqueFd content_fd{open(path.c_str(), O_RDONLY)};
  if (content_fd == -1) {
    LOG_ERRNO("open");
    if (errno == ENOENT) {
      // send_404(sock); // TODO
      return 404;
    }
    // send_500(sock); // TODO
    return 500;
  }
  // Get file size
  struct stat st{};
  if (fstat(content_fd, &st) == -1) {
    LOG_ERRNO("stat");
    // send_500(sock); // TODO
    return 500;
  }
  auto content_length = st.st_size;

  // Send headers
  // const char *mime = get_mime_type(path, path.length());
  const std::string mime = "text/html"; // TODO: detect by file extension
  std::stringstream sstr;
  sstr << "HTTP/1.1 200 OK\r\nContent-Length: " << content_length
       << "\r\nContent-Type: " << mime << "\r\n\r\n";
  if (sock.send_all(sstr.str()) == -1) {
    LOG_ERRNO("send");
    return 0;
  }

#ifdef __linux__
  ssize_t total_sent = 0;
  ssize_t sent = 1;

  while (total_sent < content_length) {
    sent = sock.sendfile(content_fd, nullptr, content_length - total_sent);
    if (sent == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        LOG_INFO("client timeout (couldn't sendfile)");
        return 0; // sendfile error, but we sent `200 OK` in headers
      }
      LOG_ERRNO("sendfile");
      return 0;
    }

    if (sent == 0) {
      break;
    }
    total_sent += sent;
  }
#elif defined __FreeBSD__
  // NOTE: in FreeBSD loop is required only for non-block I/O
  if (sock.sendfile(content_fd, 0, static_cast<size_t>(content_length), nullptr,
                    nullptr, 0) == -1) {
    if (errno == EAGAIN) {
      LOG_INFO("client timeout (couldn't sendfile)");
    } else {
      LOG_ERRNO("sendfile");
    }
    return 0; // sendfile error, but we sent `200 OK` in headers
  }
#endif
  return 200;
}
