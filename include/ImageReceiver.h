#pragma once
#include <functional>
#include "TransportTypes.h"

// 注意这里使用了 transport::DecodedFrame
using OnFrameReceivedCallback = std::function<void(const transport::DecodedFrame&)>;

class ImageReceiver {
public:
    virtual ~ImageReceiver() = default;

    virtual bool init(int local_port, int width, int height) = 0;
    virtual void registerCallback(OnFrameReceivedCallback callback) = 0;
    virtual void stop() = 0;
};