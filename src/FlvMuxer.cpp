#include "../include/FlvMuxer.h"
extern "C" {
#include "libavutil/time.h"
}

bool FlvMuxer::prepareOUTPUT(AVCodecContext *videoEncoderCtx, AVCodecContext *audioEncoderCtx, const char *path)
{
    int ret = -1;
    avformat_network_init();
    ret = avformat_alloc_output_context2(&ofmtCtx_, nullptr, "flv", path);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "分配输出上下文失败:%s\n", error_buf);
        return false;
    }

    av_dict_set(&io_opts_, "tcp_nodelay", "1", 0);

    ret = avio_open2(&ofmtCtx_->pb, path, AVIO_FLAG_WRITE, nullptr, &io_opts_);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "打开输出上下文失败:%s\n", error_buf);
        return false;
    }

    //写入一帧立马发送，不缓存
    ofmtCtx_->flags |= AVFMT_FLAG_FLUSH_PACKETS;

    outputVideoStream_ = avformat_new_stream(ofmtCtx_, nullptr);
    if(!outputVideoStream_) {
        av_log(nullptr, AV_LOG_ERROR, "配置视频输出流失败\n");
        return false;
    }

    outputAudioStream_ = avformat_new_stream(ofmtCtx_, nullptr);
    if(!outputAudioStream_) {
        av_log(nullptr, AV_LOG_ERROR, "配置音频输出流失败\n");
        return false;
    }

    ret = avcodec_parameters_from_context(outputVideoStream_->codecpar, videoEncoderCtx);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "给视频输出流配置参数失败:%s\n", error_buf);
        return false;
    }
    if (videoEncoderCtx->hw_frames_ctx) {
        AVHWFramesContext *fc = (AVHWFramesContext *)videoEncoderCtx->hw_frames_ctx->data;
        outputVideoStream_->codecpar->format = fc->sw_format;
    }
    outputVideoStream_->codecpar->codec_tag = 0;
    outputVideoStream_->time_base = AVRational{1, 1000};

    ret = avcodec_parameters_from_context(outputAudioStream_->codecpar, audioEncoderCtx);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "给音频输出流配置参数失败:%s\n", error_buf);
        return false;
    }
    outputAudioStream_->codecpar->codec_tag = 0;
    outputAudioStream_->time_base = AVRational{1, 1000};

    ret = avformat_write_header(ofmtCtx_, nullptr);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "写入文件头失败:%s\n", error_buf);
        return false;
    }

    av_dump_format(ofmtCtx_, 0, path, 1);
    return true;
}

bool FlvMuxer::sendVideoAndAudio(AVCodecContext *videoEncoderCtx, AVCodecContext *audioEncoderCtx)
{
    auto writeOnePkt = [&](AVPacket *pkt, bool isVideo) -> bool {
        if (!pkt || pkt->size <= 0) return true;

        AVPacket *tmp = pkt;

        if (tmp->pts < 0) tmp->pts = 0;
        if (tmp->dts < 0 || tmp->dts == AV_NOPTS_VALUE) tmp->dts = tmp->pts;

        if (isVideo) {
            tmp->stream_index = outputVideoStream_->index;
            av_packet_rescale_ts(tmp, videoEncoderCtx->time_base, outputVideoStream_->time_base);
        } else {
            tmp->stream_index = outputAudioStream_->index;
            av_packet_rescale_ts(tmp, audioEncoderCtx->time_base, outputAudioStream_->time_base);
        }
        tmp->pos = -1;


        int ret = av_interleaved_write_frame(ofmtCtx_, tmp);
        if (ret < 0) {
            char error_buf[256];
            av_strerror(ret, error_buf, sizeof(error_buf));
            fprintf(stderr, "[MUX] av_interleaved_write_frame失败:%s stream=%d pts=%lld dts=%lld size=%d\n",
                    error_buf, tmp->stream_index,
                    static_cast<long long>(tmp->pts),
                    static_cast<long long>(tmp->dts),
                    tmp->size);
            return false;
        }

        return true;
    };

    fprintf(stderr, "[MUX] mux线程已启动\n");

    std::shared_ptr<PacketWithCaptureTime> video_pkt;
    std::shared_ptr<PacketWithCaptureTime> audio_pkt;
    int64_t writtenCount = 0;
    bool videoActive = true, audioActive = true;

    fprintf(stderr, "[MUX] 开始音视频交织写出\n");

    while (videoActive || audioActive || video_pkt || audio_pkt) {
        if (video_pkt == nullptr && videoActive == true) {
            if (!videoPacketQue_->try_get_for(video_pkt, 10)) {
                if (videoPacketQue_->isExhausted()) videoActive = false;
            }
        }
        if (audio_pkt == nullptr && audioActive == true) {
            if (!audioPacketQue_->try_get_for(audio_pkt, 10)) {
                if (audioPacketQue_->isExhausted()) audioActive = false;
            }
        }

        if (!video_pkt && !audio_pkt) continue;


        bool doVideo;
        if (video_pkt && audio_pkt) {
            // 比较 DTS 决策音视频写出顺序
            int64_t v_us = av_rescale_q(video_pkt->packet->dts, videoEncoderCtx->time_base, {1, 1000000});
            int64_t a_us = av_rescale_q(audio_pkt->packet->dts, audioEncoderCtx->time_base, {1, 1000000});
            doVideo = (v_us <= a_us);
        } else {
            doVideo = (video_pkt != nullptr);
        }

        if (doVideo) {
            std::cout << "从第" << video_pkt->index << "帧被捕捉，到帧到达FLV模块的延迟是" << (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - video_pkt->captureTime) / 1000 << "ms" << std::endl;
            if (!writeOnePkt(video_pkt->packet.get(), true)) return false;
            std::cout << "从第" << video_pkt->index << "帧被捕捉，到帧离开FLV模块的延迟是" << (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - video_pkt->captureTime) / 1000 << "ms" << std::endl;
            video_pkt.reset();
        } else {
            if (!writeOnePkt(audio_pkt->packet.get(), false)) return false;
            audio_pkt.reset();
        }

        writtenCount++;
        if (writtenCount % 100 == 0) {
            fprintf(stderr, "[MUX] 已写出 %lld 个包\n", static_cast<long long>(writtenCount));
        }
    }

    av_write_trailer(ofmtCtx_);
    fprintf(stderr, "[MUX] mux线程正常退出，共写出 %lld 个包\n", static_cast<long long>(writtenCount));
    return true;
}
