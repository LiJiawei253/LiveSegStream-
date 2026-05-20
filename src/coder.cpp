#include "../include/coder.h"

const uint8_t Coder::kUUID[16] = {
    0x3f, 0xdb, 0x50, 0xc0, 0x18, 0x87, 0x41, 0x05,
    0x8f, 0x62, 0xdc, 0x06, 0x71, 0xe5, 0x43, 0x98
};

bool Coder::videoEncoder()
{
    int ret = -1;

    videoEncoder_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!videoEncoder_) {
        av_log(nullptr, AV_LOG_ERROR, "查找h264编码器失败\n");
        return false;
    }

    videoEncoderCtx_ = avcodec_alloc_context3(videoEncoder_);
    if(!videoEncoderCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "分配编码器上下文失败\n");
        return false;
    }

    videoEncoderCtx_->width = 640;
    videoEncoderCtx_->height = 480;
    videoEncoderCtx_->time_base = {1, 1000000};
    videoEncoderCtx_->framerate = {15, 1};
    videoEncoderCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    videoEncoderCtx_->bit_rate = 400000;
    videoEncoderCtx_->gop_size = 15;
    videoEncoderCtx_->max_b_frames = 0;
    videoEncoderCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_set(videoEncoderCtx_->priv_data, "preset", "ultrafast", 0);
    av_opt_set(videoEncoderCtx_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(videoEncoderCtx_->priv_data, "profile", "baseline", 0);
    av_opt_set(videoEncoderCtx_->priv_data, "threads", "4", 0);

    ret = avcodec_open2(videoEncoderCtx_, videoEncoder_, nullptr);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "编码器打开失败:%s\n", error_buf);
        return false;
    }

    return true;
}

bool Coder::videoHWEncoder()
{
    int ret = -1;

    const AVHWDeviceType type = av_hwdevice_find_type_by_name("cuda");
    if(type == AV_HWDEVICE_TYPE_NONE) {
        av_log(nullptr, AV_LOG_ERROR, "硬件查找失败\n");
        return false;
    }

    ret = av_hwdevice_ctx_create(&hwDeviceCtx_, type, nullptr, nullptr, 0);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "创建cuda设备上下文失败:%s\n", error_buf);
        return false;
    }

    videoHWEncoder_ = avcodec_find_encoder_by_name("h264_nvenc");
    if (!videoHWEncoder_) {
        av_log(nullptr, AV_LOG_ERROR, "videoHWEncoder_ failed\n");
        return false;
    }

    videoHWEncoderCtx_ = avcodec_alloc_context3(videoHWEncoder_);
    if (!videoHWEncoderCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "videoHWEncoderCtx_ failed\n");
        return false;
    }

    videoHWEncoderCtx_->width = 1280;
    videoHWEncoderCtx_->height = 720;
    videoHWEncoderCtx_->time_base = {1, 1000000};
    videoHWEncoderCtx_->framerate = {30, 1};
    videoHWEncoderCtx_->bit_rate = 2000000;     //编码器的平均码率
    videoHWEncoderCtx_->rc_min_rate = 2000000;    //编码器的最低码率
    videoHWEncoderCtx_->rc_max_rate = 2000000;    //编码器的最高码率
    videoHWEncoderCtx_->rc_buffer_size = 200000;  //锁死VBV Buffer，防止VBV Buffer缓存造成过大延迟,vbv buffer = 码率 x 0.1秒
    videoHWEncoderCtx_->gop_size = 15;
    videoHWEncoderCtx_->max_b_frames = 0;
    videoHWEncoderCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    

    videoHWEncoderCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    av_opt_set(videoHWEncoderCtx_->priv_data, "rc", "cbr", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "cbr", "true", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "zerolatency", "1", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "delay", "0", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "rc_lookahead", "0", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "surfaces", "4", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "preset", "p1", 0);
    av_opt_set(videoHWEncoderCtx_->priv_data, "tune", "ull", 0);
    // 启用按大小分片（关键！）
    av_opt_set(videoHWEncoderCtx_->priv_data, "slice_mode", "3", 0);      // 3=按最大切片大小分片
    av_opt_set(videoHWEncoderCtx_->priv_data, "slice_max_size", "1400", 0); // 单切片最大1400字节

    videoHWEncoderCtx_->pix_fmt = AV_PIX_FMT_CUDA;


    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hwDeviceCtx_);
    if (!hw_frames_ref) {
        av_log(nullptr, AV_LOG_ERROR, "av_hwframe_ctx_alloc 失败\n");
        return false;
    }
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ref->data;
    frames_ctx->format    = AV_PIX_FMT_CUDA;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width     = videoHWEncoderCtx_->width;
    frames_ctx->height    = videoHWEncoderCtx_->height;
    frames_ctx->initial_pool_size = 4;

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "av_hwframe_ctx_init 失败: %s\n", error_buf);
        av_buffer_unref(&hw_frames_ref);
        return false;
    }
    videoHWEncoderCtx_->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    av_buffer_unref(&hw_frames_ref);
    fprintf(stderr, "[VIDEO_HW] hw_frames_ctx 已创建 (pool_size=4, NV12)\n");

    ret = avcodec_open2(videoHWEncoderCtx_, videoHWEncoder_, nullptr);
    if (ret < 0) {
        char error_buf[256];
        av_strerror(ret, error_buf, sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "avcodec_open2 failed: %s\n", error_buf);
        return false;
    }
    return true;
}

