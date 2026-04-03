#include "UdpSenderImpl.h"
#include "../protocol/UdpHeader.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>
#include <chrono>

constexpr size_t UdpSenderImpl::MAX_PAYLOAD_SIZE;
constexpr size_t UdpSenderImpl::MAX_QUEUE_SIZE;

UdpSenderImpl::UdpSenderImpl() 
    : img_width_(0), img_height_(0), is_initialized_(false), running_(false) {
}

UdpSenderImpl::~UdpSenderImpl() {
    stop();
}

bool UdpSenderImpl::init(const char* remote_ip, int remote_port, int width, int height) {
    if (is_initialized_) return true;

    img_width_ = width;
    img_height_ = height;

    udp_client_ = std::make_unique<UDPOperation>(remote_ip, remote_port, "");
    if (!udp_client_->create_server()) {
        std::cerr << "[Sender] Failed to create UDP server." << std::endl;
        return false;
    }

    encoder_ = std::make_unique<ImgEncode>(img_width_, img_height_);
    
    running_ = true;
    worker_thread_ = std::thread(&UdpSenderImpl::sendLoop, this);
    
    is_initialized_ = true;
    std::cout << "[Sender] Initialized. Target: " << remote_ip << ":" << remote_port << std::endl;
    return true;
}


bool UdpSenderImpl::sendFrameSync(const cv::Mat& img, 
                                  const transport::WinInfo& win, 
                                  const std::vector<transport::Label>& labels, 
                                  uint32_t quality) {
    if (!is_initialized_) return false;
    // 同步发送，直接调用底层干活函数，且关闭严格平滑控流（退化为常规的微休眠洪峰发送）
    return internalEncodeAndSend(img, win, labels, quality, false);
}


bool UdpSenderImpl::sendFrameAsync(const cv::Mat& img, 
                                   const transport::WinInfo& win, 
                                   const std::vector<transport::Label>& labels, 
                                   uint32_t quality) {
    if (!is_initialized_) return false;

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (task_queue_.size() >= MAX_QUEUE_SIZE) {
        std::cerr << "[Sender] Async WARNING: Network busy, dropping new frame (mode: 0x" 
                  << std::hex << (int)win.win_mode << std::dec << ")!" << std::endl;
        return false; 
    }

    SendTask task;
    task.img = img.clone(); 
    task.win = win;
    task.labels = labels;
    task.quality = quality;

    task_queue_.push(std::move(task));
    queue_cv_.notify_one();
    
    return true;
}


void UdpSenderImpl::sendLoop() {
    while (running_) {
        SendTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { 
                return !task_queue_.empty() || !running_; 
            });

            if (!running_ && task_queue_.empty()) break;

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }
        // 异步后台线程执行底层函数，且开启严格平滑控流 (Pacing)
        internalEncodeAndSend(task.img, task.win, task.labels, task.quality, true);
    }
}


bool UdpSenderImpl::internalEncodeAndSend(const cv::Mat& img, 
                                          const transport::WinInfo& win, 
                                          const std::vector<transport::Label>& labels, 
                                          uint32_t quality, 
                                          bool apply_pacing) {
    // 极其重要：保护底层硬件编码器和 Socket 实例不被多线程乱序访问
    std::lock_guard<std::mutex> hw_lock(hardware_mutex_);

    auto frame_start_time = std::chrono::steady_clock::now();

    ::WinInfo hw_win;
    hw_win.timestamp = win.timestamp;
    hw_win.x         = win.x;
    hw_win.y         = win.y;
    hw_win.frame_id  = win.frame_id;
    hw_win.win_mode  = win.win_mode;
    hw_win.center_x  = win.center_x;
    hw_win.center_y  = win.center_y;
    std::memcpy(hw_win.system_info, win.system_info, sizeof(hw_win.system_info));

    std::vector<::Label> hw_labels;
    for (const auto& lbl : labels) hw_labels.push_back({lbl.id, lbl.x, lbl.y, lbl.w, lbl.h, lbl.conf, lbl.cls});
    cv::Mat mut_img = img;
    unsigned char* encoded_data = nullptr;
    int encoded_size = 0;
    
    bool encode_success = encoder_->encode(mut_img, hw_win, hw_labels, quality, &encoded_data, encoded_size);
    if (!encode_success || encoded_data == nullptr || encoded_size <= 0) {
        std::cerr << "[Sender] Hardware encode failed!" << std::endl;
        return false;
    }

    uint16_t total_packets = (encoded_size + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;
    
    // ======== 动态控流计算 ========
    double interval_us = 100.0; // 同步模式或非控流情况下的默认保护性微休眠 (100us)
    if (apply_pacing) {
        double frame_budget_ms = 33.3; // 默认 30Hz
        if (win.win_mode == 0x83) frame_budget_ms = 20.0;    // 红外 50Hz
        else if (win.win_mode == 0x05) frame_budget_ms = 50.0; // 512窗口 20Hz

        auto encode_end_time = std::chrono::steady_clock::now();
        double encode_cost_ms = std::chrono::duration<double, std::milli>(encode_end_time - frame_start_time).count();
        double remaining_budget_ms = frame_budget_ms - encode_cost_ms - 1.0; 
        if (remaining_budget_ms <= 0) remaining_budget_ms = 2.0; 
        
        interval_us = (remaining_budget_ms * 1000.0) / total_packets;
    }

    // ======== 物理发送与自旋卡点 ========
    auto pacing_start = std::chrono::steady_clock::now();
    for (uint16_t i = 0; i < total_packets; ++i) {
        size_t offset = i * MAX_PAYLOAD_SIZE;
        size_t payload_len = std::min(MAX_PAYLOAD_SIZE, static_cast<size_t>(encoded_size - offset));

        size_t packet_size = sizeof(protocol::UdpPacketHeader) + payload_len + 1;
        std::vector<char> send_buffer(packet_size);

        auto* header = reinterpret_cast<protocol::UdpPacketHeader*>(send_buffer.data());
        header->frame_head    = protocol::PACKET_HEAD_MAGIC;
        header->win_mode      = win.win_mode; 
        header->packet_idx    = htons(i);
        header->total_packets = htons(total_packets);
        header->payload_len   = htons(static_cast<uint16_t>(payload_len));

        std::memcpy(send_buffer.data() + sizeof(protocol::UdpPacketHeader), encoded_data + offset, payload_len);
        send_buffer[packet_size - 1] = protocol::PACKET_TAIL_MAGIC;

        udp_client_->send_buffer(send_buffer.data(), packet_size);

        if (apply_pacing) {
            auto target_time = pacing_start + std::chrono::microseconds(static_cast<long long>((i + 1) * interval_us));
            while (true) {
                auto now = std::chrono::steady_clock::now();
                if (now >= target_time) break; 
                auto diff_us = std::chrono::duration_cast<std::chrono::microseconds>(target_time - now).count();
                if (diff_us > 1000) std::this_thread::sleep_for(std::chrono::microseconds(diff_us - 500));
                else std::this_thread::yield();
            }
        } else {
            // 同步模式直接给予极短休眠，防止瞬间打爆本机环回网络
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(interval_us)));
        }
    }

    delete[] encoded_data;
    return true;
}

void UdpSenderImpl::stop() {
    running_ = false;
    queue_cv_.notify_all(); 
    if (worker_thread_.joinable()) worker_thread_.join();
    
    if (udp_client_) {
        udp_client_->destory(); 
        udp_client_.reset();
    }
    encoder_.reset();
    is_initialized_ = false;
}