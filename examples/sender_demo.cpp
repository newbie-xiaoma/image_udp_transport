#include <iostream>
#include <thread>
#include <chrono>
#include <string>

// 包含发送端实现和类型契约
#include "../src/sender/UdpSenderImpl.h"

int main(int argc, char* argv[]) {
    std::string target_ip = "127.0.0.1";
    int target_port = 8080;

    if (argc > 1) target_ip = argv[1]; 
    if (argc > 2) target_port = std::atoi(argv[2]); 

    std::cout << "=== Sender Demo Started ===" << std::endl;

    // ==========================================
    // 1. 读取 1280 主素材 (用于电视和红外)
    // ==========================================
    cv::Mat img_1280 = cv::imread("data/1280.jpg", cv::IMREAD_GRAYSCALE);
    if (img_1280.empty()) {
        std::cerr << "Failed to load test image: data/1280.jpg" << std::endl;
        return -1;
    }
    if (img_1280.cols != 1280 || img_1280.rows != 1024) {
        cv::resize(img_1280, img_1280, cv::Size(1280, 1024));
    }

    // ==========================================
    // 2. 读取 512 专门素材 (用于512窗口)
    // ==========================================
    cv::Mat img_512 = cv::imread("data/512.jpg", cv::IMREAD_GRAYSCALE);
    if (img_512.empty()) {
        std::cerr << "Failed to load test image: data/512.jpg" << std::endl;
        return -1;
    }
    // ⚠️ 关键动作：为了复用当前 1280x1024 的硬件编码通道，将其 resize
    cv::resize(img_512, img_512, cv::Size(1280, 1024));

    // 3. 实例化并初始化发送端
    UdpSenderImpl sender;
    if (!sender.init(target_ip.c_str(), target_port, 1280, 1024)) {
        std::cerr << "Sender initialization failed!" << std::endl;
        return -1;
    }

    // 4. 构造业务数据
    transport::WinInfo win_tv{};
    win_tv.timestamp = 10001;
    win_tv.win_mode  = 0x85;

    transport::WinInfo win_ir{};
    win_ir.timestamp = 10002;
    win_ir.win_mode  = 0x83;

    transport::WinInfo win_512{};
    win_512.timestamp = 10003;
    win_512.win_mode  = 0x05;

    std::vector<transport::Label> labels = {{1, 100, 100, 50, 50, 0.95f, 0}};

    // 5. 模拟并发交替发送测试
    std::cout << "Sending TV Image (0x85) from 1280.jpg..." << std::endl;
    sender.sendFrame(img_1280, win_tv, labels, 80); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Sending IR Image (0x83) from 1280.jpg..." << std::endl;
    sender.sendFrame(img_1280, win_ir, labels, 60); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 👇 这里传入的是 img_512
    std::cout << "Sending 512 Window Image (0x05) from 512.jpg..." << std::endl;
    sender.sendFrame(img_512, win_512, labels, 90); 

    std::cout << "All frames sent. Waiting 2 seconds before exit..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    sender.stop();
    return 0;
}