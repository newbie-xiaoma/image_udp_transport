#include <iostream>
#include <thread>
#include <chrono>

// 只需包含发送端实现和类型契约
#include "../src/sender/UdpSenderImpl.h"

int main() {
    std::cout << "=== Sender Demo Started ===" << std::endl;

    // 1. 读取测试图像并转为单通道灰度图
    cv::Mat img = cv::imread("data/1280.jpg", cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        std::cerr << "Failed to load test image: ../data/1280.jpg" << std::endl;
        return -1;
    }
    // 确保尺寸是 1280x1024
    if (img.cols != 1280 || img.rows != 1024) {
        cv::resize(img, img, cv::Size(1280, 1024));
    }

    // 2. 实例化发送端
    UdpSenderImpl sender;

    // 3. 初始化 (发送到本机的 8080 端口)
    if (!sender.init("127.0.0.1", 8080, 1280, 1024)) {
        std::cerr << "Sender initialization failed!" << std::endl;
        return -1;
    }

    // 4. 构造假业务数据
    transport::WinInfo win_tv{};
    win_tv.timestamp = 10001;
    win_tv.win_mode  = 0x85; // 电视
    
    transport::WinInfo win_ir{};
    win_ir.timestamp = 10002;
    win_ir.win_mode  = 0x83; // 红外

    std::vector<transport::Label> labels;
    labels.push_back({1, 100, 100, 50, 50, 0.95f, 0});

    // 5. 模拟并发发送测试
    std::cout << "Sending TV Image (0x85)..." << std::endl;
    sender.sendFrame(img, win_tv, labels, 80); // 质量 80

    // 稍微停顿一下，让网络缓冲一下（可选）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Sending IR Image (0x83)..." << std::endl;
    sender.sendFrame(img, win_ir, labels, 60); // 质量 60

    std::cout << "All frames sent. Waiting 2 seconds before exit..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    sender.stop();
    return 0;
}