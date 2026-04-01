#pragma once

#include <memory>
#include <string>

// 1. 继承第一阶段定义的纯虚接口
#include "../../include/ImageSender.h"

// 2. 引入底层的网络和硬件编解码库
#include "../network/udp_operation.h"
#include "img_codec.h" // 根据你的 CMake 包含路径调整

class UdpSenderImpl : public ImageSender {
public:
    UdpSenderImpl();
    ~UdpSenderImpl() override;

    // 实现接口方法
    bool init(const char* remote_ip, int remote_port, int width, int height) override;
    bool sendFrame(cv::Mat& img, 
                   const transport::WinInfo& win, 
                   const std::vector<transport::Label>& labels, 
                   uint32_t quality) override;
    void stop() override;

private:
    std::unique_ptr<UDPOperation> udp_client_;
    std::unique_ptr<ImgEncode> encoder_;
    
    int img_width_;
    int img_height_;
    bool is_initialized_;

    // 定义 UDP 包中载荷(Payload)的最大长度，保证总长度不超过常见局域网 MTU (1500)
    static constexpr size_t MAX_PAYLOAD_SIZE = 1024; 
};