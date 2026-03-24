#include "threadpool.hpp"
#include <chrono>
#include <iostream>
#include <thread>

static void print_hello() { std::cout << "hello\n"; }

int main() {
  std::cerr << "\n---- Start threadpool test ----\n";

  ThreadPool tpool{std::thread::hardware_concurrency()};
  tpool.submit(print_hello);

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cerr << "---- Test finishied ----\n\n";
  return 0;
}
