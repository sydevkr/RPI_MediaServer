# RPI_MediaServer 개발 이슈 노트

## 이슈: FrameBuffer 화면 캡처 실패 및 해결 과정

**작성일**: 2026-03-12  
**관련 기능**: ONLY_FRAMEBUFFER 모드 (VideoMode 5)

---

## 1. 시도한 방법 및 실패 원인

### 방법 1: fbdev (/dev/fb0) 직접 접근

**시도 내용**:
- FFmpeg의 fbdev demuxer를 사용하여 `/dev/fb0` 직접 읽기
- `video_mode=5`에서 `input_fmt = av_find_input_format("fbdev")` 설정

**결과**: ❌ 검은 화면 출력

**실패 원인**:
```
[fbdev @ 0x55559788ac90] w:1920 h:1080 bpp:16 pixfmt:rgb565le
```
- FrameBuffer에 실제 화면 데이터가 없음
- Raspberry Pi 5는 DRM/KMS 드라이버를 사용하며, 기존 fbdev가 비활성화됨
- `/dev/fb0`은 존재하지만 빈 메모리 영역만 참조

**교훈**: 최신 Linux 커널에서는 fbdev가 DRM/KMS로 대체되어 직접 접근이 불가능함

---

### 방법 2: kmsgrab (DRM/KMS 직접 접근)

**시도 내용**:
- FFmpeg의 kmsgrab demuxer를 사용하여 GPU 메모리 직접 접근
- `input_fmt = av_find_input_format("kmsgrab")` 설정

**결과**: ❌ 장치 미지원 에러

**실패 원인**:
```
[kmsgrab @ 0x5556237e2d50] Failed to set universal planes capability
[kmsgrab @ 0x5556237e2d50] Failed to get plane resources: Operation not supported
```
- Raspberry Pi 5의 VideoCore GPU 드라이버가 kmsgrab에 필요한 universal planes를 지원하지 않음
- V3D/VC4 드라이버의 제한사항

**교훈**: Raspberry Pi 5의 GPU 아키텍처는 표준 DRM/KMS 기능을 완전히 지원하지 않음

---

### 방법 3: x11grab (X11 화면 캡처)

**시도 내용**:
- FFmpeg의 x11grab demuxer를 사용하여 X11 디스플레이 캡처
- `input_fmt = av_find_input_format("x11grab")` 설정

**결과**: ❌ 검은 화면 출력 (다양한 환경에서)

**실패 원인 분석**:

#### 3.1 SSH 원격 접속 환경
```
XDG_SESSION_TYPE=tty
DISPLAY=(empty)
```
- SSH 세션은 로컬 GUI 세션과 분리됨
- X11 포워딩 없이는 로컬 디스플레이 접근 불가

#### 3.2 Xvfb 가상 디스플레이
```
Xvfb :99 -screen 0 1920x1080x24
```
- 가상 화면은 정상 작동하나, 실제 데스크톱 화면이 아님
- 테스트용으로는 유효하나 실제 사용 목적과 다름

#### 3.3 Wayland 환경 (최종 원인)
```
XDG_SESSION_TYPE=wayland
```
- 최신 Raspberry Pi OS는 Wayland를 기본 디스플레이 서버로 사용
- x11grab는 X11 프로토콜 전용 → Wayland에서는 작동하지 않음
- XWayland가 있으나, Wayland native 화면은 캡처 불가

**교훈**: 
- Wayland가 기본인 현대 Linux에서는 x11grab 사용 불가
- 세션 타입 확인 (`echo $XDG_SESSION_TYPE`)이 선행되어야 함

---

## 2. 성공한 방법: wf-recorder

### 해결책
```bash
sudo apt install wf-recorder
wf-recorder -f /tmp/test.mp4  # 녹화 테스트
wf-recorder --muxer=rtsp --file=rtsp://localhost:8554/live  # RTSP 스트리밍
```

### 동작 원리
- Wayland native 화면 캡처 도구
- wlroots 기반 compositor와 호환 (Raspberry Pi OS의 Wayfire/Wayland)
- PipeWire 없이도 직접 Wayland 프로토콜로 화면 접근

---

## 3. 최종 구현 방향

### 현재 상태
- pipeline.cpp의 x11grab 기반 코드는 Wayland 환경에서 작동하지 않음
- wf-recorder를 외부 프로세스로 호출하거나, 비슷한 방식으로 Wayland 캡처 구현 필요

### 권장 구현
1. **단기**: wf-recorder를 fork/exec로 실행하여 RTSP 직접 푸시
2. **장기**: pipewire 또는 wlroots API를 사용한 native Wayland 캡처 구현

---

## 4. 체크리스트 (향후 유사 이슈 방지)

- [ ] `echo $XDG_SESSION_TYPE`으로 세션 타입 확인
- [ ] Wayland 환경에서는 x11grab 사용 불가 (wf-recorder/pipewire 사용)
- [ ] SSH 원격 접속에서는 로컬 디스플레이 접근 불가 (로컬 터미널 사용)
- [ ] fbdev는 최신 DRM/KMS 환경에서 작동하지 않음
- [ ] kmsgrab는 GPU 드라이버 지원 여부 확인 필요

---

## 관련 파일

- `src/pipeline/pipeline.cpp`: x11grab 코드 (Wayland 미지원)
- `config.ini`: fb_device 설정 (현재 :99, Wayland 환경에서 변경 필요)
- `docs/development-status.md`: FrameBuffer 캡처 방식 비교 문서