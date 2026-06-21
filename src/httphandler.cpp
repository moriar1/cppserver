#include "customlogger.hpp"
#include "httphandler.hpp"
#include "socket.hpp"
#include "uniquefd.hpp"
#include <array>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>

#ifdef __FreeBSD__
#include <sys/syslimits.h>
#elif defined __linux__
#include <limits.h>
#endif

namespace http {

namespace fs = std::filesystem;

using HttpStatus = unsigned;

static std::optional<HttpStatus> handle_http_request(const Socket &sock,
                                                     std::string_view request);
static std::optional<HttpStatus>
send_file_response(const Socket &sock, const fs::path &path, bool is_send_body);

[[nodiscard]] static inline std::optional<HttpStatus>
send_response(const Socket &sock, HttpStatus status, std::string_view msg) {
  if (sock.send_all(msg) == -1) {
    LOG_ERRNO("failed send response fot status ", status);
    return std::nullopt;
  }
  return status;
}

// clang-format off
// Both simple and fast functions for sending HTTP status
[[nodiscard]] static std::optional<HttpStatus> send_400(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n400";
  return send_response(sock,  400,  msg);
}
[[nodiscard]] static std::optional<HttpStatus> send_403(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n403";
  return send_response(sock,  403,  msg);
}
[[nodiscard]] static std::optional<HttpStatus> send_404(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n404";
  return send_response(sock,  404,  msg);
}
[[nodiscard]] static std::optional<HttpStatus> send_405(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n405";
  return send_response(sock,  405,  msg);
}
[[nodiscard]] static std::optional<HttpStatus> send_431(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n431";
  return send_response(sock,  431,  msg);
}
[[nodiscard]] static std::optional<HttpStatus> send_500(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n500";
  return send_response(sock,  500,  msg);
}
// clang-format on

std::optional<std::string> read_request_headers(const Socket &sock,
                                                std::string_view ip) {
  size_t total_nbytes = 0;
  std::array<char, MAXDATASIZE> buf{};
  while (true) {
    size_t spaceleft = buf.size() - total_nbytes;
    ssize_t nbytes = sock.recv(&buf[total_nbytes], spaceleft, 0);

    if (nbytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN) {
        LOG_INFO(ip, " timeout");
      } else {
        LOG_ERRNO(ip, "recv");
      }
      return std::nullopt;
    }

    if (nbytes == 0) {
      LOG_INFO(ip, " failed get request headers: client disconnected");
      return std::nullopt;
    }
    // cast `ssize_t` to `size_t` to prevent `-Wsign-conversion` further
    auto bytes_read = static_cast<size_t>(nbytes);

    total_nbytes += bytes_read;

    // Too long headers => 431
    if (total_nbytes >= buf.size()) {
      (void)http::send_431(sock);
      return std::nullopt;
    }

    // Find `\r\n\r\n` in new data (including 3 symbols in previous data)
    size_t pos =
        (total_nbytes - bytes_read >= 3) ? total_nbytes - bytes_read - 3 : 0;
    size_t sz = total_nbytes - pos;

    std::string_view slice(&buf[pos], sz);
    if (slice.find("\r\n\r\n") !=
        std::string_view::npos) { // found end of headers
      break;
    }
  }
  return std::string(buf.data(), total_nbytes);
}

void handle_client(Socket sock, std::string ip, std::chrono::seconds timeout) {
  try {
    // Set timeout for connection
    const struct timeval time = {timeout.count(), 0};
    if (sock.setsockopt(SOL_SOCKET, SO_RCVTIMEO, &time, sizeof(time)) != 0) {
      LOG_ERRNO(ip, "failed set rcv timout");
    }
    if (sock.setsockopt(SOL_SOCKET, SO_SNDTIMEO, &time, sizeof(time)) != 0) {
      LOG_ERRNO(ip, "failed set snd timout");
    }

    // Recieve request (HTTP headers)
    auto request = read_request_headers(sock, ip);
    if (!request) {
      return;
    }

    // Send requested file
    auto s = handle_http_request(sock, request.value());
    if (!s) {
      LOG_INFO(ip, " error occured in `send()` or `sendfile()` call");
    } else {
      LOG_INFO(ip, " status: ", s.value());
    }
  } catch (const std::exception &e) {
    LOG_INFO(ip, " unexpected error: ", e.what());
  }
}

