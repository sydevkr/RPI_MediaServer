# 비디오 소스 분석 및 검증 가이드

## 1. 전체 파이프라인 흐름

```
┌─────────────┐    ┌──────────────┐    ┌───────────┐    ┌──────────┐    ┌──────────┐
│  1. INPUT   │───►│ 2. CAPTURE   │───►│ 3. FILTER │───►│ 4. ENCODE│───►│ 5. STREAM│
│  (V4L2)     │    │  (Demux/    │    │ (Mixing)  │    │ (H264)   │    │ (RTSP)   │
│             │    │   Decode)   │    │           │    │          │    │          │
└─────────────┘    └──────────────┘    └───────────┘    └──────────┘    └──────────┘
                           │                                       
                           ▼                                       
                    ┌──────────────┐                              
                    │  AI Overlay  │                              
                    │ (Hailo-8L)   │                              
                    └──────────────┘                              
```

---

## 2. 단계별 분석 및 검증 방법

### 2.1 입력 (Input) - V4L2 장치 설정

**목적**: 비디오 소스 장치(HDMI 캡처/웹캠) 초기화 및 설정

**핵심 동작**:
- V4L2 장치 파일 열기 (`/dev/video0`, `/dev/video1`)
- 비디오 포맷 설정 (YUYV422, 해상도, 프레임레이트)
- FFmpeg 입력 포맷 컨텍스트 생성

**참조 소스**: `src/pipeline/pipeline.cpp` - `init_input()`

```cpp
// 핵심 코드 위치: pipeline.cpp:85-120
// V4L2 옵션 설정
AVDictionary* opts = nullptr;
av_dict_set(&opts, "video_size", "1920x1080", 0);
av_dict_set(&opts, "framerate", "30", 0);
av_dict_set(&opts, "input_format", "yuyv422", 0);
av_dict_set(&opts, "thread_queue_size", "4096", 0);
```

**검증 명령어**:
```bash
# 1. 장치 존재 확인
ls -la /dev/video*

# 2. V4L2 장치 정보 확인
v4l2-ctl -d /dev/video0 --all

# 3. 지원 포맷 확인
v4l2-ctl -d /dev/video0 --list-formats-ext

# 4. FFmpeg 직접 테스트
ffmpeg -f v4l2 -video_size 1920x1080 -framerate 30 -i /dev/video0 -f null -
```

**설정 파일**: `config.ini`
```ini
hdmi_device=/dev/video0
webcam_device=/dev/video1
width=1920
height=1080
fps=30
```

---

### 2.2 영상 획득 (Capture) - 디코딩

**목적**: V4L2에서 패킷을 읽어 디코딩하여 원시 프레임(YUV)으로 변환

**핵심 동작**:
- `av_read_frame()`: V4L2에서 패킷 읽기
- `avcodec_send_packet()`: 디코더로 패킷 전송
- `avcodec_receive_frame()`: YUV420P 프레임 수신

**참조 소스**: `src/pipeline/pipeline.cpp` - `capture()`

```cpp
// 핵심 코드 위치: pipeline.cpp:310-360
while ((ret = av_read_frame(input_ctx_.get(), pkt.get())) >= 0) {
    ret = avcodec_send_packet(decoder_ctx_.get(), pkt.get());
    ret = avcodec_receive_frame(decoder_ctx_.get(), frame.get());
}
```

**검증 포인트**:
| 항목 | 예상값 | 확인 방법 |
|------|--------|-----------|
| 입력 포맷 | YUYV422 | 로그 메시지 |
| 출력 포맷 | YUV420P | `frame->format` 확인 |
| 해상도 | 1920x1080 | `frame->width/height` |
| PTS 간격 | 33ms (30fps) | 프레임 타임스탬프 |

---

### 2.3 Mixing / Filter Graph 처리

**목적**: 4가지 비디오 모드에 따른 프레임 처리 및 변환

**비디오 모드별 처리**:

| 모드 | enum | FFmpeg 필터 | 상태 |
|------|------|-------------|------|
| HDMI 단일 | `HDMI_ONLY` | `scale=1920:1080` | ✅ 완료 |
| 웹캠 단일 | `WEBCAM_ONLY` | `scale=1920:1080` | ✅ 완료 |
| 좌우 분할 | `SIDE_BY_SIDE` | `hstack` | ⚠️ 폴백 |
| PIP | `PIP` | `overlay` | ⚠️ 폴백 |

