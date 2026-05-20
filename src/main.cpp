
#include <iostream>
#include <csignal>
#include <atomic>
#include "../include/PushStream.h"


static std::atomic<bool> g_running{true};

static void signalHandler(int sig) {
    fprintf(stderr, "\n[MAIN] 收到信号 %d，正在停止...\n", sig);
    g_running = false;
}

static void printUsage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -v <video_device>   Video device name (default: video=Integrated Camera)\n"
        "  -a <audio_device>   Audio device name (default: audio=Microphone Array)\n"
        "  -u <rtmp_url>       RTMP push URL (default: rtmp://127.0.0.1/live/room)\n"
        "  -m <model_path>     ONNX model path (default: ../model/RMBG-1.4.onnx)\n"
        "  -h                  Show this help\n",
        prog);
}

int main(int argc, char *argv[]) {
    const char *video_device = "video=Integrated Camera";
    const char *audio_device = "audio=@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{7A7442D2-9E12-49B9-A48E-B6CF69B53744}";
    const char *url = "rtmp://192.168.1.160:1935/livehime";
    std::string model_str = "E:/work/video/RMBG-1.4-trt10-fp16.engine";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-v") && i + 1 < argc) {
            video_device = argv[++i];
        } else if ((arg == "-a") && i + 1 < argc) {
            audio_device = argv[++i];
        } else if ((arg == "-u") && i + 1 < argc) {
            url = argv[++i];
        } else if ((arg == "-m") && i + 1 < argc) {
            model_str = argv[++i];
        } else if (arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }


    std::wstring model_path(model_str.begin(), model_str.end());

    fprintf(stderr, "[MAIN] video=%s\n", video_device);
    fprintf(stderr, "[MAIN] audio=%s\n", audio_device);
    fprintf(stderr, "[MAIN] url=%s\n", url);
    fprintf(stderr, "[MAIN] model=%s\n", model_str.c_str());

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    PushStream pStream;

    if (!pStream.open(video_device, audio_device, url, model_path)) {
        fprintf(stderr, "[MAIN] 初始化失败\n");
        return -1;
    }

    pStream.start();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    pStream.stop();
    fprintf(stderr, "[MAIN] 正常退出\n");
    return 0;
}
