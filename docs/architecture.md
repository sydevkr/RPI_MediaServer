# 시스템 아키텍처

## 개요
FFmpeg C API 기반 실시간 영상 처리 + Hailo-8L AI 객체 인식

```
┌─────────────┐    ┌──────────────┐    ┌───────────┐    ┌──────────┐
│  HDMI/웹캠  │───►│  FFmpeg API  │───►│  Encoder  │───►│ MediaMTX │
│  (V4L2)     │    │  Pipeline    │    │(h264_v4l2)│    │ (RTSP)   │
└─────────────┘    └──────────────┘    └───────────┘    └──────────┘
                              │
                              ▼
                       ┌──────────────┐
                       │  Hailo-8L    │
                       │  (Person Det)│
                       └──────────────┘
```

## 소스 파일 구조도 및 역할

```
RPI_MediaServer/
├── src/
│   ├── main.cpp              # 진입점: 시스템 초기화 및 메인 루프
│   │                           - 시그널 핸들러 등록 (SIGINT/SIGTERM)
│   │                           - 설정 로드 → Pipeline + Detector 초기화
│   │                           - 메인 루프: 캡처 → AI 큐 → 오버레이 → 인코딩
│   │
│   ├── pipeline/
│   │   ├── pipeline.hpp      # FFmpeg 파이프라인 인터페이스
│   │   │                       - RAII 래퍼: AVFramePtr, CodecCtxPtr 등
│   │   │                       - Pipeline 클래스 선언
│   │   └── pipeline.cpp      # 파이프라인 구현
│   │                           - init_input(): V4L2 캡처 초기화
│   │                           - init_filter_graph(): FFmpeg 필터 체인
│   │                           - init_encoder(): h264_v4l2m2m 하드웨어 인코딩
│   │                           - init_output(): RTSP 스트리밍
│   │                           - capture(): 프레임 캡처 및 필터 적용
│   │                           - overlay_detections(): AI 결과 오버레이
│   │                           - encode_and_send(): H264 인코딩 및 전송
│   │
│   ├── ai/
│   │   ├── detector.hpp      # AI 탐지기 인터페이스
│   │   │                       - Detection 구조체 (바운딩 박스)
│   │   │                       - Detector 클래스 (비동기 스레드)
│   │   └── detector.cpp      # Hailo-8L 구현
│   │                           - init_hailo(): VDevice 초기화
│   │                           - inference_loop(): 추론 워커 스레드
│   │                           - enqueue(): 프레임 입력 (생산자)
│   │                           - get_results(): 결과 조회 (소비자)
│   │                           - run_inference(): YUV→RGB→Hailo 추론
│   │
│   └── utils/
│       ├── logger.hpp        # 스레드 안전 로깅 (템플릿 기반)
│       ├── logger.cpp        # 로그 출력 구현 (mutex 보호)
│       ├── config.hpp        # 설정 구조체 및 VideoMode 열거형
│       └── config.cpp        # INI 파일 파서 (key=value)
│
├── CMakeLists.txt            # CMake 빌드 설정
├── build.sh                  # 빌드 스크립트
├── start.sh                  # 실행 스크립트 (MediaMTX + 서버)
├── config.ini                # 런타임 설정 파일
└── mediamtx.yml              # RTSP 서버 설정
```

## 데이터 흐름 상세

