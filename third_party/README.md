# img_utils

基于 Ascend ACL DVPP JPEGE 的 JPEG 编码库，支持在 JPEG APP15 段中嵌入自定义业务信息，并在解码侧只解析业务字段、剥离出剩余标准 JPEG 码流。

当前实现重点是两件事：

- 编码：把灰度图编码成 JPEG，并把 `WinInfo + labels` 追加到 JPEG APP15 段中。
- 解码：从 JPEG APP15 段中解析业务字段，同时返回去掉该自定义段后的标准 JPEG 码流。

库本身不负责把 JPEG 再解码成像素图。示例程序中使用 OpenCV 的 `imdecode` 验证剥离后的 JPEG 是否仍然可正常解码。

## 功能概览

- 使用 ACL DVPP JPEGE 编码，不再依赖 `hi_mpi_venc`。
- 每次编码都可以单独指定 JPEG 质量，无需重新构造 `ImgEncode` 对象。
- 自定义业务信息写入 JPEG APP15 (`0xFFEF`)。
- 解码时只解析 APP15 中的业务数据，不做硬件 JPEG 解码。
- 解码返回两部分结果：
  - 解析后的 `WinInfo`
  - 解析后的 `labels`
  - 去掉自定义 APP15 段后的标准 JPEG 码流

## 目录结构

- [include/img_codec.h](/root/workspace/cgc/img_utils-hw/img_utils/include/img_codec.h)：对外头文件，定义 `Label`、`WinInfo`、`ImgEncode`、`ImgDecode`
- [src/img_codec.cpp](/root/workspace/cgc/img_utils-hw/img_utils/src/img_codec.cpp)：编码、元数据封装、元数据解析实现
- [main.cpp](/root/workspace/cgc/img_utils-hw/img_utils/main.cpp)：最小可运行示例
- [CMakeLists.txt](/root/workspace/cgc/img_utils-hw/img_utils/CMakeLists.txt)：构建脚本

## 环境要求

- Ascend CANN / ACL 运行环境
- DVPP 可用
- OpenCV
- CMake 3.10 及以上
- 支持 C++11 的编译器

当前工程默认从下面的路径查找 Ascend：

```cmake
/usr/local/Ascend/ascend-toolkit/latest
```

如果本机实际安装路径不同，可以在 CMake 配置时传入 `ASCEND_PATH`。

## 构建方法

在工程根目录执行：

```bash
mkdir -p build
cd build
cmake .. -DASCEND_PATH=/usr/local/Ascend/ascend-toolkit/latest
cmake --build . -j4
```

生成的可执行文件默认是：

```bash
build/venc_test
```

## 数据结构

### Label

