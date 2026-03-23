# 2x2 Mix 모드 영상 미출력 버그 패치

## 증상
- USB Camera 싱글 모드는 정상 동작
- 2x2 Mix 모드 진입 시 영상 없음, 약 16초마다 파이프라인 재시작 반복

## 근본 원인
2x2 필터 그래프에서 입력 소스 간 PTS 불일치로 `av_buffersink_get_frame`이 장시간 블로킹.

- USB V4L2 입력의 PTS: 시스템 부팅 기준 절대 시각 (~320,000초)
- `color` 가상 소스 필터(FB/CSI/HDMI 슬롯)의 PTS: 0부터 시작
- `xstack` 필터가 두 입력을 PTS 기준으로 동기화하려 하면서, color 필터가 수백만 프레임을 생성해야 하는 상황 발생
- 결과: 첫 프레임 이후 두 번째 프레임부터 필터 그래프 블로킹 → MediaMTX `readTimeout: 10s` 만료 → RTSP 연결 끊김

## 수정 내용 (`src/pipeline/pipeline.cpp`)

### 1. `init_filter_graph_2x2()`
buffersrc의 `time_base`를 V4L2 원본(`1/1000000`)에서 `1/fps`로 변경.
color 필터(`r=fps`)와 동일한 시간 단위를 사용하도록 통일.

### 2. `capture()`
2x2 모드에서 USB 프레임 PTS를 순차 프레임 번호(`0, 1, 2, ...`)로 정규화.
color 필터의 PTS 진행과 1:1 매칭 → xstack 동기화 정상화.

### 3. `pipeline.hpp`
`int64_t pts_2x2_` 멤버 변수 추가, `shutdown()` 시 0으로 리셋.