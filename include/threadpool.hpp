#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
  std::queue<std::function<void()>> qtasks;
  std::vector<std::thread> threads;
  std::mutex mut;
  std::condition_variable cond_task_submited; // signal when user adds new task
  bool shutdown{};

public:
  explicit ThreadPool(unsigned);
  ~ThreadPool();

  void submit(const std::function<void()> &);

  ThreadPool() = delete;
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
};
