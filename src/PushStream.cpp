#include "../include/PushStream.h"
extern "C" {
#include "libavutil/time.h"
}

bool PushStream::open(const char *video_device, const char *audio_device, const char *url, const std::wstring& model_path)
{
     if (!coder_->videoHWEncoder()) {
         av_log(nullptr, AV_LOG_ERROR, "配置视频硬件编码器失败\n");
         return false;
     }
     AVCodecContext *videoEncoderCtx_ = coder_->getVideoHWEncoderContext();
     if (!videoEncoderCtx_) {
         av_log(nullptr, AV_LOG_ERROR, "获取视频硬件编码器失败\n");
         return false;
     }

     if (!infra_->laodEngine(model_path)) {
         av_log(nullptr, AV_LOG_ERROR, "模型预设置失败\n");
         return false;
     }

     if (!infra_->intialize()) {
         av_log(nullptr, AV_LOG_ERROR, "model initialize failed\n");
         return false;
     }

     if(!coder_->audioEncoder()) {
         av_log(nullptr, AV_LOG_ERROR, "配置音频编码器失败\n");
         return false;
     }
     AVCodecContext *audioEncoderCtx_ = coder_->getAudioEncoderContext();
     if(!audioEncoderCtx_) {
         av_log(nullptr, AV_LOG_ERROR, "获取音频编码器上下文失败\n");
         return false;
     }

    if( !camera_->open_video(video_device)) {
        av_log(nullptr, AV_LOG_ERROR, "打开摄像头设备失败\n");
        return false;
    }

    if( !camera_->open_audio(audio_device)) {
        av_log(nullptr, AV_LOG_ERROR, "打开音频设备失败\n");
        return false;
    }

    if( !flvMuxer_->prepareOUTPUT(videoEncoderCtx_, audioEncoderCtx_, url)) {
        av_log(nullptr, AV_LOG_ERROR, "打开输出网络流失败\n");
        return false;
    }

    return true;
}

void PushStream::start()
{
    captureVideoData_ = std::thread(&CameraCapture::videoStart, camera_.get());
    captureAudioData_ = std::thread(&CameraCapture::audioStart, camera_.get());
    infraVideoData_ = std::thread(&Infra::run, infra_.get());
    encoderVideoData_ = std::thread(&Coder::encoderVideoData, coder_.get());
    encoderAudioData_ = std::thread(&Coder::encoderAudioData, coder_.get());
    sendVideoAndAudioData_ = std::thread(&FlvMuxer::sendVideoAndAudio, flvMuxer_.get(), coder_->getVideoHWEncoderContext(), coder_->getAudioEncoderContext());
}

void PushStream::stop()
{
    camera_->stopCapture();
    videoFrameQue_->setDone();
    audioFrameQue_->setDone();

    if(captureVideoData_.joinable()) captureVideoData_.join();
    if(captureAudioData_.joinable()) captureAudioData_.join();

    if (infraVideoData_.joinable()) infraVideoData_.join();

    videoInfraFrameQue_->setDone();

    if(encoderVideoData_.joinable()) encoderVideoData_.join();
    if(encoderAudioData_.joinable()) encoderAudioData_.join();

    videoPacketQue_->setDone();
    audioPacketQue_->setDone();

    if(sendVideoAndAudioData_.joinable()) sendVideoAndAudioData_.join();
}