bool Coder::swsInit(std::shared_ptr<AVFrame> frame_ptr)
{
    AVPixelFormat dst_fmt = AV_PIX_FMT_NV12;
    if (videoHWEncoderCtx_->hw_frames_ctx) {
        AVHWFramesContext *fc = (AVHWFramesContext *)videoHWEncoderCtx_->hw_frames_ctx->data;
        dst_fmt = fc->sw_format;
    } else if (videoHWEncoderCtx_->pix_fmt != AV_PIX_FMT_CUDA) {
        dst_fmt = videoHWEncoderCtx_->pix_fmt;
    }

    swsCtx_ = sws_getContext(frame_ptr->width, frame_ptr->height, (AVPixelFormat)frame_ptr->format,
                            videoHWEncoderCtx_->width, videoHWEncoderCtx_->height, dst_fmt,
                            SWS_BICUBIC,
                            nullptr, nullptr, nullptr);
    if(!swsCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "swsCtx_ 分配上下文失败\n");
        return false;
    }

    fprintf(stderr, "[VIDEO_ENC] swsInit: %dx%d fmt=%d → %dx%d fmt=%d\n",
            frame_ptr->width, frame_ptr->height, frame_ptr->format,
            videoHWEncoderCtx_->width, videoHWEncoderCtx_->height, dst_fmt);
    return true;
}

bool Coder::swrInit(std::shared_ptr<AVFrame> frame_ptr)
{
    int ret = -1;
    swrCtx_ = swr_alloc();
    if(!swrCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "分配音频重采样上下文失败\n");
        return false;
    }

    ret = swr_alloc_set_opts2(&swrCtx_, &audioEncoderCtx_->ch_layout,  AV_SAMPLE_FMT_FLTP, audioEncoderCtx_->sample_rate,
                                        &frame_ptr->ch_layout, (AVSampleFormat)frame_ptr->format, frame_ptr->sample_rate,
                                        0, nullptr);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "音频重采样打开失败:%s\n", error_buf);
        return false;
    }

    ret = swr_init(swrCtx_);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "音频重采样初始化失败:%s\n", error_buf);
        return false;
    }
    return true;
}

bool Coder::audioEncoder()
{
    int ret = -1;

    audioEncoder_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!audioEncoder_) {
        av_log(nullptr, AV_LOG_ERROR, "查找编码器失败\n");
        return false;
    }

    audioEncoderCtx_ = avcodec_alloc_context3(audioEncoder_);
    if(!audioEncoderCtx_) {
        av_log(nullptr, AV_LOG_ERROR, "分配编码器上下文失败\n");
        return false;
    }

    audioEncoderCtx_->sample_rate = 32000;
    audioEncoderCtx_->time_base = {1, audioEncoderCtx_->sample_rate};
    audioEncoderCtx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audioEncoderCtx_->bit_rate = 96000;
    av_channel_layout_default(&audioEncoderCtx_->ch_layout, 1);
    audioEncoderCtx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(audioEncoderCtx_, audioEncoder_, nullptr);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "编码器打开失败:%s\n", error_buf);
        return false;
    }

    audioFifo_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 1, 1024 * 10);
    if(!audioFifo_ ) {
        av_log(nullptr, AV_LOG_ERROR, "audio fifo分配内存失败\n");
        return false;
    }
    
    return true;
}


