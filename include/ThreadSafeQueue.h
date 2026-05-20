#pragma once
#include <memory>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <iostream>
#include <chrono>


template<class T>
class ThreadSafeQueue
{
public:
    ThreadSafeQueue(int safeQueMaxThreshold) : safeQueMaxThreshold_(safeQueMaxThreshold)
    {}
    ~ThreadSafeQueue() = default;

    // 生产者写入；队列已 done 或 quit 时返回 false
    bool put(T data)
    {
        std::unique_lock<std::mutex> lock(safeQueMTX_);
        while (true) {
            if (quit || done_) return false;
            if (safeQue_.size() < static_cast<size_t>(safeQueMaxThreshold_)) break;
            condFull_.wait(lock);
        }

        safeQue_.push(data);
        condEmpty_.notify_one();
        return true;
    }

    // 消费者读取；quit 时立即返回 false；done_ 且队列为空时返回 false（已排空）
    bool get(T& data)
    {
        std::unique_lock<std::mutex> lock(safeQueMTX_);
        while (true) {
            if (quit) return false;
            if (!safeQue_.empty()) break;
            if (done_) return false;
            condEmpty_.wait(lock);
        }

        data = safeQue_.front();
        safeQue_.pop();
        condFull_.notify_one();
        return true;
    }

    // 带超时的获取：超时或 done_+空 返回 false，quit 也返回 false
    bool try_get_for(T& data, int timeout_ms)
    {
        std::unique_lock<std::mutex> lock(safeQueMTX_);
        auto expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            if (quit) return false;
            if (!safeQue_.empty()) break;
            if (done_) return false;
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(expire - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) return false;
            if (condEmpty_.wait_until(lock, expire) == std::cv_status::timeout) return false;
        }

        data = safeQue_.front();
        safeQue_.pop();
        condFull_.notify_one();
        return true;
    }

    // 生产者通知"不再写入"，消费者排空后自然退出
    void setDone()
    {
        std::lock_guard<std::mutex> lock(safeQueMTX_);
        done_ = true;
        condEmpty_.notify_all();
        condFull_.notify_all();
    }

    // 队列是否已排空（done_ 或 quit，且无剩余数据）
    bool isExhausted()
    {
        std::lock_guard<std::mutex> lock(safeQueMTX_);
        return (done_ || quit) && safeQue_.empty();
    }

    // 紧急停止：立即唤醒所有等待者并返回 false
    void stop()
    {
        std::lock_guard<std::mutex> lock(safeQueMTX_);
        quit = true;
        condEmpty_.notify_all();
        condFull_.notify_all();
    }

    bool getQuit() const { return quit; }

private:
    std::queue<T> safeQue_;
    std::mutex safeQueMTX_;
    std::condition_variable condEmpty_;
    std::condition_variable condFull_;
    std::atomic<bool> quit{false};
    bool done_{false};
    int safeQueMaxThreshold_;
};
