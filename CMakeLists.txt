cmake_minimum_required(VERSION 3.10)
project(ImageUdpTransport)

# 设置 C++ 标准和编译选项
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fPIC -DENABLE_DVPP_INTERFACE -Wall -Wextra")

# ==========================================
# 1. 昇腾 CANN 路径与依赖配置
# ==========================================
if(NOT DEFINED ASCEND_PATH)
    # 如果外部没有传入 ASCEND_PATH，使用默认路径
    set(ASCEND_PATH "/usr/local/Ascend/ascend-toolkit/latest")
endif()

# 包含 Ascend 头文件
include_directories(
    ${ASCEND_PATH}/include
    ${ASCEND_PATH}/include/acl
)

# 包含 Ascend 库文件路径
link_directories(
    ${ASCEND_PATH}/lib64
)

# ==========================================
# 2. OpenCV 依赖配置
# ==========================================
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# ==========================================
# 3. 本地项目头文件包含路径
# ==========================================
# 包含对外的 SDK 接口目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 包含第三方硬件编解码库的头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/img_utils/include)

# 包含内部实现的头文件目录 (用于找到 protocol 和 network 等)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)


# ==========================================
# 4. 收集核心源码文件 (构建 SDK 的基石)
# ==========================================
set(SDK_SOURCES
    src/network/udp_operation.cpp
    src/sender/UdpSenderImpl.cpp
    src/receiver/UdpReceiverImpl.cpp
    third_party/img_utils/src/img_codec.cpp
)

# ==========================================
# 5. 编译接收端 Demo 可执行文件
# ==========================================
add_executable(receiver_demo examples/receiver_demo.cpp ${SDK_SOURCES})

# 链接所需的动态库
target_link_libraries(receiver_demo
    ${OpenCV_LIBS}
    ascendcl
    acl_dvpp
    acl_dvpp_mpi
    pthread
)

# ==========================================
# 6. 编译发送端 Demo 可执行文件
# ==========================================
add_executable(sender_demo examples/sender_demo.cpp ${SDK_SOURCES})

# 链接所需的动态库
target_link_libraries(sender_demo
    ${OpenCV_LIBS}
    ascendcl
    acl_dvpp
    acl_dvpp_mpi
    pthread
)