AVFrame* Coder::videoConvert(std::shared_ptr<AVFrame> frame_ptr)
{
    int ret = -1;

    AVPixelFormat sw_fmt = AV_PIX_FMT_NV12;
    if (videoHWEncoderCtx_->hw_frames_ctx) {
        AVHWFramesContext *fc = (AVHWFramesContext *)videoHWEncoderCtx_->hw_frames_ctx->data;
        sw_fmt = fc->sw_format;
    }

    AVFrame *cpuFrame = av_frame_alloc();
    if (!cpuFrame) {
        av_log(nullptr, AV_LOG_ERROR, "av_frame_alloc 失败\n");
        return nullptr;
    }
    cpuFrame->format = sw_fmt;
    cpuFrame->width  = videoHWEncoderCtx_->width;
    cpuFrame->height = videoHWEncoderCtx_->height;

    ret = av_frame_get_buffer(cpuFrame, 32);
    if (ret < 0) {
        av_frame_free(&cpuFrame);
        return nullptr;
    }

    ret = sws_scale(swsCtx_, (const uint8_t * const *)frame_ptr->data, frame_ptr->linesize,
                    0, frame_ptr->height, cpuFrame->data, cpuFrame->linesize);
    if (ret < 0) {
        av_frame_free(&cpuFrame);
        return nullptr;
    }

    if (videoHWEncoderCtx_->hw_frames_ctx) {
        AVFrame *hwFrame = av_frame_alloc();
        if (!hwFrame) {
            av_frame_free(&cpuFrame);
            return nullptr;
        }

        ret = av_hwframe_get_buffer(videoHWEncoderCtx_->hw_frames_ctx, hwFrame, 0);
        if (ret < 0) {
            av_frame_free(&hwFrame);
            av_frame_free(&cpuFrame);
            return nullptr;
        }

        ret = av_hwframe_transfer_data(hwFrame, cpuFrame, 0);
        av_frame_free(&cpuFrame);
        if (ret < 0) {
            av_frame_free(&hwFrame);
            return nullptr;
        }

        return hwFrame;
    }

    return cpuFrame;
}

