#pragma once
#include <cstdint>
#include <mutex>
#include <cstdio>
extern "C" {
#include "libavutil/rational.h"
}

class Clock {
public:
    explicit Clock(int sampleRate = 32000);


    // 全局时钟：首帧建立基准，之后都用这个
    void initGlobalClock(int64_t t);
    int64_t getGlobalClockUs() const;
    bool isGlobalClockStarted() const { std::lock_guard<std::mutex> lock(mtx_); return globalStartTime_ != 0; }

private:
    mutable std::mutex mtx_;

    int64_t globalStartTime_ = 0;  // 全局时钟基准
    int sampleRate_;
};
