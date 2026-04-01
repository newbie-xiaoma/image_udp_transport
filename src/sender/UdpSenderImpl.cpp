#include "UdpSenderImpl.h"
#include "../protocol/UdpHeader.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h> // 用于 htons (网络字节序转换)
constexpr size_t UdpSenderImpl::MAX_PAYLOAD_SIZE;
UdpSenderImpl::UdpSenderImpl() 
    : img_width_(0), img_height_(0), is_initialized_(false) {
}

UdpSenderImpl::~UdpSenderImpl() {
    stop();
}

bool UdpSenderImpl::init(const char* remote_ip, int remote_port, int width, int height) {
    if (is_initialized_) {
        return true;
    }

    img_width_ = width;
    img_height_ = height;

    // 1. 初始化底层的 UDP 客户端
    // 这里传入 nullptr 作为网卡 interface，代表让操作系统自动路由
    udp_client_ = std::make_unique<UDPOperation>(remote_ip, remote_port, nullptr);
    if (!udp_client_->create_client()) {
        std::cerr << "[Sender] Failed to create UDP client." << std::endl;
        return false;
    }

    // 2. 初始化硬件 JPEG 编码器
    encoder_ = std::make_unique<ImgEncode>(img_width_, img_height_);
    
    is_initialized_ = true;
    std::cout << "[Sender] Initialized successfully. Target: " << remote_ip << ":" << remote_port << std::endl;
    return true;
}

bool UdpSenderImpl::sendFrame(cv::Mat& img, 
                              const transport::WinInfo& win, 
                              const std::vector<transport::Label>& labels, 
                              uint32_t quality) {
    if (!is_initialized_ || !encoder_ || !udp_client_) {
        std::cerr << "[Sender] Not initialized." << std::endl;
        return false;
    }

    // ==========================================
    // 步骤一：数据结构桥接转换 (transport:: -> 原始类型)
    // ==========================================
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
    hw_labels.reserve(labels.size());
    for (const auto& lbl : labels) {
        ::Label hw_lbl;
        hw_lbl.id   = lbl.id;
        hw_lbl.x    = lbl.x;
        hw_lbl.y    = lbl.y;
        hw_lbl.w    = lbl.w;
        hw_lbl.h    = lbl.h;
        hw_lbl.conf = lbl.conf;
        hw_lbl.cls  = lbl.cls;
        hw_labels.push_back(hw_lbl);
    }

    // ==========================================
    // 步骤二：调用 Ascend 硬件执行 JPEG 编码并注入 APP15
    // ==========================================
    unsigned char* encoded_data = nullptr;
    int encoded_size = 0;
    
    bool encode_success = encoder_->encode(img, hw_win, hw_labels, quality, &encoded_data, encoded_size);
    if (!encode_success || encoded_data == nullptr || encoded_size <= 0) {
        std::cerr << "[Sender] Hardware encode failed!" << std::endl;
        return false;
    }

    // ==========================================
    // 步骤三：UDP 分片与封包发送
    // ==========================================
    // 计算总共需要切多少片 (向上取整)
    uint16_t total_packets = (encoded_size + MAX_PAYLOAD_SIZE - 1) / MAX_PAYLOAD_SIZE;

    for (uint16_t i = 0; i < total_packets; ++i) {
        // 计算当前这片要发多少字节
        size_t offset = i * MAX_PAYLOAD_SIZE;
        size_t payload_len = std::min(MAX_PAYLOAD_SIZE, static_cast<size_t>(encoded_size - offset));

        // 申请一块内存装载: 头 + 载荷 + 尾
        size_t packet_size = sizeof(protocol::UdpPacketHeader) + payload_len + 1;
        std::vector<char> send_buffer(packet_size);

        // 1. 填充协议头
        auto* header = reinterpret_cast<protocol::UdpPacketHeader*>(send_buffer.data());
        header->frame_head    = protocol::PACKET_HEAD_MAGIC;
        header->win_mode      = win.win_mode; // 从传入的业务结构中提取类型
        
        // 🚨 核心细节：网络传输必须使用大端序 (htons)，防止跨 CPU 架构解析出错
        header->packet_idx    = htons(i);
        header->total_packets = htons(total_packets);
        header->payload_len   = htons(static_cast<uint16_t>(payload_len));

        // 2. 拷贝 JPEG 载荷
        std::memcpy(send_buffer.data() + sizeof(protocol::UdpPacketHeader), 
                    encoded_data + offset, payload_len);

        // 3. 填充帧尾
        send_buffer[packet_size - 1] = protocol::PACKET_TAIL_MAGIC;

        // 4. 调用 UDPOperation 发送
        udp_client_->send_buffer(send_buffer.data(), packet_size);
    }

    // ==========================================
    // 步骤四：清理硬件编码器分配的内存
    // ==========================================
    delete[] encoded_data;

    return true;
}

void UdpSenderImpl::stop() {
    if (udp_client_) {
        udp_client_->destory(); // 调用你 udp_operation.h 里的清理方法
        udp_client_.reset();
    }
    encoder_.reset();
    is_initialized_ = false;
}