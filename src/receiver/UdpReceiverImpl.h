#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <map>
#include <vector>

// 1. 继承第一阶段定义的纯虚接口
#include "../../include/ImageReceiver.h"

// 2. 引入底层的网络和硬件编解码库
#include "../network/udp_operation.h"
#include "img_codec.h" 

// 定义单个通道（某种 win_mode）的接收状态机
struct ChannelState {
    std::map<uint16_t, std::vector<uint8_t>> slices; // 按包号缓存的切片数据
    uint16_t expected_total = 0;                     // 该帧期望的总包数
    uint64_t last_update_time = 0;                   // 最后一次收到包的时间戳(毫秒)，用于超时判定
};

class UdpReceiverImpl : public ImageReceiver {
public:
    UdpReceiverImpl();
    ~UdpReceiverImpl() override;

    bool init(int local_port, int width, int height) override;
    void registerCallback(OnFrameReceivedCallback callback) override;
    void stop() override;

private:
    // 后台接收线程函数
    void receiveLoop();
    
    // 获取当前毫秒时间戳
    uint64_t now_ms();

    // 核心动作：处理丢包，生成黑屏兜底数据并触发回调
    void handleLostFrame(uint8_t win_mode);

    // 核心动作：拼装完整载荷，调用两次解码，并触发回调
    void assembleAndDecode(uint8_t win_mode, ChannelState& channel);

private:
    std::unique_ptr<UDPOperation> udp_server_;
    std::unique_ptr<ImgDecode> decoder_;
    OnFrameReceivedCallback callback_;

    int img_width_;
    int img_height_;
    
    // 多通道独立重组缓冲区 (Key 为 win_mode)
    std::map<uint8_t, ChannelState> rx_channels_;

    std::thread rx_thread_;
    std::atomic<bool> running_;
};