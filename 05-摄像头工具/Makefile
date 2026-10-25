# 摄像头工具构建（camcap / facetest）
#
# camcap: 单帧抓图（V4L2 mmap + 格式枚举协商, 输出 raw/转换图）
# facetest: 人脸链路诊断（Haar 检测 + LBPH, 依赖 OpenCV contrib 的 face 模块）
#
# 板上直接编译(作者环境: ARM 版 OpenCV world 在 /root/opencv-arm 之类路径):
#   make OPENCV_DIR=/你的/opencv     # 默认 /usr/local
# 交叉编译: make CXX=arm-linux-gnueabihf-g++ OPENCV_DIR=...
# facetest 依赖 opencv_world(contrib); camcap 只用 core/imgproc

CXX      ?= g++
CXXFLAGS ?= -O2 -Wall
OPENCV_DIR ?= /usr/local
INC  := -I$(OPENCV_DIR)/include
LIB  := -L$(OPENCV_DIR)/lib -lopencv_world -lpthread

all: camcap facetest

camcap: camcap.cpp
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $< $(LIB)

facetest: facetest.cpp
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $< $(LIB)

clean:
	rm -f camcap facetest

.PHONY: all clean
