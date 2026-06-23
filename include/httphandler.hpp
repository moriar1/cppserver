#pragma once
#include "socket.hpp"
#include <chrono>
#include <string>

namespace http {

inline constexpr size_t MAXDATASIZE = 4096;

using namespace std::chrono_literals;
void handle_client(Socket, std::string, std::chrono::seconds timeout = 10s);

} // namespace http
