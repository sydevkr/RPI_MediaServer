# 03TASKS.md

## 문서 역할
- 이 문서는 작업 상태의 단일 기준 문서다.
- 작업 시작 전 AI는 `docs/01REQUIREMENTS.md`, `docs/02ENVIRONMENT.md`를 읽은 뒤 이 문서를 확인한다.
- 매 작업 마무리 시 반드시 이 문서를 업데이트한다.
- 다음날 AI는 작업 시작 전에 이 문서를 반드시 참조한다.

## AI 작업 시작 시 참조 순서
1. `docs/01REQUIREMENTS.md`
2. `docs/02ENVIRONMENT.md`
3. `docs/03TASKS.md`
4. `docs/04ISSUES.md`

## 상태 정의
- `TODO`: 아직 시작 전
- `IN_PROGRESS`: 현재 진행 중
- `BLOCKED`: 외부 요인으로 진행 불가
- `DONE`: 완료

## Recently Done
| ID | 작업명 | 우선순위 | 상태 | 메모 |
|----|--------|----------|------|------|
| TASK-001 | `USB Camera` 단일 모드 안정화 | High | DONE | 단일 모드 송출 정상 동작 |
| TASK-002 | `2x2 Mix` 임시 구성 송출 안정화 | High | DONE | 현재는 `USB + black + black + black` |
| TASK-003 | 웹 UI / API 포트 정합성 정리 | High | DONE | 웹 `8080`, 앱 API `8081` |
| TASK-004 | 초기 `/api/status` 기반 UI 상태 동기화 | High | DONE | 현재 모드 표시와 버튼 상태 정합성 확보 |
| TASK-005 | `USB <-> 2x2` 모드 전환 안정화 | High | DONE | FFmpeg 리소스 정리 후 재시작 보강 |
| TASK-006 | HLS 재연결 로직 보강 | High | DONE | 모드 전환 후 재접속 안정화 |
| TASK-007 | WebRTC 재연결 로직 보강 | High | DONE | iframe 재로드 및 재연결 보강 |
| TASK-008 | `2x2 Mix` 지연 완화 | High | DONE | `thread_queue_size=64`, `fps=30`, `gop_size=15` |
| TASK-009 | 입력 라벨 오버레이 정리 | Medium | DONE | `USB / FB / CSI / HDMI` 라벨 표시 |
| TASK-010 | wallclock 오버레이 정리 | Medium | DONE | end-to-end 지연 관찰용 |
| TASK-011 | 웹 UI 비디오 모드 메뉴 정리 | Medium | DONE | `FrameBuffer` 비활성 표시 포함 |

## In Progress
| ID | 작업명 | 우선순위 | 상태 | 메모 |
|----|--------|----------|------|------|
| TASK-012 | 현재 문서 체계 기반 작업 운영 정착 | Medium | IN_PROGRESS | 종료 시 `03TASKS.md`, `04ISSUES.md` 갱신 규칙 적용 중 |

## Next Tasks
| ID | 작업명 | 우선순위 | 상태 | 메모 |
|----|--------|----------|------|------|
| TASK-013 | `CSI Camera` 실입력 확인 | High | TODO | 장비 연결 후 화면 수신 확인 |
| TASK-014 | `CSI Camera` 해상도 / FPS 측정 | High | TODO | 실제 입력 스펙 기록 필요 |
| TASK-015 | `CSI Camera` HLS / WebRTC 지연 확인 | High | TODO | 스트리밍 안정성 포함 |
| TASK-016 | `HDMI Capture` 실입력 확인 | High | TODO | 장비 연결 후 입력 확인 |
| TASK-017 | `HDMI Capture` 입력 포맷 / FPS 측정 | High | TODO | 실제 포맷 기록 필요 |
| TASK-018 | `HDMI Capture` 장시간 송출 안정성 확인 | High | TODO | 장시간 동작 테스트 필요 |
| TASK-019 | 실제 `USB + CSI + HDMI` 기반 `2x2 Mix` 구성 | High | TODO | black 3칸 대체 시작 |
| TASK-020 | 실제 3입력 `2x2 Mix` 입력별 FPS 측정 | High | TODO | 성능 병목 확인 |
| TASK-021 | 실제 3입력 `2x2 Mix` 전체 지연 측정 | High | TODO | HLS / WebRTC 비교 포함 |
| TASK-022 | 실제 3입력 `2x2 Mix` 모드 전환 안정성 확인 | High | TODO | 런타임 전환 기준 |
| TASK-023 | Hailo 장비 도착 후 추론 연동 검증 | High | TODO | 현재는 비활성 상태 |
| TASK-024 | USB Camera AI 사람 인식 | Medium | TODO | Hailo 연동 후 구현 |
| TASK-025 | USB Camera AI 화재 인식 | Medium | TODO | 모델/룰 정의 필요 |
| TASK-026 | USB Camera AI 위기상황 인식 | Medium | TODO | 기준 정의 필요 |
| TASK-027 | CSI Camera AI 사람 인식 | Medium | TODO | 실입력 검증 후 진행 |
| TASK-028 | CSI Camera AI 화재 인식 | Medium | TODO | 실입력 검증 후 진행 |
| TASK-029 | CSI Camera AI 위기상황 인식 | Medium | TODO | 실입력 검증 후 진행 |
| TASK-030 | HDMI Capture AI OCR | Medium | TODO | 입력 특성 확인 후 진행 |
| TASK-031 | HDMI Capture AI 위기상황 인식 | Medium | TODO | OS 모니터링 기반 |
| TASK-032 | USB-C HID OTG 인식 확인 | Low | TODO | PC에서 HID 장치 인식 확인 |
| TASK-033 | GPIO 전원 + USB-C HID OTG 동시 구성 확인 | Low | TODO | 동시 사용 가능 여부 확인 |
| TASK-034 | AI HAT + GPIO 전원 + USB-C HID OTG 동시 구성 확인 | Low | TODO | 충돌 여부 및 안정성 확인 |
| TASK-035 | 장시간 전원 안정성 / 발열 / 재부팅 여부 확인 | Low | TODO | 운영 적합성 판단용 |

## Blocked
| ID | 작업명 | 우선순위 | 상태 | 메모 |
|----|--------|----------|------|------|
| TASK-036 | FrameBuffer 재도입 여부 재검토 | Medium | BLOCKED | Raspberry Pi 5 Wayland 환경에서 직접 캡처 불안정 |
| TASK-037 | FrameBuffer AI OCR | Low | BLOCKED | 안정적 캡처 경로 확보 전 진행 불가 |
| TASK-038 | FrameBuffer AI 위기상황 인식 | Low | BLOCKED | 운영 가능한 입력 경로 부재 |

## 현재 우선순위
1. `CSI Camera` 실입력 확인
2. `HDMI Capture` 실입력 확인
3. 실제 입력 기반 `2x2 Mix` 성능 측정
4. Hailo 연동 검증 준비

## 블로커
- `CSI Camera`, `HDMI Capture`, Hailo 관련 검증은 실장비 연결 전까지 진행 불가
- FrameBuffer 직접 캡처는 현재 Raspberry Pi 5 Wayland 환경에서 운영 경로로 사용하기 어렵다

## 운영 규칙
- 상태값, 우선순위, 블로커는 이 문서에만 기록한다.
- 판단 이유, 시행착오, 반복 실수, 주의사항은 `docs/04ISSUES.md`에 기록한다.
- 작업을 끝낼 때는 관련 작업의 상태와 메모를 반드시 갱신한다.
