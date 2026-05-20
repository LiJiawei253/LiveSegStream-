#include "../include/Infra.h"
#include <fstream>
#include <vector>

bool Infra::laodEngine(const std::wstring& model_path)
{

    std::string model_path_str(model_path.begin(), model_path.end());


    std::ifstream engine_file(model_path_str, std::ios::binary);
    if (!engine_file) {
        std::cerr << "Error opening file " << model_path_str << std::endl;
        return false;
    }

    engine_file.seekg(0, std::ios::end);
    size_t engine_size = engine_file.tellg();
    engine_file.seekg(0, std::ios::beg);
    std::vector<char> engine_data(engine_size);
    engine_file.read(engine_data.data(), engine_size);
    engine_file.close();

    std::cout << "load engine_file data success" << std::endl;

    runtime_ = nvinfer1::createInferRuntime(logger_);
    if (!runtime_) {
        std::cerr << "Error creating infer runtime" << std::endl;
        return false;
    }

    engine_ = runtime_->deserializeCudaEngine(engine_data.data(), engine_size);
    if (!engine_) {
        std::cerr << "Error deserializing CudaEngine" << std::endl;
        return false;
    }

    std::cout << "engine create success" << std::endl;
    return true;
}


bool Infra::intialize()
{
    if (!engine_) {
        std::cerr << "There is no CudaEngine" << std::endl;
        return false;
    }

    context_ = engine_->createExecutionContext();
    if (!context_) {
        std::cerr << "Error creating CudaExecutionContext" << std::endl;
        return false;
    }

    int32_t tensors = engine_->getNbIOTensors();
    for (int i = 0; i < tensors; i++) {

            const char *tensorName = engine_->getIOTensorName(i);

            nvinfer1::TensorIOMode mode = engine_->getTensorIOMode(tensorName);

            nvinfer1::Dims dim = engine_->getTensorShape(tensorName);
            std::vector<int64_t> shape;
            for (int32_t j = 0; j < dim.nbDims; j++) {
                shape.push_back(dim.d[j]);
            }

            int64_t elemNums = 1;
            for (auto num : shape) {
                elemNums *= num;
            }

            void *buffer = nullptr;
            cudaMalloc(&buffer, elemNums * sizeof(float));

            if (mode == nvinfer1::TensorIOMode::kINPUT) {
                inputNames_.push_back(tensorName);
                inputShapes_.push_back(shape);
                inputBuffers_.push_back(buffer);
            } else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
                outputNames_.push_back(tensorName);
                outputShapes_.push_back(shape);
                outputBuffers_.push_back(buffer);
            }
    }

    cudaStreamCreate(&stream_);

    nppCtx_.hStream = stream_;
    nppCtx_.nCudaDeviceId = 0;
    cudaDeviceGetAttribute(&nppCtx_.nMultiProcessorCount, cudaDevAttrMultiProcessorCount, 0);
    cudaDeviceGetAttribute(&nppCtx_.nMaxThreadsPerMultiProcessor, cudaDevAttrMaxThreadsPerMultiProcessor, 0);
    cudaDeviceGetAttribute(&nppCtx_.nMaxThreadsPerBlock, cudaDevAttrMaxThreadsPerBlock, 0);
    cudaDeviceGetAttribute((int*)&nppCtx_.nSharedMemPerBlock, cudaDevAttrMaxSharedMemoryPerBlock, 0);
    cudaDeviceGetAttribute(&nppCtx_.nCudaDevAttrComputeCapabilityMajor, cudaDevAttrComputeCapabilityMajor, 0);
    cudaDeviceGetAttribute(&nppCtx_.nCudaDevAttrComputeCapabilityMinor, cudaDevAttrComputeCapabilityMinor, 0);

    context_->setTensorAddress(inputNames_.at(0).c_str(), inputBuffers_.at(0));
    context_->setTensorAddress(outputNames_.at(0).c_str(), outputBuffers_.at(0));

    return true;
}

bool Infra::getInformation() {
    //先读取一帧获取到frame的信息
    std::shared_ptr<FrameWithCaptureTime> frame_ptr;
    if (!videoFrameQueue_->get(frame_ptr)) {
        std::cerr << "videoFrameQueue_->get() failed" << std::endl;
        return false;
    }

    video_width_ = frame_ptr->frame->width;
    video_height_ = frame_ptr->frame->height;
    model_width_ = inputShapes_.at(0)[3];
    model_height_ = inputShapes_.at(0)[2];

    //计算各个平面的大小
    y_plane_ =  frame_ptr->frame->linesize[0] * video_height_;
    uv_plane_ =  frame_ptr->frame->linesize[1] * video_height_;
    int total_size = y_plane_ + uv_plane_ * 2;

    cudaMalloc(&d_original, total_size);
    cudaMalloc(&d_YUVToRGB, video_width_ * video_height_ * 3);
    cudaMalloc(&d_resize, model_width_ * model_height_ * 3);

    cudaMalloc(&mask_current_buffer, video_width_ * video_height_ * sizeof(float));
    cudaMalloc(&mask_previous_buffer, video_width_ * video_height_ * sizeof(float));
    cudaMalloc(&smooth_buffer, video_width_ * video_height_ * sizeof(float));
    cudaMalloc(&refine_buffer, video_width_ * video_height_ * sizeof(float));
    cudaMalloc(&blur_buffer, video_width_ * video_height_ * 3 * sizeof(uint8_t));
    cudaMalloc(&output_rgb, video_width_ * video_height_ * 3 * sizeof(uint8_t));
    cudaMemset(refine_buffer, 0, video_width_ * video_height_ * sizeof(float));
    videoFrameQueue_->put(frame_ptr);
    return true;
}


