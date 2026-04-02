#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <atomic>
#include <getopt.h> // 引入标准命令行解析库
#include <opencv2/opencv.hpp>

#include "../src/sender/UdpSenderImpl.h"

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -h, --help       Show this help message and exit\n"
              << "  -i, --ip         Target IP address (default: 127.0.0.1)\n"
              << "  -p, --port       Target Port (default: 8080)\n"
              << "  -t, --time       Duration in seconds to run the test (default: 10)\n"
              << "\nExample:\n"
              << "  " << prog_name << " -i 192.168.1.100 -p 9000 -t 60\n";
}

int main(int argc, char* argv[]) {
    // 默认参数
    std::string target_ip = "127.0.0.1";
    int target_port = 8080;
    int duration_sec = 10;

    // 定义长短选项
    const char* const short_opts = "hi:p:t:";
    const option long_opts[] = {
        {"help", no_argument,       nullptr, 'h'},
        {"ip",   required_argument, nullptr, 'i'},
        {"port", required_argument, nullptr, 'p'},
        {"time", required_argument, nullptr, 't'},
        {nullptr, no_argument,      nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'i':
                target_ip = optarg;
                break;
            case 'p':
                target_port = std::stoi(optarg);
                break;
            case 't':
                duration_sec = std::stoi(optarg);
                break;
            case 'h':
            case '?': // 遇到未知参数或未带值的参数，自动走这里
            default:
                print_help(argv[0]);
                return 0;
        }
    }

    std::cout << "=== Sender Demo Started ===" << std::endl;
    std::cout << "Target IP:   " << target_ip << std::endl;
    std::cout << "Target Port: " << target_port << std::endl;
    std::cout << "Duration:    " << duration_sec << " seconds\n" << std::endl;

    // --- 加载素材 ---
    cv::Mat img_1280 = cv::imread("data/1280.jpg", cv::IMREAD_GRAYSCALE);
    cv::Mat img_512 = cv::imread("data/512.jpg", cv::IMREAD_GRAYSCALE);
    
    if (img_1280.empty() || img_512.empty()) {
        std::cerr << "Failed to load test images from data/ directory!" << std::endl;
        return -1;
    }
    
    if (img_1280.cols != 1280 || img_1280.rows != 1024) cv::resize(img_1280, img_1280, cv::Size(1280, 1024));
    if (img_512.cols != 1280 || img_512.rows != 1024) cv::resize(img_512, img_512, cv::Size(1280, 1024));

    // --- 初始化发送引擎 ---
    UdpSenderImpl sender;
    if (!sender.init(target_ip.c_str(), target_port, 1280, 1024)) {
        std::cerr << "Sender init failed!" << std::endl;
        return -1;
    }

    std::atomic<bool> is_running{true};

    // --- 传感器模拟闭包 ---
    auto sensor_loop = [&](cv::Mat& img, uint8_t mode, int fps, const std::string& name) {
        auto interval = std::chrono::microseconds(1000000 / fps);
        auto next_tick = std::chrono::steady_clock::now();
        uint32_t frame_cnt = 0;
        std::vector<transport::Label> dummy_labels = {{1, 100, 100, 50, 50, 0.9f, 0}};

        while (is_running) {
            transport::WinInfo win{};
            win.win_mode = mode;
            win.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            win.frame_id = frame_cnt++;

            sender.sendFrameAsync(img, win, dummy_labels, 80);
            
            next_tick += interval;
            std::this_thread::sleep_until(next_tick);
        }
        std::cout << "[" << name << "] Stopped. Generated: " << frame_cnt << " frames." << std::endl;
    };

    std::cout << "--- Starting 3 Concurrent Sensor Threads ---" << std::endl;
    std::cout << "  -> IR (0x83) @ 50Hz" << std::endl;
    std::cout << "  -> TV (0x85) @ 30Hz" << std::endl;
    std::cout << "  -> 512 (0x05) @ 20Hz\n" << std::endl;

    std::thread ir_thread(sensor_loop, std::ref(img_1280), 0x83, 50, "IR_Camera");
    std::thread tv_thread(sensor_loop, std::ref(img_1280), 0x85, 30, "TV_Camera");
    std::thread w512_thread(sensor_loop, std::ref(img_512), 0x05, 20, "512_Camera");

    // --- 倒计时 ---
    for (int i = duration_sec; i > 0; --i) {
        std::cout << "\rRunning... Time left: " << i << "s   " << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\n\nTime is up! Stopping..." << std::endl;

    is_running = false;
    ir_thread.join();
    tv_thread.join();
    w512_thread.join();

    sender.stop();
    return 0;
}