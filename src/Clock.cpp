#include "../include/Clock.h"
#include <mutex>
#include <chrono>
extern "C" {
#include "libavutil/time.h"
}

Clock::Clock(int sampleRate) : sampleRate_(sampleRate) {}


void Clock::initGlobalClock(int64_t t) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (globalStartTime_ == 0) {
        globalStartTime_ = t;
        fprintf(stderr, "[CLOCK] 全局时钟已建立: %lld (微秒)\n", (long long)t);
    }
}

int64_t Clock::getGlobalClockUs() const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (globalStartTime_ == 0) return 0;
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    return now - globalStartTime_;
}