static std::string_view get_mime_type(const fs::path &path) {
  std::string ext = path.extension().string();

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

bool is_path_safe(const fs::path &path) {
  if (path.native().length() > PATH_MAX - 1) {
    LOG_DEBUG("too long path");
    return false;
  }

  std::error_code ec;

  auto canon_path = fs::weakly_canonical(path, ec);
  if (ec) {
    return false;
  }

  auto root_path = fs::current_path(ec);
  if (ec) {
    return false;
  }

  auto relative = canon_path.lexically_relative(root_path);

  // if there is `../` in the beggining of path (`..` in iterator) => false
  return !(*relative.begin() == "..");
}

static std::optional<char> hex_to_char(std::string_view s) {
  if (s.size() < 2) {
    return std::nullopt;
  }

  // this array is not `constexpr` because of `fill()` but it still lazy static
  static const std::array<int, 256> table = []() {
    std::array<int, 256> arr{};
    std::fill(arr.begin(), arr.end(), -1);

    for (size_t i = 0; i < 10; i++) {
      arr['0' + i] = static_cast<int>(i);
    }
    for (size_t i = 0; i < 6; i++) {
      arr['a' + i] = static_cast<int>(10 + i);
      arr['A' + i] = static_cast<int>(10 + i);
    }

    return arr;
  }();

  auto first = table[static_cast<unsigned char>(s[0])];
  auto second = table[static_cast<unsigned char>(s[1])];

  if (first == -1 || second == -1) {
    return std::nullopt;
  }

  return static_cast<char>((first * 16) + second);
}

static std::string url_decode(std::string_view s) {
  size_t idx = s.find('%');
  if (idx == std::string_view::npos) {
    return std::string(s);
  }

  std::string decoded;
  decoded.reserve(s.size());

  while (idx != std::string_view::npos) {
    if (idx + 2 >= s.size()) { // check two characters after found `%`
      break;
    }
    decoded.append(s.substr(0, idx));
    std::string_view maybe_hex = s.substr(idx + 1, 2);

    if (auto ch_hexed = hex_to_char(maybe_hex)) {
      decoded += ch_hexed.value(); // add valid character
      s.remove_prefix(idx + 3);
    } else {
      decoded.push_back('%'); // add `%` as is
      s.remove_prefix(idx + 1);
    }

    idx = s.find('%');
  }
  decoded += s;
  return decoded;
}

std::optional<HttpStatus> handle_http_request(const Socket &sock,
                                              std::string_view request) {
  if (request.length() < 10) { // prevent `out_of_range` before using substr
    return http::send_400(sock);
  }

  // Handling only `GET` and `HEAD` requests
  bool is_get = false;
  if (request.substr(0, 5) == "GET /") {
    is_get = true;
  } else if (request.substr(0, 6) != "HEAD /") {
    return http::send_405(sock);
  }

  // Remove `GET /` from `request`
  request.remove_prefix(is_get ? 5 : 6);

  // Find position where file path ends and extract file path itself
  auto pos = request.find_first_of(" ?"); // `?` - to skip http query
  std::string path_str = (pos == 0 || pos == std::string::npos)
                             ? "./index.html"
                             : "./" + url_decode(request.substr(0, pos));
  fs::path path{path_str};

  // Prevent Path Traversal (no `../../` in path)
  if (!is_path_safe(path)) {
    return http::send_403(sock);
  }

  return send_file_response(sock, path, is_get);
}

std::optional<HttpStatus> send_file_response(const Socket &sock,
                                             const fs::path &content_path,
                                             bool is_send_body) {
  // Prepare file
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
    return std::nullopt;
  }
  if (!is_send_body) { // No body sending for HEAD request
    return 200;
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
        return std::nullopt; // sendfile error, but we sent `200 OK` in headers
      }
      LOG_ERRNO("sendfile");
      return std::nullopt;
    }

    if (sent == 0) {
      break;
    }
    total_sent += static_cast<size_t>(sent);
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
    return std::nullopt; // sendfile error, but we sent `200 OK` in headers
  }
#endif
  return 200;
}

} // namespace http
