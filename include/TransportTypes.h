#pragma once

#include <cstdint>
#include <vector>
#include <opencv2/opencv.hpp>

// 使用 transport 命名空间，隔离底层 img_codec.h 中的同名结构体，防止冲突
namespace transport {

/**
 * @brief 目标检测画框信息 (对外暴露版)
 */
struct Label {
    uint16_t id;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    float    conf;
    uint8_t  cls;
};

/**
 * @brief 业务窗口信息 (对外暴露版)
 */
struct WinInfo {
    int64_t  timestamp;
    uint16_t x;
    uint16_t y;
    uint8_t  frame_id;
    uint8_t  win_mode;   // 0x85：电视图像, 0x83：红外图像, 0x05：512窗口图像
    uint16_t center_x;
    uint16_t center_y;
    uint8_t  system_info[36];
};

/**
 * @brief 接收端最终抛给外部业务的解码帧数据
 */
struct DecodedFrame {
    cv::Mat image;                     // 解码后的像素图 (正常图或兜底黑图)
    WinInfo win_info;                  // 提取出的窗口信息
    std::vector<Label> labels;         // 提取出的目标列表
    bool is_valid;                     // true: 完美接收并解码, false: 发生丢包，这是兜底黑图
};

} // namespace transport