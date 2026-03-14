# RPI_MediaServer 운영 가이드

## 목적
- Raspberry Pi 5 부팅 후 관리자가 관련 서비스를 직접 실행하고 상태를 확인하기 위한 문서다.
- 현재 기준 운영 대상은 아래 3개다.
  - `MediaMTX`
  - `RPI_MediaServer`
  - 웹 정적 서버 (`python3 -m http.server 8080`)

## 현재 서비스 구성
- 웹 UI: `http://10.1.119.31:8080/`
- 앱 API: `http://10.1.119.31:8081/api/status`
- RTSP publish: `rtsp://10.1.119.31:8554/live`
- HLS: `http://10.1.119.31:8888/live`
- WebRTC: `http://10.1.119.31:8889/live`

## 사전 확인
- Raspberry Pi 5가 정상 부팅되었는지 확인
- USB Camera가 연결되어 있는지 확인
- 프로젝트 경로가 아래와 같은지 확인

```bash
cd ~/my_code/RPI_MediaServer
```

- 현재 기본 설정 파일은 `config.ini`
- 현재 기본 비디오 모드는 `video_mode=1` (`2x2 Mix`)

## 서비스 실행 순서

### 1. MediaMTX 실행
```bash
cd ~/my_code/RPI_MediaServer
sudo /opt/mediamtx/mediamtx ./mediamtx.yml
```

- 이 프로세스는 RTSP/HLS/WebRTC 중계 서버 역할을 한다.
- 포트:
  - `8554`
  - `8888`
  - `8889`

### 2. RPI_MediaServer 실행
새 터미널에서 실행:

```bash
cd ~/my_code/RPI_MediaServer
sudo ./build/RPI_MediaServer config.ini
```

- 이 프로세스는 카메라 입력 캡처, FFmpeg 파이프라인 처리, RTSP publish, API 서버 역할을 한다.
- API 포트:
  - `8081`

### 3. 웹 정적 서버 실행
새 터미널에서 실행:

```bash
cd ~/my_code/RPI_MediaServer/web
sudo python3 -m http.server 8080
```

- 이 프로세스는 관리자 웹페이지를 제공한다.
- 웹 포트:
  - `8080`

## 실행 후 확인 방법

### 1. 웹 브라우저에서 확인
맥북 또는 같은 네트워크의 PC에서 아래 주소 접속:

```text
http://10.1.119.31:8080/
```

확인 항목:
- 페이지가 정상 로드되는지
- 좌측 영상이 보이는지
- 우측 비디오 모드 메뉴가 보이는지
- 모드 전환이 동작하는지

### 2. API 상태 확인
Raspberry Pi 또는 다른 PC에서:

```bash
curl http://10.1.119.31:8081/api/status
```

정상 응답 예시:

```json
{
  "status": "running",
  "mode": "2x2",
  "mode_id": 1,
  "resolution": {
    "width": 320,
    "height": 240,
    "fps": 30
  }
}
```

### 3. 스트리밍 포트 확인
Raspberry Pi에서:

```bash
sudo ss -ltnp | grep -E ':(8080|8081|8554|8888|8889)\b'
```

정상 상태면 아래 포트가 보여야 한다.
- `8080`
- `8081`
- `8554`
- `8888`
- `8889`

## 현재 웹 UI에서 가능한 동작
- 스트리밍 프로토콜 전환
  - `RTSP/HLS`
  - `WebRTC`
- 비디오 모드 전환
  - `USB Camera`
  - `CSI Camera`
  - `HDMI Capture`
  - `2×2 Mix`

참고:
- `FrameBuffer`는 메뉴에 보이지만 현재 비활성화 상태다.
- 현재 `2×2 Mix`는 실제 4입력 합성이 아니라 `USB + black + black + black`이다.

## 서비스 종료 방법

### 개별 종료
각 실행 터미널에서 `Ctrl+C`

### 강제 종료
```bash
sudo killall -9 mediamtx
sudo killall -9 RPI_MediaServer
sudo pkill -f "python3 -m http.server 8080"
```

## 재시작 절차
문제가 생기면 아래 순서로 다시 올린다.

### 1. 기존 프로세스 종료
```bash
sudo killall -9 mediamtx
sudo killall -9 RPI_MediaServer
sudo pkill -f "python3 -m http.server 8080"
```

### 2. 순서대로 재실행
```bash
cd ~/my_code/RPI_MediaServer
sudo /opt/mediamtx/mediamtx ./mediamtx.yml
```

새 터미널:

```bash
cd ~/my_code/RPI_MediaServer
sudo ./build/RPI_MediaServer config.ini
```

새 터미널:

```bash
cd ~/my_code/RPI_MediaServer/web
sudo python3 -m http.server 8080
```

## 장애 점검 순서

### 1. 웹페이지가 안 열릴 때
확인 순서:
- `python3 -m http.server 8080` 실행 여부
- `8080` 포트 리슨 여부
- 브라우저 접속 주소가 `http://10.1.119.31:8080/`인지 확인

확인 명령:

```bash
sudo ss -ltnp | grep ':8080'
```

### 2. 웹페이지는 열리는데 영상이 안 나올 때
확인 순서:
- `RPI_MediaServer` 실행 여부
- `MediaMTX` 실행 여부
- `8081`, `8554`, `8888`, `8889` 포트 리슨 여부

확인 명령:

```bash
curl http://10.1.119.31:8081/api/status
sudo ss -ltnp | grep -E ':(8081|8554|8888|8889)\b'
```

### 3. 카메라 입력 오류가 날 때
확인 순서:
- `/dev/video0`가 실제 존재하는지
- 다른 프로세스가 카메라를 점유하고 있는지

확인 명령:

```bash
ls -l /dev/video*
sudo fuser /dev/video0
```

### 4. HLS 또는 WebRTC가 끊길 때
확인 순서:
- `MediaMTX` 프로세스 확인
- `RPI_MediaServer`가 RTSP publish 중인지 확인
- 웹페이지 새로고침 후 재확인

## 관리자 참고 메모
- 현재 운영 기준 핵심 서비스는 반드시 3개가 모두 살아 있어야 한다.
  - `mediamtx`
  - `RPI_MediaServer`
  - `python3 -m http.server 8080`
- 서비스가 중간에 내려가면 웹 UI, API, HLS, WebRTC 중 일부 또는 전체가 즉시 동작하지 않을 수 있다.
- 현재 문서는 수동 실행 기준 가이드다.
- CSI Camera와 HDMI Capture 실물 장비가 도착하면, 이 문서에 장비 연결 후 점검 절차를 추가할 예정이다.
