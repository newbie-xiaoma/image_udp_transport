/*************************************************************************************************************************
 * Copyright 2026 Grifcc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the “Software”), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 *all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *SOFTWARE.
 *************************************************************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "acl/ops/acl_dvpp.h"
#include <opencv2/opencv.hpp>

struct Label
{
    unsigned short id;
    unsigned short x;
    unsigned short y;
    unsigned short w;
    unsigned short h;
    float conf;
    unsigned char cls;
};

struct WinInfo
{
    long long timestamp;
    unsigned short x;         // 红外和电视填充0
    unsigned short y;         // 红外和电视填充0
    unsigned char frame_id;   // 帧id
    unsigned char win_mode;   // 窗口模式 ：0x85：电视图像  0x83：红外图像  0x05：512窗口图像
    unsigned short center_x;  // 轴位信息x
    unsigned short center_y;  // 轴位信息y
    unsigned char  system_info[36]; // 系统信息
};

class ImgEncode
{
public:
    ImgEncode(int width, int height);
    ~ImgEncode();
    bool encode(cv::Mat& img, const WinInfo& win, const std::vector<Label>& labels,
                uint32_t quality, unsigned char **dst, int &size);

private:
    void initResource(uint32_t wStride, uint32_t hStride);

    uint32_t width_, height_, wStride_, hStride_;
    acldvppChannelDesc* channelDesc_ = nullptr;
    acldvppJpegeConfig* jpegeConfig_ = nullptr;
    bool channelCreated_ = false;
};

class ImgDecode
{
public:
    ImgDecode(uint32_t width, uint32_t height);
    ~ImgDecode() = default;
    bool parseSei(unsigned char* data, int size, WinInfo &win_info, std::vector<Label> &labels, int &consumed);
    bool decode(unsigned char* src, int size, unsigned char** jpegData, int& jpegSize,
                std::vector<Label> &labels, WinInfo &win_info);

private:
    uint32_t width_;
    uint32_t height_;
};
