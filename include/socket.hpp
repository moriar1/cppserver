#pragma once
#include "uniquefd.hpp"
#include <sys/socket.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

// Socket wrapper.
// It calls UNIX function for socket and returns its value.
// There's also `send_all()` function which handles `send()` interruptions.
class Socket : public UniqueFd {
  using UniqueFd::UniqueFd;

public:
  [[nodiscard]] int listen(int backlog) const noexcept {
    return ::listen(fd, backlog);
  }
  int setsockopt(int level, int optname, const void *optval,
                 socklen_t optlen) const noexcept {
    return ::setsockopt(fd, level, optname, optval, optlen);
  }
  [[nodiscard]] int bind(const struct sockaddr *ai_addr,
                         socklen_t ai_addrlen) const noexcept {
    return ::bind(fd, ai_addr, ai_addrlen);
  }
  [[nodiscard]] int accept(sockaddr *addr, socklen_t *addrlen) const noexcept {
    return ::accept(fd, addr, addrlen);
  }
  [[nodiscard]] ssize_t recv(void *buf, size_t len, int flags) const noexcept {
    return ::recv(fd, buf, len, flags);
  }
  [[nodiscard]] ssize_t send(const void *msg, size_t len,
                             int flags) const noexcept {
    return ::send(fd, msg, len, flags);
  }
  [[nodiscard]] int send_all(std::string_view msg) const noexcept {
    ssize_t n = 0;

    while (!msg.empty()) {
      if ((n = send(msg.data(), msg.size(), 0)) == -1) {
        if (errno == EINTR) {
          continue;
        }
        return -1;
      }
      msg.remove_prefix(static_cast<size_t>(n));
    }
    return 0;
  }

#ifdef __linux__
  [[nodiscard]] ssize_t sendfile(int content_fd, off_t *offset,
                                 size_t count) const noexcept {
    return ::sendfile(fd, content_fd, offset, count);
  }
#elif defined __FreeBSD__
  [[nodiscard]] int sendfile(int content_fd, off_t offset, size_t nbytes,
                             sf_hdtr *hdtr, off_t *sbytes,
                             int flags) const noexcept {
    return ::sendfile(content_fd, fd, offset, nbytes, hdtr, sbytes, flags);
  }
#endif
};
