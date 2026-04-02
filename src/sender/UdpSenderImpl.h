#pragma once

#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

#include "../../include/ImageSender.h"
#include "../network/udp_operation.h"
#include "img_codec.h" 

struct SendTask {
    cv::Mat img;
    transport::WinInfo win;
    std::vector<transport::Label> labels;
    uint32_t quality;
};

class UdpSenderImpl : public ImageSender {
public:
    UdpSenderImpl();
    ~UdpSenderImpl() override;

    bool init(const char* remote_ip, int remote_port, int width, int height) override;
    
    bool sendFrameSync(const cv::Mat& img, 
                       const transport::WinInfo& win, 
                       const std::vector<transport::Label>& labels, 
                       uint32_t quality) override;

    bool sendFrameAsync(const cv::Mat& img, 
                        const transport::WinInfo& win, 
                        const std::vector<transport::Label>& labels, 
                        uint32_t quality) override;
                   
    void stop() override;

private:
    void sendLoop();

    bool internalEncodeAndSend(const cv::Mat& img, 
                               const transport::WinInfo& win, 
                               const std::vector<transport::Label>& labels, 
                               uint32_t quality, 
                               bool apply_pacing);

private:
    std::unique_ptr<UDPOperation> udp_client_;
    std::unique_ptr<ImgEncode> encoder_;
    
    int img_width_;
    int img_height_;
    bool is_initialized_;

    static constexpr size_t MAX_PAYLOAD_SIZE = 1024; 

    std::mutex hardware_mutex_;

    std::queue<SendTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::thread worker_thread_;
    std::atomic<bool> running_;
    
    static constexpr size_t MAX_QUEUE_SIZE = 50; 
};