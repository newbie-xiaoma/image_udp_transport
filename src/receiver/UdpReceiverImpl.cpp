#include "UdpReceiverImpl.h"
#include "../protocol/UdpHeader.h"
#include <iostream>
#include <chrono>
#include <cstring>
#include <arpa/inet.h> // 用于 ntohs

UdpReceiverImpl::UdpReceiverImpl() 
    : img_width_(0), img_height_(0), running_(false) {
}

UdpReceiverImpl::~UdpReceiverImpl() {
    stop();
}

uint64_t UdpReceiverImpl::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool UdpReceiverImpl::init(int local_port, int width, int height) {
    if (running_) return true;

    img_width_ = width;
    img_height_ = height;

    // 1. 初始化 UDP 服务端 (监听 0.0.0.0 的指定端口)
    udp_server_ = std::make_unique<UDPOperation>("0.0.0.0", local_port, nullptr);
    if (!udp_server_->create_client()) {
        std::cerr << "[Receiver] Failed to create UDP server on port " << local_port << std::endl;
        return false;
    }

    // 2. 初始化硬件 JPEG 解码器
    decoder_ = std::make_unique<ImgDecode>(img_width_, img_height_);

    // 3. 启动后台接收线程
    running_ = true;
    rx_thread_ = std::thread(&UdpReceiverImpl::receiveLoop, this);

    std::cout << "[Receiver] Initialized successfully. Listening on port: " << local_port << std::endl;
    return true;
}

void UdpReceiverImpl::registerCallback(OnFrameReceivedCallback callback) {
    callback_ = std::move(callback);
}

void UdpReceiverImpl::stop() {
    running_ = false;
    if (udp_server_) {
        // 这一步是为了打破 recv_buffer 的阻塞
        udp_server_->destory(); 
    }
    if (rx_thread_.joinable()) {
        rx_thread_.join();
    }
    udp_server_.reset();
    decoder_.reset();
    rx_channels_.clear();
}

void UdpReceiverImpl::receiveLoop() {
    char buffer[2048]; // 足够容纳 MTU 1500 内的包
    
    while (running_) {
        int recv_len = udp_server_->recv_buffer(buffer, sizeof(buffer));
        if (recv_len <= 0) continue;

        auto* header = reinterpret_cast<protocol::UdpPacketHeader*>(buffer);
        
        // 1. 魔数校验：防御网络上的随机脏报文
        if (header->frame_head != protocol::PACKET_HEAD_MAGIC) continue;
        if (static_cast<uint8_t>(buffer[recv_len - 1]) != protocol::PACKET_TAIL_MAGIC) continue;

        uint8_t mode = header->win_mode;
        // 🚨 必须使用 ntohs 把网络大端序转回本机的 CPU 字节序
        uint16_t packet_idx = ntohs(header->packet_idx);
        uint16_t total_packets = ntohs(header->total_packets);
        uint16_t payload_len = ntohs(header->payload_len);

        ChannelState& channel = rx_channels_[mode];
        bool is_new_frame_started = false;

        // ==========================================
        // 步骤一：帧边界与丢包检测 (状态机核心)
        // ==========================================
        if (!channel.slices.empty()) {
            // 条件A: 收到 0 号包，说明新一帧开始了
            if (packet_idx == 0) is_new_frame_started = true;
            // 条件B: 总包数变了，说明已经是下一张图的报文了
            else if (channel.expected_total != 0 && total_packets != channel.expected_total) is_new_frame_started = true;
            // 条件C: 超时检测 (距离上次收到该通道的包超过 80ms)
            else if (now_ms() - channel.last_update_time > 80) is_new_frame_started = true;
        }

        if (is_new_frame_started) {
            std::cerr << "[Receiver] Packet loss detected for mode 0x" << std::hex << (int)mode << std::dec << "! Triggering fallback." << std::endl;
            handleLostFrame(mode); // 触发黑屏兜底
            channel.slices.clear();
        }

        // ==========================================
        // 步骤二：存储当前切片
        // ==========================================
        channel.expected_total = total_packets;
        channel.last_update_time = now_ms();
        
        std::vector<uint8_t> payload_data(
            buffer + sizeof(protocol::UdpPacketHeader), 
            buffer + sizeof(protocol::UdpPacketHeader) + payload_len
        );
        channel.slices[packet_idx] = std::move(payload_data);

        // ==========================================
        // 步骤三：判断是否拼装完毕
        // ==========================================
        if (channel.slices.size() == channel.expected_total) {
            assembleAndDecode(mode, channel);
            channel.slices.clear();
            channel.expected_total = 0;
        }
    }
}

