# LiveSegStream

Real-time camera streaming with AI background removal. Full GPU pipeline powered by TensorRT, CUDA, and NVENC — zero host-device round-trips on the inference path.

## Architecture

```
 ┌─────────────────────────────────────────────────────────────────────────────────────┐
 │                                    Pipeline                                         │
 │                                                                                     │
 │  CameraCapture(video) ─► videoFrameQue ─► Infra ─► videoInfraFrameQue ─► Coder     │
 │       (dshow)              (cap=4)       (TensorRT)     (cap=4)         (NVENC)     │
 │                                                                          │          │
 │                                                                     videoPacketQue  │
 │                                                                       (cap=4)       │
 │                                                                          │          │
 │  CameraCapture(audio) ─► audioFrameQue ─► Coder ─► audioPacketQue ───────┤          │
 │       (dshow)              (cap=4)        (AAC)      (cap=4)              │          │
 │                                                                            │          │
 │                                                                     ┌──────▼──────┐  │
 │                                                                     │  FlvMuxer   │  │
 │                                                                     │  (FLV/RTMP) │  │
 │                                                                     └─────────────┘  │
 └─────────────────────────────────────────────────────────────────────────────────────┘
```

**7 threads**, **6 thread-safe blocking queues** (each capacity = 4). Each stage is decoupled — bursts and jitter in one stage don't cascade to the next.

### Thread Layout

| Thread | Class::Method | Role |
|--------|--------------|------|
| `videoStart` | `CameraCapture::videoStart` | Read raw frames from dshow, decode, push to `videoFrameQue` |
| `audioStart` | `CameraCapture::audioStart` | Read raw audio from dshow, decode, push to `audioFrameQue` |
| (unnamed) | `Infra::run` | AI background removal + compositing, push to `videoInfraFrameQue` |
| `encoderVideoData` | `Coder::encoderVideoData` | NVENC H.264 encode, inject SEI timestamps, push to `videoPacketQue` |
| `encoderAudioData` | `Coder::encoderAudioData` | AAC encode with leftover-buffer frame assembly, push to `audioPacketQue` |
| (unnamed) | `FlvMuxer::sendVideoAndAudio` | DTS-ordered interleave, FLV mux, write to RTMP |

### Queue Semantics

- `put()` blocks when full → natural backpressure
- `get()` blocks when empty → no busy-waiting
- `setDone()` signals producers will write no more → consumers drain remaining items then exit
- `stop()` emergency wake → all waiters return immediately
- All queues use timed-wait to prevent deadlock during coordinated shutdown

### Stop Sequence (Deadlock Prevention)

Order matters — each stage must be drained before the next is signaled:

```
camera_->stopCapture()            // interrupt av_read_frame() via interrupt callback
videoFrameQue_->setDone()         // signal video capture will stop
audioFrameQue_->setDone()         // signal audio capture will stop
captureVideoData_.join()          // wait for video capture thread
captureAudioData_.join()          // wait for audio capture thread
infraVideoData_.join()            // wait for Infra to drain
videoInfraFrameQue_->setDone()    // signal Infra output is done
encoderVideoData_.join()          // wait for video encoder
encoderAudioData_.join()          // wait for audio encoder
videoPacketQue_->setDone()        // signal mux input is done
audioPacketQue_->setDone()
sendVideoAndAudioData_.join()     // wait for mux to flush
```

## Infra Pipeline (GPU Background Removal)

Every frame goes through this GPU pipeline with **zero host-device copies** on the inference path:

```
 ┌─────────┐    ┌──────────────┐    ┌───────────┐    ┌──────────────┐
 │ YUV422  │───►│ NPP YUV→RGB │───►│ NPP Resize │───►│ CUDA HWC→CHW│
 │ (d_orig)│    └──────────────┘    │  → model   │    │  + normalize│
 └─────────┘                        │   input    │    └──────┬───────┘
                                    └───────────┘           │
                                                            ▼
                       ┌─────────────────────────────────────────┐
                       │         TensorRT Inference              │
                       │      RMBG-1.4 (portrait mask)           │
                       └────────────────────┬────────────────────┘
                                            │
                       ┌────────────────────▼────────────────────┐
                       │     NPP Resize mask → video resolution  │
                       └────────────────────┬────────────────────┘
                                            │
              ┌─────────────────────────────┼─────────────────────────────┐
              │                      Temporal Smoothing                   │
              │              mask = current*0.7 + previous*0.3            │
              └─────────────────────────────┬────────────────────────────┘
                                            │
              ┌─────────────────────────────▼────────────────────────────┐
              │               CUDA refine_edge kernel                   │
              │       smoothstep to sharpen alpha channel edges         │
              └─────────────────────────────┬────────────────────────────┘
                                            │
       ┌────────────────────────────────────┼────────────────────────────────────┐
       │                          NPP Box Blur                                    │
       │                    (background blur on d_YUVToRGB)                       │
       └────────────────────────┬───────────────────────────────────────────────┘
                                │
       ┌────────────────────────▼───────────────────────────────────────────────┐
       │                     CUDA blend_frames kernel                           │
       │          output = foreground*alpha + blur_bg*(1-alpha)                 │
       └────────────────────────┬───────────────────────────────────────────────┘
                                │
       ┌────────────────────────▼───────────────────────────────────────────────┐
       │                  NPP RGB→YUV422 → cudaMemcpy back to host              │
       │                  Push frame to videoInfraFrameQue                       │
       └────────────────────────────────────────────────────────────────────────┘
```

