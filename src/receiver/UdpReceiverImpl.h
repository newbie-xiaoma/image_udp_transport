#pragma once

#include <memory>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "../../include/ImageReceiver.h"
#include "../network/udp_operation.h"
#include "img_codec.h"

// 内部流转的解码任务结构体
struct DecodeTask {
    uint8_t win_mode;
    transport::WinInfo win_info;
    std::vector<uint8_t> jpeg_buffer; 
    bool is_fallback; // 标识是否为丢包触发的兜底(黑屏)任务
};

struct ChannelState {
    uint16_t expected_total = 0;
    std::map<uint16_t, std::vector<uint8_t>> slices;
    int64_t last_update_time = 0;
};

class UdpReceiverImpl : public ImageReceiver {
public:
    UdpReceiverImpl();
    ~UdpReceiverImpl() override;

    bool init(int local_port, int fallback_w, int fallback_h) override;
    void registerCallback(OnFrameReceivedCallback callback) override;
    void stop() override;

private:
    // 线程 1：纯粹的极速网络收包与拼图线程
    void receiveLoop();
    
    // 线程 2：专职的耗时硬件解码与业务回调线程
    void decodeLoop();

    int64_t now_ms();

private:
    std::unique_ptr<UDPOperation> udp_server_;
    std::unique_ptr<ImgDecode> decoder_;
    OnFrameReceivedCallback callback_;

    std::atomic<bool> running_;
    
    // === 网络线程专用 ===
    std::thread rx_thread_;
    std::map<uint8_t, ChannelState> rx_channels_;

    // === 异步解码流水线核心组件 ===
    std::thread decode_thread_;
    std::queue<DecodeTask> decode_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 队列防爆上限：积压超过此值，执行 Tail Drop (丢弃新帧)
    static constexpr size_t MAX_DECODE_QUEUE_SIZE = 10; 

    int fallback_width_;
    int fallback_height_;
};