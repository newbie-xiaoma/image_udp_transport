#include "img_codec.h"

#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#include "utils.h"

namespace {

constexpr uint8_t kJpegAppMarker = 0xEF;
constexpr uint32_t kJpegQuality = 100;
constexpr uint32_t kJpegeAlignW = 16;
constexpr uint32_t kJpegeAlignH = 2;
constexpr int32_t kDeviceId = 0;

struct RuntimeState {
    std::mutex mutex;
    bool initialized = false;
    aclrtContext context = nullptr;
    aclrtRunMode runMode = ACL_HOST;
    aclrtStream stream = nullptr;
};

struct ImageLayout {
    uint32_t widthStride = 0;
    uint32_t heightStride = 0;
    uint32_t bufferSize = 0;
};

struct DvppBufferDeleter {
    void operator()(void* buffer) const
    {
        if (buffer != nullptr) {
            (void)acldvppFree(buffer);
        }
    }
};

struct DvppPicDescDeleter {
    void operator()(acldvppPicDesc* desc) const
    {
        if (desc != nullptr) {
            (void)acldvppDestroyPicDesc(desc);
        }
    }
};

using DvppBufferPtr = std::unique_ptr<void, DvppBufferDeleter>;
using DvppPicDescPtr = std::unique_ptr<acldvppPicDesc, DvppPicDescDeleter>;

RuntimeState& GetRuntimeState()
{
    static RuntimeState state;
    return state;
}

uint32_t AlignUp(uint32_t value, uint32_t align)
{
    return ((value + align - 1) / align) * align;
}

bool EnsureRuntimeInitialized()
{
    RuntimeState& state = GetRuntimeState();
    std::lock_guard<std::mutex> lock(state.mutex);

    if (state.initialized) {
        return aclrtSetCurrentContext(state.context) == ACL_SUCCESS;
    }

    aclError aclRet = aclInit(nullptr);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclInit failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtSetDevice(kDeviceId);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtSetDevice failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtCreateContext(&state.context, kDeviceId);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtCreateContext failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtSetCurrentContext(state.context);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtSetCurrentContext failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtCreateStream(&state.stream);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtCreateStream failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtGetRunMode(&state.runMode);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtGetRunMode failed, ret=" << aclRet << std::endl;
        return false;
    }

    state.initialized = true;
    return true;
}

aclrtRunMode GetRunMode()
{
    return GetRuntimeState().runMode;
}

aclrtStream GetRuntimeStream()
{
    return GetRuntimeState().stream;
}

ImageLayout BuildImageLayout(uint32_t width, uint32_t height)
{
    ImageLayout layout;
    layout.widthStride = AlignUp(width, kJpegeAlignW);
    layout.heightStride = AlignUp(height, kJpegeAlignH);
    layout.bufferSize = layout.widthStride * layout.heightStride * 3 / 2;
    return layout;
}

bool FillGrayImageToNv12(const cv::Mat& img, const ImageLayout& layout, void* dvppInput)
{
    if (dvppInput == nullptr || img.empty() || img.type() != CV_8UC1) {
        return false;
    }

    std::vector<uint8_t> hostBuffer(layout.bufferSize, 0x80);
    std::memset(hostBuffer.data(), 0, static_cast<size_t>(layout.widthStride) * layout.heightStride);

    for (int row = 0; row < img.rows; ++row) {
        std::memcpy(hostBuffer.data() + static_cast<size_t>(row) * layout.widthStride, img.ptr(row), img.cols);
    }

    if (GetRunMode() == ACL_HOST) {
        const aclError aclRet = aclrtMemcpy(dvppInput, layout.bufferSize,
                                            hostBuffer.data(), layout.bufferSize,
                                            ACL_MEMCPY_HOST_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
            std::cerr << "aclrtMemcpy input image failed, ret=" << aclRet << std::endl;
            return false;
        }
        return true;
    }

    std::memcpy(dvppInput, hostBuffer.data(), layout.bufferSize);
    return true;
}

