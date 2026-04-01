#include <iostream>
#include <thread>
#include <chrono>
#include <string>

// 只需包含你的接收端实现和类型契约
#include "../src/receiver/UdpReceiverImpl.h"

int main(int argc, char* argv[]) {
    // 默认监听 8080，如果运行程序时传了参数，就用传入的端口
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::cout << "=== Receiver Demo Started ===" << std::endl;
    UdpReceiverImpl receiver;

    // 将写死的 8080 替换为变量 port
    if (!receiver.init(port, 1280, 1024)) {
        std::cerr << "Receiver initialization failed!" << std::endl;
        return -1;
    }

    // 3. 注册回调函数 (Lambda 表达式)
    receiver.registerCallback([](const transport::DecodedFrame& frame) {
        std::cout << "\n[Callback Triggered]" << std::endl;
        uint8_t mode = frame.win_info.win_mode;
        
        if (frame.is_valid) {
            std::cout << " -> Success! Received perfect frame for mode: 0x" 
                      << std::hex << (int)mode << std::dec << std::endl;
            std::cout << " -> Image Size: " << frame.image.cols << "x" << frame.image.rows << std::endl;
            std::cout << " -> Labels Count: " << frame.labels.size() << std::endl;
            
            // 将正确的图像保存下来
            std::string filename = "recv_success_mode_" + std::to_string(mode) + ".jpg";
            cv::imwrite(filename, frame.image);
            std::cout << " -> Saved to: " << filename << std::endl;
        } else {
            std::cerr << " -> Fallback! Packet loss detected for mode: 0x" 
                      << std::hex << (int)mode << std::dec << ". Outputting black image." << std::endl;
            
            // 将兜底的黑图保存下来验证
            std::string filename = "recv_fallback_mode_" + std::to_string(mode) + ".jpg";
            cv::imwrite(filename, frame.image);
        }
    });

    std::cout << "Listening on port 8080... Press Ctrl+C to stop." << std::endl;

    // 4. 保持主线程存活 (接收端在后台线程运行)
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    receiver.stop();
    return 0;
}