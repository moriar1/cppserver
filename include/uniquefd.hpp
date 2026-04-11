#pragma once
#include "customlogger.hpp"
#include <unistd.h>
#include <utility>

class UniqueFd {
  int fd = -1;

public:
  explicit UniqueFd(int s = -1) : fd(s) { LOG_DEBUG("created new fd: ", fd); }

  ~UniqueFd() {
    LOG_DEBUG("closing fd: ", fd);
    if (fd != -1) {
      close(fd);
    }
  }

  // Move
  UniqueFd(UniqueFd &&other) noexcept : fd{other.fd} {
    other.fd = -1;
    LOG_DEBUG("fd moved from ", other.fd, " to ", fd);
  }
  UniqueFd &operator=(UniqueFd &&other) noexcept {
    std::swap(fd, other.fd);
    return *this;
  }

  // Copy deleted
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  void reset(int s = -1) {
    if (fd != -1) {
      close(fd);
    }
    fd = s;
  }

  operator int() const { return fd; }
};

using Socket = UniqueFd;
