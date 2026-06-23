#include "httphandler.hpp"
#include "httputils.hpp"
#include <array>
#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

TEST(HttpHandlerTest, PathTraversalUnsafe) {
  EXPECT_FALSE(http::is_path_safe("../../../../../etc/passwd"));
}

TEST(HttpHandlerTest, NormalPathSafe) {
  EXPECT_TRUE(http::is_path_safe("./index.html"));
}

TEST(HttpHandlerTest, ReadRequestByParts) {
  std::array<int, 2> fd{};
  if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fd.data()) != 0) {
    FAIL() << "socketpair failed: " << std::system_category().message(errno);
  }

  Socket read_sock{fd[0]};
  Socket write_sock{fd[1]};

  std::thread t([write_sock = std::move(write_sock)]() {
    std::string part1 = {"GET / HTTP1.1\r"};
    if (write(write_sock.get(), part1.data(), part1.size()) < 0) {
      FAIL() << "write part1 failed: " << std::system_category().message(errno);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string part2 = {"\n\r\n"};
    if (write(write_sock.get(), part2.data(), part2.size()) < 0) {
      FAIL() << "write part2 failed: " << std::system_category().message(errno);
    }
  });

  auto data = http::read_request_headers(read_sock, "::1");
  t.join();

  ASSERT_TRUE(data.has_value()) << "`std::nullopt` from read_request_headers()";
  ASSERT_EQ(data.value(), "GET / HTTP1.1\r\n\r\n");
}

TEST(HttpHandlerTest, Sends431) {
  std::array<int, 2> fd{};
  if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fd.data()) != 0) {
    FAIL() << "socketpair failed: " << std::system_category().message(errno);
  }
  Socket server{fd[0]};
  Socket client{fd[1]};

  // Send long header
  std::string request(http::MAXDATASIZE + 1, 'a');
  if (write(client.get(), request.data(), request.size()) < 0) {
    FAIL() << "write failed: " << std::system_category().message(errno);
  }
  auto read_headers = http::read_request_headers(server, "::1");
  ASSERT_FALSE(read_headers.has_value())
      << "Expected `std::nullopt` from read_request_headers() ";

  // Recieve http 431
  std::array<char, http::MAXDATASIZE> buf{};
  ssize_t nread = read(client.get(), buf.data(), buf.size());
  if (nread <= 0) {
    FAIL() << "read failed: " << std::system_category().message(errno);
  }
  std::string_view res{buf.data(), static_cast<size_t>(nread)};
  ASSERT_NE(res.find("431"), std::string_view::npos)
      << "Response does not contain `431`. Response: " << res;
}
