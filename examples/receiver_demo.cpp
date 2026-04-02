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
#include <queue>
#include <condition_variable>
#include <sys/stat.h>
#include <sys/types.h>

#include "../src/receiver/UdpReceiverImpl.h"

// 全局运行标志位
std::atomic<bool> is_running{true};

void signal_handler(int signal) {
    std::cout << "\n[Receiver] Caught signal " << signal << ", stopping..." << std::endl;
    is_running = false;
}

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -h, --help       Show this help message\n"
              << "  -p, --port       Local Port (default: 8080)\n";
}

std::string getModeName(uint8_t mode) {
    if (mode == 0x83) return "IR (0x83)";
    if (mode == 0x85) return "TV (0x85)";
    if (mode == 0x05) return "512(0x05)";
    return "Unknown  ";
}

// ========================================================
// 🌟 异步存图任务队列组件
// ========================================================
struct SaveTask {
    std::string filepath;
    cv::Mat image;
};

std::queue<SaveTask> save_queue;
std::mutex save_mtx;
std::condition_variable save_cv;
// 保护内存：如果磁盘写入速度跟不上收包速度，最多在内存里缓存 100 张待存图片
constexpr size_t MAX_SAVE_QUEUE = 100; 

// 后台专职写硬盘的线程
void diskWriteLoop() {
    while (is_running) {
        SaveTask task;
        {
            std::unique_lock<std::mutex> lock(save_mtx);
            save_cv.wait(lock, []() { return !save_queue.empty() || !is_running; });
            if (!is_running && save_queue.empty()) break;
            
            task = std::move(save_queue.front());
            save_queue.pop();
        }
        
        // 执行耗时的磁盘 IO
        cv::imwrite(task.filepath, task.image);
    }
}
// ========================================================


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
            case 'p': local_port = std::stoi(optarg); break;
            case 'h': default: print_help(argv[0]); return 0;
        }
    }

    std::signal(SIGINT, signal_handler);

    // 自动创建一个文件夹用来存放图片 (Linux 命令)
    std::system("mkdir -p saved_frames");

    std::cout << "=== Receiver Demo Started ===" << std::endl;
    std::cout << "Listening on Port: " << local_port << "\n" << std::endl;

    UdpReceiverImpl receiver;
    if (!receiver.init(local_port, 1280, 1024)) return -1;

    // 启动异步写硬盘线程
    std::thread save_thread(diskWriteLoop);

    std::mutex stats_mutex;
    std::map<uint8_t, int> frame_counters;  
    int fallback_counter = 0;               

    // --- 注册异步回调 ---
    receiver.registerCallback([&](const transport::DecodedFrame& frame) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        if (frame.is_valid) {
            frame_counters[frame.win_info.win_mode]++;
            
            // 🌟 构造存图任务，扔给后台磁盘线程
            std::string filename = "saved_frames/mode_" + std::to_string(frame.win_info.win_mode) 
                                 + "_f" + std::to_string(frame.win_info.frame_id) + ".jpg";
            
            {
                std::lock_guard<std::mutex> save_lock(save_mtx);
                if (save_queue.size() < MAX_SAVE_QUEUE) {
                    // cv::Mat 的拷贝只会增加引用计数，极快，不会阻塞当前网络回调
                    save_queue.push({filename, frame.image});
                    save_cv.notify_one();
                } else {
                    // 如果硬盘实在太慢导致队列积压，静默丢弃存图任务，坚决不反噬网络层
                    // std::cerr << "Disk too slow, skipped saving frame!" << std::endl;
                }
            }

        } else {
            fallback_counter++;
        }
    });

    auto last_time = std::chrono::steady_clock::now();

    while (is_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_time).count();
        last_time = now;

        std::map<uint8_t, int> current_counters;
        int current_fallback = 0;
        int current_save_backlog = 0;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            current_counters = frame_counters;
            current_fallback = fallback_counter;
            frame_counters.clear();
            fallback_counter = 0;
            
            std::lock_guard<std::mutex> save_lock(save_mtx);
            current_save_backlog = save_queue.size();
        }

        std::cout << "---------------------------------------" << std::endl;
        int total_fps = 0;
        for (const auto& pair : current_counters) {
            double fps = pair.second / elapsed_sec;
            total_fps += pair.second;
            std::cout << " [CH] " << getModeName(pair.first) << " \t| FPS: " << std::fixed << std::setprecision(1) << fps << std::endl;
        }
        
        std::cout << " [ALL] Total Valid \t| FPS: " << std::fixed << std::setprecision(1) << (total_fps / elapsed_sec) << std::endl;
        
        if (current_fallback > 0) std::cout << " [!] Net Fallback \t| Count: " << current_fallback << " drops/sec" << std::endl;
        
        // 实时监控磁盘写入的压力情况
        std::cout << " [DISK] Save Queue \t| Backlog: " << current_save_backlog << " / " << MAX_SAVE_QUEUE << std::endl;
        std::cout << "---------------------------------------\n" << std::endl;
    }

    // 优雅退出逻辑
    receiver.stop();
    
    // 唤醒并等待磁盘线程收尾
    save_cv.notify_all();
    if (save_thread.joinable()) save_thread.join();
    
    std::cout << "=== Receiver Demo Finished Successfully ===" << std::endl;
    return 0;
}