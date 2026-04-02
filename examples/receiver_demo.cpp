#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <map>
#include <mutex>
#include <csignal>
#include <getopt.h>
#include <iomanip>

#include "../src/receiver/UdpReceiverImpl.h"

// 全局运行标志位，用于捕获 Ctrl+C
std::atomic<bool> is_running{true};

void signal_handler(int signal) {
    std::cout << "\n[Receiver] Caught signal " << signal << ", stopping..." << std::endl;
    is_running = false;
}

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -h, --help       Show this help message and exit\n"
              << "  -p, --port       Local Port to listen on (default: 8080)\n"
              << "\nExample:\n"
              << "  " << prog_name << " -p 9000\n";
}

// 将 16 进制模式转换为可读的字符串名
std::string getModeName(uint8_t mode) {
    if (mode == 0x83) return "IR (0x83)";
    if (mode == 0x85) return "TV (0x85)";
    if (mode == 0x05) return "512(0x05)";
    return "Unknown  ";
}

int main(int argc, char* argv[]) {
    int local_port = 8080;

    const char* const short_opts = "hp:";
    const option long_opts[] = {
        {"help", no_argument,       nullptr, 'h'},
        {"port", required_argument, nullptr, 'p'},
        {nullptr, no_argument,      nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, short_opts, long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'p':
                local_port = std::stoi(optarg);
                break;
            case 'h':
            case '?':
            default:
                print_help(argv[0]);
                return 0;
        }
    }

    // 注册 Ctrl+C 信号处理
    std::signal(SIGINT, signal_handler);

    std::cout << "=== Receiver Demo Started ===" << std::endl;
    std::cout << "Listening on Port: " << local_port << "\n" << std::endl;

    // --- 初始化接收端 ---
    UdpReceiverImpl receiver;
    if (!receiver.init(local_port, 1280, 1024)) {
        std::cerr << "Receiver init failed!" << std::endl;
        return -1;
    }

    // --- FPS 统计组件 ---
    std::mutex stats_mutex;
    std::map<uint8_t, int> frame_counters;  // 记录各通道正常收到的帧数
    int fallback_counter = 0;               // 记录因丢包触发的黑屏兜底次数

    // --- 注册异步回调 ---
    receiver.registerCallback([&](const transport::DecodedFrame& frame) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        if (frame.is_valid) {
            frame_counters[frame.win_info.win_mode]++;
            
            // 注意：压测时不要解除这里的注释，否则硬盘写入会严重拖垮 FPS！
            // cv::imwrite("test_" + std::to_string(frame.win_info.win_mode) + ".jpg", frame.image);
        } else {
            fallback_counter++;
        }
    });

    // --- 主线程：定时打印实时 FPS ---
    auto last_time = std::chrono::steady_clock::now();

    while (is_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        // 加锁，拷贝统计数据并清零
        std::map<uint8_t, int> current_counters;
        int current_fallback = 0;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            current_counters = frame_counters;
            current_fallback = fallback_counter;
            frame_counters.clear();
            fallback_counter = 0;
        }

        // 打印绚丽的 FPS 面板
        std::cout << "---------------------------------------" << std::endl;
        int total_fps = 0;
        for (const auto& pair : current_counters) {
            double fps = pair.second / elapsed_sec;
            total_fps += pair.second;
            std::cout << " [CH] " << getModeName(pair.first) 
                      << " \t| FPS: " << std::fixed << std::setprecision(1) << fps << std::endl;
        }
        
        std::cout << " [ALL] Total Valid \t| FPS: " << std::fixed << std::setprecision(1) << (total_fps / elapsed_sec) << std::endl;
        
        if (current_fallback > 0) {
            std::cout << " [!] Fallback/Drop \t| Count: " << current_fallback << " times/sec" << std::endl;
        }
        std::cout << "---------------------------------------\n" << std::endl;
    }

    receiver.stop();
    std::cout << "=== Receiver Demo Finished Successfully ===" << std::endl;
    return 0;
}