bool Coder::encoderVideoData()
{
    int ret = -1;
    std::shared_ptr<FrameWithCaptureTime> frameWithTime;
    int64_t captureTime = 0;
    int64_t frameIndex = 0;
    int pakcet_count = 0;
    fprintf(stderr, "[VIDEO_ENC] 视频编码线程已启动，使用全局时钟\n");

    while(videoInfraFrameQue_->get(frameWithTime)) {
        std::cout << "从第" << frameWithTime->index << "帧被捕捉，到帧到达编码器的延迟是" << (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - frameWithTime->captureTime) / 1000 << "ms" << std::endl;

        if(videoFrame_ == nullptr) {
            int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
            clock_->initGlobalClock(now);
            videoFrame_ = frameWithTime->frame.get();
            fprintf(stderr, "[VIDEO_ENC] 收到第一帧: %dx%d fmt=%d 建立全局时钟\n",
                    frameWithTime->frame->width, frameWithTime->frame->height,
                    frameWithTime->frame->format);
            if (frameWithTime->frame->format != AV_PIX_FMT_CUDA) {
                if (!swsInit(frameWithTime->frame)) {
                    fprintf(stderr, "[VIDEO_ENC] swsInit失败，线程退出\n");
                    return false;
                }
            }
        }

        AVFrame *frame = nullptr;
        bool frameCudaDirect = (frameWithTime->frame->format == AV_PIX_FMT_CUDA);
        captureTime = frameWithTime->captureTime;

        if (frameCudaDirect) {
            frame = av_frame_clone(frameWithTime->frame.get());
        } else {
            frame = videoConvert(frameWithTime->frame);
            if(!frame) {
                fprintf(stderr, "[VIDEO_ENC] videoConvert失败，线程退出\n");
                return false;
            }
        }

        frame->pts = clock_->getGlobalClockUs();

        ret = avcodec_send_frame(videoHWEncoderCtx_, frame);
        av_frame_free(&frame);
        if (ret < 0) {
            fprintf(stderr, "[VIDEO_ENC] avcodec_send_frame失败 ret=%d，线程退出\n", ret);
            return false;
        }
        while (ret >= 0) {
            AVPacket *pkt = av_packet_alloc();
            ret = avcodec_receive_packet(videoHWEncoderCtx_, pkt);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_packet_free(&pkt);
                ret = 0;
                break;
            } else if (ret < 0) {
                av_packet_free(&pkt);
                return false;
            }

            if (frameIndex < 10 || frameIndex % 30 == 0) {
                fprintf(stderr, "[VIDEO_ENC] 产出包 #%lld pts=%lld dts=%lld\n",
                        static_cast<long long>(frameIndex),
                        static_cast<long long>(pkt->pts),
                        static_cast<long long>(pkt->dts));
            }
            frameIndex++;
            pakcet_count++;

            // ----- 构建 SEI NAL -----
            uint8_t sei_rbsp[28];
            int sei_len = 0;
            sei_rbsp[sei_len++] = 0x06;  // NAL type = SEI
            sei_rbsp[sei_len++] = 0x05;  // payload_type = user_data_unregistered
            sei_rbsp[sei_len++] = 24;    // payload_size = 16(UUID) + 8(timestamp)
            memcpy(sei_rbsp + sei_len, kUUID, 16); sei_len += 16;
            memcpy(sei_rbsp + sei_len, &captureTime, 8); sei_len += 8;
            sei_rbsp[sei_len++] = 0x80;  // rbsp_stop_one_bit

            // ----- 将 SEI 以 Annex B 格式插入 packet 最前面 -----
            // NVENC 输出 Annex B (起始码) 而非 AVCC (长度前缀)
            int orig_size = pkt->size;
            int64_t saved_pts = pkt->pts;
            int64_t saved_dts = pkt->dts;
            int64_t saved_duration = pkt->duration;
            int saved_flags = pkt->flags;

            // Annex B 起始码 00 00 00 01
            static const uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};

            uint8_t *orig_copy = (uint8_t*)av_malloc(orig_size);
            if (orig_copy) {
                memcpy(orig_copy, pkt->data, orig_size);
                av_packet_unref(pkt);

                int new_size = 4 + sei_len + orig_size;
                av_new_packet(pkt, new_size);
                pkt->pts = saved_pts;
                pkt->dts = saved_dts;
                pkt->duration = saved_duration;
                pkt->flags = saved_flags;

                memcpy(pkt->data, kStartCode, 4);
                memcpy(pkt->data + 4, sei_rbsp, sei_len);
                memcpy(pkt->data + 4 + sei_len, orig_copy, orig_size);
                av_free(orig_copy);
            }

            // 验证：前 5 帧 hex dump，确认 SEI 已写入
            if (pakcet_count <= 5) {
                fprintf(stderr, "[VIDEO_ENC] pkt#%d size=%d hex: ",
                        pakcet_count + 1, pkt->size);
                for (int i = 0; i < 36 && i < pkt->size; i++)
                    fprintf(stderr, "%02X ", pkt->data[i]);
                fprintf(stderr, "\n");
            }

            std::shared_ptr<AVPacket> packet(pkt, [](AVPacket *p){
                if(p) av_packet_free(&p);
            });
            std::shared_ptr<PacketWithCaptureTime> packet_ptr = std::make_shared<PacketWithCaptureTime>(packet, captureTime, frameIndex);

            videoPacketQue_->put(packet_ptr);
            std::cout << "从第" << packet_ptr->index << "帧被捕捉，到帧离开编码器的延迟是" << (std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()  - frameWithTime->captureTime) / 1000 << "ms" << std::endl;
        }
    }

    avcodec_send_frame(videoHWEncoderCtx_, nullptr);
    {
        int flush_ret = 0;
        while (flush_ret >= 0) {
            AVPacket *pkt = av_packet_alloc();
            flush_ret = avcodec_receive_packet(videoHWEncoderCtx_, pkt);
            if (flush_ret == AVERROR_EOF || flush_ret == AVERROR(EAGAIN)) {
                av_packet_free(&pkt);
                break;
            } else if (flush_ret < 0) {
                av_packet_free(&pkt);
                break;
            }
            if (pkt->pts == AV_NOPTS_VALUE) pkt->pts = frameIndex;
            if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = frameIndex;
            frameIndex++;
            pakcet_count++;
            std::shared_ptr<AVPacket> packet(pkt, [](AVPacket *p){ av_packet_free(&p); });
            std::shared_ptr<PacketWithCaptureTime> packet_ptr = std::make_shared<PacketWithCaptureTime>(packet, captureTime, frameIndex);
            videoPacketQue_->put(packet_ptr);
        }
    }
    fprintf(stderr, "[VIDEO_ENC] 编码线程退出\n");
    return true;
}

