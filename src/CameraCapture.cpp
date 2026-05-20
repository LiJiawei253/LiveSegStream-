#include "../include/CameraCapture.h"
#include <vector>
#include <chrono>



bool CameraCapture::open_video(const char *path)
{
    int ret = -1;
    avdevice_register_all();

    video_input_format_ = av_find_input_format("dshow");
    if(!video_input_format_) {
        std::cerr << "查找输出格式失败" << std::endl;
        return false;
    }

    av_dict_set(&videoOptions_, "input_format", "yuyv422", 0);
    av_dict_set(&videoOptions_, "video_size", "1280x720", 0);
    av_dict_set(&videoOptions_, "framerate", "30", 0);
    av_dict_set(&videoOptions_, "rtbufsize", "100000000", 0);

    videoFmtCtx_ = avformat_alloc_context();
    videoFmtCtx_->interrupt_callback.callback = interruptCallback;
    videoFmtCtx_->interrupt_callback.opaque   = this;

    ret = avformat_open_input(&videoFmtCtx_, path, video_input_format_, &videoOptions_);
    if(ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "打开设备失败:%s\n", error_buf);
        return false;
    }

    ret = avformat_find_stream_info(videoFmtCtx_, nullptr);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "查找流信息失败:%s\n", error_buf);
        return false;
    }

    videoStreamID_ = av_find_best_stream(videoFmtCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if(videoStreamID_ < 0) {
        av_log(nullptr, AV_LOG_ERROR, "获取流索引失败\n");
        return false;
    }

     videoDecoder_ = avcodec_find_decoder(videoFmtCtx_->streams[videoStreamID_]->codecpar->codec_id);
     if(!videoDecoder_) {
         av_log(nullptr, AV_LOG_ERROR, "查找解码器失败\n");
         return false;
     }

     videoDecoderCtx_ = avcodec_alloc_context3(videoDecoder_);
     if(!videoDecoderCtx_) {
         av_log(nullptr, AV_LOG_ERROR, "分配解码器上下文失败\n");
         return false;
     }

     ret = avcodec_parameters_to_context(videoDecoderCtx_, videoFmtCtx_->streams[videoStreamID_]->codecpar);
     if(ret < 0) {
         char error_buf[256];
         av_strerror(ret, error_buf, sizeof(error_buf));
         av_log(nullptr, AV_LOG_ERROR, "拷贝解码器参数失败:%s\n", error_buf);
         return false;
     }

     ret = avcodec_open2(videoDecoderCtx_, videoDecoder_, nullptr);
     if(ret < 0){
         char error_buf[256];
         av_strerror(ret, error_buf, sizeof(error_buf));
         av_log(nullptr, AV_LOG_ERROR, "打开解码器失败:%s\n", error_buf);
         return false;
     }


    return true;
}


bool CameraCapture::open_audio(const char *path)
{
    int ret = -1;

    audio_input_format_ = av_find_input_format("dshow");
    if(!audio_input_format_) {
        av_log(nullptr, AV_LOG_ERROR, "查找音频格式失败\n");
        return false;
    }

    av_dict_set(&audioOptions_, "sample_fmt", "s16", 0);
    av_dict_set(&audioOptions_, "sample_rate", "32000", 0);
    av_dict_set(&audioOptions_, "channels", "1", 0);
    av_dict_set(&audioOptions_, "audio_buffer_size", "5", 0);

    audioFmtCtx_ = avformat_alloc_context();
    audioFmtCtx_->interrupt_callback.callback = interruptCallback;
    audioFmtCtx_->interrupt_callback.opaque   = this;

    ret = avformat_open_input(&audioFmtCtx_, path, audio_input_format_, &audioOptions_);
    if(ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "打开音频设备失败:%s\n", error_buf);
        return false;
    }

    ret = avformat_find_stream_info(audioFmtCtx_, nullptr);
    if(ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "获取音频设备流信息失败:%s\n", error_buf);
        return false;
    }

    audioStreamID_ = av_find_best_stream(audioFmtCtx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if(audioStreamID_ < 0) {
        av_log(nullptr, AV_LOG_ERROR, "获取音频流索引失败\n");
        return false;
    }

    audioDecoder_ = avcodec_find_decoder(audioFmtCtx_->streams[audioStreamID_]->codecpar->codec_id);
    if(!audioDecoder_) {
        av_log(nullptr, AV_LOG_ERROR, "获取解码器失败\n");
        return false;
    }

    audioDecoderCtx_ = avcodec_alloc_context3(audioDecoder_);
    if(!audioDecoderCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "分配解码器上下文失败\n");
        return false;
    }

    ret = avcodec_parameters_to_context(audioDecoderCtx_, audioFmtCtx_->streams[audioStreamID_]->codecpar);
    if(ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "拷贝解码器参数失败:%s\n", error_buf);
        return false;
    }

    ret = avcodec_open2(audioDecoderCtx_, audioDecoder_, nullptr);
    if(ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "打开音频解码器失败:%s\n", error_buf);
        return false;
    }

    return true;
}

