#pragma once
#include <memory>
#include <chrono>
#include <windows.h>

#include "NvInfer.h"
#include "NvOnnxParser.h"
#include "cuda_runtime.h"
#include "npp.h"

#include "../include/ThreadSafeQueue.h"
#include "../include/CameraCapture.h"

extern "C"{
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"

}

extern "C"
void hwc_to_chw(
    float *mode_input,
    uint8_t *d_resize,
    int model_width,
    int model_height,
    cudaStream_t stream_
);

extern "C"
void refine_edge(
    float *refine_buffer,
    float *mask_current_buffer,
    int video_width,
    int video_height,
    cudaStream_t stream_
);

extern "C"
void blend_frames(
    uint8_t *output,
    uint8_t *d_original,
    float *refine_buffer,
    uint8_t *blur_buffer,
    int video_width,
    int video_height,
    cudaStream_t stream_
);

// TensorRT Logger 实现 (TensorRT 10 需要)
class TRTLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            fprintf(stderr, "[TensorRT] %s\n", msg);
        }
    }
};

class Infra {
public:
    Infra(std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQueue,
        std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoInfraFrameQueue,
        int inferSize = 1024, int skipInterval = 3)
        : videoFrameQueue_(videoFrameQueue),
          videoInfraFrameQueue_(videoInfraFrameQueue)
    {}

    ~Infra() {

        if (context_) delete context_;
        // engine 由 runtime 拥有，delete runtime 时自动释放，不要单独 delete engine_
        if (runtime_) delete runtime_;

        cudaFree(d_original);
        cudaFree(d_YUVToRGB);
        cudaFree(d_resize);
        cudaFree(mask_current_buffer);
        cudaFree(mask_previous_buffer);
        cudaFree(smooth_buffer);
        cudaFree(refine_buffer);
        cudaFree(blur_buffer);
        cudaFree(output_rgb);

        cudaStreamDestroy(stream_);

    }

    bool laodEngine(const std::wstring& model_path);
    bool intialize();
    bool getInformation();
    bool run();

private:


    TRTLogger logger_;
    nvinfer1::IRuntime* runtime_ = nullptr;
    nvinfer1::ICudaEngine* engine_ = nullptr;
    nvinfer1::IExecutionContext* context_ = nullptr;
    cudaStream_t stream_ = nullptr;
    NppStreamContext nppCtx_ = {};

    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<std::vector<int64_t>> inputShapes_;
    std::vector<std::vector<int64_t>> outputShapes_;
    std::vector<void*> inputBuffers_;
    std::vector<void*> outputBuffers_;

    int video_width_;
    int video_height_;
    int model_width_;
    int model_height_;
    int y_plane_;
    int uv_plane_;

    uint8_t *d_original = nullptr;
    uint8_t *d_YUVToRGB = nullptr;
    uint8_t *d_resize = nullptr;
    float *mask_current_buffer = nullptr;
    float *mask_previous_buffer = nullptr;
    float *smooth_buffer = nullptr;
    float *refine_buffer = nullptr;
    uint8_t *blur_buffer = nullptr;
    uint8_t *output_rgb = nullptr;


    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoFrameQueue_;
    std::shared_ptr<ThreadSafeQueue<std::shared_ptr<FrameWithCaptureTime>>> videoInfraFrameQueue_;
};
