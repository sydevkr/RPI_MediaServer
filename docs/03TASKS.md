# 03TASKS.md

## 문서 역할
- 이 문서는 작업 상태의 단일 기준 문서다.
- 매 작업 마무리 시 반드시 이 문서를 업데이트한다.
- 상태와 메모만 기록하고, 판단 이유와 시행착오는 `docs/04ISSUES.md`에 남긴다.

## 상태 표시 규칙
- `[x]` 완료
- `[-]` 진행 중
- `[ ]` 예정
- `[!]` 막힘

## 완료
- [x] 모드별 스트리밍 해상도 설정 분리 - `2x2`는 `320x240`, 싱글 모드는 `1280x720` 적용
- [x] 모드별 비트레이트 설정 분리 - `2x2`는 `mix_bitrate`, 싱글 모드는 `single_bitrate` 적용
- [x] 모드별 wallclock 오버레이 배치/크기 분리 - `2x2`는 좌상단 작게, 싱글 모드는 우상단 크게 적용
- [x] `USB Camera` 단일 모드 안정화 - 단일 모드 송출 정상 동작
- [x] `2x2 Mix` 임시 구성 송출 안정화 - 현재는 `USB + black + black + black`
- [x] 웹 UI / API 포트 정합성 정리 - 웹 `8080`, 앱 API `8081`
- [x] 초기 `/api/status` 기반 UI 상태 동기화 - 현재 모드 표시와 버튼 상태 정합성 확보
- [x] `USB <-> 2x2` 모드 전환 안정화 - FFmpeg 리소스 정리 후 재시작 보강
- [x] HLS 재연결 로직 보강 - 모드 전환 후 재접속 안정화
- [x] WebRTC 재연결 로직 보강 - iframe 재로드 및 재연결 보강
- [x] `2x2 Mix` 지연 완화 - `thread_queue_size=64`, `fps=30`, `gop_size=15`
- [x] 입력 라벨 오버레이 정리 - `USB / FB / CSI / HDMI` 라벨 표시
- [x] wallclock 오버레이 정리 - end-to-end 지연 관찰용
- [x] 웹 UI 비디오 모드 메뉴 정리 - `FrameBuffer` 비활성 표시 포함
- [x] `FrameBuffer` 이미지 스냅샷 경로 반영 - `grim + ffmpeg` 기반 최신 JPEG 생성
- [x] `screen` 모드 이미지 뷰 전환 - `5초` 주기 최신 이미지 갱신
- [x] `2x2 Mix`의 `FB` 칸 이미지 반영 - 최신 스냅샷 JPEG를 우상단 슬롯에 합성
- [x] `/framebuffer/latest.jpg` 제공 - 최신 FrameBuffer 이미지 직접 확인 경로 추가
- [x] `FrameBuffer` 스냅샷 기반 운영 안정화 - `screen` 5초 갱신, `2x2 FB` 칸 반영, GUI 세션 의존성 점검
- [x] 모드 전환 시 해상도 자동 반영 확인 HLS, WebRTC 기준
- [x] 모드 전환 시 wallclock 오버레이 위치/크기 반영 확인 - `2x2` 좌상단, 싱글 우상단 기준
- [x] USB `720p` 비트레이트 품질 비교 - `single_bitrate=10000000` 기준, 필요 시 `8000000` 재비교

## 진행 중

## 예정
- [ ] `CSI Camera` 실입력 확인 - 장비 연결 후 화면 수신 확인
- [ ] `CSI Camera` 해상도 / FPS 측정 - 실제 입력 스펙 기록 필요
- [ ] `CSI Camera` HLS / WebRTC 지연 확인 - 스트리밍 안정성 포함
- [ ] `HDMI Capture` 실입력 확인 - 장비 연결 후 입력 확인
- [ ] `HDMI Capture` 입력 포맷 / FPS 측정 - 실제 포맷 기록 필요
- [ ] `HDMI Capture` 장시간 송출 안정성 확인 - 장시간 동작 테스트 필요

- [ ] Hailo 장비 도착 후 추론 연동 검증 - 현재는 비활성 상태

- [ ] USB Camera AI 사람 인식 - Hailo 연동 후 구현
- [ ] USB Camera AI 화재 인식 - 모델/룰 정의 필요
- [ ] USB Camera AI 위기상황 인식 - 기준 정의 필요
- [ ] CSI Camera AI 사람 인식 - 실입력 검증 후 진행
- [ ] CSI Camera AI 화재 인식 - 실입력 검증 후 진행
- [ ] CSI Camera AI 위기상황 인식 - 실입력 검증 후 진행
- [ ] HDMI Capture AI OCR - 입력 특성 확인 후 진행
- [ ] HDMI Capture AI 위기상황 인식 - OS 모니터링 기반

- [ ] USB-C HID OTG 인식 확인 - PC에서 HID 장치 인식 확인
- [ ] GPIO 전원 + USB-C HID OTG 동시 구성 확인 - 동시 사용 가능 여부 확인

- [ ] 장시간 전원 안정성 / 발열 / 재부팅 여부 확인 - 운영 적합성 판단용

## 막힘
- [!] `2x2 Mix`의 `FB` 칸 갱신/지연 특성 확인 - 스냅샷 갱신 주기와 체감 반영 속도 점검
- [!] `FrameBuffer` AI OCR - 선행조건: `FrameBuffer` 스냅샷 입력 경로 안정화 및 운영 기준 확정
- [!] `FrameBuffer` AI 위기상황 인식 - 선행조건: `FrameBuffer` 스냅샷 입력 경로 안정화 및 운영 가능한 캡처 경로 확보

## 현재 우선순위
1. `FrameBuffer` 스냅샷 기반 운영 안정화 확인
2. `2x2 Mix`의 `FB` 칸 갱신/지연 특성 확인
3. `CSI Camera` 실입력 확인
4. `HDMI Capture` 실입력 확인
5. 실제 입력 기반 `2x2 Mix` 성능 측정

## 운영 규칙
- 상태값, 우선순위, 블로커는 이 문서에만 기록한다.
- 판단 이유, 시행착오, 반복 실수, 주의사항은 `docs/04ISSUES.md`에 기록한다.
- 작업을 끝낼 때는 관련 작업의 상태와 메모를 반드시 갱신한다.
