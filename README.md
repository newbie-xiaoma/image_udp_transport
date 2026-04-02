# 高性能 UDP 图像实时传输 SDK (Image UDP Transport)

基于华为昇腾（Ascend NPU）硬件加速的图像 UDP 传输 SDK。本库旨在解决 AI 推理流水线或多媒体系统中，**高分辨率图像与业务元数据（如画框坐标、时间戳等）的高效网络传输**问题。

SDK 内部实现了基于 ACL DVPP 的 JPEG 硬件编解码、自动 UDP 分片与重组、多通道并发传输以及防撕裂的丢包兜底策略，对外提供极其纯净、易用的 C++ 接口。

## ✨ 核心特性 (Core Features)

* **🚀 硬件级加速**：无缝对接 `img_utils`，利用底层硬件完成 NV12 转换与高压缩比 JPEG 编解码，极大降低 CPU 负载。
* **🛡️ 纳秒级精准控流 (Micro-Pacing)**：发送端内置极高精度的发包间隙控制算法（15微秒自旋锁），将大体积图像平滑切片，完美防止千兆交换机和系统内核的突发洪峰击穿 (Micro-burst Overflow)。
* **🌊 三级异步流水线 (Async Pipeline)**：接收端采用 `网络极速收包 -> NPU解码 -> 异步磁盘 I/O` 的无阻塞架构。即便面对每秒上百张的高清图片落盘需求，也绝不反噬阻塞网络层。
* **🛡️ 丢包兜底机制 (Anti-Tear & Fallback)**：UDP 层自带帧切片重组与超时机制，一旦侦测到不可逆的网络丢包，立即阻断残缺帧解码并触发纯黑帧兜底，彻底杜绝图像撕裂和野指针崩溃。
* **🛑 优雅退出 (Graceful Shutdown)**：内置自发唤醒包 (Dummy Wake-up Packet) 机制，完美解决 Linux UDP `recvfrom` 线程死锁问题，实现 `Ctrl+C` 秒级安全无感退出。

---

## 🛠️ 环境依赖 (Dependencies)

* **操作系统**: Linux (推荐 Ubuntu 20.04 / 22.04)
* **编译器**: GCC / G++ (支持 C++ 14 及以上)
* **构建工具**: CMake (>= 3.10)
* **核心依赖库**: 
  * `OpenCV` (用于图像深拷贝、格式重排与 `cv::imdecode` 像素解码)
  * `img_utils` (华为昇腾底层 NPU 硬件加速库)

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
## 🔨 编译构建 (Build Instructions)

提供了一键构建脚本，在项目根目录下执行：

```bash
chmod +x build.sh
./build.sh
```
编译产物将生成在 `build/` 目录下，包含 `sender_demo` 和 `receiver_demo` 两个可执行文件。

---

## 🚀 运行与测试 (Usage & Demos)

### 1. 启动接收端 (Receiver)

接收端默认监听 `0.0.0.0`，适配本机所有网卡。

```bash
cd build
./receiver_demo -p 8080
```
* **自动存图功能**：启动后，程序会在当前目录下自动创建 `saved_frames/` 文件夹，并将收到的有效图片以 `mode_XX_fXXX.jpg` 的格式交由后台线程实时落盘。

### 2. 启动发送端 (Sender)

推荐使用单路模式进行点对点压测。支持自定义目标 IP、端口、压测时间和传感器模拟模式。

```bash
cd build
# 测试 IR 红外模式 (50 FPS)
./sender_demo -i <接收端IP> -p 8080 -t 60 -m 1

# 测试 TV 电视模式 (30 FPS)
./sender_demo -i <接收端IP> -p 8080 -t 60 -m 2

# 测试 512 窗口模式 (20 FPS)
./sender_demo -i <接收端IP> -p 8080 -t 60 -m 3
```

#### 发送端命令行参数说明：
* `-h, --help` : 显示帮助菜单
* `-i, --ip`   : 目标接收端物理 IP 地址 (默认 127.0.0.1)
* `-p, --port` : 目标接收端监听端口 (默认 8080)
* `-t, --time` : 压测持续时间(秒) (默认 60)
* `-m, --mode` : 传感器模拟模式 (1: IR@50Hz, 2: TV@30Hz, 3: 512@20Hz)

---

## 📊 仪表盘监控说明 (Dashboard Monitoring)

接收端运行时，终端会每秒刷新一次极客仪表盘，各项指标含义如下：

* **`[CH] <Mode> | FPS: 50.0`** : 当前通道实际成功接收并解码的有效帧率。
* **`[ALL] Total Valid | FPS: 50.0`** : 所有通道的总有效吞吐量。
* **`[!] Net Fallback | Count: X drops/sec`** : 网络层丢包导致的废帧次数。理想状态下应为 0；若持续飙升，请检查网线物理连接 (`ifconfig` 查看 RX errors) 或验证系统 UDP 缓冲区是否已扩容。
* **`[DISK] Save Queue | Backlog: X / 100`** : 异步存图队列的积压量。代表当前硬盘写入速度是否能跟上网卡收包速度。若 Backlog 长期顶满 100，说明磁盘 I/O 达到物理瓶颈，系统将自动静默丢弃部分存图任务以保全网络层运行不受反噬。

---

## 📝 架构设计概述 (Architecture)

