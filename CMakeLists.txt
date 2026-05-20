cmake_minimum_required(VERSION 3.20)
project(myRT)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# ==============================
# 路径配置（已无空格）
# ==============================
set(FFMPEG_DIR "E:/work/ffmpeg")
set(CUDA_DIR "E:/work/AIInfra/cuda_toolkit")
set(OpenCV_DIR "E:/work/opencv_for_MSVC/opencv/build")
set(TENSORRT_DIR "E:/work/AIInfra/TensorRT-10.14.1.48")

# ==============================
# CUDA 核心配置
# ==============================
set(CUDAToolkit_ROOT ${CUDA_DIR})
set(CMAKE_CUDA_COMPILER "${CUDA_DIR}/bin/nvcc.exe")
set(CMAKE_CUDA_ARCHITECTURES 75)
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

# ==============================
# 包含目录
# ==============================
include_directories(${FFMPEG_DIR}/include)
include_directories(${CUDA_DIR}/include)
include_directories(${TENSORRT_DIR}/include)
include_directories(${OpenCV_INCLUDE_DIRS})

# ==============================
# 库目录
# ==============================
link_directories(${FFMPEG_DIR}/lib)
link_directories(${CUDA_DIR}/lib/x64)
link_directories(${TENSORRT_DIR}/lib)

# ==============================
# 编译目标
# ==============================
file(GLOB SOURCE_FILE src/*.cpp)
add_executable(myRT ${SOURCE_FILE} src/kernel.cu
)

# 【关键修复】给目标设置 CUDA 属性
set_target_properties(myRT PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

if(MSVC)
    target_compile_options(myRT PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug>:ProgramDatabase>")
endif()

# ==============================
# 链接库
# ==============================
target_link_libraries(myRT PRIVATE
        # FFmpeg
        avdevice avformat avcodec swresample swscale avutil

        # OpenCV
        ${OpenCV_LIBS}

        # CUDA + NPP
        CUDA::cudart
        CUDA::nppig
        CUDA::nppicc
        CUDA::nppial
        CUDA::nppif
        CUDA::nppidei
        CUDA::nppc
        CUDA::nppisu
        cudadevrt

        # TensorRT
        nvinfer_10
        nvinfer_plugin_10
        nvonnxparser_10

        # Windows
        ws2_32 bcrypt secur32 strmiids uuid ole32
)

target_compile_definitions(myRT PRIVATE HAS_CUDA_TOOLKIT HAS_TENSORRT)

# ==============================
# 复制 DLL
# ==============================
add_custom_command(TARGET myRT POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFMPEG_DIR}/bin/avcodec-62.dll"
        "${FFMPEG_DIR}/bin/avdevice-62.dll"
        "${FFMPEG_DIR}/bin/avformat-62.dll"
        "${FFMPEG_DIR}/bin/avutil-60.dll"
        "${FFMPEG_DIR}/bin/swresample-6.dll"
        "${FFMPEG_DIR}/bin/swscale-9.dll"
        "${FFMPEG_DIR}/bin/avfilter-11.dll"

        "E:/work/opencv_for_MSVC/opencv/build/x64/vc16/bin/opencv_world4120d.dll"
        "E:/work/opencv_for_MSVC/opencv/build/x64/vc16/bin/opencv_videoio_ffmpeg4120_64.dll"

        "${TENSORRT_DIR}/bin/nvinfer_10.dll"
        "${TENSORRT_DIR}/bin/nvinfer_plugin_10.dll"
        "${TENSORRT_DIR}/bin/nvonnxparser_10.dll"
        "${TENSORRT_DIR}/bin/nvinfer_lean_10.dll"
        "${TENSORRT_DIR}/bin/nvinfer_dispatch_10.dll"

        # CUDA 13.2 NPP DLL
        "${CUDA_DIR}/bin/x64/nppc64_13.dll"
        "${CUDA_DIR}/bin/x64/nppial64_13.dll"
        "${CUDA_DIR}/bin/x64/nppicc64_13.dll"
        "${CUDA_DIR}/bin/x64/nppidei64_13.dll"
        "${CUDA_DIR}/bin/x64/nppif64_13.dll"
        "${CUDA_DIR}/bin/x64/nppig64_13.dll"
        "${CUDA_DIR}/bin/x64/nppisu64_13.dll"

        "$<TARGET_FILE_DIR:myRT>"
)

# 创建 trt 缓存目录
add_custom_command(TARGET myRT POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_SOURCE_DIR}/trt_cache"
)