bool CameraCapture::dataVideoLoop()
{
    int ret = -1;
    AVPacket *pkt = av_packet_alloc();
    int frameCount = 0;

    fprintf(stderr, "[VIDEO_CAP] 视频采集线程启动\n");

    while(av_read_frame(videoFmtCtx_, pkt) >= 0) {
        int64_t captureTime = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();    // Unix epoch 微秒


        if(pkt->stream_index != videoStreamID_) {
            av_packet_unref(pkt);
            continue;
        }

        if(pkt->stream_index == videoStreamID_) {
            static bool printed = false;
            if (!printed) {
                AVStream *vs = videoFmtCtx_->streams[videoStreamID_];
                fprintf(stderr, "[VIDEO_CAP] 实际流信息: codec_id=0x%x width=%d height=%d format=%d pkt_size=%d\n",
                        vs->codecpar->codec_id,
                        vs->codecpar->width,
                        vs->codecpar->height,
                        vs->codecpar->format,
                        pkt->size);
                printed = true;
            }
            ret = avcodec_send_packet(videoDecoderCtx_, pkt);
            if(ret < 0) {
                char error_buf[256];
                av_strerror(ret, error_buf, sizeof(error_buf));
                fprintf(stderr, "[VIDEO_CAP] send_packet 失败 ret=%d (%s)，跳过此帧继续\n", ret, error_buf);
                av_packet_unref(pkt);
                continue;
            }
            while(ret >= 0) {
                AVFrame *frame = av_frame_alloc();

                ret = avcodec_receive_frame(videoDecoderCtx_, frame);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_frame_free(&frame);
                    ret = 0;
                    break;
                } else if(ret < 0) {
                    char error_buf[256];
                    av_strerror(ret, error_buf, sizeof(error_buf));
                    fprintf(stderr, "[VIDEO_CAP] receive_frame 失败 ret=%d (%s)，跳过\n", ret, error_buf);
                    av_frame_free(&frame);
                    ret = 0;
                    break;
                }

                frameCount++;
                int64_t best_effort = frame->best_effort_timestamp;
                if (frameCount <= 10 || frameCount % 30 == 0) {
                    fprintf(stderr, "[VIDEO_CAP] 采集到第 %d 帧 best_effort=%lld pts=%lld captureTime=%lld\n",
                    frameCount, (long long)best_effort, (long long)frame->pts, (long long)captureTime);
                }

                std::shared_ptr<AVFrame> frame_ptr(frame, [](AVFrame *f){if(f) {
                    av_frame_free(&f);
                    }}
                );

                std::shared_ptr<FrameWithCaptureTime> frameWithTime = std::make_shared<FrameWithCaptureTime>(frame_ptr, captureTime, frameCount);
                if(!videoFrameQue_->put(frameWithTime)) {
                    fprintf(stderr, "[VIDEO_CAP] put 入队失败，队列已 done\n");
                    av_packet_free(&pkt);
                    return false;
                }
            }
        }
         av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    fprintf(stderr, "[VIDEO_CAP] av_read_frame 返回错误，采集线程退出\n");
    return true;
}


bool CameraCapture::dataAudioLoop() {
    int ret = -1;
    int frameCount = 0;

    AVPacket *pkt = av_packet_alloc();
    fprintf(stderr, "[AUDIO_CAP] 音频采集线程启动\n");

    while(av_read_frame(audioFmtCtx_, pkt) >= 0) {
        int64_t captureTime = av_gettime_relative();

        if(pkt->stream_index != audioStreamID_) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(audioDecoderCtx_, pkt);
        if(ret < 0) {
            char error_buf[256];
            av_strerror(ret, error_buf, sizeof(error_buf));
            fprintf(stderr, "[AUDIO_CAP] send_packet 失败 ret=%d (%s)，跳过\n", ret, error_buf);
            av_packet_unref(pkt);
            continue;
        }
        while(ret >= 0) {
            AVFrame *frame = av_frame_alloc();

            ret = avcodec_receive_frame(audioDecoderCtx_, frame);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                ret = 0;
                break;
            } else if(ret < 0) {
                char error_buf[256];
                av_strerror(ret, error_buf, sizeof(error_buf));
                fprintf(stderr, "[AUDIO_CAP] receive_frame 失败 ret=%d (%s)，跳过\n", ret, error_buf);
                av_frame_free(&frame);
                ret = 0;
                break;
            }

            frameCount++;

            std::shared_ptr<AVFrame> frame_ptr(frame, [](AVFrame *f){if(f) {
                av_frame_free(&f);
                }}
            );

            std::shared_ptr<FrameWithCaptureTime> frameWithTime = std::make_shared<FrameWithCaptureTime>(frame_ptr, captureTime, frameCount);
            if(!audioFrameQue_->put(frameWithTime)) {
                fprintf(stderr, "[AUDIO_CAP] put 入队失败，队列已 done\n");
                av_packet_free(&pkt);
                return false;
            }

            if (frameCount <= 10 || frameCount % 100 == 0) {
                fprintf(stderr, "[AUDIO_CAP] 采集到第 %d 帧 nb_samples=%d captureTime=%lld\n",
                        frameCount, frame->nb_samples, (long long)captureTime);
            }
        }
        av_packet_unref(pkt);
    }


    av_packet_free(&pkt);
    fprintf(stderr, "[AUDIO_CAP] av_read_frame 返回错误，音频采集线程退出，共采集 %d 帧\n", frameCount);
    return true;
}






CameraCapture::~CameraCapture()
{
    if(videoFmtCtx_) {
        avformat_close_input(&videoFmtCtx_);
    }
    if(audioFmtCtx_) {
        avformat_close_input(&audioFmtCtx_);
    }

    if(videoDecoderCtx_) {
        avcodec_free_context(&videoDecoderCtx_);
    }
    if(audioDecoderCtx_) {
        avcodec_free_context(&audioDecoderCtx_);
    }

    if(videoOptions_) {
        av_dict_free(&videoOptions_);
    }
    if(audioOptions_) {
        av_dict_free(&audioOptions_);
    }

}

void CameraCapture::videoStart()
{

    dataVideoLoop();
}

void CameraCapture::audioStart()
{

    dataAudioLoop();
}

void CameraCapture::stopCapture()
{
    stopFlag_ = true;
}

int CameraCapture::interruptCallback(void *ctx)
{
    auto *self = static_cast<CameraCapture *>(ctx);
    return self->stopFlag_.load() ? 1 : 0;
}












