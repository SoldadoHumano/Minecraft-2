#pragma once

#include <functional>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <memory>
#include <stdexcept>

namespace mc::core {

class JobSystem {
public:
    static void Initialize();
    static void Shutdown();

    // Enqueue a job and return a future to wait on or get the result
    template<typename F, typename... Args>
    static auto Execute(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(s_queueMutex);
            if (s_stop) {
                throw std::runtime_error("Cannot enqueue on stopped JobSystem");
            }
            s_jobs.emplace([task]() { (*task)(); });
        }
        s_condition.notify_one();
        return res;
    }

private:
    static void WorkerThread();

    static std::vector<std::jthread> s_threads;
    static std::queue<std::function<void()>> s_jobs;
    static std::mutex s_queueMutex;
    static std::condition_variable s_condition;
    static std::atomic<bool> s_stop;
};

} // namespace mc::core
