#include "profiler.h"

namespace mc::core {

void Profiler::BeginFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentResults.clear();
}

void Profiler::EndFrame() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frameResults = m_currentResults;
}

void Profiler::SubmitResult(const ProfileResult& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentResults.push_back(result);
}

ProfileTimer::ProfileTimer(const char* name)
    : m_name(name), m_stopped(false) {
    m_startTimePoint = std::chrono::high_resolution_clock::now();
}

ProfileTimer::~ProfileTimer() {
    if (!m_stopped) {
        Stop();
    }
}

void ProfileTimer::Stop() {
    auto endTimePoint = std::chrono::high_resolution_clock::now();
    long long start = std::chrono::time_point_cast<std::chrono::nanoseconds>(m_startTimePoint).time_since_epoch().count();
    long long end = std::chrono::time_point_cast<std::chrono::nanoseconds>(endTimePoint).time_since_epoch().count();

    Profiler::Get().SubmitResult({m_name, end - start, std::this_thread::get_id()});
    m_stopped = true;
}

} // namespace mc::core
