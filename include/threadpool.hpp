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
  const size_t max_tasks;

public:
  explicit ThreadPool(size_t nthreads, size_t mt = SIZE_MAX);
  ~ThreadPool();

  template <typename F> bool submit(F &&f) {
    if (shutdown) {
      LOG_DEBUG("submit() called when shutown=true");
      return false;
    }

    {
      std::lock_guard lk(mut);
      if (qtasks.size() >= max_tasks) {
        LOG_INFO("queue limit reached: ", max_tasks, ", skipping task");
        return false;
      }
      qtasks.emplace([task = std::forward<F>(f)]() mutable { task(); });
    }
    cond_task_submited.notify_one();
    return true;
  }

  ThreadPool() = delete;
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;
};
