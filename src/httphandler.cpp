#include "customlogger.hpp"
#include "httphandler.hpp"
#include "socket.hpp"
#include "uniquefd.hpp"
#include <array>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>

namespace http {

using HttpStatus = unsigned;
static constexpr size_t MAXDATASIZE = 4096;
static constexpr time_t TIMEOUT = 10;

static HttpStatus handle_http_request(const Socket &sock,
                                      std::string_view request);
static HttpStatus send_file_response(const Socket &sock,
                                     const std::string &path);

[[nodiscard]] static inline HttpStatus
send_response(const Socket &sock, HttpStatus status, std::string_view msg) {
  if (sock.send_all(msg) == -1) {
    LOG_ERRNO("failed send response fot status ", status);
    return 0;
  }
  return status;
}

// clang-format off
// Both simple and fast functions for sending HTTP status
[[nodiscard]] static inline HttpStatus send_403(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n403";
  return send_response(sock,  403,  msg);
}
[[nodiscard]] static inline HttpStatus send_404(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n404";
  return send_response(sock,  404,  msg);
}
[[nodiscard]] static inline HttpStatus send_405(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n405";
  return send_response(sock,  405,  msg);
}
[[nodiscard]] static inline HttpStatus send_431(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n431";
  return send_response(sock,  431,  msg);
}
[[nodiscard]] static inline HttpStatus send_500(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n500";
  return send_response(sock,  500,  msg);
}
// clang-format on

void handle_client(Socket sock, std::string ip) {
  try {
    // Set timeout for connection
    const struct timeval time = {TIMEOUT, 0};
    if (sock.setsockopt(SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time)) != 0) {
      LOG_ERRNO("failed set rcv timout");
    }
    if (sock.setsockopt(SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time)) != 0) {
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
        return;
      }

      total_nbytes += static_cast<size_t>(nbytes);
      if (std::string_view(buf.data(), total_nbytes).find("\r\n\r\n") !=
          std::string::npos) { // found end of headers
        break;
      }

      // Too long headers => 431
      if (total_nbytes >= buf.size()) {
        (void)http::send_431(sock);
        return;
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

static std::string_view get_mime_type(std::string_view path) {
  size_t dpos = path.find_last_of('.');
  if (dpos == std::string_view::npos) {
    return "application/octet-stream";
  }
  std::string_view ext = path.substr(dpos);

  if (ext == ".html" || ext == ".htm") {
    return "text/html";
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return "image/jpeg";
  }
  if (ext == ".js") {
    return "application/javascript";
  }
  if (ext == ".css") {
    return "text/css";
  }
  return "application/octet-stream";
}

HttpStatus handle_http_request(const Socket &sock, std::string_view request) {
  if (request.substr(0, 5) != "GET /") {
    return http::send_405(sock);
  }
  // Remove `GET /` from `request`
  request.remove_prefix(5);

  // Find position where file path ends and extract file path itself
  // TODO: add HTTP encoding
  auto pos = request.find(' ');
  std::string path = (pos == 0 || pos == std::string::npos)
                         ? "index.html"
                         : std::string(request.substr(0, pos));

  // Prevent Path Traversal (no `../../` in path)
  if (std::filesystem::weakly_canonical(path).string().find(
          std::filesystem::current_path().string() +
          std::filesystem::path::preferred_separator) != 0) {
    return http::send_403(sock);
  }

  return send_file_response(sock, path);
}

HttpStatus send_file_response(const Socket &sock,
                              const std::string &content_path) {
  // Prepare file
  // NOTE: may add generating HTML document if directory is requested
  UniqueFd content_fd{open(content_path.c_str(), O_RDONLY)};
  if (content_fd.get() == -1) {
    if (errno == ENOENT) {
      return http::send_404(sock);
    }
    LOG_ERRNO("open");
    return http::send_500(sock);
  }
  // Get file size
  struct stat st{};
  if (fstat(content_fd.get(), &st) == -1) {
    LOG_ERRNO("stat");
    return http::send_500(sock);
  }
  auto content_length = static_cast<size_t>(st.st_size);

  // Send headers
  std::stringstream sstr;
  sstr << "HTTP/1.1 200 OK\r\nContent-Type: " << get_mime_type(content_path)
       << "\r\nContent-Length: " << content_length
       << "\r\nConnection: close\r\n\r\n";
  if (sock.send_all(sstr.str()) == -1) {
    LOG_ERRNO("send");
    return 0;
  }

#ifdef __linux__
  size_t total_sent = 0;
  ssize_t sent = 1;

  while (total_sent < content_length) {
    sent =
        sock.sendfile(content_fd.get(), nullptr, content_length - total_sent);
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
  if (sock.sendfile(content_fd.get(), 0, content_length, nullptr, nullptr, 0) ==
      -1) {
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

} // namespace http
