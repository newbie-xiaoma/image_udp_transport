#pragma once

#include <vector>
#include <opencv2/opencv.hpp>
#include "TransportTypes.h"

class ImageSender {
public:
    virtual ~ImageSender() = default;

    virtual bool init(const char* remote_ip, int remote_port, int width, int height) = 0;
    
    // 1. 同步发送：阻塞当前线程，发完才返回（适合单步调试或低频发送）
    virtual bool sendFrameSync(const cv::Mat& img, 
                               const transport::WinInfo& win, 
                               const std::vector<transport::Label>& labels, 
                               uint32_t quality) = 0;

    // 2. 异步发送：瞬间返回，交由后台线程平滑发包（适合高频流媒体、AI推理主循环）
    virtual bool sendFrameAsync(const cv::Mat& img, 
                                const transport::WinInfo& win, 
                                const std::vector<transport::Label>& labels, 
                                uint32_t quality) = 0;

    virtual void stop() = 0;
};