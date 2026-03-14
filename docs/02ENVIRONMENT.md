# 02ENVIRONMENT.md

## 문서 역할
- 이 문서는 개발 환경과 기술 설계를 함께 관리하는 기준 문서다.
- 작업 시작 전 AI는 `docs/01REQUIREMENTS.md` 다음으로 이 문서를 읽어야 한다.

## 개발 환경
- 언어: C++17
- 빌드: CMake 3.16+, `build.sh`
- 플랫폼: Raspberry Pi 5, Raspberry Pi OS 64-bit (Bookworm)
- 주요 라이브러리: FFmpeg, HailoRT(선택), pthread
- 스트리밍 서버: MediaMTX
- 화면 캡처: V4L2 입력, Wayland는 `wf-recorder`

## 디렉터리 구조

```text
RPI_MediaServer/
├── src/
│   ├── main.cpp
│   ├── ai/
│   ├── pipeline/
│   └── utils/
├── web/
├── inc/
├── build.sh
├── start.sh
├── config.ini
├── mediamtx.yml
├── docs/01REQUIREMENTS.md
├── docs/02ENVIRONMENT.md
├── docs/03TASKS.md
├── docs/04ISSUES.md
├── docs/05MANUAL.md
└── docs/archive/
```

## 아키텍처 개요
- 입력 소스는 V4L2 장치 또는 Wayland 화면 캡처를 통해 수집한다.
- `pipeline` 계층이 FFmpeg 입력, 필터, 인코딩, 출력 처리를 담당한다.
- `ai` 계층은 비동기 감지 파이프라인을 담당하며 HailoRT가 없을 때는 비활성 동작을 허용한다.
- `web`은 관리자 페이지를 제공하고 앱 API와 연동해 상태를 제어한다.
- MediaMTX가 RTSP ingest 이후 HLS/WebRTC 경로를 제공한다.

## 모듈 책임
- `src/main.cpp`
  - 설정 로드, 서버 초기화, 메인 실행 흐름 관리
- `src/pipeline/`
  - 입력 초기화, 필터 그래프, 인코딩, 송출, 모드 전환 처리
- `src/ai/`
  - 추론 큐, 워커 스레드, 감지 결과 관리
- `src/utils/`
  - 설정 파싱, 로깅, 화면 캡처 보조 기능
- `web/`
  - 운영자 웹 UI, 모드 전환 및 상태 표시

## 데이터 흐름
1. `config.ini`에서 런타임 설정을 읽는다.
2. 입력 소스에서 프레임을 수집한다.
3. FFmpeg 필터 그래프에서 스케일링/합성/오버레이를 수행한다.
4. 필요 시 AI 큐로 프레임을 전달하고 최신 감지 결과를 가져온다.
5. 인코딩 후 RTSP publish를 수행한다.
6. MediaMTX가 HLS/WebRTC 재생 경로를 노출한다.
7. 웹 UI는 상태 API와 MediaMTX 재생 경로를 사용한다.

## 비디오 모드 기준
- `1=MIXING_2X2`
- `2=ONLY_HDMI`
- `3=ONLY_CSI`
- `4=ONLY_USB`
- `5=ONLY_SCREEN_WAYLAND`
- `6=PIP_FB_RIGHT_TOP`

현재 실동작 해석:
- `MIXING_2X2`는 현시점에서 `USB + black + black + black` 기반이다.
- `FrameBuffer` 직접 캡처는 운영 기능이 아니다.
- `ONLY_SCREEN_WAYLAND`는 `wf-recorder` 기반 경로를 사용한다.

## 주요 설정값
- 기본 출력 해상도: `320x240`
- FPS: `30`
- 코덱: `libx264`
- GOP: `15`
- RTSP URL: `rtsp://10.1.119.31:8554/live`
- 기본 AI 모델: `/usr/share/hailo-models/yolov8s_person.hef`

## 설계 원칙
- 실시간성 우선: 오래된 프레임 처리보다 최신 프레임 유지가 우선이다.
- 비동기 AI: 추론은 메인 영상 파이프라인을 막지 않는다.
- RAII 기반 자원 관리: FFmpeg 객체 수명 관리를 명확히 한다.
- 명시적 에러 처리: FFmpeg/Hailo 호출 실패 시 로그를 남긴다.
- 모드 전환 안정성: 입력/출력 리소스를 명확히 정리한 뒤 재시작한다.

## 구현 제약 및 주의
- Wayland 환경에서는 `fbdev`, `x11grab`, `kmsgrab`가 그대로 동작하지 않을 수 있다.
- CSI/HDMI 메뉴 준비 상태와 실제 하드웨어 검증 완료 상태를 혼동하지 않는다.
- HailoRT가 없을 경우에도 빌드는 가능해야 한다.

## 빌드 및 실행 기준

### 빌드
```bash
./build.sh
```

### 실행
```bash
./start.sh
```

## AI 작업 시작 시 참조 순서
1. `docs/01REQUIREMENTS.md`
2. `docs/02ENVIRONMENT.md`
3. `docs/03TASKS.md`
4. `docs/04ISSUES.md`