- Inference runs every **3rd frame** (skip interval); mask is cached and reused
- Model input: **1024×1024** RGB normalized to [0,1] in CHW layout
- Model output: single-channel float mask, resized back to **1280×720**

## Encoding

### Video (NVENC)

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Codec | `h264_nvenc` | NVIDIA hardware encoder |
| Resolution | 1280×720 @ 30fps | |
| Rate control | CBR 2 Mbps | Constant bitrate, no VBR jitter |
| VBV buffer | 200 kbit (0.1s) | Limits encoder-side buffering |
| GOP | 15 frames (0.5s) | Key-frame interval |
| B-frames | 0 | No reordering delay |
| Preset | `p1` | Fastest NVENC preset |
| Tune | `ull` (ultra-low-latency) | Disables lookahead, minimizes latency |
| Surfaces | 4 | Small surface pool = less queuing |
| Slice mode | `slice_max_size` = 1400 bytes | Each NAL unit fits in one MTU-sized slice, reduces transmission buffering |
| Pixel format | `AV_PIX_FMT_CUDA` | CUDA device memory, zero-copy to NVENC |

### Audio (AAC)

| Parameter | Value |
|-----------|-------|
| Codec | `libfdk_aac` / FFmpeg native AAC |
| Sample rate | 32000 Hz, mono |
| Format | FLTP (float planar) |
| Bitrate | 96 kbps |
| Frame size | 1024 samples (32ms per frame) |

Input is S16 → FLTP via `libswresample`. Leftover samples are accumulated in a ring buffer until a full 1024-sample frame is assembled.

## Latency Measurement

End-to-end latency is tracked at **every pipeline stage** using capture timestamps and SEI injection:

### 1. Capture Timestamping

Each frame carries a `captureTime` — a Unix-epoch microsecond timestamp recorded immediately after `av_read_frame()` returns:

```cpp
int64_t captureTime = std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
```

This timestamp follows the frame through the entire pipeline inside `FrameWithCaptureTime` / `PacketWithCaptureTime`.

### 2. Per-Stage Logging

At each pipeline stage boundary, the current wall-clock time is compared against the original `captureTime`:

| Stage | Log Prefix | What It Measures |
|-------|-----------|-----------------|
| Capture → Infra | `[Infra] "从第X帧视频帧被捕捉，到达infra推理模块的时间延迟是"` | Camera → inference queue latency |
| Infra → Encoder | `[VIDEO_ENC] "从第X帧被捕捉，到帧到达编码器的延迟是"` | Camera → encoder queue latency |
| Encoder → Mux | `[VIDEO_ENC] "从第X帧被捕捉，到帧离开编码器的延迟是"` | Camera → after-encode latency |
| Mux → RTMP | `[MUX] "从第X帧被捕捉，到帧到达/离开FLV模块的延迟是"` | Camera → mux write latency |

### 3. SEI Timestamp Injection (Receiver-Side Measurement)

The capture timestamp is embedded into the H.264 bitstream as an **SEI NAL unit** of type `user_data_unregistered(5)`:

```
[0x00 0x00 0x00 0x01]       // Annex B start code
[0x06]                      // NAL type = SEI
[0x05]                      // payload_type = user_data_unregistered
[24]                         // payload_size = 16 (UUID) + 8 (timestamp)
[16-byte UUID]               // custom UUID for identification
[8-byte int64_t captureTime] // original capture timestamp
[0x80]                       // rbsp_stop_one_bit
[H.264 slice data...]
```

**How to use this on the receiver side:**

1. Parse the H.264 stream, look for NAL type `0x06` (SEI) followed by payload type `0x05` (user_data_unregistered)
2. Match the 16-byte UUID to identify packets from this pipeline: `3FDB50C0-1887-4105-8F62-DC0671E54398`
3. Read the next 8 bytes as a `int64_t` capture timestamp (Unix epoch microseconds)
4. Compare against current wall-clock time: `latency = now() - captureTime`

Example SEI payload in the first encoded packet:
```
00 00 00 01 06 05 18 3F DB 50 C0 18 87 41 05 8F 62 DC 06 71 E5 43 98 XX XX XX XX XX XX XX XX 80 [slice data]
│start code│ │SEI│ │user│ │len│ │────────────── UUID ──────────────────────────│ │─ captureTime ───│ │stop│
```

