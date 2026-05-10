#pragma once
#include "customlogger.hpp"
#include <unistd.h>

class UniqueFd {
protected:
  int fd = -1;

public:
  explicit UniqueFd(int s = -1) : fd(s) { LOG_DEBUG("created new fd: ", fd); }

  ~UniqueFd() {
    if (fd != -1) {
      LOG_DEBUG("closing fd: ", fd);
      close(fd);
    }
  }

  // Move constructor
  UniqueFd(UniqueFd &&other) noexcept : fd{other.fd} {
    if (fd != -1) {
      LOG_DEBUG("fd move-constructed: ", fd);
    }
    other.fd = -1;
  }

  // No need in move assign so it isn't defined.
  UniqueFd &operator=(UniqueFd &&) = delete;

  // Copy deleted
  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  void reset(int s = -1) {
    if (fd == s) {
      return;
    }

    if (fd != -1) {
      LOG_DEBUG("fd reset: closing old: ", fd);
      close(fd);
    }
    fd = s;
    if (fd != -1) {
      LOG_DEBUG("fd reset: assigned new: ", fd);
    }
  }

  [[nodiscard]] int get() const noexcept { return fd; }
};
