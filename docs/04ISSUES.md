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
