# 04ISSUES.md

## Raspberry Pi 5 FrameBuffer 캡쳐 관련 이슈 
- Raspberry Pi 5의 GUI는 Wayland를 사용한다.
- FrameBuffer 를 실시간 캡쳐하기 위해선는 `wf-recorder` 캡쳐해야 한다. 
- `wf-recorder` 원격 SSH에서 실행하기 위해서는 Wayland GUI 사용자 세션에 연결된 `systemd --user` service로 제어한다.
- `start-wfrec.sh`는 `XDG_RUNTIME_DIR`, `WAYLAND_DISPLAY`를 동적으로 찾고 
- systemd 이 `start-wfrec.sh` 셀을 통해 wf-recorder 를 제어한다.  
- `wf-recorder -> MediaMTX` direct RTSP publish는 `avio_open failed`로 실패했다.(wf-recorder 방식 스펙아웃)

## Raspberry Pi 5 FrameBuffer 켭쳐 방식을 이미지 방식으로 처리  
- 장기 운영성과 단순성을 위해 `grim + ffmpeg` 기반 최신 JPEG 스냅샷 방식으로 전환했다.
- 현재 `screen` 모드는 WebRTC/HLS 영상이 아니라 HTTP 최신 이미지 갱신 방식이다.
- 현재 `2x2`의 `FB` 칸은 영상 입력이 아니라 최근 FrameBuffer 스냅샷 이미지를 사용한다.
- FrameBuffer 스냅샷 루프가 실패해도 `2x2`는 전체 송출을 멈추지 않고 `FB` 칸을 black fallback으로 유지하는 것이 현재 기준이다.

## 웹 UI 메뉴 구성 관련 메모
- 사용자 요청에 따라 제어 패널을 좌측으로 이동하고, 비디오 영역은 우측으로 배치했다.
- 좌측 메뉴의 상단 순서는 `스트리밍 모드 -> AI 메뉴 -> 비디오 모드`로 고정했다.
- AI 메뉴는 현재 UI 상태 선택/표시 및 URL/localStorage 동기화까지만 지원하며, 백엔드 AI API 연동은 후속 단계다.

## start.sh 실행 위치 의존성 이슈
- HTTP 서버는 `main.cpp`에서 `server.set_base_dir("./web")`를 사용하므로 실행 CWD에 따라 다른 정적 파일이 노출될 수 있다.
- `start.sh`에 스크립트 디렉터리 기준 `cd`를 추가해, 어디서 실행해도 프로젝트의 `./web`을 서빙하도록 고정했다.
