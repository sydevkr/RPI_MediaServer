# 개발환경 설정

## 하드웨어
- SBC: Raspberry Pi 5 (8GB)
- AI Accelerator: AI Kit (Hailo-8L)
- 입력: HDMI 캡처 (`/dev/video0`), USB 웹캠 (`/dev/video1`)

## 소프트웨어 스택
- OS: Raspberry Pi OS 64-bit (Bookworm)
- Media Server: MediaMTX
- 개발: C/C++ (C++17), CMake 3.16+
- 라이브러리: FFmpeg (libavcodec/libavformat), HailoRT

## 설치

```bash
# FFmpeg 개발 라이브러리
sudo apt install -y \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libavdevice-dev libavfilter-dev libv4l-dev

# Hailo
sudo apt install hailo-all hailo-tappas-core

# MediaMTX
wget https://github.com/bluenviron/mediamtx/releases/download/v1.6.0/mediamtx_v1.6.0_linux_arm64v8.tar.gz
tar xzf mediamtx_*.tar.gz
sudo mv mediamtx /usr/local/bin/
```

## 빌드

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)