std::vector<uint8_t> BuildBusinessData(const WinInfo& win, const std::vector<Label>& labels)
{
    const size_t winInfoSize = 8 + 2 + 2 + 1 + 1 + 2 + 2 + sizeof(win.system_info);
    const size_t totalSize = winInfoSize + 2 + (labels.size() * 12) + 1;
    std::vector<uint8_t> payload(totalSize);
    size_t offset = 0;

    for (int i = 7; i >= 0; --i) {
        payload[offset++] = static_cast<uint8_t>((win.timestamp >> (i * 8)) & 0xFF);
    }

    const auto packUint16 = [&](uint16_t value) {
        payload[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(value & 0xFF);
    };
    packUint16(win.x);
    packUint16(win.y);

    payload[offset++] = win.frame_id;
    payload[offset++] = win.win_mode;

    packUint16(win.center_x);
    packUint16(win.center_y);

    std::memcpy(payload.data() + offset, win.system_info, sizeof(win.system_info));
    offset += sizeof(win.system_info);

    const uint16_t labelCount = static_cast<uint16_t>(labels.size());
    packUint16(labelCount);

    for (const auto& label : labels) {
        payload[offset++] = static_cast<uint8_t>((label.id >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.id & 0xFF);
        payload[offset++] = static_cast<uint8_t>((label.x >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.x & 0xFF);
        payload[offset++] = static_cast<uint8_t>((label.y >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.y & 0xFF);
        payload[offset++] = static_cast<uint8_t>((label.w >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.w & 0xFF);
        payload[offset++] = static_cast<uint8_t>((label.h >> 8) & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.h & 0xFF);
        payload[offset++] = static_cast<uint8_t>(label.conf * 255.0f);
        payload[offset++] = label.cls;
    }

    payload[offset++] = 0x50;
    return payload;
}

bool BuildJpegWithMetadata(const uint8_t* jpegData, uint32_t jpegSize, const std::vector<uint8_t>& businessData,
                           unsigned char** dst, int& size)
{
    if (jpegData == nullptr || dst == nullptr || jpegSize < 2) {
        return false;
    }
    if (jpegData[0] != 0xFF || jpegData[1] != 0xD8) {
        std::cerr << "Encoded jpeg stream has invalid SOI marker." << std::endl;
        return false;
    }

    const size_t appPayloadSize = businessData.size();
    if (appPayloadSize > 0xFFFF - 2) {
        std::cerr << "JPEG APP payload is too large." << std::endl;
        return false;
    }

    const size_t totalSize = jpegSize + 4 + appPayloadSize;
    *dst = new unsigned char[totalSize];

    size_t offset = 0;
    (*dst)[offset++] = jpegData[0];
    (*dst)[offset++] = jpegData[1];
    (*dst)[offset++] = 0xFF;
    (*dst)[offset++] = kJpegAppMarker;

    const uint16_t segmentLength = static_cast<uint16_t>(appPayloadSize + 2);
    (*dst)[offset++] = static_cast<uint8_t>((segmentLength >> 8) & 0xFF);
    (*dst)[offset++] = static_cast<uint8_t>(segmentLength & 0xFF);

    std::memcpy(*dst + offset, businessData.data(), businessData.size());
    offset += businessData.size();

    std::memcpy(*dst + offset, jpegData + 2, jpegSize - 2);
    offset += jpegSize - 2;

    size = static_cast<int>(offset);
    return true;
}

bool ParseBusinessData(const uint8_t* data, size_t size, WinInfo& winInfo, std::vector<Label>& labels)
{
    const size_t winInfoSize = 8 + 2 + 2 + 1 + 1 + 2 + 2 + sizeof(winInfo.system_info);
    const size_t minSize = winInfoSize + 2 + 1;
    if (data == nullptr || size < minSize) {
        return false;
    }

    size_t offset = 0;
    winInfo.timestamp = Stream2Timestamp(data + offset);
    offset += 8;
    winInfo.x = fromBigEndian2Ushort(data + offset);
    offset += 2;
    winInfo.y = fromBigEndian2Ushort(data + offset);
    offset += 2;
    winInfo.frame_id = data[offset++];
    winInfo.win_mode = data[offset++];
    winInfo.center_x = fromBigEndian2Ushort(data + offset);
    offset += 2;
    winInfo.center_y = fromBigEndian2Ushort(data + offset);
    offset += 2;
    std::memcpy(winInfo.system_info, data + offset, sizeof(winInfo.system_info));
    offset += sizeof(winInfo.system_info);

    const uint16_t labelCount = fromBigEndian2Ushort(data + offset);
    offset += 2;
    const size_t expectedSize = offset + (static_cast<size_t>(labelCount) * 12) + 1;
    if (size < expectedSize || data[expectedSize - 1] != 0x50) {
        return false;
    }

    labels.clear();
    labels.reserve(labelCount);
    for (uint16_t i = 0; i < labelCount; ++i) {
        const size_t base = offset + (static_cast<size_t>(i) * 12);
        Label label;
        label.id = fromBigEndian2Ushort(data + base);
        label.x = fromBigEndian2Ushort(data + base + 2);
        label.y = fromBigEndian2Ushort(data + base + 4);
        label.w = fromBigEndian2Ushort(data + base + 6);
        label.h = fromBigEndian2Ushort(data + base + 8);
        label.conf = static_cast<float>(data[base + 10]) / 255.0f;
        label.cls = data[base + 11];
        labels.push_back(label);
    }

    return true;
}

bool CopyDvppBufferToHost(const void* dvppData, uint32_t dataSize, std::vector<uint8_t>* hostData)
{
    if (dvppData == nullptr || hostData == nullptr || dataSize == 0) {
        return false;
    }

    hostData->resize(dataSize);
    if (GetRunMode() == ACL_HOST) {
        const aclError aclRet = aclrtMemcpy(hostData->data(), dataSize, dvppData, dataSize,
                                            ACL_MEMCPY_DEVICE_TO_HOST);
        if (aclRet != ACL_SUCCESS) {
            std::cerr << "aclrtMemcpy encoded jpeg failed, ret=" << aclRet << std::endl;
            return false;
        }
        return true;
    }

    std::memcpy(hostData->data(), dvppData, dataSize);
    return true;
}

bool ExtractMetadataAndRebuildJpeg(const uint8_t* src, int size, WinInfo& winInfo, std::vector<Label>& labels,
                                   int& consumed, std::vector<uint8_t>* jpegData)
{
    consumed = 0;
    if (jpegData == nullptr || src == nullptr || size < 4 || src[0] != 0xFF || src[1] != 0xD8) {
        return false;
    }

    labels.clear();
    std::vector<uint8_t> rebuilt;
    rebuilt.reserve(size);
    rebuilt.push_back(src[0]);
    rebuilt.push_back(src[1]);

    bool found = false;
    size_t offset = 2;
    while (offset < static_cast<size_t>(size)) {
        if (src[offset] != 0xFF) {
            rebuilt.insert(rebuilt.end(), src + offset, src + size);
            *jpegData = std::move(rebuilt);
            return found;
        }

        const size_t markerStart = offset;
        while (offset < static_cast<size_t>(size) && src[offset] == 0xFF) {
            ++offset;
        }
        if (offset >= static_cast<size_t>(size)) {
            break;
        }

        const uint8_t marker = src[offset++];
        if (marker == 0x00) {
            rebuilt.insert(rebuilt.end(), src + markerStart, src + size);
            *jpegData = std::move(rebuilt);
            return found;
        }

        if (marker == 0xDA || marker == 0xD9) {
            rebuilt.insert(rebuilt.end(), src + markerStart, src + size);
            *jpegData = std::move(rebuilt);
            return found;
        }

        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            rebuilt.insert(rebuilt.end(), src + markerStart, src + offset);
            continue;
        }

        if (offset + 2 > static_cast<size_t>(size)) {
            break;
        }

        const uint16_t segmentLength = static_cast<uint16_t>((src[offset] << 8) | src[offset + 1]);
        if (segmentLength < 2 || offset + segmentLength > static_cast<size_t>(size)) {
            break;
        }

        const uint8_t* segmentPayload = src + offset + 2;
        const size_t payloadSize = segmentLength - 2;
        const size_t segmentEnd = offset + segmentLength;

        if (marker == kJpegAppMarker) {
            if (found || !ParseBusinessData(segmentPayload, payloadSize, winInfo, labels)) {
                labels.clear();
                jpegData->assign(src, src + size);
                consumed = 0;
                return false;
            }
            found = true;
            consumed = static_cast<int>(segmentEnd - markerStart);
        } else {
            rebuilt.insert(rebuilt.end(), src + markerStart, src + segmentEnd);
        }

        offset = segmentEnd;
    }

    if (!rebuilt.empty()) {
        *jpegData = std::move(rebuilt);
    } else {
        jpegData->assign(src, src + size);
    }
    return false;
}

}  // namespace

ImgEncode::ImgEncode(int width, int height) : width_(width), height_(height)
{
    wStride_ = AlignUp(width_, kJpegeAlignW);
    hStride_ = AlignUp(height_, kJpegeAlignH);
    initResource(wStride_, hStride_);
}

ImgEncode::~ImgEncode()
{
    if (channelDesc_ != nullptr) {
        (void)acldvppDestroyChannel(channelDesc_);
        (void)acldvppDestroyChannelDesc(channelDesc_);
        channelDesc_ = nullptr;
    }
    if (jpegeConfig_ != nullptr) {
        (void)acldvppDestroyJpegeConfig(jpegeConfig_);
        jpegeConfig_ = nullptr;
    }
}

void ImgEncode::initResource(uint32_t wStride, uint32_t hStride)
{
    (void)wStride;
    (void)hStride;

    if (!EnsureRuntimeInitialized()) {
        return;
    }

    channelDesc_ = acldvppCreateChannelDesc();
    if (channelDesc_ == nullptr) {
        std::cerr << "acldvppCreateChannelDesc failed" << std::endl;
        return;
    }

    aclError aclRet = acldvppCreateChannel(channelDesc_);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "acldvppCreateChannel failed, ret=" << aclRet << std::endl;
        (void)acldvppDestroyChannelDesc(channelDesc_);
        channelDesc_ = nullptr;
        return;
    }

    jpegeConfig_ = acldvppCreateJpegeConfig();
    if (jpegeConfig_ == nullptr) {
        std::cerr << "acldvppCreateJpegeConfig failed" << std::endl;
        (void)acldvppDestroyChannel(channelDesc_);
        (void)acldvppDestroyChannelDesc(channelDesc_);
        channelDesc_ = nullptr;
        return;
    }

    aclRet = acldvppSetJpegeConfigLevel(jpegeConfig_, kJpegQuality);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "acldvppSetJpegeConfigLevel failed, ret=" << aclRet << std::endl;
        (void)acldvppDestroyJpegeConfig(jpegeConfig_);
        jpegeConfig_ = nullptr;
        (void)acldvppDestroyChannel(channelDesc_);
        (void)acldvppDestroyChannelDesc(channelDesc_);
        channelDesc_ = nullptr;
        return;
    }

    channelCreated_ = true;
}

bool ImgEncode::encode(cv::Mat& img, const WinInfo& win, const std::vector<Label>& labels,
                       uint32_t quality, unsigned char** dst, int& size)
{
    if (!channelCreated_ || dst == nullptr || img.empty() || img.type() != CV_8UC1) {
        return false;
    }
    if (img.cols != static_cast<int>(width_) || img.rows != static_cast<int>(height_)) {
        std::cerr << "Input image size does not match encoder init size." << std::endl;
        return false;
    }
    if (!EnsureRuntimeInitialized()) {
        return false;
    }
    if (quality > 100) {
        std::cerr << "JPEG quality must be within [0, 100], got " << quality << std::endl;
        return false;
    }

    const ImageLayout layout = BuildImageLayout(width_, height_);
    aclError aclRet = acldvppSetJpegeConfigLevel(jpegeConfig_, quality);
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "acldvppSetJpegeConfigLevel failed, ret=" << aclRet << std::endl;
        return false;
    }

    void* rawInput = nullptr;
    aclRet = acldvppMalloc(&rawInput, layout.bufferSize);
    if (aclRet != ACL_SUCCESS || rawInput == nullptr) {
        std::cerr << "acldvppMalloc input buffer failed, ret=" << aclRet << std::endl;
        return false;
    }
    DvppBufferPtr dvppInput(rawInput);

    if (!FillGrayImageToNv12(img, layout, dvppInput.get())) {
        return false;
    }

    DvppPicDescPtr inputDesc(acldvppCreatePicDesc());
    if (!inputDesc) {
        std::cerr << "acldvppCreatePicDesc failed" << std::endl;
        return false;
    }

    aclRet = acldvppSetPicDescData(inputDesc.get(), dvppInput.get());
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescSize(inputDesc.get(), layout.bufferSize);
    }
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescFormat(inputDesc.get(), PIXEL_FORMAT_YUV_SEMIPLANAR_420);
    }
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescWidth(inputDesc.get(), width_);
    }
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescHeight(inputDesc.get(), height_);
    }
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescWidthStride(inputDesc.get(), layout.widthStride);
    }
    if (aclRet == ACL_SUCCESS) {
        aclRet = acldvppSetPicDescHeightStride(inputDesc.get(), layout.heightStride);
    }
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "Failed to initialize input picture description, ret=" << aclRet << std::endl;
        return false;
    }

    uint32_t predictedSize = 0;
    aclRet = acldvppJpegPredictEncSize(inputDesc.get(), jpegeConfig_, &predictedSize);
    if (aclRet != ACL_SUCCESS || predictedSize == 0) {
        std::cerr << "acldvppJpegPredictEncSize failed, ret=" << aclRet
                  << ", predictedSize=" << predictedSize << std::endl;
        return false;
    }

    void* rawOutput = nullptr;
    aclRet = acldvppMalloc(&rawOutput, predictedSize);
    if (aclRet != ACL_SUCCESS || rawOutput == nullptr) {
        std::cerr << "acldvppMalloc output buffer failed, ret=" << aclRet << std::endl;
        return false;
    }
    DvppBufferPtr dvppOutput(rawOutput);

    uint32_t actualSize = predictedSize;
    aclRet = acldvppJpegEncodeAsync(channelDesc_, inputDesc.get(), dvppOutput.get(), &actualSize,
                                    jpegeConfig_, GetRuntimeStream());
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "acldvppJpegEncodeAsync failed, ret=" << aclRet << std::endl;
        return false;
    }

    aclRet = aclrtSynchronizeStream(GetRuntimeStream());
    if (aclRet != ACL_SUCCESS) {
        std::cerr << "aclrtSynchronizeStream failed, ret=" << aclRet << std::endl;
        return false;
    }

    std::vector<uint8_t> jpegData;
    if (!CopyDvppBufferToHost(dvppOutput.get(), actualSize, &jpegData)) {
        return false;
    }

    const std::vector<uint8_t> businessData = BuildBusinessData(win, labels);
    return BuildJpegWithMetadata(jpegData.data(), actualSize, businessData, dst, size);
}

