#include "customlogger.hpp"
#include "threadpool.hpp"
#include <mutex>

ThreadPool::ThreadPool(unsigned nthreads) {
  LOG_DEBUG("ThreadPool creating");

  for (unsigned i = 0; i < nthreads; i++) {
    threads.emplace_back([this] { // push task runner lambda
      while (true) {
        std::unique_lock lk(mut);
        cond_task_submited.wait(lk,
                                [this] { return !qtasks.empty() || shutdown; });

        if (shutdown && qtasks.empty()) {
          break;
        }

        LOG_DEBUG("Starting task executing...");
        auto task = qtasks.front();
        qtasks.pop();
        lk.unlock();
        task();
        LOG_DEBUG("Finished task executing.");
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::lock_guard lk(mut);
    shutdown = true;
  }
  cond_task_submited.notify_all();
  LOG_DEBUG("joining threads...");
  for (auto &thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  LOG_DEBUG("threads joined.");
};

void ThreadPool::submit(const std::function<void()> &f) {
  {
    std::lock_guard lk(mut);
    qtasks.emplace(f);
  }
  cond_task_submited.notify_one();
}