bool Coder::encoderAudioData()
{

    int ret = -1;
    std::shared_ptr<FrameWithCaptureTime> frameWithTime;
    int64_t captureTime = 0;
    int64_t audioPktCount = 0;

    const int encFrameSize = audioEncoderCtx_->frame_size > 0 ? audioEncoderCtx_->frame_size : 1024;

    std::vector<uint8_t> leftoverBuf;
    int leftoverSamples = 0;  // leftoverBuf 中有效的样本数

    fprintf(stderr, "[AUDIO_ENC] 音频编码线程已启动，frame_size=%d sampleRate=%d\n",
            encFrameSize, audioEncoderCtx_->sample_rate);

    while(audioFrameQue_->get(frameWithTime)) {
        if(audioFrame_ == nullptr) {
            // 首帧初始化全局时钟（如果视频还没初始化的话）
            if(!clock_->isGlobalClockStarted()) {
                int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
                clock_->initGlobalClock(now);
                fprintf(stderr, "[AUDIO_ENC] 音频首帧建立全局时钟\n");
            }
            audioFrame_ = frameWithTime->frame.get();
            if (!swrInit(frameWithTime->frame)) {
                fprintf(stderr, "[AUDIO_ENC] swrInit失败，线程退出\n");
                return false;
            }
        }

        AVFrame *frame = audioConvert(frameWithTime->frame);
        if(!frame) {
            fprintf(stderr, "[AUDIO_ENC] 格式转换失败\n");
            return false;
        }
        captureTime = frameWithTime->captureTime;

        int newSamples = frame->nb_samples;
        int bps = av_get_bytes_per_sample((AVSampleFormat)frame->format);

        // 将新样本追加到 leftoverBuf
        size_t oldSize = leftoverBuf.size();
        leftoverBuf.resize(oldSize + newSamples * bps);
        memcpy(&leftoverBuf.at(oldSize), frame->data[0], newSamples * bps);
        leftoverSamples += newSamples;
        av_frame_free(&frame);

        while (leftoverSamples >= encFrameSize) {
            int64_t pts_aac = clock_->getGlobalClockUs() * audioEncoderCtx_->sample_rate / 1000000;

            AVFrame *pkt_frame = av_frame_alloc();
            pkt_frame->format = AV_SAMPLE_FMT_FLTP;
            av_channel_layout_default(&pkt_frame->ch_layout, 1);
            pkt_frame->sample_rate = audioEncoderCtx_->sample_rate;
            pkt_frame->nb_samples = encFrameSize;
            ret = av_frame_get_buffer(pkt_frame, 0);
            if (ret < 0) {
                av_frame_free(&pkt_frame);
                return false;
            }

            memcpy(pkt_frame->data[0], &leftoverBuf.at(0), encFrameSize * bps);
            // 只有 leftoverSamples > encFrameSize 时才需要 memmove 移位
            if (leftoverSamples > encFrameSize) {
                memmove(&leftoverBuf.at(0), &leftoverBuf.at(encFrameSize * bps), (leftoverSamples - encFrameSize) * bps);
                leftoverBuf.resize((leftoverSamples - encFrameSize) * bps);
            } else {
                leftoverBuf.resize(0);
            }
            leftoverSamples -= encFrameSize;

            pkt_frame->pts = pts_aac;

            if (audioPktCount <= 10 || audioPktCount % 30 == 0) {
                fprintf(stderr, "[AUDIO_ENC] 送入编码第 %lld 帧 pts=%lld globalUs=%lld\n",
                        static_cast<long long>(audioPktCount + 1),
                        static_cast<long long>(pts_aac),
                        static_cast<long long>(clock_->getGlobalClockUs()));
            }

            audioPktCount++;

            ret = avcodec_send_frame(audioEncoderCtx_, pkt_frame);
            av_frame_free(&pkt_frame);
            if(ret < 0) return false;

            while(ret >= 0) {
                AVPacket *pkt = av_packet_alloc();
                ret = avcodec_receive_packet(audioEncoderCtx_, pkt);
                if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_packet_free(&pkt);
                    ret = 0;
                    break;
                } else if (ret < 0) {
                    av_packet_free(&pkt);
                    return false;
                }

                if (pkt->pts == AV_NOPTS_VALUE) {
                    pkt->pts = pts_aac;
                    pkt->dts = pkt->pts;
                }

                if (audioPktCount <= 10 || audioPktCount % 30 == 0) {
                    fprintf(stderr, "[AUDIO_ENC] 产出包 pts=%lld dts=%lld\n",
                            static_cast<long long>(pkt->pts),
                            static_cast<long long>(pkt->dts));
                }

                std::shared_ptr<AVPacket> packet(pkt, [](AVPacket *p){
                    if(p) av_packet_free(&p);
                });

                std::shared_ptr<PacketWithCaptureTime> packet_ptr = std::make_shared<PacketWithCaptureTime>(packet, captureTime, audioPktCount);

                audioPacketQue_->put(packet_ptr);
            }
        }
    }

    avcodec_send_frame(audioEncoderCtx_, nullptr);
    {
        int flush_ret = 0;
        int64_t flushPktIdx = audioPktCount;  // 从当前计数继续
        while (flush_ret >= 0) {
            AVPacket *pkt = av_packet_alloc();
            flush_ret = avcodec_receive_packet(audioEncoderCtx_, pkt);
            if (flush_ret == AVERROR_EOF || flush_ret == AVERROR(EAGAIN)) {
                av_packet_free(&pkt);
                break;
            } else if (flush_ret < 0) {
                av_packet_free(&pkt);
                break;
            }
            if (pkt->pts == AV_NOPTS_VALUE) {
                pkt->pts = flushPktIdx * encFrameSize;
                pkt->dts = pkt->pts;
                flushPktIdx++;
            }
            std::shared_ptr<AVPacket> packet(pkt, [](AVPacket *p){ av_packet_free(&p); });

            std::shared_ptr<PacketWithCaptureTime> packet_ptr = std::make_shared<PacketWithCaptureTime>(packet, captureTime, audioPktCount);
            audioPacketQue_->put(packet_ptr);
        }
    }

    fprintf(stderr, "[AUDIO_ENC] 编码线程退出，共产出 %lld 包\n",
            static_cast<long long>(audioPktCount));
    return true;
}