ImgDecode::ImgDecode(uint32_t width, uint32_t height) : width_(width), height_(height)
{
}

bool ImgDecode::parseSei(unsigned char* src, int size, WinInfo& win_info, std::vector<Label>& labels, int& consumed)
{
    std::vector<uint8_t> jpegData;
    return ExtractMetadataAndRebuildJpeg(src, size, win_info, labels, consumed, &jpegData);
}

bool ImgDecode::decode(unsigned char* src, int size, unsigned char** jpegData, int& jpegSize,
                       std::vector<Label>& labels, WinInfo& win_info)
{
    (void)width_;
    (void)height_;

    jpegSize = 0;
    if (jpegData == nullptr || src == nullptr || size <= 0) {
        return false;
    }

    *jpegData = nullptr;
    int consumed = 0;
    std::vector<uint8_t> rebuiltJpeg;
    const bool parsed = ExtractMetadataAndRebuildJpeg(src, size, win_info, labels, consumed, &rebuiltJpeg);
    if (rebuiltJpeg.empty()) {
        return false;
    }

    *jpegData = new unsigned char[rebuiltJpeg.size()];
    std::memcpy(*jpegData, rebuiltJpeg.data(), rebuiltJpeg.size());
    jpegSize = static_cast<int>(rebuiltJpeg.size());
    return parsed;
}
