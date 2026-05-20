#pragma once

#include <thread>

#include "ThreadSafeQueue.h"
#include "CameraCapture.h"
#include "coder.h"
#include "FlvMuxer.h"
#include "Clock.h"
#include "Infra.h"

class PushStream
{
public:
    PushStream(){
        clock_ = std::make_shared<Clock>(32000);

        videoFrameQue_ = std::make_shared<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>>(4);
        videoInfraFrameQue_ = std::make_shared<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>>(4);
        videoPacketQue_ = std::make_shared<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>>(4);
        audioFrameQue_ = std::make_shared<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>>(4);
        audioPacketQue_ = std::make_shared<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>>(4);

        camera_ = std::make_unique<CameraCapture>(videoFrameQue_, audioFrameQue_, clock_);
        infra_ = std::make_unique<Infra>(videoFrameQue_, videoInfraFrameQue_);
        coder_ = std::make_unique<Coder>(videoPacketQue_, videoFrameQue_, audioPacketQue_, audioFrameQue_, videoInfraFrameQue_, audioInfraFrameQue_, clock_);
        flvMuxer_ = std::make_unique<FlvMuxer>(videoPacketQue_, audioPacketQue_);
    }

    ~PushStream() { stop(); }

    bool open(const char *device, const char *audio_device, const char *url, const std::wstring& model_path);

    void start();
    void stop();



private:
    std::shared_ptr<Clock> clock_;

    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoInfraFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<AVFrame>>> audioInfraFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> videoPacketQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> audioFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> audioPacketQue_;

    std::unique_ptr<CameraCapture> camera_;
    std::unique_ptr<Coder> coder_;
    std::unique_ptr<Infra> infra_;
    std::unique_ptr<FlvMuxer> flvMuxer_;


    std::thread captureVideoData_;
    std::thread captureAudioData_;
    std::thread encoderVideoData_;
    std::thread encoderAudioData_;
    std::thread infraVideoData_;
    std::thread sendVideoAndAudioData_;
};