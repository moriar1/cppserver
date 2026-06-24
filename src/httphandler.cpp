#include "customlogger.hpp"
#include "httphandler.hpp"
#include "httputils.hpp"
#include "socket.hpp"
#include "uniquefd.hpp"
#include <array>
#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>

namespace http {

namespace fs = std::filesystem;

static std::optional<HttpStatus> handle_http_request(const Socket &sock,
                                                     std::string_view request);
static std::optional<HttpStatus>
send_file_response(const Socket &sock, const fs::path &content_path,
                   bool is_send_body);
static std::optional<HttpStatus> send_dir_response(const Socket &sock,
                                                   const fs::path &web_path,
                                                   bool is_send_body);

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
      (void)send_431(sock);
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

std::optional<HttpStatus> handle_http_request(const Socket &sock,
                                              std::string_view request) {
  if (request.length() < 10) { // prevent `out_of_range` before using substr
    return send_400(sock);
  }

  // Handling only `GET` and `HEAD` requests
  bool is_get = false;
  if (request.substr(0, 5) == "GET /") {
    is_get = true;
  } else if (request.substr(0, 6) != "HEAD /") {
    return send_405(sock);
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
    return send_403(sock);
  }

  std::error_code ec;
  bool is_dir = fs::is_directory(path, ec);
  if (ec) {
    if (ec == std::errc::no_such_file_or_directory) {
      return send_404(sock);
    }
    return send_500(sock);
  }

  return is_dir ? send_dir_response(sock, path, is_get)
                : send_file_response(sock, path, is_get);
}

std::optional<HttpStatus> send_file_response(const Socket &sock,
                                             const fs::path &content_path,
                                             bool is_send_body) {
  // Prepare file
  UniqueFd content_fd{open(content_path.c_str(), O_RDONLY)};
  if (content_fd.get() == -1) {
    if (errno == ENOENT) {
      return send_404(sock);
    }
    LOG_ERRNO("open");
    return send_500(sock);
  }
  // Get file size
  struct stat st{};
  if (fstat(content_fd.get(), &st) == -1) {
    LOG_ERRNO("stat");
    return send_500(sock);
  }
  auto content_length = static_cast<size_t>(st.st_size);

  // Send headers
  std::string headers;
  headers.reserve(128);
  headers += "HTTP/1.1 200 OK\r\nContent-Type: ";
  headers += get_mime_type(content_path);
  headers += "\r\nContent-Length: ";
  headers += std::to_string(content_length);
  headers += "\r\nConnection: close\r\n\r\n";

  if (sock.send_all(headers) == -1) {
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

// Sends HTML with entries in requested directory
std::optional<HttpStatus> send_dir_response(const Socket &sock,
                                            const fs::path &web_path,
                                            bool is_send_body) {
  std::error_code ec;
  auto it = fs::directory_iterator(web_path, ec);
  if (ec) {
    LOG_ERROR("directory_iterator: ", ec.message());
    return http::send_500(sock);
  }

  std::string body = generate_dir_html(it, web_path);

  // Send headers
  std::string headers;
  headers = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: ";
  headers += std::to_string(body.size());
  headers += "\r\nConnection: close\r\n\r\n";

  if (sock.send_all(headers) == -1) {
    LOG_ERRNO("send");
    return std::nullopt;
  }

  if (!is_send_body) { // No body sending for HEAD request
    return 200;
  }

  // body
  if (sock.send_all(body) == -1) {
    LOG_ERRNO("send");
    return std::nullopt;
  }

  return 200;
}

} // namespace http
