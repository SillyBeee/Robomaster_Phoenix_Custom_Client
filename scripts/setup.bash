#!/bin/bash

set -e

# ...existing code...
# 更新 apt 并安装系统依赖（Paho C 使用 apt 安装，其他库也用 apt）
echo "更新 apt 并安装系统依赖..."
sudo apt update
sudo apt install -y build-essential cmake git wget tar pkg-config \
  libssl-dev libopencv-dev libsdl2-dev libspdlog-dev \
  nlohmann-json3-dev protobuf-compiler libprotobuf-dev \
  libpaho-mqtt-dev libavcodec-dev libavformat-dev libavutil-dev \
  libswscale-dev pkg-config qt6-base-dev

# ...existing code...
# 下载并安装 Slint（保持原有方式），但先判断是否已安装
echo "检查 Slint 是否已安装..."
SLINT_INSTALLED=false

# 检查常见安装位置与可执行文件
if command -v slint >/dev/null 2>&1; then
  SLINT_INSTALLED=true
fi

if [ -d "/usr/lib/cmake/Slint" ] || [ -d "/usr/local/lib/cmake/Slint" ] || [ -d "/usr/share/cmake/Slint" ]; then
  SLINT_INSTALLED=true
fi

if [ -f "/usr/include/slint/slint.h" ] || [ -f "/usr/include/slint.h" ]; then
  SLINT_INSTALLED=true
fi

if [ "$SLINT_INSTALLED" = true ]; then
  echo "检测到 Slint 已安装，跳过 Slint 安装。"
else
  echo "未检测到 Slint，开始下载并安装 Slint..."
  cd /tmp
  SLINT_TAR="Slint-cpp-1.14.1-Linux-x86_64.tar.gz"
  wget -q "https://github.com/slint-ui/slint/releases/download/v1.14.1/${SLINT_TAR}"

  echo "解压 Slint..."
  tar -xzf "${SLINT_TAR}"

  SLINT_DIR="${SLINT_TAR%.tar.gz}"
  echo "安装 Slint 到系统目录..."
  cd "${SLINT_DIR}"
  sudo cp -r include/slint /usr/include/ || true
  # 有些发行版打包的库名或路径可能不同，尽量拷贝所有可能的文件
  sudo cp -r include/* /usr/include/ || true
  sudo cp lib/libslint_cpp.so /usr/lib/ 2>/dev/null || sudo cp lib/libslint_cpp.so /usr/local/lib/ 2>/dev/null || true
  sudo cp -r lib/cmake/Slint /usr/lib/cmake/ 2>/dev/null || sudo cp -r lib/cmake/Slint /usr/local/lib/cmake/ 2>/dev/null || true
  sudo cp -r bin/* /usr/bin/ 2>/dev/null || true

  # 清理 Slint 临时文件
  cd /tmp
  rm -rf "${SLINT_DIR}"
  rm -f "${SLINT_TAR}"
  echo "Slint 安装完成。"
fi

# ...existing code...
# 编译并安装 paho-mqtt.cpp（C++ 客户端）——使用系统已安装的 paho-mqtt (C)
echo "编译并安装 paho-mqtt.cpp (C++ 客户端)..."
cd /tmp
rm -rf paho.mqtt.cpp
git clone https://github.com/eclipse-paho/paho.mqtt.cpp.git
cd paho.mqtt.cpp
git checkout v1.5.3
git submodule init
git submodule update

cmake -Bbuild -H. -DPAHO_BUILD_SAMPLES=ON
sudo cmake --build build/ --target install

# 清理 paho 源码
cd /tmp
rm -rf paho.mqtt.cpp

echo "完成：系统依赖、Slint 与 paho-mqtt.cpp 已安装。"