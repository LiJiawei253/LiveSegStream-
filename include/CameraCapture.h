#pragma once

extern "C" {
#include "libavdevice/avdevice.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/log.h"
#include "libavutil/error.h"
#include "libavutil/time.h"
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}
#include <memory>
#include <iostream>
#include <atomic>
#include "ThreadSafeQueue.h"
#include "Clock.h"

struct FrameWithCaptureTime {
    std::shared_ptr<AVFrame> frame;
    int64_t captureTime;
    int64_t index;

    FrameWithCaptureTime() : captureTime(0), index(0) {}
    FrameWithCaptureTime(std::shared_ptr<AVFrame> f, int64_t ct, int64_t idx) : frame(f), captureTime(ct), index(idx) {}
};

class CameraCapture
{
public:
    CameraCapture(std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQue,
                  std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> audioFrameQue,
                  std::shared_ptr<Clock> clock)
        : videoFrameQue_(videoFrameQue), audioFrameQue_(audioFrameQue), clock_(clock),
        video_input_format_(nullptr), videoFmtCtx_(nullptr), videoStreamID_(-1), videoDecoder_(nullptr), videoDecoderCtx_(nullptr), videoOptions_(nullptr),
        audioFmtCtx_(nullptr), audioStreamID_(-1), audio_input_format_(nullptr), audioDecoder_(nullptr), audioDecoderCtx_(nullptr), audioOptions_(nullptr)
    {}

    ~CameraCapture();

    bool open_video(const char *path);
    bool open_audio(const char *path);

    AVFormatContext* getVideoFormatContext() const {return videoFmtCtx_;}
    AVFormatContext* getAudioFormatContext() const {return audioFmtCtx_;}


    int getVideoStreamID() const {return videoStreamID_;}
    int getAudioStreamID() const {return audioStreamID_;}


    void videoStart();
    void audioStart();
    void stopCapture();


 private:
    static int interruptCallback(void *ctx);
    std::atomic<bool> stopFlag_{false};
    bool dataVideoLoop();
    bool dataAudioLoop();

private:
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> audioFrameQue_;
    std::shared_ptr<Clock> clock_;

    const AVInputFormat *video_input_format_;
    const AVInputFormat *audio_input_format_;

    AVFormatContext *videoFmtCtx_;
    AVFormatContext *audioFmtCtx_;

    int videoStreamID_;
    int audioStreamID_;

    const AVCodec *videoDecoder_;
    const AVCodec *audioDecoder_;

    AVCodecContext *videoDecoderCtx_;
    AVCodecContext *audioDecoderCtx_;

    AVDictionary *videoOptions_;
    AVDictionary *audioOptions_;
};