**참조 소스**: 
- `src/pipeline/pipeline.cpp` - `build_filter_string()`, `init_filter_graph()`
- `src/pipeline/pipeline.hpp` - `buffersrc_ctx_`, `buffersink_ctx_`

```cpp
// 핵심 코드 위치: pipeline.cpp:140-170
std::string Pipeline::build_filter_string() {
    switch (cfg_.video_mode) {
        case utils::VideoMode::HDMI_ONLY:
        case utils::VideoMode::WEBCAM_ONLY:
            filter = "scale=" + std::to_string(cfg_.width) + ":" + std::to_string(cfg_.height);
            break;
        // ...
    }
    filter += ",format=yuv420p";  // 인코더용 포맷 변환
}
```

**검증 명령어**:
```bash
# FFmpeg 필터 그래프 테스트
ffmpeg -f v4l2 -i /dev/video0 -vf "scale=1920:1080,format=yuv420p" \
       -f null - 2>&1 | grep -E "(Stream|Output)"
```

---

### 2.4 Muxing - 컨테이너 포맷 처리

**목적**: 인코딩된 패킷을 RTSP 프로토콜에 맞는 컨테이너로 패키징

**핵심 동작**:
- RTSP 출력 컨텍스트 생성 (`rtsp://localhost:8554/live`)
- 스트림 헤더 작성
- 인터리브드 프레임 쓰기

**참조 소스**: `src/pipeline/pipeline.cpp` - `init_output()`, `encode_and_send()`

```cpp
// 핵심 코드 위치: pipeline.cpp:250-280, 380-410
avformat_alloc_output_context2(&ctx, nullptr, "rtsp", cfg_.rtsp_url.c_str());
avformat_write_header(output_ctx_.get(), &opts);
av_interleaved_write_frame(output_ctx_.get(), pkt.get());
```

**설정 파일**: `mediamtx.yml`
```yaml
rtspAddress: :8554
paths:
  live:
    source: publisher
    sourceProtocol: rtsp
```

**검증 명령어**:
```bash
# RTSP 스트림 정보 확인
ffprobe -rtsp_transport tcp rtsp://localhost:8554/live

# 패킷 덤프 (muxing 확인)
ffmpeg -i rtsp://localhost:8554/live -c copy -f h264 - | head -c 1000 | xxd
```

---

### 2.5 인코딩 (Encoding) - H264 하드웨어 가속

**목적**: YUV420P 프레임을 H264 비트스트림으로 인코딩

**핵심 설정**:
- 코덱: `h264_v4l2m2m` (Raspberry Pi VideoCore VI 하드웨어 가속)
- 비트레이트: 4Mbps CBR
- GOP: 30프레임 (1초)
- B-frame: 0 (지연 최소화)

**참조 소스**: `src/pipeline/pipeline.cpp` - `init_encoder()`

```cpp
// 핵심 코드 위치: pipeline.cpp:200-240
const AVCodec* encoder = avcodec_find_encoder_by_name("h264_v4l2m2m");
encoder_ctx_->bit_rate = 4000000;
encoder_ctx_->gop_size = 30;
encoder_ctx_->max_b_frames = 0;
```

**설정 파일**: `config.ini`
```ini
bitrate=4000000
gop_size=30
```

**검증 명령어**:
```bash
# 인코딩 통계 확인
ffmpeg -f v4l2 -i /dev/video0 -c:v h264_v4l2m2m -b:v 4M \
       -f null - 2>&1 | grep -E "(fps|bitrate|speed)"

# 하드웨어 인코더 확인
v4l2-ctl -d /dev/video11 --all  # VideoCore 인코더 장치
```

---

### 2.6 스트리밍 (Streaming) - RTSP 출력

**목적**: 인코딩된 H264 스트림을 RTSP로 전송

**핵심 동작**:
- TCP 기반 RTSP 연결 (안정성 우선)
- PTS/DTS 타임스탬프 재조정
- 실시간 패킷 전송

**참조 소스**: `src/pipeline/pipeline.cpp` - `encode_and_send()`

```cpp
// 핵심 코드 위치: pipeline.cpp:390-410
av_packet_rescale_ts(pkt.get(), encoder_ctx_->time_base, out_stream_->time_base);
av_interleaved_write_frame(output_ctx_.get(), pkt.get());
```

**설정 파일**: 
- `config.ini`: `rtsp_url=rtsp://localhost:8554/live`
- `mediamtx.yml`: RTSP 서버 설정

