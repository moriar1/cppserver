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

  template <typename F, typename... Args> void submit(F &&f, Args &&...args) {
    if (shutdown) {
      LOG_DEBUG("submit() called when shutown=true");
      return;
    }

    // Assemble function with its args and push it in queue
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    {
      std::lock_guard lk(mut);
      qtasks.emplace(std::move(task));
    }
    cond_task_submited.notify_one();
  }

  ThreadPool() = delete;
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
};
