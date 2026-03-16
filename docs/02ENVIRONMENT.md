# 02ENVIRONMENT.md

## 문서 역할
- 이 문서는 현재 개발환경, 개발 스펙, 설계 구조를 설명하는 문서다.

## 개발 환경
- 플랫폼: Raspberry Pi 5, Raspberry Pi OS 64-bit (Bookworm)
- 언어: C++17
- 빌드: CMake, `build.sh`
- 주요 라이브러리: FFmpeg, pthread, HailoRT(선택)
- 스트리밍 서버: MediaMTX
- 화면 캡처: V4L2, Wayland `wf-recorder`

## 개발 스펙
- 스트리밍 구조: 앱은 MediaMTX로 RTSP publish를 보내고, 웹 UI는 HLS 또는 WebRTC로 재생한다.
- 출력 해상도 목표:
  - `2x2 Mix`: `320x240`
  - 싱글 모드: `1280x720`
- 설정 파일은 모드별 출력 해상도 키를 사용한다:
  - `mix_width`, `mix_height`
  - `single_width`, `single_height`
- 설정 파일은 모드별 비트레이트 키를 사용한다:
  - `mix_bitrate`
  - `single_bitrate`
- 성능은 실제 장비에서 FPS와 지연 측정이 필요하다.

## 현재 설계 구조
- `src/main.cpp`: 설정 로드, HTTP API, 모드 전환, 메인 실행 흐름 관리
- `src/pipeline/`: 입력, 필터, 인코딩, RTSP publish 처리
- `src/ai/`: 비동기 추론 큐와 결과 관리
- `src/utils/`: 설정, 로깅, Wayland 화면 캡처 보조 기능
- `web/`: 상태 조회, 모드 전환, HLS/WebRTC 재생 UI

## 데이터 흐름
1. 앱이 입력 소스에서 프레임을 수집한다.
2. FFmpeg 파이프라인이 필터와 인코딩을 처리한다.
3. 앱이 MediaMTX로 RTSP publish를 보낸다.
4. MediaMTX가 HLS와 WebRTC 재생 경로를 제공한다.
5. 웹 UI가 상태 API와 HLS/WebRTC 경로를 사용한다.
