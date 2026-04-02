#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <atomic>
#include <getopt.h>
#include <opencv2/opencv.hpp>

#include "../src/sender/UdpSenderImpl.h"

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -h, --help       Show this help message\n"
              << "  -i, --ip         Target IP (default: 127.0.0.1)\n"
              << "  -p, --port       Target Port (default: 8080)\n"
              << "  -t, --time       Duration in seconds (default: 60)\n"
              << "  -m, --mode       Sensor Mode: 1=IR(50Hz), 2=TV(30Hz), 3=512(20Hz) (default: 1)\n";
}

int main(int argc, char* argv[]) {
    std::string target_ip = "127.0.0.1";
    int target_port = 8080;
    int duration_sec = 60;
    int mode_select = 1;

    const char* const short_opts = "hi:p:t:m:";
    const option long_opts[] = {
        {"help", no_argument,       nullptr, 'h'},
        {"ip",   required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"time", required_argument, nullptr, 't'},
        {"mode", required_argument, nullptr, 'm'},
        {nullptr, no_argument,      nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'i': target_ip = optarg; break;
            case 'p': target_port = std::stoi(optarg); break;
            case 't': duration_sec = std::stoi(optarg); break;
            case 'm': mode_select = std::stoi(optarg); break;
            case 'h': default: print_help(argv[0]); return 0;
        }
    }

    uint8_t win_mode = 0x83;
    int target_fps = 50;
    std::string mode_name = "IR (0x83)";
    std::string img_path = "data/1280.jpg";

    if (mode_select == 2) {
        win_mode = 0x85; target_fps = 30; mode_name = "TV (0x85)"; img_path = "data/1280.jpg";
    } else if (mode_select == 3) {
        win_mode = 0x05; target_fps = 20; mode_name = "512 (0x05)"; img_path = "data/512.jpg";
    }

    std::cout << "=== Single-Channel Sender Demo ===" << std::endl;
    std::cout << "Target: " << target_ip << ":" << target_port << " | Time: " << duration_sec << "s" << std::endl;
    std::cout << "Mode:   " << mode_name << " @ " << target_fps << " FPS\n" << std::endl;

    cv::Mat img = cv::imread(img_path, cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        std::cerr << "Failed to load image: " << img_path << std::endl;
        return -1;
    }
    if (img.cols != 1280 || img.rows != 1024) cv::resize(img, img, cv::Size(1280, 1024));

    UdpSenderImpl sender;
    if (!sender.init(target_ip.c_str(), target_port, 1280, 1024)) return -1;

    std::atomic<bool> is_running{true};

    auto sensor_loop = [&]() {
        auto interval = std::chrono::microseconds(1000000 / target_fps);
        auto next_tick = std::chrono::steady_clock::now();
        uint32_t frame_cnt = 0;
        std::vector<transport::Label> dummy_labels = {{1, 100, 100, 50, 50, 0.9f, 0}};

        while (is_running) {
            transport::WinInfo win{};
            win.win_mode = win_mode;
            win.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            win.frame_id = frame_cnt++;

            sender.sendFrameAsync(img, win, dummy_labels, 80);
            
            next_tick += interval;
            std::this_thread::sleep_until(next_tick);
        }
        std::cout << "\n[" << mode_name << "] Stopped. Total frames: " << frame_cnt << std::endl;
    };

    std::thread worker_thread(sensor_loop);

    for (int i = duration_sec; i > 0; --i) {
        std::cout << "\rRunning... Time left: " << i << "s   " << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\nTime is up! Stopping..." << std::endl;

    is_running = false;
    worker_thread.join();
    sender.stop();
    return 0;
}