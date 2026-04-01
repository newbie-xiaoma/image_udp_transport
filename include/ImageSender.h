#pragma once
#include <vector>
#include <opencv2/opencv.hpp>
#include "TransportTypes.h" 

class ImageSender {
public:
    virtual ~ImageSender() = default;

    virtual bool init(const char* remote_ip, int remote_port, int width, int height) = 0;

    // 注意这里使用了 transport::WinInfo 和 transport::Label
    virtual bool sendFrame(cv::Mat& img, 
                           const transport::WinInfo& win, 
                           const std::vector<transport::Label>& labels, 
                           uint32_t quality) = 0;

    virtual void stop() = 0;
};