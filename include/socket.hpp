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
  int listen(int backlog) { return ::listen(fd, backlog); }
  int setsockopt(int level, int optname, const void *optval, socklen_t optlen) {
    return ::setsockopt(fd, level, optname, optval, optlen);
  }
  int bind(const struct sockaddr *ai_addr, socklen_t ai_addrlen) {
    return ::bind(fd, ai_addr, ai_addrlen);
  }
  int accept(sockaddr *addr, socklen_t *addrlen) {
    return ::accept(fd, addr, addrlen);
  }
  ssize_t recv(void *buf, size_t len, int flags) {
    return ::recv(fd, buf, len, flags);
  }
  ssize_t send(const void *msg, size_t len, int flags) {
    return ::send(fd, msg, len, flags);
  }
  int send_all(std::string_view msg) {
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
  ssize_t sendfile(int content_fd, off_t *offset, size_t count) {
    return ::sendfile(fd, content_fd, offset, count);
  }
#elif defined __FreeBSD__
  int sendfile(int content_fd, off_t offset, size_t nbytes, sf_hdtr *hdtr,
               off_t *sbytes, int flags) {
    return ::sendfile(content_fd, fd, offset, nbytes, hdtr, sbytes, flags);
  }
#endif
};
