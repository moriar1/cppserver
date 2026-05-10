#pragma once

#include "customlogger.hpp"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

class ThreadPool {
  std::queue<std::function<void()>> qtasks;
  std::vector<std::thread> threads;
  std::mutex mut;
  std::condition_variable cond_task_submited; // signal when user adds new task
  bool shutdown{};

public:
  explicit ThreadPool(unsigned);
  ~ThreadPool();

  template <typename F> void submit(F &&f) {
    if (shutdown) {
      LOG_DEBUG("submit() called when shutown=true");
      return;
    }

    {
      std::lock_guard lk(mut);
      qtasks.emplace([task = std::forward<F>(f)]() mutable { task(); });
    }
    cond_task_submited.notify_one();
  }

  ThreadPool() = delete;
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
};
