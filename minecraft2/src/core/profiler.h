#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <mutex>
#include <thread>

namespace mc::core {

struct ProfileResult {
    const char* name;
    long long durationNs;
    std::thread::id threadId;
};

class Profiler {
public:
    static Profiler& Get() {
        static Profiler instance;
        return instance;
    }

    void BeginFrame();
    void EndFrame();
    void SubmitResult(const ProfileResult& result);

    const std::vector<ProfileResult>& GetResults() const { return m_frameResults; }

private:
    Profiler() = default;
    
    std::vector<ProfileResult> m_currentResults;
    std::vector<ProfileResult> m_frameResults;
    std::mutex m_mutex;
};

class ProfileTimer {
public:
    ProfileTimer(const char* name);
    ~ProfileTimer();
    void Stop();

private:
    const char* m_name;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_startTimePoint;
    bool m_stopped;
};

#define PROFILE_SCOPE(name) ::mc::core::ProfileTimer timer##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__FUNCTION__)

} // namespace mc::core
