# Image UDP Transport SDK

基于华为昇腾（Ascend NPU）硬件加速的图像 UDP 传输 SDK。本库旨在解决 AI 推理流水线或多媒体系统中，**高分辨率图像与业务元数据（如画框坐标、时间戳等）的高效网络传输**问题。

SDK 内部实现了基于 ACL DVPP 的 JPEG 硬件编解码、自动 UDP 分片与重组、多通道并发传输以及防撕裂的丢包兜底策略，对外提供极其纯净、易用的 C++ 接口。

---

## ✨ 核心特性

* 🚀 **硬件加速编码**：底层集成昇腾 ACL DVPP JPEGE，将单通道灰度图直接编码为 JPEG 码流。
* 📦 **业务数据隐式绑定**：独创性地将自定义业务数据（`WinInfo` 窗口信息、`Label` 检测框信息）打包注入到 JPEG 的 **APP15** 段中，实现图像与数据的物理级绑定。
* 🌐 **工业级 UDP 传输**：
  * **自动分片与重组**：主动将大块 JPEG 码流切割为 MTU 安全（载荷 1024 字节）的 UDP 数据包。
  * **多通道复用**：接收端内部维护独立的通道状态机，支持 `电视(0x85)`、`红外(0x83)`、`512窗口(0x05)` 图像的并发交替传输，互不干扰。
* 🛡️ **抗丢包与黑屏兜底**：通过严格的报头魔数校验、0号包检测与超时（Timeout）机制，一旦侦测到丢包，立即阻断残缺帧的解码，并向外部输出一张纯黑底图（Fallback），彻底杜绝下游画面撕裂或程序崩溃。
* 🔌 **极简解耦接口**：外部调用者无需关心任何底层网络 Socket 逻辑或硬件 Context 绑定，只需包含 `TransportTypes.h` 和相关接口类即可。

---

## 📂 目录结构

```text
image_udp_transport/
├── CMakeLists.txt              # 顶层构建脚本
├── build.sh                    # 一键编译脚本
├── include/                    # 🚀 对外暴露的公共 API 目录 (外部项目仅需引入此目录)
│   ├── ImageSender.h           # 发送端接口
│   ├── ImageReceiver.h         # 接收端接口
│   └── TransportTypes.h        # 纯净的业务数据结构 (WinInfo, Label, DecodedFrame)
├── src/                        # 内部实现源码 (外部项目无需关心)
│   ├── network/                # 底层 UDP Socket 封装
│   ├── protocol/               # UDP 分片协议头定义
│   ├── receiver/               # 接收端多通道状态机与重组逻辑
│   └── sender/                 # 发送端编码与分片发包逻辑
├── third_party/                # 第三方依赖
│   └── img_utils/              # 核心硬件 JPEG 编解码库 (ACL DVPP)
└── examples/                   # 最小可用使用范例
    ├── sender_demo.cpp         
    └── receiver_demo.cpp       
```

---

## 🛠️ 环境要求

* **OS**: Linux (支持华为昇腾环境的 OS)
* **编译器**: 支持 **C++14** 及以上的 GCC/Clang
* **构建工具**: CMake 3.10+
* **依赖库**: 
  * Ascend CANN Toolkit (默认路径: `/usr/local/Ascend/ascend-toolkit/latest`)
  * OpenCV 4.x

---

## 🚀 快速构建与测试

项目提供了一键编译脚本 `build.sh`，内部会自动执行 CMake 和多线程并行编译。

```bash
# 1. 赋予执行权限
chmod +x build.sh

# 2. 一键编译
./build.sh
```
*(注：如果你的 Ascend CANN 未安装在默认路径，可修改 CMakeLists.txt 中的 `ASCEND_PATH`)*

编译成功后，在 `build/` 目录下会生成两个可执行程序。**请开启两个终端窗口进行闭环测试：**

**终端 1 (先启动接收端)：**
```bash
cd build
./receiver_demo 8080
```

**终端 2 (后启动发送端)：**
```bash
cd build
./sender_demo 127.0.0.1 8080
```

测试成功后，终端将输出 `[Callback Triggered] -> Success!`，并在目录下生成解码还原出的 `.jpg` 测试图片。

---

## 📖 接口集成指南

将本 SDK 嵌入到你的主业务项目中极其简单。

### 发送端 (Sender) 使用示例

```cpp
#include "ImageSender.h"
#include "TransportTypes.h"
#include <opencv2/opencv.hpp>

// 1. 初始化发送端
UdpSenderImpl sender;
sender.init("192.168.1.100", 8080, 1280, 1024); // 目标IP, 端口, 图像宽高

// 2. 准备原始图像与业务数据
cv::Mat img = cv::imread("test.jpg", cv::IMREAD_GRAYSCALE);
transport::WinInfo win_info;
win_info.win_mode = 0x85; // 设置图像类型
std::vector<transport::Label> labels = { /* 填入画框数据 */ };

// 3. 触发硬件编码并发送 (参数4为JPEG压缩质量 0-100)
sender.sendFrame(img, win_info, labels, 80);

// 4. 退出前清理
sender.stop();
```

### 接收端 (Receiver) 使用示例

接收端采用**异步回调**机制，不会阻塞你的主业务线程。

```cpp
#include "ImageReceiver.h"
#include "TransportTypes.h"
#include <iostream>

// 1. 初始化接收端
UdpReceiverImpl receiver;
// 监听本地 8080 端口，并设置预期分辨率用于生成兜底黑图
receiver.init(8080, 1280, 1024); 

// 2. 注册数据回调函数
receiver.registerCallback([](const transport::DecodedFrame& frame) {
    if (frame.is_valid) {
        std::cout << "收到完整图像，类型: 0x" << std::hex << (int)frame.win_info.win_mode << std::endl;
        // frame.image 即为解码后的 cv::Mat 像素图
        // frame.labels 为提取出的画框数据
    } else {
        std::cerr << "发生网络丢包，触发防撕裂机制，当前 frame.image 是一张纯黑图" << std::endl;
    }
});

// 3. 业务主线程可继续执行其他任务...

// 4. 退出前清理
receiver.stop();
```
---
## 📡 附录：底层通信协议说明

本 SDK 在底层 UDP 传输时，采用了紧凑的自定义切片协议头，保障了多路复用与乱序检测的能力。单个 UDP 包的内存布局如下：

| 字段 | 大小 | 说明 |
| :--- | :--- | :--- |
| `frame_head` | 1 Byte | 帧头魔数验证，固定为 `0x5A` |
| `win_mode` | 1 Byte | 通道类型标识 (如 `0x85`, `0x83`, `0x05`) |
| `packet_idx` | 2 Bytes| 当前切片包号 (大端序)，从 0 开始 |
| `total_packets` | 2 Bytes| 本帧切片总数 (大端序) |
| `payload_len` | 2 Bytes| 后续携带的 JPEG 载荷真实长度 (大端序) |
| **`Payload`** | N Bytes| 携带 APP15 的 JPEG 局部码流 (最大 1024 字节) |
| `frame_tail` | 1 Byte | 帧尾魔数验证，固定为 `0xA5` |
---