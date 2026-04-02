#include "UdpReceiverImpl.h"
#include "../protocol/UdpHeader.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

UdpReceiverImpl::UdpReceiverImpl() 
    : running_(false), fallback_width_(1280), fallback_height_(1024) {
}

UdpReceiverImpl::~UdpReceiverImpl() {
    stop();
}

int64_t UdpReceiverImpl::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool UdpReceiverImpl::init(int local_port, int fallback_w, int fallback_h) {
    fallback_width_ = fallback_w;
    fallback_height_ = fallback_h;
    local_port_ = local_port; // 记录监听端口

    udp_server_ = std::make_unique<UDPOperation>("0.0.0.0", local_port, "");
    if (!udp_server_->create_client()) {
        std::cerr << "[Receiver] Failed to bind local port " << local_port << std::endl;
        return false;
    }

    decoder_ = std::make_unique<ImgDecode>(fallback_width_, fallback_height_);
    if (!decoder_) {
        std::cerr << "[Receiver] HW Decoder init failed!" << std::endl;
        return false;
    }

    running_ = true;
    rx_thread_ = std::thread(&UdpReceiverImpl::receiveLoop, this);
    decode_thread_ = std::thread(&UdpReceiverImpl::decodeLoop, this);

    std::cout << "[Receiver] Pipeline Initialized. Listening on port: " << local_port << std::endl;
    return true;
}

void UdpReceiverImpl::registerCallback(OnFrameReceivedCallback callback) {
    callback_ = std::move(callback);
}

void UdpReceiverImpl::receiveLoop() {
    char buffer[2048];
    while (running_) {
        int recv_len = udp_server_->recv_buffer(buffer, sizeof(buffer));
        if (recv_len <= 0) continue;

        auto* header = reinterpret_cast<protocol::UdpPacketHeader*>(buffer);
        if (header->frame_head != protocol::PACKET_HEAD_MAGIC) continue;
        if (static_cast<uint8_t>(buffer[recv_len - 1]) != protocol::PACKET_TAIL_MAGIC) continue;

        uint8_t mode = header->win_mode;
        uint16_t packet_idx = ntohs(header->packet_idx);
        uint16_t total_packets = ntohs(header->total_packets);
        uint16_t payload_len = ntohs(header->payload_len);

        ChannelState& channel = rx_channels_[mode];
        bool is_new_frame_started = false;

        if (!channel.slices.empty()) {
            if (packet_idx == 0) is_new_frame_started = true;
            else if (channel.expected_total != 0 && total_packets != channel.expected_total) is_new_frame_started = true;
            else if (now_ms() - channel.last_update_time > 80) is_new_frame_started = true;
        }

        if (is_new_frame_started) {
            DecodeTask fallback_task;
            fallback_task.win_mode = mode;
            fallback_task.is_fallback = true;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                decode_queue_.push(std::move(fallback_task));
                queue_cv_.notify_one();
            }
            channel.slices.clear();
        }

        channel.expected_total = total_packets;
        channel.last_update_time = now_ms();
        
        std::vector<uint8_t> payload_data(
            buffer + sizeof(protocol::UdpPacketHeader), 
            buffer + sizeof(protocol::UdpPacketHeader) + payload_len
        );
        channel.slices[packet_idx] = std::move(payload_data);

        if (channel.slices.size() == channel.expected_total) {
            DecodeTask task;
            task.win_mode = mode;
            task.is_fallback = false;
            
            size_t total_size = 0;
            for (const auto& pair : channel.slices) total_size += pair.second.size();
            task.jpeg_buffer.reserve(total_size);
            
            for (const auto& pair : channel.slices) {
                task.jpeg_buffer.insert(task.jpeg_buffer.end(), pair.second.begin(), pair.second.end());
            }

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                if (decode_queue_.size() >= MAX_DECODE_QUEUE_SIZE) {
                    // 队列满时静默丢弃最新帧，防止内存泄漏
                } else {
                    decode_queue_.push(std::move(task));
                    queue_cv_.notify_one();
                }
            }
            
            channel.slices.clear();
            channel.expected_total = 0;
        }
    }
}

void UdpReceiverImpl::decodeLoop() {
    while (running_) {
        DecodeTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { 
                return !decode_queue_.empty() || !running_; 
            });

            if (!running_ && decode_queue_.empty()) break;

            task = std::move(decode_queue_.front());
            decode_queue_.pop();
        }

        transport::DecodedFrame out_frame;

        if (task.is_fallback) {
            out_frame.is_valid = false;
            out_frame.win_info.win_mode = task.win_mode;
            out_frame.image = cv::Mat::zeros(fallback_height_, fallback_width_, CV_8UC1);
            if (callback_) callback_(out_frame);
            continue;
        }

        unsigned char* decoded_data = nullptr;
        int decoded_size = 0;
        ::WinInfo hw_win;
        std::vector<::Label> hw_labels;

        bool decode_ok = decoder_->decode(task.jpeg_buffer.data(), task.jpeg_buffer.size(), 
                                          &decoded_data, decoded_size, hw_labels, hw_win);

        if (!decode_ok || decoded_data == nullptr) {
            out_frame.is_valid = false;
            out_frame.win_info.win_mode = task.win_mode;
            out_frame.image = cv::Mat::zeros(fallback_height_, fallback_width_, CV_8UC1);
            if (callback_) callback_(out_frame);
            continue;
        }

        // === 关键修复：使用 OpenCV 解码底层吐出的纯净 JPEG 码流 ===
        cv::Mat raw_jpeg(1, decoded_size, CV_8UC1, decoded_data);
        cv::Mat decoded_img = cv::imdecode(raw_jpeg, cv::IMREAD_GRAYSCALE);
        
        if (decoded_img.empty()) {
            out_frame.is_valid = false;
            out_frame.image = cv::Mat::zeros(fallback_height_, fallback_width_, CV_8UC1);
        } else {
            out_frame.is_valid = true;
            out_frame.image = decoded_img; 
        }

        // 释放 img_utils 内部 new[] 出来的内存，绝对不能用 free！
        delete[] decoded_data; 
        
        // === 组装回调数据 ===
        out_frame.win_info.timestamp = hw_win.timestamp;
        out_frame.win_info.x         = hw_win.x;
        out_frame.win_info.y         = hw_win.y;
        out_frame.win_info.frame_id  = hw_win.frame_id;
        out_frame.win_info.win_mode  = hw_win.win_mode;
        out_frame.win_info.center_x  = hw_win.center_x;
        out_frame.win_info.center_y  = hw_win.center_y;
        std::memcpy(out_frame.win_info.system_info, hw_win.system_info, sizeof(out_frame.win_info.system_info));

        for (const auto& hl : hw_labels) {
            out_frame.labels.push_back({hl.id, hl.x, hl.y, hl.w, hl.h, hl.conf, hl.cls});
        }

        if (callback_) callback_(out_frame);
    }
}

void UdpReceiverImpl::stop() {
    if (!running_) return; // 防止重复停止
    running_ = false;
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(local_port_);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        char dummy_byte = 0;
        sendto(sock, &dummy_byte, 1, 0, (struct sockaddr*)&addr, sizeof(addr));
        close(sock);
    }
    
    if (udp_server_) {
        udp_server_->destory();
    }
    
    queue_cv_.notify_all();
    
    if (rx_thread_.joinable()) rx_thread_.join();
    if (decode_thread_.joinable()) decode_thread_.join();
    
    udp_server_.reset();
    decoder_.reset();
}