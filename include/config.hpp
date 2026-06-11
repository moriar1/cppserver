#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <vector>

struct Config {
  uint16_t port;
  size_t workers;
  std::chrono::seconds timeout;
  size_t queue_size;
  int backlog; // POSIX API requires `int`
};

std::optional<Config> parse(const std::vector<std::string_view> &);