1. **UdpSenderImpl (发送端引擎)**: 接收业务层封装的 `cv::Mat`，调用 `ImgEncode` 进行硬件压缩。随后将大包拆分为安全载荷 (`<1024 Bytes`)，并基于**纳秒级自旋锁**进行均匀发包，杜绝 Linux 线程调度误差导致的流量突刺。
2. **UdpReceiverImpl (接收端双线程)**: 
   * **网络收包线程 (Producer)**: 纯粹调用 `recvfrom` 与内存切片重组，不含任何阻塞逻辑，速度极快。
   * **硬件解码线程 (Consumer)**: 从重组队列提取完整帧流，调用底层 NPU 提取附带的 Metadata 业务数据，并使用 OpenCV 将剥离出的纯净 JPEG 解码为像素图。
3. **Async IO (业务落盘)**: 业务层通过 `save_queue` 建立第三级缓存接管落盘操作，确保磁盘的毫秒级延迟绝对不会向上污染底层的微秒级网络收发。

---
这部分内容确实需要更新，特别是要体现我们后来加入的**`sendFrameAsync` 异步发送防阻塞机制**、`init` 函数的错误捕获，以及最重要的——**在接收端回调中禁止直接写磁盘的“最佳实践警告”**。

## 📖 接口集成指南

将本 SDK 嵌入到你的主业务项目中极其简单。SDK 内部已经完美封装了多线程调度与硬件资源管理，你只需关注业务数据的收发。

### 发送端 (Sender) 使用示例

发送端提供 `sendFrameSync`（同步阻塞）和 `sendFrameAsync`（异步排队）两种接口。推荐在高频并发场景下使用 `sendFrameAsync`。

```cpp
#include "ImageSender.h"
#include "TransportTypes.h"
#include <opencv2/opencv.hpp>

// 1. 实例化发送端
UdpSenderImpl sender;

// 2. 初始化网络与硬件资源 (目标IP, 目标端口, 图像物理宽, 图像物理高)
if (!sender.init("192.168.1.100", 8080, 1280, 1024)) {
    std::cerr << "发送端初始化失败！" << std::endl;
    return -1;
}

// 3. 准备原始图像与业务数据
cv::Mat img = cv::imread("test.jpg", cv::IMREAD_GRAYSCALE);
transport::WinInfo win_info;
win_info.win_mode = 0x85; // 设置图像通道类型 (如 0x85电视, 0x83红外, 0x05窗口)
std::vector<transport::Label> labels = { /* 填入画框等业务数据 */ };

// 4. 触发硬件编码并异步发送 (参数4为JPEG压缩质量 0-100)
// 异步接口会瞬间将数据推入后台抗压队列 (MAX_QUEUE_SIZE=50)，绝不阻塞当前业务的主线程
sender.sendFrameAsync(img, win_info, labels, 80);

// 5. 退出前优雅清理（会等待队列中残余的帧发送完毕，并销毁 Socket）
sender.stop();
```

### 接收端 (Receiver) 使用示例

接收端采用**纯异步回调**机制。底层已内置“网络极速收包”与“NPU硬件解码”的双线程流水线，业务侧只需注册回调即可获取纯净的像素图和数据。

```cpp
#include "ImageReceiver.h"
#include "TransportTypes.h"
#include <iostream>

// 1. 实例化接收端
UdpReceiverImpl receiver;

// 2. 初始化网络与硬件资源
// 监听本地 8080 端口，并设置预期分辨率（用于网络丢包时生成对应尺寸的兜底黑图）
if (!receiver.init(8080, 1280, 1024)) {
    std::cerr << "接收端初始化失败！" << std::endl;
    return -1;
}

// 3. 注册数据回调函数
receiver.registerCallback([](const transport::DecodedFrame& frame) {
    if (frame.is_valid) {
        std::cout << "收到完整图像，类型: 0x" << std::hex << (int)frame.win_info.win_mode << std::endl;
        
        // frame.image 即为 OpenCV 解码后的纯净 cv::Mat 像素图
        // frame.win_info 包含时间戳、帧序号等系统数据
        // frame.labels 包含从底层解耦出的画框业务数据
        
        // 🚨 最佳实践性能警告：
        // 请勿在此回调中直接执行 cv::imwrite 等极其耗时的磁盘 I/O 或阻塞操作！
        // 否则会反噬卡死底层的解码与收包线程，导致严重的网络丢包。
        // 建议在此处使用 std::queue 将 cv::Mat 浅拷贝推送给你的业务后台线程去处理。
        
    } else {
        std::cerr << "发生网络丢包，触发防撕裂机制，当前 frame.image 是一张纯黑图" << std::endl;
    }
});

// 4. 主线程可被阻塞或继续执行其他业务逻辑...
// while (is_running) { std::this_thread::sleep_for(std::chrono::seconds(1)); }

// 5. 退出前优雅清理（底层自动发送 Dummy 包打断死锁，并安全释放 NPU 内存池）
receiver.stop();
```
---
## 📡 附录：底层通信协议说明

本 SDK 在底层 UDP 传输时，采用了极其紧凑的自定义切片协议头，保障了多路复用、0号包防撕裂与乱序检测的能力。单个 UDP 包的内存布局如下：

| 字段 | 大小 | 说明 |
| :--- | :--- | :--- |
| `frame_head` | 1 Byte | 帧头魔数验证，固定为 `0x5A` |
| `win_mode` | 1 Byte | 通道类型标识 (如 `0x85` 电视, `0x83` 红外, `0x05` 窗口) |
| `packet_idx` | 2 Bytes| 当前切片包号 (网络大端序)，从 0 开始 |
| `total_packets` | 2 Bytes| 本帧切片总数 (网络大端序) |
| `payload_len` | 2 Bytes| 后续携带的 JPEG 载荷真实长度 (网络大端序) |
| **`Payload`** | N Bytes| 携带 APP15 业务数据的 JPEG 局部码流 (最大 1024 字节) |
| `frame_tail` | 1 Byte | 帧尾魔数验证，固定为 `0xA5` |
```