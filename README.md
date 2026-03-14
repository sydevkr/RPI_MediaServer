# RPi5 Remote Guard & Control

라즈베리파이 5와 AI Kit(Hailo-8L)을 활용한 실시간 영상 감시 및 원격 제어 시스템

## 개요

FFmpeg C API 기반 저지연 영상 처리 + Hailo-8L AI 객체 인식

Wayland 화면 캡처 모드에서는 `wf-recorder -> MediaMTX -> WebRTC/HLS` 경로를 사용합니다.

## 문서

| 문서 | 내용 |
|------|------|
| [docs/setup.md](docs/setup.md) | 개발환경 설정 (HW/SW, 설치, 빌드) |
| [docs/architecture.md](docs/architecture.md) | 시스템 아키텍처, 소스 구조, 개발 규칙 |

## 기술 스택

- **플랫폼**: Raspberry Pi 5 + AI Kit (Hailo-8L)
- **언어**: C/C++ (C++17)
- **영상**: FFmpeg API (libavcodec/libavformat)
- **AI**: HailoRT (YOLOv8s)
- **스트리밍**: MediaMTX (RTSP)

## 라이선스

MIT License
