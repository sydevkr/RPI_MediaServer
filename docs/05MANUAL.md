# 05MANUAL.md

## 문서 역할
- 이 문서는 관리자 실행 및 운영 절차를 정리하는 기준 문서다.
- 작업 완료 후 운영 방식이 바뀌면 점진적으로 계속 갱신한다.

## 현재 서비스 구성
- 웹 UI: `http://10.1.119.31:8080/`
- 앱 API: `http://10.1.119.31:8081/api/status`
- RTSP publish: `rtsp://10.1.119.31:8554/live`
- HLS: `http://10.1.119.31:8888/live`
- WebRTC: `http://10.1.119.31:8889/live`

## 사전 확인
- Raspberry Pi 5가 정상 부팅되었는지 확인
- 필요한 입력 장치가 연결되었는지 확인
- 프로젝트 루트 경로에서 작업 중인지 확인
- 설정 파일 `config.ini`가 현재 운영값을 반영하는지 확인

## 빌드
```bash
./build.sh
```

## 실행
```bash
./start.sh
```

## 수동 실행

### 1. MediaMTX 실행
```bash
/usr/local/bin/mediamtx ./mediamtx.yml
```

### 2. 앱 서버 실행
새 터미널:

```bash
./build/RPI_MediaServer config.ini
```

### 3. 웹 정적 서버 실행
새 터미널:

```bash
cd web
python3 -m http.server 8080
```

## 실행 후 확인

### API 상태 확인
```bash
curl http://10.1.119.31:8081/api/status
```

### 포트 확인
```bash
ss -ltnp | grep -E ':(8080|8081|8554|8888|8889)\b'
```

### 장치 확인
```bash
ls -l /dev/video*
```

## 현재 웹 UI에서 가능한 동작
- 스트리밍 프로토콜 전환
  - `RTSP/HLS`
  - `WebRTC`
- 비디오 모드 전환
  - `USB Camera`
  - `CSI Camera`
  - `HDMI Capture`
  - `2x2 Mix`

참고:
- `FrameBuffer`는 메뉴상 비활성 또는 재검토 대상이다.
- `2x2 Mix`는 현재 실제 4입력 완성 상태가 아니다.

## 종료

### 포그라운드 종료
- 각 터미널에서 `Ctrl+C`

### 강제 종료
```bash
killall -9 mediamtx
killall -9 RPI_MediaServer
pkill -f "python3 -m http.server 8080"
```

## 재시작 절차
1. 기존 프로세스를 종료한다.
2. `MediaMTX`를 먼저 실행한다.
3. 앱 서버를 실행한다.
4. 웹 정적 서버를 실행한다.
5. API와 포트를 확인한다.

## 장애 점검

### 웹페이지가 열리지 않을 때
- `python3 -m http.server 8080` 실행 여부 확인
- `8080` 포트 리슨 여부 확인

### 웹은 열리지만 영상이 안 나올 때
- 앱 서버 실행 여부 확인
- MediaMTX 실행 여부 확인
- `8081`, `8554`, `8888`, `8889` 포트 확인

### 카메라 입력 오류가 날 때
- `/dev/video*` 장치 존재 여부 확인
- 다른 프로세스가 장치를 점유하는지 확인

### HLS 또는 WebRTC가 끊길 때
- MediaMTX 프로세스 확인
- 앱 서버의 RTSP publish 상태 확인
- 웹페이지를 새로고침해 재연결 확인
