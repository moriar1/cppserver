#pragma once
#include "socket.hpp"
#include <string>

namespace http {

inline constexpr size_t MAXDATASIZE = 4096;
void handle_client(Socket accept_sock, std::string ip);

// for tests
bool is_path_safe(std::string_view path);
std::optional<std::string> read_request_headers(const Socket &,
                                                std::string_view);
} // namespace http
