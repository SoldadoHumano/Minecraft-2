#include "job_system.h"
#include <algorithm>
#include <iostream>

namespace mc::core {

std::vector<std::jthread> JobSystem::s_threads;
std::queue<std::function<void()>> JobSystem::s_jobs;
std::mutex JobSystem::s_queueMutex;
std::condition_variable JobSystem::s_condition;
std::atomic<bool> JobSystem::s_stop{false};

void JobSystem::Initialize() {
  uint32_t numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
    numThreads = 4;

  // We reserve one thread for the OS Main loop and one for the Vulkan Render
  // thread. So we use max(2, hardware_threads - 2) for our worker pool.
  uint32_t workerCount = std::max(2u, numThreads > 2 ? numThreads - 2 : 2);

  std::cout << "[JobSystem] Initializing with " << workerCount
            << " worker threads.\n";

  for (uint32_t i = 0; i < workerCount; ++i) {
    s_threads.emplace_back(WorkerThread);
  }
}

void JobSystem::Shutdown() {
  std::cout << "[JobSystem] Shutting down...\n";
  {
    std::unique_lock<std::mutex> lock(s_queueMutex);
    s_stop = true;
  }
  s_condition.notify_all();
  s_threads.clear(); // jthread will auto-join on destruction
  std::cout << "[JobSystem] Shutdown complete.\n";
}

void JobSystem::WorkerThread() {
  while (true) {
    std::function<void()> job;
    {
      std::unique_lock<std::mutex> lock(s_queueMutex);
      s_condition.wait(lock, [] { return s_stop || !s_jobs.empty(); });

      if (s_stop && s_jobs.empty()) {
        return;
      }

      job = std::move(s_jobs.front());
      s_jobs.pop();
    }

    job();
  }
}

} // namespace mc::core
