#pragma once

#include "socket.hpp"
#include <filesystem>
#include <optional>

namespace http {

using HttpStatus = unsigned;

std::string_view get_mime_type(const std::filesystem::path &);
std::optional<char> hex_to_char(std::string_view);
std::string url_decode(std::string_view);
bool is_path_safe(const std::filesystem::path &path);
std::optional<std::string> read_request_headers(const Socket &,
                                                std::string_view);
std::string generate_dir_html(std::filesystem::directory_iterator it,
                              const std::filesystem::path &web_path);

[[nodiscard]] std::optional<HttpStatus>
send_response(const Socket &, HttpStatus, std::string_view);
[[nodiscard]] std::optional<HttpStatus> send_400(const Socket &);
[[nodiscard]] std::optional<HttpStatus> send_403(const Socket &);
[[nodiscard]] std::optional<HttpStatus> send_404(const Socket &);
[[nodiscard]] std::optional<HttpStatus> send_405(const Socket &);
[[nodiscard]] std::optional<HttpStatus> send_431(const Socket &);
[[nodiscard]] std::optional<HttpStatus> send_500(const Socket &);

} // namespace http
