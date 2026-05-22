#include "threadpool.hpp"
#include <atomic>
#include <gtest/gtest.h>

TEST(ThreadPoolTest, AccumulatesCorrectly) {
  std::atomic<int> counter{};
  constexpr int iterations = 100;

  {
    ThreadPool tpool{std::thread::hardware_concurrency()};

    for (int i = 0; i < iterations; i++) {
      tpool.submit([&]() { counter++; });
    }
  } // waits all threads in destructor

  EXPECT_EQ(counter, iterations);
}