void UdpReceiverImpl::assembleAndDecode(uint8_t win_mode, ChannelState& channel) {
    // 1. 将所有切片拼接成一块连续内存
    size_t total_size = 0;
    for (const auto& pair : channel.slices) {
        total_size += pair.second.size();
    }

    std::vector<unsigned char> full_jpeg(total_size);
    size_t offset = 0;
    for (const auto& pair : channel.slices) {
        std::memcpy(full_jpeg.data() + offset, pair.second.data(), pair.second.size());
        offset += pair.second.size();
    }

    // 2. 第一层解码：调用硬件库提取业务信息并剥离 APP15
    unsigned char* stripped_jpeg = nullptr;
    int stripped_jpeg_size = 0;
    ::WinInfo hw_win;
    std::vector<::Label> hw_labels;

    bool parse_success = decoder_->decode(full_jpeg.data(), total_size, &stripped_jpeg, stripped_jpeg_size, hw_labels, hw_win);

    if (!parse_success || stripped_jpeg == nullptr || stripped_jpeg_size <= 0) {
        std::cerr << "[Receiver] ImgDecode failed for mode 0x" << std::hex << (int)win_mode << std::dec << std::endl;
        if (stripped_jpeg) delete[] stripped_jpeg;
        handleLostFrame(win_mode);
        return;
    }

    // 3. 第二层解码：利用 OpenCV 将纯净 JPEG 字节流还原为像素图 cv::Mat
    std::vector<uchar> jpeg_buffer(stripped_jpeg, stripped_jpeg + stripped_jpeg_size);
    cv::Mat img = cv::imdecode(jpeg_buffer, cv::IMREAD_GRAYSCALE); // 假设我们传的是灰度图
    
    delete[] stripped_jpeg; // 🚨 极其重要：释放硬件库 malloc 的内存

    if (img.empty()) {
        std::cerr << "[Receiver] OpenCV cv::imdecode failed!" << std::endl;
        handleLostFrame(win_mode);
        return;
    }

    // 4. 数据结构桥接转换 (原始类型 -> transport::) 并触发回调
    if (callback_) {
        transport::DecodedFrame out_frame;
        out_frame.image = img;
        out_frame.is_valid = true;

        out_frame.win_info.timestamp = hw_win.timestamp;
        out_frame.win_info.x         = hw_win.x;
        out_frame.win_info.y         = hw_win.y;
        out_frame.win_info.frame_id  = hw_win.frame_id;
        out_frame.win_info.win_mode  = hw_win.win_mode;
        out_frame.win_info.center_x  = hw_win.center_x;
        out_frame.win_info.center_y  = hw_win.center_y;
        std::memcpy(out_frame.win_info.system_info, hw_win.system_info, sizeof(hw_win.system_info));

        out_frame.labels.reserve(hw_labels.size());
        for (const auto& lbl : hw_labels) {
            out_frame.labels.push_back({lbl.id, lbl.x, lbl.y, lbl.w, lbl.h, lbl.conf, lbl.cls});
        }

        callback_(out_frame);
    }
}

void UdpReceiverImpl::handleLostFrame(uint8_t win_mode) {
    if (!callback_) return;

    transport::DecodedFrame err_frame;
    // 生成一张长宽与预期一致的纯黑底图 (CV_8UC1 单通道黑图)
    err_frame.image = cv::Mat::zeros(img_height_, img_width_, CV_8UC1);
    err_frame.is_valid = false;
    
    // 填充默认值，确保业务层知道是哪种类型发生了丢包
    err_frame.win_info = {}; 
    err_frame.win_info.win_mode = win_mode;
    
    callback_(err_frame);
}