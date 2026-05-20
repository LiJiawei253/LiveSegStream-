#pragma once

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
}

#include "ThreadSafeQueue.h"
#include "coder.h"

#include <memory>
#include <mutex>
#include <chrono>

class FlvMuxer
{
public:
    FlvMuxer(std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> videoPacketQue, std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> audioPacketQue) :
        videoPacketQue_(videoPacketQue), audioPacketQue_(audioPacketQue),
        ofmtCtx_(nullptr), outputVideoStream_(nullptr), outputAudioStream_(nullptr), io_opts_(nullptr)
    {}


    ~FlvMuxer() {
        if(ofmtCtx_){
            if (ofmtCtx_->pb) {
                avio_closep(&ofmtCtx_->pb);
            }
            avformat_free_context(ofmtCtx_);
            ofmtCtx_ = nullptr;
        }

        if (io_opts_) {
            av_dict_free(&io_opts_);
        }
    }

    bool prepareOUTPUT(AVCodecContext *videoEncoderCtx, AVCodecContext *audioEncoderCtx, const char *path);
    bool sendVideoAndAudio(AVCodecContext *videoEncoderCtx, AVCodecContext *audioEncoderCtx);

private:
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> videoPacketQue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<PacketWithCaptureTime>>> audioPacketQue_;

    AVFormatContext *ofmtCtx_;
    AVStream *outputVideoStream_;
    AVStream* outputAudioStream_;

    AVDictionary *io_opts_;

    std::mutex writeMtx_;
};