AVFrame* Coder::audioConvert(std::shared_ptr<AVFrame> frame_ptr)
{
    int ret = -1;

    int out_nb_samples = frame_ptr->nb_samples;

    AVFrame *swrFrame = av_frame_alloc();
    swrFrame->format = audioEncoderCtx_->sample_fmt;  // FLTP
    swrFrame->sample_rate = frame_ptr->sample_rate;  // 保持 16kHz，不重采样
    swrFrame->ch_layout = audioEncoderCtx_->ch_layout;
    swrFrame->nb_samples = out_nb_samples;

    ret = av_frame_get_buffer(swrFrame, 0);
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "分配frame buffer失败:%s\n", error_buf);
        return nullptr;
    }

    // S16 → FLTP 格式转换，采样率不变
    ret = swr_convert(swrCtx_,
                swrFrame->data, swrFrame->nb_samples,                    // 输出 buffer
                (const uint8_t *const *)frame_ptr->data, frame_ptr->nb_samples);  // 输入数据
    if(ret < 0){
        char error_buf[256];
        av_strerror(ret, error_buf,sizeof(error_buf));
        av_log(nullptr, AV_LOG_ERROR, "音频格式转换失败:%s\n", error_buf);
        av_frame_free(&swrFrame);
        return nullptr;
    }

    swrFrame->nb_samples = ret;
    return swrFrame;
}








