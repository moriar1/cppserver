#include "threadpool.hpp"
#include <atomic>
#include <iostream>

int main() {
  std::cerr << "\n---- Start threadpool test ----\n";

  std::atomic<int> counter{};
  constexpr int iterations = 100;

  {
    ThreadPool tpool{std::thread::hardware_concurrency()};

    for (int i = 0; i < iterations; i++) {
      tpool.submit([&]() { counter++; });
    }
  } // waits all threads in destructor

  int status = (counter == iterations) ? 0 : 1;
  std::cerr << "---- Test finishied ----\n\n";
  return status;
}
