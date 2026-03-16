# 05MANUAL.md

## 실행

```bash
vi config.ini
# mix_width=320, mix_height=240, single_width=1280, single_height=720 확인
# mix_bitrate=2000000, single_bitrate=10000000 확인
# framebuffer_snapshot_path=/tmp/framebuffer_latest.jpg 확인
# framebuffer_snapshot_interval_sec=5 확인
# framebuffer_snapshot_quality=7 확인

./build.sh
# 프로젝트 빌드

sudo systemctl start mediamtx
# MediaMTX 서비스 시작

./start.sh
# 앱 서버 실행
```

## FrameBuffer 확인

```bash
curl http://10.1.119.31:8081/api/status
# 현재 모드와 snapshot 주기 확인

curl -I http://10.1.119.31:8081/framebuffer/latest.jpg
# 최신 FrameBuffer JPEG 응답 확인

xdg-open http://10.1.119.31:8081/
# 메인 UI에서 screen 모드 확인
```

`screen` 모드는 현재 영상 스트림이 아니라 `5초`마다 갱신되는 최신 이미지 모드다. 직접 확인 경로는 아래와 같다.

- 메인 UI: `http://10.1.119.31:8081/`
- 최신 이미지: `http://10.1.119.31:8081/framebuffer/latest.jpg`
- 상태 API: `http://10.1.119.31:8081/api/status`

## 실행 특이사항

- FrameBuffer 캡처는 `grim`을 사용하므로 Wayland GUI 세션이 살아 있어야 한다.
- TTY/SSH에서 직접 실행하면 display 접근 실패가 날 수 있다.
- 필요 시 아래 환경값을 명시해 GUI 세션 기준으로 실행한다.

```bash
WAYLAND_DISPLAY=wayland-0 \
XDG_RUNTIME_DIR=/run/user/1000 \
XDG_SESSION_TYPE=wayland \
./build/RPI_MediaServer config.ini
```

## 확인

```bash
curl http://10.1.119.31:8081/api/status
# 앱 상태 확인

curl http://10.1.119.31:8081/api/status | jq '.resolution'
# 현재 모드에 적용된 출력 해상도 확인

grep -E 'mix_bitrate|single_bitrate' config.ini
# 현재 설정된 모드별 비트레이트 확인

systemctl status mediamtx --no-pager
# MediaMTX 서비스 상태 확인

ss -ltnp | grep -E ':(8080|8081|8554|8888|8889)\b'
# 주요 포트 리슨 확인

ls -l /dev/video*
# 비디오 장치 확인
```

## wf-recorder 참고용 절차

아래 내용은 이전 실험 경로이며 참고용이다. 현재 권장 운영 경로는 아니다.

```bash
mkdir -p ~/.config/systemd/user
cp systemd/user/wf-recorder.service ~/.config/systemd/user/wf-recorder.service

chmod +x ./start-wfrec.sh

systemctl --user daemon-reload
systemctl --user enable wf-recorder.service

loginctl enable-linger "$USER"
# SSH 세션 밖에서도 user service 유지가 필요할 때 1회 실행

systemctl --user start wf-recorder.service
systemctl --user stop wf-recorder.service
systemctl --user restart wf-recorder.service
systemctl --user status wf-recorder.service --no-pager

journalctl --user -u wf-recorder.service -f
# wf-recorder 실행 로그 확인

./start-wfrec.sh single
./start-wfrec.sh mix
```
