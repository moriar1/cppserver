#pragma once
#include "customlogger.hpp"
#include <unistd.h>
#include <utility>

class Socket {
  int fd = -1;

public:
  explicit Socket(int s = -1) : fd(s) {}

  ~Socket() {
    LOG_DEBUG("closing socket");
    if (fd != -1) {
      close(fd);
    }
  }

  // Move
  Socket(Socket &&other) noexcept : fd{other.fd} { other.fd = -1; }
  Socket &operator=(Socket &&other) noexcept {
    std::swap(fd, other.fd);
    return *this;
  }

  // Copy deleted
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  void reset(int s = -1) {
    if (fd != -1) {
      close(fd);
    }
    fd = s;
  }

  operator int() const { return fd; }
};
