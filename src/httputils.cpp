#include "httputils.hpp"
#include <string>

#ifdef __FreeBSD__
#include <sys/syslimits.h>
#elif defined __linux__
#include <limits.h>
#endif

namespace http {

std::optional<HttpStatus> send_response(const Socket &sock, HttpStatus status,
                                        std::string_view msg) {
  if (sock.send_all(msg) == -1) {
    LOG_ERRNO("failed send response fot status ", status);
    return std::nullopt;
  }
  return status;
}

// clang-format off
// Both simple and fast functions for sending HTTP status
std::optional<HttpStatus> send_400(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n400";
  return send_response(sock,  400,  msg);
}
std::optional<HttpStatus> send_403(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n403";
  return send_response(sock,  403,  msg);
}
std::optional<HttpStatus> send_404(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n404";
  return send_response(sock,  404,  msg);
}
std::optional<HttpStatus> send_405(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n405";
  return send_response(sock,  405,  msg);
}
std::optional<HttpStatus> send_431(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n431";
  return send_response(sock,  431,  msg);
}
std::optional<HttpStatus> send_500(const Socket &sock) {
  static constexpr std::string_view msg = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\n500";
  return send_response(sock,  500,  msg);
}
// clang-format on

std::string_view get_mime_type(const std::filesystem::path &path) {
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

bool is_path_safe(const std::filesystem::path &path) {
  namespace fs = std::filesystem;

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

std::optional<char> hex_to_char(std::string_view s) {
  if (s.size() < 2) {
    return std::nullopt;
  }

  // this array is not `constexpr` because of `fill()` but it still lazy static
  static const std::array<int, 256> table = []() {
    std::array<int, 256> arr{};
    arr.fill(-1);

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

std::string url_decode(std::string_view s) {
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

// NOTE: use std::move() to pass str
static std::string html_encode(std::string str) {
  size_t pos = str.find_first_of("&\"'<>/");
  if (pos == std::string::npos) {
    return str;
  }

  std::string out;
  out.reserve(str.size());
  out.append(str, 0, pos);

  for (size_t i = pos; i < str.size(); i++) {
    switch (str[i]) { // clang-format off
    case '&':  out += "&amp;"; break;
    case '"':  out += "&quot;"; break;
    case '\'': out += "&apos;"; break;
    case '>':  out += "&gt;"; break;
    case '<':  out += "&lt;"; break;
    default:   out += str[i]; // clang-format on
    }
  }
  return out;
}

std::string generate_dir_html(std::filesystem::directory_iterator it,
                              const std::filesystem::path &web_path) {
  std::string body;
  body.reserve(2048);

  std::string display_path = html_encode(web_path);
  body += "<html><head><title>Index of ";
  body += display_path;
  body += "</title></head><body><h1>Index of ";
  body += display_path;
  body += "/</h1><hr><pre>";

  if (web_path != "./") {
    body += "<a href=\"/../\">../</a>\n";
  }

  for (const auto &entry : it) {
    // TODO: use `html_encode` for `display_path` and `url_encode` for href
    std::string full_path = html_encode(entry.path().string());
    std::string_view href = full_path;
    href.remove_prefix(1); // remove dot `./path` -> `/path`

    std::string filename = html_encode(entry.path().filename().string());

    body += "<a href=\"";
    body += href;
    body += "\">";
    body += filename;
    body += "</a>\n";
  }

  body += "</pre><hr></body></html>\n";
  return body;
}

} // namespace http