**검증 명령어**:
```bash
# 1. RTSP 스트림 재생 테스트
ffplay -rtsp_transport tcp rtsp://localhost:8554/live

# 2. 네트워크 패킷 캡처
sudo tcpdump -i lo port 8554 -w rtsp_capture.pcap

# 3. 지연 측정
ffmpeg -rtsp_transport tcp -i rtsp://localhost:8554/live \
       -f null - 2>&1 | grep -E "fps|frame"
```

---

## 3. AI 오버레이 파이프라인

**목적**: Hailo-8L을 활용한 실시간 객체 탐지 및 시각화

**데이터 흐름**:
```
FFmpeg 캡처 (YUV420P)
    │
    ├──► [AI 큐] ──► [Hailo-8L 추론] ──► [바운딩 박스 결과]
    │                                         │
    │                                         ▼
    │                              [overlay_detections()]
    │                                         │
    ▼                                         ▼
[인코딩] ◄────── [Y 플레인에 박스 그리기] ─────┘
```

**참조 소스**:
- `src/ai/detector.hpp`, `detector.cpp` - Hailo-8L 추론
- `src/pipeline/pipeline.cpp` - `overlay_detections()` (YUV Y 플레인에 그리기)

```cpp
// 핵심 코드 위치: pipeline.cpp:365-385
void Pipeline::overlay_detections(const std::vector<ai::Detection>& detections) {
    uint8_t* y_plane = filtered_frame_->data[0];
    // Y 플레인에 흰색 사각형 그리기
    y_plane[y1 * y_stride + x1] = 255;  // 흰색
}
```

**검증 명령어**:
```bash
# Hailo 모델 정보 확인
hailortcli scan
hailortcli fw-control identify

# AI 추론 성능 확인
hailortcli benchmark /usr/share/hailo-models/yolov8s_person.hef
```

---

## 4. 참조 소스 파일 요약

| 파일 | 역할 | 핵심 함수/클래스 |
|------|------|-----------------|
| `src/pipeline/pipeline.hpp` | 파이프라인 인터페이스 | `Pipeline` 클래스 |
| `src/pipeline/pipeline.cpp` | 파이프라인 구현 | `init_input()`, `capture()`, `encode_and_send()` |
| `src/ai/detector.hpp/cpp` | AI 추론 | `Detector` 클래스, `inference_loop()` |
| `src/utils/config.hpp/cpp` | 설정 관리 | `Config` 구조체, `load_config()` |
| `config.ini` | 런타임 설정 | 비디오 모드, 해상도, 비트레이트 |
| `mediamtx.yml` | RTSP 서버 설정 | 포트, 패스, 인증 |

---

## 5. 종합 검증 체크리스트

### 5.1 초기화 단계
- [ ] V4L2 장치 열기 성공 (`/dev/video0` 또는 `/dev/video1`)
- [ ] FFmpeg 입력 포맷 컨텍스트 생성
- [ ] 디코더 초기화 (YUYV422 → YUV420P)
- [ ] 필터 그래프 초기화 (scale, format)
- [ ] H264 인코더 초기화 (`h264_v4l2m2m`)
- [ ] RTSP 출력 초기화 (`rtsp://localhost:8554/live`)
- [ ] Hailo-8L 초기화 및 워커 스레드 시작

### 5.2 런타임 단계
- [ ] 30fps 캡처 유지
- [ ] AI 추론 큐 정상 작동 (max 3, drop 정책)
- [ ] H264 인코딩 4Mbps CBR 출력
- [ ] RTSP 스트림 안정적 전송
- [ ] 탐지 결과 오버레이 정상 표시

### 5.3 종료 단계
- [ ] 인코더 플러시 (남은 프레임 처리)
- [ ] RTSP 트레일러 작성
- [ ] Hailo 워커 스레드 종료
- [ ] 모든 FFmpeg 리소스 해제

---

## 6. 디버깅 유틸리티

```bash
# 전체 파이프라인 FFmpeg 명령어로 시뮬레이션
ffmpeg -f v4l2 -video_size 1920x1080 -framerate 30 -i /dev/video0 \
       -vf "scale=1920:1080,format=yuv420p" \
       -c:v h264_v4l2m2m -b:v 4M -g 30 -preset ultrafast -tune zerolatency \
       -f rtsp -rtsp_transport tcp rtsp://localhost:8554/live
```

> **참고**: `.clinerules`에 명시된 대로 성능 최적화를 위해 `-preset ultrafast`와 `-tune zerolatency` 사용