定义见 [include/img_codec.h](/root/workspace/cgc/img_utils-hw/img_utils/include/img_codec.h#L30)：

```cpp
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
```

编码入业务段时，`Label` 的实际字节布局为固定 12 字节：

- `id`: 2 字节，大端
- `x`: 2 字节，大端
- `y`: 2 字节，大端
- `w`: 2 字节，大端
- `h`: 2 字节，大端
- `conf`: 1 字节，`float * 255` 后截断
- `cls`: 1 字节

### WinInfo

定义见 [include/img_codec.h](/root/workspace/cgc/img_utils-hw/img_utils/include/img_codec.h#L42)：

```cpp
struct WinInfo
{
    long long timestamp;
    unsigned short x;              // 红外和电视填充0
    unsigned short y;              // 红外和电视填充0
    unsigned char frame_id;        // 帧id
    unsigned char win_mode;        // 0x85 电视, 0x83 红外, 0x05 512窗口
    unsigned short center_x;       // 轴位信息x
    unsigned short center_y;       // 轴位信息y
    unsigned char system_info[36]; // 系统信息
};
```

编码入业务段时，`WinInfo` 的实际字节布局是：

- `timestamp`: 8 字节，大端
- `x`: 2 字节，大端
- `y`: 2 字节，大端
- `frame_id`: 1 字节
- `win_mode`: 1 字节
- `center_x`: 2 字节，大端
- `center_y`: 2 字节，大端
- `system_info`: 36 字节，原样拷贝

紧随其后的是：

- `labelCount`: 2 字节，大端
- `labels`: `labelCount * 12` 字节
- 结束标志：`0x50`

这套打包逻辑在 [src/img_codec.cpp](/root/workspace/cgc/img_utils-hw/img_utils/src/img_codec.cpp#L187)，解析逻辑在 [src/img_codec.cpp](/root/workspace/cgc/img_utils-hw/img_utils/src/img_codec.cpp#L279)。

## 对外接口

### ImgEncode

构造函数：

```cpp
ImgEncode(int width, int height);
```

说明：

- `width` 和 `height` 是编码器初始化尺寸。
- 当前实现要求后续传入的图像尺寸与构造时一致。
- 如果尺寸不一致，`encode` 会直接返回失败。

编码接口：

```cpp
bool encode(cv::Mat& img,
            const WinInfo& win,
            const std::vector<Label>& labels,
            uint32_t quality,
            unsigned char** dst,
            int& size);
```

参数说明：

- `img`：输入图像，必须是 `CV_8UC1` 灰度图
- `win`：要打进 APP15 的业务窗口信息
- `labels`：目标列表
- `quality`：JPEG 质量，范围 `[0, 100]`
- `dst`：输出 JPEG 码流，内部用 `new[]` 分配，需要调用方 `delete[]`
- `size`：输出码流字节数

返回值：

- `true`：编码成功
- `false`：编码失败

注意：

- 质量是按次生效的，不需要重新构造 `ImgEncode` 对象。
- 同一个 `ImgEncode` 可以连续编码多张同尺寸图像，只需在每次 `encode` 时传入不同质量值即可。

### ImgDecode

构造函数：

```cpp
ImgDecode(uint32_t width, uint32_t height);
```

当前实现中，这两个参数主要用于保持接口一致，解码不再真正做像素级 JPEG 解码。

元数据解析接口：

```cpp
bool parseSei(unsigned char* data,
              int size,
              WinInfo& win_info,
              std::vector<Label>& labels,
              int& consumed);
```

说明：

- 只解析业务信息，不返回剥离后的 JPEG。
- `consumed` 表示命中的 APP15 段长度。

推荐直接使用完整解码接口：

```cpp
bool decode(unsigned char* src,
            int size,
            unsigned char** jpegData,
            int& jpegSize,
            std::vector<Label>& labels,
            WinInfo& win_info);
```

参数说明：

- `src`：输入 JPEG 码流，要求包含自定义 APP15 段
- `size`：输入 JPEG 码流长度
- `jpegData`：输出剥离 APP15 后的标准 JPEG 码流，内部用 `new[]` 分配，需要调用方 `delete[]`
- `jpegSize`：剥离后的 JPEG 码流长度
- `labels`：输出目标列表
- `win_info`：输出窗口信息

返回值：

- `true`：成功解析到业务字段，并成功返回剥离后的 JPEG
- `false`：未能解析业务字段，或输入不合法

注意：

- 当前解码逻辑会扫描 JPEG 头部段，遇到第一个匹配的 APP15 段就按业务协议解析。
- 由于为了节省空间，业务段前的 UUID 标识已经移除，所以默认约定 APP15 只由本库使用。

## 输入图像要求

当前编码实现对输入图像有明确约束：

- 必须是单通道灰度图
- `cv::Mat` 类型必须为 `CV_8UC1`
- 尺寸必须和 `ImgEncode(width, height)` 初始化尺寸一致

如果你的原图是彩色图，可以先在调用侧转成灰度图：

```cpp
cv::Mat bgr = cv::imread("input.jpg", cv::IMREAD_COLOR);
cv::Mat gray;
cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
```

如果尺寸不一致，可以先缩放：

```cpp
cv::resize(gray, gray, cv::Size(width, height));
```

## 最小使用示例

下面是一个最小调用示例，和当前工程里的 [main.cpp](/root/workspace/cgc/img_utils-hw/img_utils/main.cpp) 一致：

```cpp
#include "img_codec.h"
#include <opencv2/opencv.hpp>
#include <vector>

int main()
{
    const int width = 1280;
    const int height = 1024;
    const uint32_t quality = 50;

    ImgEncode encoder(width, height);
    ImgDecode decoder(width, height);

    cv::Mat img = cv::imread("data/1280.jpg", cv::IMREAD_GRAYSCALE);
    if (img.empty()) {
        return 1;
    }
    if (img.cols != width || img.rows != height) {
        cv::resize(img, img, cv::Size(width, height));
    }

    WinInfo win{};
    win.timestamp = 1706282989000;
    win.x = 0;
    win.y = 0;
    win.frame_id = 1;
    win.win_mode = 0x05;
    win.center_x = 320;
    win.center_y = 256;
    for (size_t i = 0; i < sizeof(win.system_info); ++i) {
        win.system_info[i] = static_cast<unsigned char>(i);
    }

    std::vector<Label> labels;
    labels.push_back(Label{101, 50, 50, 100, 80, 0.95f, 1});
    labels.push_back(Label{102, 300, 400, 40, 60, 0.88f, 0});

    unsigned char* encoded = nullptr;
    int encodedSize = 0;
    if (!encoder.encode(img, win, labels, quality, &encoded, encodedSize)) {
        return 1;
    }

    unsigned char* strippedJpeg = nullptr;
    int strippedJpegSize = 0;
    WinInfo parsedWin{};
    std::vector<Label> parsedLabels;
    if (!decoder.decode(encoded, encodedSize, &strippedJpeg, strippedJpegSize, parsedLabels, parsedWin)) {
        delete[] encoded;
        return 1;
    }

    delete[] strippedJpeg;
    delete[] encoded;
    return 0;
}
```

## 输出结果说明

编码成功后，`dst` 指向的是：

- 一个完整的 JPEG 文件流
- JPEG SOI 后插入了一段 APP15
- APP15 的 payload 就是本库编码的业务信息

解码成功后：

- `win_info` 和 `labels` 是从 APP15 中恢复出来的业务信息
- `jpegData` 是删掉 APP15 后的标准 JPEG 文件流

如果你需要保存到文件：

```cpp
std::ofstream out("encoded_with_metadata.jpg", std::ios::binary);
out.write(reinterpret_cast<char*>(encoded), encodedSize);
```

剥离后的 JPEG 同理：

```cpp
std::ofstream out("stripped_result.jpg", std::ios::binary);
out.write(reinterpret_cast<char*>(strippedJpeg), strippedJpegSize);
```

## 质量参数说明

`quality` 范围是 `[0, 100]`，由 ACL DVPP JPEGE 控制。

库的行为是：

- `quality` 越高，输出图像质量通常越高，文件也通常越大
- `quality` 越低，压缩更强，文件通常更小

调用方式示例：

```cpp
ImgEncode encoder(width, height);

encoder.encode(img1, win1, labels1, 90, &out1, size1);
encoder.encode(img2, win2, labels2, 60, &out2, size2);
encoder.encode(img3, win3, labels3, 30, &out3, size3);
```

这里不需要重新构造 `encoder`。

## 资源管理约定

库内部已经按 RAII 管理了 DVPP 临时资源，但有两类输出内存仍然是交给调用方释放的：

- `ImgEncode::encode` 输出的 `dst`
- `ImgDecode::decode` 输出的 `jpegData`

这两块内存都由内部使用 `new[]` 分配，调用方必须使用：

```cpp
delete[] ptr;
```

## 当前限制

- 仅支持灰度图输入，即 `CV_8UC1`
- 当前示例中使用灰度图转 NV12 后编码
- 编码器初始化尺寸必须和实际输入尺寸一致
- 自定义业务段使用 APP15
- 为了节省空间，APP15 里没有额外 UUID 标识，因此默认约定 APP15 只由本库使用
- `parseSei` 名称沿用了旧命名，但现在处理的是 JPEG APP15 自定义段，不是视频 SEI

## 调试建议

如果编码失败，优先检查：

- Ascend 设备和 ACL 环境是否可用
- `ASCEND_PATH` 是否正确
- OpenCV 是否正确安装
- 输入图像是否是可读的灰度图
- 输入图像尺寸是否与编码器初始化尺寸一致
- `quality` 是否在 `[0, 100]` 范围内

如果解码失败，优先检查：

- 输入 JPEG 是否确实由本库编码生成
- APP15 是否被其他链路修改或覆盖
- `WinInfo` 结构是否与你当前业务协议保持一致

## 示例程序

当前示例程序在 [main.cpp](/root/workspace/cgc/img_utils-hw/img_utils/main.cpp)，已经改成命令行参数形式。默认流程是：

1. 读取输入图
2. 转灰度并缩放到指定尺寸
3. 组织示例 `WinInfo` 和 `labels`
4. 调用 `ImgEncode::encode`
5. 保存带业务字段的 JPEG
6. 调用 `ImgDecode::decode`
7. 保存剥离后的标准 JPEG
8. 用 OpenCV `imdecode` 验证剥离后的 JPEG 是否可解码

支持的命令行参数：

```text
--input <path>
--width <int>
--height <int>
--quality <0-100>
--encoded-out <path>
--stripped-out <path>
--decoded-out <path>
--help
```

默认运行方式：

```bash
./build/venc_test
```

显式指定参数的运行方式：

```bash
./build/venc_test \
  --input data/1280.jpg \
  --width 1280 \
  --height 1024 \
  --quality 50 \
  --encoded-out encoded_with_metadata.jpg \
  --stripped-out stripped_result.jpg \
  --decoded-out decoded_result.jpg
```
