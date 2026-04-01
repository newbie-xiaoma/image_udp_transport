#pragma once
#include <cstdint>

namespace protocol {

// 约定的包头和包尾魔数，用于校验防止脏数据
constexpr uint8_t PACKET_HEAD_MAGIC = 0x5A; 
constexpr uint8_t PACKET_TAIL_MAGIC = 0xA5;

// 定义图像类型（与 WinInfo 中的 win_mode 保持一致）
constexpr uint8_t WIN_MODE_TV  = 0x85;  // 电视图像
constexpr uint8_t WIN_MODE_IR  = 0x83;  // 红外图像
constexpr uint8_t WIN_MODE_512 = 0x05;  // 512窗口图像

// 强制编译器按 1 字节对齐，防止内存自动补齐导致网络错位
#pragma pack(push, 1) 
struct UdpPacketHeader {
    uint8_t  frame_head;    // 1字节: 帧头标识 (固定为 0x5A)
    uint8_t  win_mode;      // 1字节: 图像类型 (0x85 / 0x83 / 0x05)
    uint16_t packet_idx;    // 2字节: 当前包号 (从 0 开始)
    uint16_t total_packets; // 2字节: 当前帧的总包数
    uint16_t payload_len;   // 2字节: 当前 UDP 包承载的真实 JPEG 切片长度
};
#pragma pack(pop)

} // namespace protocol