#include "customlogger.hpp"
#include "threadpool.hpp"

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

        LOG_DEBUG("starting task execution...");
        auto task = qtasks.front();
        if (!task) {
          LOG_DEBUG("wrong task");
          continue;
        }

        qtasks.pop();
        lk.unlock();
        try {
          task();
        } catch (const std::exception &e) {
          LOG_ERROR("got exception from task: ", e.what());
        } catch (...) {
          LOG_ERROR("got exception from task");
        }
        LOG_DEBUG("finished task executing.");
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
