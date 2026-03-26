#include "threadpool.hpp"
#include <iostream>

[[noreturn]] static void print_and_throw(int a) {
  std::cout << "hello " << a << '\n';
  throw(std::exception());
}

int main() {
  try {
    std::cerr << "\n---- Start threadpool test ----\n";

    ThreadPool tpool{std::thread::hardware_concurrency()};
    tpool.submit(print_and_throw, 10);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cerr << "---- Test finishied ----\n\n";
  } catch (...) {
    std::cout << "a";
  }
  return 0;
}
