#!/bin/bash

set -e

# 下载
echo "下载 Slint..."
cd /tmp
wget -q https://github.com/slint-ui/slint/releases/download/v1.14.1/Slint-cpp-1.14.1-Linux-x86_64.tar.gz

# 解压
echo "解压..."
tar -xzf Slint-cpp-1.14.1-Linux-x86_64.tar.gz

# 安装
echo "安装..."
cd Slint-cpp-1.14.1-Linux-x86_64
sudo cp -r include/slint /usr/include/
sudo cp  lib/libslint_cpp.so /usr/lib/
sudo cp -r lib/cmake/Slint /usr/lib/cmake/
sudo cp -r bin/* /usr/bin/


# 清理
cd /tmp
rm -rf Slint-cpp-1.14.1-Linux-x86_64
rm -f Slint-cpp-1.14.1-Linux-x86_64.tar.gz

echo "完成"