bool Infra::run()
{
    if (!getInformation()) {
        std::cout << "getInformation failed" << std::endl;
        return false;
    }

    Npp8u* pSrc[3];
    int nSrcStep[3];

    Npp8u* pDst[3];
    int nDstStep[3];

    int64_t start_time = 0;
    int64_t end_time = 0;
    int frame_count = 0;

    while (!videoFrameQueue_->getQuit()) {
        std::shared_ptr<FrameWithCaptureTime> frame_ptr;
        if (!videoFrameQueue_->get(frame_ptr)) {
            std::cerr << "videoFrameQueue_->get() empty" << std::endl;
            continue;
        }
        frame_count++;

        start_time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        std::cout << "从第" << frame_ptr->index << "帧视频帧被捕捉，到达infra推理模块的时间延迟是" << (start_time - frame_ptr->captureTime) / 1000 << "ms" << std::endl;

        cudaMemcpyAsync(d_original, frame_ptr->frame->data[0], y_plane_, cudaMemcpyHostToDevice, stream_ );
        cudaMemcpyAsync(d_original + y_plane_, frame_ptr->frame->data[1], uv_plane_, cudaMemcpyHostToDevice, stream_ );
        cudaMemcpyAsync(d_original + y_plane_ + uv_plane_, frame_ptr->frame->data[2], uv_plane_, cudaMemcpyHostToDevice, stream_ );


        // Y 平面
        pSrc[0] = d_original;
        nSrcStep[0] = frame_ptr->frame->linesize[0];

        // U 平面：Y 平面总大小 = linesize[0] * height
        pSrc[1] = d_original + frame_ptr->frame->linesize[0] * frame_ptr->frame->height;
        nSrcStep[1] = frame_ptr->frame->linesize[1];

        // V 平面：U 平面总大小 = linesize[1] * height
        pSrc[2] = pSrc[1] + frame_ptr->frame->linesize[1] * frame_ptr->frame->height;
        nSrcStep[2] = frame_ptr->frame->linesize[2];
        nppiYUV422ToRGB_8u_P3C3R_Ctx(pSrc, nSrcStep, d_YUVToRGB, video_width_ * 3, {video_width_, video_height_}, nppCtx_);

        NppiSize srcVideoSize = {video_width_, video_height_};
        NppiSize dstVideoSize = {model_width_, model_height_};
        NppiRect srcVideoRoi = {0, 0, video_width_, video_height_};
        NppiRect dstVideoRoi = {0, 0, model_width_, model_height_};

        if (frame_count % 3 == 0) {
            nppiResize_8u_C3R_Ctx(d_YUVToRGB, video_width_ * 3 , srcVideoSize, srcVideoRoi,
                                  d_resize, model_width_ * 3,dstVideoSize, dstVideoRoi,
                                  NPPI_INTER_CUBIC, nppCtx_);

            hwc_to_chw(static_cast<float*>(inputBuffers_.at(0)), d_resize, model_width_, model_height_, stream_);

            context_->enqueueV3(stream_);

            nppiResize_32f_C1R_Ctx(static_cast<float*>(outputBuffers_.at(0)), model_width_ * sizeof(float), dstVideoSize, dstVideoRoi,
                                    mask_current_buffer, video_width_ * sizeof(float), srcVideoSize, srcVideoRoi,
                                    NPPI_INTER_CUBIC, nppCtx_);

            nppiAlphaCompC_32f_C1R_Ctx(mask_current_buffer, video_width_ * sizeof(float), 0.7f,
                                       mask_previous_buffer, video_width_ * sizeof(float), 1 - 0.7f,
                                       smooth_buffer, video_width_ * sizeof(float), {video_width_, video_height_},
                                       NPPI_OP_ALPHA_OVER, nppCtx_);
            cudaMemcpyAsync(mask_previous_buffer, mask_current_buffer, video_width_ * video_height_ * sizeof(float), cudaMemcpyDeviceToDevice, stream_);


            refine_edge(refine_buffer, smooth_buffer, video_width_, video_height_, stream_);
        }
        nppiFilterBox_8u_C3R_Ctx(d_YUVToRGB, video_width_ * 3,
                                     blur_buffer,   video_width_ * 3,
                                     {video_width_, video_height_},
                                     {15, 15}, {25, 25}, nppCtx_);

        blend_frames(output_rgb, d_YUVToRGB, refine_buffer, blur_buffer, video_width_, video_height_, stream_);

        pDst[0] = d_original;
        nDstStep[0] = frame_ptr->frame->linesize[0];

        pDst[1] = d_original + y_plane_;
        nDstStep[1] = frame_ptr->frame->linesize[1];

        pDst[2] = d_original + y_plane_ + uv_plane_;
        nDstStep[2] = frame_ptr->frame->linesize[2];

        nppiRGBToYUV422_8u_C3P3R_Ctx(output_rgb,  video_width_ * 3, pDst, nDstStep, {video_width_, video_height_}, nppCtx_);


        cudaMemcpyAsync(frame_ptr->frame->data[0], d_original, y_plane_, cudaMemcpyDeviceToHost, stream_);
        cudaMemcpyAsync(frame_ptr->frame->data[1], d_original + y_plane_, uv_plane_, cudaMemcpyDeviceToHost, stream_);
        cudaMemcpyAsync(frame_ptr->frame->data[2], d_original + y_plane_ + uv_plane_, uv_plane_, cudaMemcpyDeviceToHost, stream_);

        cudaStreamSynchronize(stream_);


        end_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::cout << "第" << frame_ptr->index <<"帧离开infra模块的时间是" << (end_time - frame_ptr->captureTime) / 1000 << "ms" << std::endl;

        if (!videoInfraFrameQueue_->put(frame_ptr)) {
            std::cerr << "videoInfraFrameQueue_->put() failed" << std::endl;
            return false;
        }
    }

    return true;
}