```
[main.cpp]
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           INITIALIZATION                                  │
├─────────────────────────────────────────────────────────────────────────┤
│  1. load_config("config.ini")  → Config 구조체                           │
│  2. logger::init()             → 로그 레벨 설정                          │
│  3. Detector::start()          → Hailo RT 초기화 + 워커 스레드 시작       │
│  4. Pipeline::init()           → FFmpeg 전체 파이프라인 초기화            │
│     ├── init_input()           → V4L2 open (/dev/video0)                │
│     ├── init_filter_graph()    → buffersrc → filter → buffersink        │
│     ├── init_encoder()         → h264_v4l2m2m 설정                      │
│     └── init_output()          → RTSP 연결 (rtsp://localhost:8554/live) │
└─────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           MAIN LOOP (30fps)                               │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│  WHILE running:                                                           │
│    │                                                                      │
│    ├──► [Pipeline::capture()]                                             │
│    │      ├──► av_read_frame()      → V4L2에서 패킷 읽기                 │
│    │      ├──► avcodec_send_packet() → 디코더로 전송                     │
│    │      ├──► avcodec_receive_frame() → YUV420P 프레임 수신             │
│    │      ├──► av_buffersrc_add_frame() → 필터 그래프 입력               │
│    │      └──► av_buffersink_get_frame() → 스케일링된 프레임 반환        │
│    │                                                                      │
│    ├──► [Detector::enqueue(frame)] → AI 입력 큐에 추가 (non-blocking)    │
│    │      └──► 큐 크기 3, full 시 drop                                    │
│    │                                                                      │
│    ├──► [Detector::get_results()] → 최신 탐지 결과 조회                  │
│    │      └──► 바운딩 박스 좌표 (정규화된 0-1 값)                         │
│    │                                                                      │
│    ├──► [Pipeline::overlay_detections()]                                  │
│    │      └──► Y 플레인에 흰색 사각형 그리기                             │
│    │                                                                      │
│    └──► [Pipeline::encode_and_send()]                                     │
│           ├──► avcodec_send_frame() → H264 인코더                        │
│           ├──► avcodec_receive_packet() → 인코딩된 패킷                  │
│           └──► av_interleaved_write_frame() → RTSP 전송                  │
│                                                                           │
└─────────────────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           SHUTDOWN                                        │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Detector::stop()           → 워커 스레드 종료                        │
│  2. Pipeline::shutdown()       → 인코더 플러시 + RTSP 종료               │
└─────────────────────────────────────────────────────────────────────────┘
```

## 4가지 비디오 모드

| 모드 | enum | 설명 | FFmpeg 필터 | 구현 상태 |
|------|------|------|-------------|-----------|
| 1 | HDMI_ONLY | HDMI 단일 | `scale=1920:1080` | ✅ 완료 |
| 2 | WEBCAM_ONLY | 웹캠 단일 | `scale=1920:1080` | ✅ 완료 |
| 3 | SIDE_BY_SIDE | 좌우 분할 | `hstack` | ⚠️ 단일 입력 폴백 |
| 4 | PIP | PIP 오버레이 | `overlay` | ⚠️ 단일 입력 폴백 |

> **참고**: Multi-input 모드(3,4)는 복잡도로 인해 현재 단일 입력으로 폴백됩니다.  
> 완전한 구현을 위해서는 두 번째 V4L2 입력 스레드와 필터 그래프 다중 입력 연결이 필요합니다.

## 출력 규격

```
codec: h264_v4l2m2m (HW accel via VideoCore VI)
resolution: 1920x1080
bitrate: 4Mbps (CBR)
gop: 30 (1초)
pix_fmt: YUV420P
```

## AI 파이프라인 상세

```
FFmpeg 캡처 (30fps)
    │
    ▼
┌─────────────────┐
│  Input Queue    │  ← max_size=3, full 시 drop (최신 유지)
│  (thread-safe)  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Worker Thread  │
│                 │
│  1. dequeue()   │
│  2. sws_scale() │  ← YUV420P → RGB888 (640x640)
│  3. Hailo infer │  ← YOLOv8s on Hailo-8L
│  4. NMS/Filter  │  ← confidence_threshold 적용
│  5. store       │  ← latest_results_ 업데이트
└────────┬────────┘
         │
         ▼
   Detection 결과
   (x1,y1,x2,y2,confidence,class_id)
```

## 개발 규칙

- **RAII**: FFmpeg 객체는 smart pointer로 관리 (`FramePtr`, `CodecCtxPtr` 등)
- **비동기**: AI 추론은 별도 스레드, 메인루프 블로킹 금지
- **에러 처리**: 모든 `av_` 함수 반환값 체크, 실패 시 로그 출력
- **Queue**: AI 입력 큐 size=3, full 시 drop (실시간성 우선)
- **Thread Safety**: 공유 데이터는 mutex로 보호

## FFmpeg API 사용 현황

| 기능 | 사용 함수 | 버전 |
|------|-----------|------|
| V4L2 캡처 | `avformat_open_input`, `av_read_frame` | 4.x+ |
| 디코딩 | `avcodec_send_packet`, `avcodec_receive_frame` | 3.x+ |
| 필터 | `avfilter_graph_create_filter`, `av_buffersrc/sink` | 3.x+ |
| 인코딩 | `avcodec_send_frame`, `avcodec_receive_packet` | 3.x+ |
| RTSP | `avformat_alloc_output_context2`, `av_interleaved_write_frame` | 3.x+ |

> **API 호환성**: 현재 구현은 FFmpeg 4.x/5.x/6.x 호환 (send/receive API 사용)