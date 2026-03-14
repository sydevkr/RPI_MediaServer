# RPi5 Remote Guard & Control - 운영자 가이드

## 목차
1. [시스템 개요](#시스템-개요)
2. [설치 및 빌드](#설치-및-빌드)
3. [시스템 설정](#시스템-설정)
4. [비디오 모드 설정](#비디오-모드-설정)
5. [AI 위기 감지 설정](#ai-위기-감지-설정)
6. [시스템 운영](#시스템-운영)
7. [문제 해결](#문제-해결)

---

## 시스템 개요

### 아키텍처
```
┌─────────────────────────────────────────────────────────┐
│                    RPi5 Remote Guard                     │
├─────────────────────────────────────────────────────────┤
│  [V4L2 입력] → [AI Detector (Hailo-8L)] → [오버레이]    │
│                    ↓                                    │
│           [FFmpeg 파이프라인]                           │
│                    ↓                                    │
│           [RTSP 서버 (MediaMTX)]                        │
└─────────────────────────────────────────────────────────┘
```

### 구성 요소
| 구성 요소 | 역할 |
|-----------|------|
| `rpi_mediaserver` | 메인 애플리케이션 (C++) |
| `mediamtx` | RTSP 스트리밍 서버 |
| Hailo-8L | AI 객체 탐지 (HW 가속) |
| FFmpeg | 영상 인코딩 (h264_v4l2m2m) |

---

## 설치 및 빌드

자세한 설치 절차는 [setup.md](setup.md)를 참조하세요.

### 빠른 시작
```bash
# 빌드
./build.sh

# 실행
./start.sh
```

---

## 시스템 설정

### 설정 파일 위치
- **메인 설정**: `config.ini`
- **MediaMTX 설정**: `mediamtx.yml`

### config.ini 설정 항목

| 섹션 | 항목 | 설명 | 기본값 |
|------|------|------|--------|
| 비디오 | `video_mode` | 1=HDMI, 2=Webcam, 3=Side-by-Side, 4=PIP | 1 |
| 입력장치 | `hdmi_device` | HDMI 입력 장치 경로 | /dev/video0 |
| 입력장치 | `webcam_device` | 웹캠 장치 경로 | /dev/video1 |
| 해상도 | `width`/`height` | 출력 해상도 | 1920x1080 |
| 해상도 | `fps` | 프레임 레이트 | 30 |
| 인코딩 | `bitrate` | 비디오 비트레이트 (bps) | 4000000 |
| 인코딩 | `gop_size` | GOP 크기 | 30 |
| 스트리밍 | `rtsp_url` | RTSP 출력 URL | rtsp://localhost:8554/live |
| AI | `ai_model` | HEF 모델 경로 | /usr/share/hailo-models/... |
| AI | `confidence_threshold` | 탐지 임계값 | 0.5 |
| 로그 | `log_level` | 0=Debug, 1=Info, 2=Warning, 3=Error | 1 |

---

## 비디오 모드 설정

### Mode 1: HDMI 전용
```ini
video_mode=1
hdmi_device=/dev/video0
```

**사용 시나리오**: HDMI 캡처 카드로 TV/카메라 입력 스트리밍

### Mode 2: 웹캠 전용
```ini
video_mode=2
webcam_device=/dev/video1
```

**사용 시나리오**: USB 웹캠 단독 사용

### Mode 3: Side-by-Side (좌우 분할)
```ini
video_mode=3
hdmi_device=/dev/video0
webcam_device=/dev/video1
width=1920
height=1080
```

**사용 시나리오**: HDMI와 웹캠 영상을 좌우로 분할 표시

### Mode 4: PIP (Picture-in-Picture)
```ini
video_mode=4
hdmi_device=/dev/video0
webcam_device=/dev/video1
```

**사용 시나리오**: 메인 영상(HDMI)에 웹캠 영상을 작게 겹쳐 표시

---

## AI 위기 감지 설정

### 지원 모델
- **YOLOv8s Person**: 사람 탐지 전용
- 경로: `/usr/share/hailo-models/yolov8s_person.hef`

### 탐지 설정 조정
```ini
# 높은 민감도 (더 많은 탐지, 오탐 가능)
confidence_threshold=0.3

# 낮은 민감도 (정확한 탐지, 미탐 가능)
confidence_threshold=0.7

# 표준 설정
confidence_threshold=0.5
```

### 탐지 결과 형식
```cpp
struct Detection {
    float x, y, w, h;  // 바운딩 박스 (정규화 좌표)
    float confidence;   // 신뢰도 (0.0 ~ 1.0)
    int class_id;       // 클래스 ID
};
```

---

## 시스템 운영

### 시스템 시작
```bash
# 전체 시스템 시작 (MediaMTX + 애플리케이션)
./start.sh

# 또는 수동 시작
./mediamtx mediamtx.yml &
./build/rpi_mediaserver config.ini
```

### 시스템 중지
```bash
# 프로세스 종료
pkill -f rpi_mediaserver
pkill -f mediamtx

# 또는 Ctrl+C (포그라운드 실행 시)
```

### 설정 변경 후 재시작
```bash
# 1. 현재 프로세스 중지
pkill -f rpi_mediaserver

# 2. 설정 수정
nano config.ini

# 3. 재시작
./start.sh
```

### 자동 시작 설정 (systemd)
```ini
# /etc/systemd/system/rpi-mediaserver.service
[Unit]
Description=RPi5 Remote Guard Media Server
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/RPI_MediaServer
ExecStart=/home/pi/RPI_MediaServer/start.sh
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable rpi-mediaserver
sudo systemctl start rpi-mediaserver
sudo systemctl status rpi-mediaserver
```

---

## 문제 해결

### 로그 확인

**애플리케이션 로그**
```bash
# 콘솔 출력 (기본)
./build/rpi_mediaserver

# 파일 로깅
./build/rpi_mediaserver 2>&1 | tee mediaserver.log
```

**MediaMTX 로그**
```bash
# 로그 파일
tail -f mediamtx.log

# 콘솔 출력
./mediamtx mediamtx.yml
```

### 일반적인 문제

#### RTSP 연결 실패
| 증상 | 원인 | 해결책 |
|------|------|--------|
| `Connection refused` | MediaMTX 미실행 | `./mediamtx mediamtx.yml &` |
| `404 Not Found` | 경로 오류 | `rtsp_url` 확인 |
| 타임아웃 | 방화벽 | 포트 8554 개방 |

#### 영상 입력 문제
| 증상 | 원인 | 해결책 |
|------|------|--------|
| `No such device` | 장치 경로 오류 | `ls /dev/video*` 확인 |
| 검은 화면 | 해상도 불일치 | 입력 장치 지원 해상도 확인 |
| 프레임 드랍 | USB 대역폭 | USB 3.0 포트 사용 |

#### AI 탐지 문제
| 증상 | 원인 | 해결책 |
|------|------|--------|
| 탐지 안됨 | 모델 파일 없음 | `ls /usr/share/hailo-models/` |
| 느린 탐지 | CPU 부하 | Hailo-8L PCIe 연결 확인 |
| 오탐 다수 | 임계값 문제 | `confidence_threshold` 조정 |

### 디버깅 명령어
```bash
# V4L2 장치 확인
v4l2-ctl --list-devices

# 특정 장치 정보
v4l2-ctl -d /dev/video0 --all

# Hailo-8L 상태
hailortcli scan

# 네트워크 포트 확인
netstat -tlnp | grep 8554

# 시스템 리소스
htop
```

---

## 참조 문서
- [setup.md](setup.md) - 상세 설치 가이드
- [architecture.md](architecture.md) - 시스템 아키텍처
- MediaMTX 공식 문서: https://github.com/bluenviron/mediamtx
- Hailo 개발자 문서: https://hailo.ai/developer-zone/