### 4. Clock / PTS Model

```
Global Clock Architecture
─────────────────────────────────────────────────────────────

  First frame arrives
        │
        ▼
  globalStartTime_ = av_gettime_relative()   ← wall clock anchor
        │
        ▼
  All subsequent PTS = now() - globalStartTime_   ← relative to anchor

  Video PTS:  getGlobalClockUs()                     → microseconds
  Audio PTS:  getGlobalClockUs() * sample_rate / 1e6 → sample-rate units

  FlvMuxer rescales to output time_base {1, 1000} via av_packet_rescale_ts()
```

## RTMP Muxing

- **Container**: FLV (required by RTMP protocol)
- **Interleave**: DTS-ordered — at each write decision, compare video and audio DTS (both converted to microseconds), write whichever is earlier
- **Low-latency flags**:
  - `AVFMT_FLAG_FLUSH_PACKETS` — write each packet immediately, no internal buffering
  - `tcp_nodelay=1` — disable Nagle's algorithm on the RTMP TCP socket
  - `av_interleaved_write_frame()` — FFmpeg's built-in interleaving write

## Requirements

| Dependency | Version | Notes |
|------------|---------|-------|
| CMake | 3.20+ | |
| MSVC | VS 2022 | Windows only (DirectShow capture) |
| FFmpeg | 6.x / 7.x | libavcodec, libavformat, libavutil, libswscale, libswresample, libavdevice |
| OpenCV | 4.x | Ideally `opencv_world` (MSVC prebuilt) |
| CUDA Toolkit | 12.x / 13.x | Includes NPP libraries |
| TensorRT | 10.x | Engine file from RMBG-1.4 ONNX model |

## Build

```bash
# 1. Edit CMakeLists.txt — set paths to your local installs:
#    FFMPEG_DIR, CUDA_DIR, OpenCV_DIR, TENSORRT_DIR

# 2. Configure and build
cmake -B build -S .
cmake --build build

# Output binary: ./bin/myRT.exe
```

Post-build, required DLLs are automatically copied to `./bin/`.

## Usage

```bash
./bin/myRT.exe \
  -v "video=Integrated Camera" \
  -a "audio=@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{7A7442D2-9E12-49B9-A48E-B6CF69B53744}" \
  -u "rtmp://your-server:1935/live/stream" \
  -m "E:/path/to/RMBG-1.4-trt10-fp16.engine"
```

| Option | Default | Description |
|--------|---------|-------------|
| `-v` | `video=Integrated Camera` | DirectShow video device name |
| `-a` | (DirectShow GUID string) | DirectShow audio device name |
| `-u` | `rtmp://192.168.1.160:1935/livehime` | RTMP push URL |
| `-m` | `RMBG-1.4-trt10-fp16.engine` | Serialized TensorRT engine file |
| `-h` | | Show help |

Press `Ctrl+C` for graceful shutdown.

## Project Structure

```
├── include/
│   ├── CameraCapture.h      # DirectShow video/audio capture
│   ├── Clock.h              # Global PTS clock
│   ├── coder.h              # NVENC H.264 + AAC encoding
│   ├── FlvMuxer.h           # FLV muxing + RTMP output
│   ├── Infra.h              # TensorRT inference + GPU compositing
│   ├── PushStream.h         # Pipeline orchestration
│   └── ThreadSafeQueue.h    # Blocking thread-safe queue
├── src/
│   ├── CameraCapture.cpp
│   ├── Clock.cpp
│   ├── coder.cpp
│   ├── FlvMuxer.cpp
│   ├── Infra.cpp
│   ├── kernel.cu            # CUDA kernels: HWC→CHW, edge refine, blend
│   ├── main.cpp
│   └── PushStream.cpp
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## Performance Tuning Summary

| Technique | Mechanism | Impact |
|-----------|-----------|--------|
| No B-frames | `max_b_frames = 0` | Zero reordering delay |
| CBR + VBV clamp | `rc_buffer_size = bitrate × 0.1s` | Bounded encoder queuing |
| ULL preset | `preset=p1`, `tune=ull`, `zerolatency=1` | NVENC lowest latency path |
| No lookahead | `rc_lookahead=0` | Zero frame buffering |
| Slice encoding | `slice_max_size=1400` | MTU-aligned NAL, no sender-side reassembly |
| Low surfaces | `surfaces=4` | Minimal CUDA surface queue |
| GPU zero-copy | `AV_PIX_FMT_CUDA` + `av_hwframe_transfer_data` | No CPU staging on encode path |
| Flush on write | `AVFMT_FLAG_FLUSH_PACKETS` | No FFmpeg output buffering |
| TCP nodelay | `tcp_nodelay=1` | Disable Nagle, immediate socket send |
| Temporal smoothing | `alpha*0.7 + prev*0.3` | Mask flicker reduction without extra inference |
| Inference skip | every 3rd frame | 60% GPU compute saving, imperceptible quality loss |

## License

MIT
