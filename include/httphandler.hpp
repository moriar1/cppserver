#pragma once
#include "socket.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace http {

using namespace std::chrono_literals;

inline constexpr size_t MAXDATASIZE = 4096;

void handle_client(Socket, std::string, std::chrono::seconds timeout = 10s);

// for tests
bool is_path_safe(std::string_view path);
std::optional<std::string> read_request_headers(const Socket &,
                                                std::string_view);
} // namespace http
