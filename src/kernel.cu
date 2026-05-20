#include <cuda_runtime.h>

__global__ void
hwc_to_chw_kernel(float *mode_input, const uint8_t *d_resize, int width, int height)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height)
        return;

    for (int c = 0; c < 3; c++) {
        int hwc_idx = y * width * 3 + x * 3 + c;
        int chw_idx = c * width * height + y * width + x;
        mode_input[chw_idx] = __uint2float_rn(d_resize[hwc_idx]) / 255.0f;
    }
}

__global__ void
refine_edge_kernel(float *refine_buffer, const float *mask_current_buffer, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    int midx = y * width + x;
    float alpha = mask_current_buffer[midx];
    if (alpha > 0.2f && alpha < 0.8f) {
        float t = (alpha - 0.2f) / 0.6f;
        refine_buffer[midx] = 0.2f + 0.6f * (3.0f * t * t - 2.0f * t * t * t);
    } else {
        refine_buffer[midx] = alpha;
    }
}

__global__ void
blend_frames_kernel(uint8_t *output, uint8_t *d_rgb, float *refine_buffer, uint8_t *blur_rgb, int width, int height) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    for (int c = 0; c < 3; c++) {
        int pidx = y * width + x;
        int bidx = y * width * 3 + x * 3 + c;
        float alpha = refine_buffer[pidx];
        float val = static_cast<float>(d_rgb[bidx]) * alpha
                  + static_cast<float>(blur_rgb[bidx]) * (1.0f - alpha);
        output[bidx] = static_cast<uint8_t>(val);
    }

}

extern "C"
void hwc_to_chw(float *mode_input, uint8_t *d_resize, int model_width, int model_height, cudaStream_t stream_) {
    dim3 blockDim(16, 16, 1);
    dim3 gridDim((model_width + blockDim.x - 1) / blockDim.x, (model_height + blockDim.y - 1) / blockDim.y);

    hwc_to_chw_kernel<<<gridDim, blockDim, 0, stream_>>>(mode_input, d_resize, model_width, model_height);
}

extern "C"
void refine_edge(float *refine_buffer, float *mask_current_buffer, int video_width, int video_height, cudaStream_t stream_) {
    dim3 blockDim(16, 16, 1);
    dim3 gridDim((video_width + blockDim.x - 1) / blockDim.x, (video_height + blockDim.y - 1) / blockDim.y);
    refine_edge_kernel<<<gridDim, blockDim, 0, stream_>>>(refine_buffer, mask_current_buffer, video_width, video_height);
}

extern "C"
void blend_frames(uint8_t *output, uint8_t *d_rgb, float *refine_buffer, uint8_t *blur_rgb, int video_width, int video_height, cudaStream_t stream_) {
    dim3 blockDim(16, 16, 1);
    dim3 gridDim((video_width + blockDim.x - 1) / blockDim.x, (video_height + blockDim.y - 1) / blockDim.y, 1);

    blend_frames_kernel<<<gridDim, blockDim, 0, stream_>>>(output, d_rgb, refine_buffer, blur_rgb, video_width, video_height);
}
