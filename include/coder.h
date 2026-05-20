#pragma once

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/hwcontext.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
}

#include <memory>
#include <vector>
#include "ThreadSafeQueue.h"
#include "Clock.h"
#include "CameraCapture.h"

struct PacketWithCaptureTime {
    std::shared_ptr<AVPacket> packet;
    int64_t captureTime;
    int64_t index;
    PacketWithCaptureTime() : captureTime(0), index(0) {}
    PacketWithCaptureTime(std::shared_ptr<AVPacket> p, int64_t ct, int64_t idx) : packet(p), captureTime(ct), index(idx) {}

};

class Coder
{
public:
    Coder(std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> videoPacketQue, std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQue,
          std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> audioPacketQue, std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> audioFrameQue,
          std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoInfraFrameQue, std::shared_ptr<ThreadSafeQueue<std::shared_ptr<AVFrame>>> audioInfraFrameQue,
          std::shared_ptr<Clock> clock)
        : videoEncoder_(nullptr), videoEncoderCtx_(nullptr), swsCtx_(nullptr),
                audioEncoder_(nullptr), audioEncoderCtx_(nullptr), swrCtx_(nullptr),
                videoPacketQue_(videoPacketQue), videoFrameQue_(videoFrameQue),
                audioPacketQue_(audioPacketQue), audioFrameQue_(audioFrameQue),
                videoFrame_(nullptr), audioFrame_(nullptr), audioFifo_(nullptr), clock_(clock),
                hwDeviceCtx_(nullptr), videoHWEncoder_(nullptr), videoHWEncoderCtx_(nullptr),
                videoInfraFrameQue_(videoInfraFrameQue), audioInfraFrameQue_(audioInfraFrameQue)
    {}


    ~Coder()
    {
        if (videoEncoderCtx_) {
            avcodec_free_context(&videoEncoderCtx_);
        }
        if(audioEncoderCtx_) {
            avcodec_free_context(&audioEncoderCtx_);
        }

        if(swsCtx_) {
            sws_freeContext(swsCtx_);
        }
        if(swrCtx_) {
            swr_free(&swrCtx_);
        }
        if(audioFifo_) {
            av_audio_fifo_free(audioFifo_);
        }

    }

    bool videoEncoder();
    bool audioEncoder();
    bool videoHWEncoder();

    bool encoderVideoData();
    bool encoderAudioData();

    bool swsInit(std::shared_ptr<AVFrame>);
    bool swrInit(std::shared_ptr<AVFrame>);

    AVCodecContext *getVideoEncoderContext() const { return videoEncoderCtx_; }
    AVCodecContext *getAudioEncoderContext() const { return audioEncoderCtx_; }
    AVCodecContext *getVideoHWEncoderContext() const { return videoHWEncoderCtx_; }
    AVBufferRef *getHWFramesCtx() const { return videoHWEncoderCtx_ ? videoHWEncoderCtx_->hw_frames_ctx : nullptr; }

    AVFrame* videoConvert(std::shared_ptr<AVFrame> frame_ptr);
    AVFrame* audioConvert(std::shared_ptr<AVFrame> frame_ptr);

    AVFrame* getVideoFrame() const {return videoFrame_;}
    AVFrame* getAudioFrame() const {return audioFrame_;}

    std::shared_ptr<Clock> getClock() const { return clock_; }

private:
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> videoPacketQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> audioPacketQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> audioFrameQue_;

    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoInfraFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<AVFrame>>> audioInfraFrameQue_;

    std::shared_ptr<Clock> clock_;

    AVFrame *audioFrame_;
    AVFrame *videoFrame_;

    AVAudioFifo *audioFifo_;

    const AVCodec *videoEncoder_;
    AVCodecContext *videoEncoderCtx_;

    const AVCodec *audioEncoder_;
    AVCodecContext *audioEncoderCtx_;

    AVBufferRef *hwDeviceCtx_;
    const AVCodec *videoHWEncoder_;
    AVCodecContext *videoHWEncoderCtx_;

    SwrContext *swrCtx_;
    SwsContext *swsCtx_;

    static const uint8_t kUUID[16];

};