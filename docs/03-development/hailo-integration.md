# Hailo-8L 통합 가이드

AI Kit(Hailo-8L)을 활용한 실시간 객체 인식 시스템 개발

## 개요

Hailo-8L은 라즈베리파이 5에 PCIe를 통해 연결되는 AI 가속기로, 13 TOPS의 연산 성능을 제공합니다. 본 문서는 실시간 영상 처리와의 통합 방법을 설명합니다.

## 하드웨어 연결

```
┌─────────────────┐      PCIe x1      ┌─────────────┐
│  Raspberry Pi 5 │◄─────────────────►│  Hailo-8L   │
│                 │   AI Kit (M.2)    │  AI Module  │
└─────────────────┘                   └─────────────┘
```

### 연결 확인
```bash
# PCIe 장치 인식 확인
lspci | grep Hailo

# 출력 예시:
# 0000:01:00.0 Co-processor: Hailo Technologies Ltd. Hailo-8 AI Processor (rev 01)

# HailoRT 상태 확인
hailortcli scan
```

## 설치

### HailoRT (런타임)
```bash
# 공식 패키지 설치
sudo apt update
sudo apt install hailo-all hailo-tappas-core

# Python 바인딩
pip install hailo-platform hailo-model-zoo
```

### 펌웨어 업데이트
```bash
# 최신 펌웨어 확인
hailortcli fw-control identify

# 업데이트 (필요 시)
sudo hailortcli fw-control update fw.bin
```

## 기본 사용법

### 동기 추론 (비권장)
```python
from hailo_platform import HailoDevice, HailoStreamInterface

# 장치 열기
device = HailoDevice()

# 모델 로드
hef_path = "yolov8s.hef"
network_group = device.load_hef(hef_path)[0]

# 동기 추론 (메인 스레드 차단)
input_tensor = preprocess(frame)
results = network_group.infer(input_tensor)  # ❌ blocking
```

### 비동기 추론 (권장)
```python
import hailo_platform as hailo
from queue import Queue
import threading

class AsyncHailoInference:
    """비동기 Hailo 추론 클래스."""
    
    def __init__(self, hef_path: str, input_queue: Queue, output_queue: Queue):
        self.device = hailo.HailoDevice()
        self.network = self.device.load_hef(hef_path)[0]
        self.input_queue = input_queue
        self.output_queue = output_queue
        self.running = False
        
    def start(self):
        """추론 스레드 시작."""
        self.running = True
        self.thread = threading.Thread(target=self._inference_loop)
        self.thread.start()
        
    def _inference_loop(self):
        """백그라운드 추론 루프."""
        vstreams = self.network.create_vstream_params(
            hailo.HailoStreamInterface.INFERENCE
        )
        
        with self.network.activate():
            with hailo.InputVStream(vstreams[0]) as input_vstream, \
                 hailo.OutputVStream(vstreams[1]) as output_vstream:
                
                while self.running:
                    try:
                        # 입력 프레임 가져오기 (non-blocking)
                        frame = self.input_queue.get(timeout=0.001)
                        
                        # 추론 실행
                        input_vstream.write(frame)
                        result = output_vstream.read()
                        
                        # 결과 전달
                        self.output_queue.put({
                            'detections': self._parse_detections(result),
                            'timestamp': time.time()
                        })
                        
                    except Empty:
                        continue
                        
    def stop(self):
        """추론 중지."""
        self.running = False
        self.thread.join()

# 사용 예시
input_q = Queue(maxsize=5)    # 프레임 버퍼 (과부하 시 드롭)
output_q = Queue(maxsize=10)  # 결과 버퍼

inference = AsyncHailoInference("yolov8s.hef", input_q, output_q)
inference.start()

# 메인 루프에서 프레임 전달
while True:
    frame = capture_frame()
    if not input_q.full():
        input_q.put(frame)  # ✅ non-blocking
```

## 실시간 영상 통합

### 파이프라인 구조
```
┌──────────┐     ┌──────────────┐     ┌──────────┐     ┌──────────┐
│  FFmpeg  │────►│  프레임      │────►│  Hailo   │────►│  결과    │
│  캡처    │     │  버퍼        │     │  추론    │     │  처리    │
└──────────┘     └──────────────┘     └──────────┘     └──────────┘
                                              │
                                              ▼
                                       ┌──────────────┐
                                       │  경보/로깅   │
                                       │  오버레이    │
                                       └──────────────┘
```

### 통합 예시
```python
import subprocess
import numpy as np
from multiprocessing import Process, Queue

class VideoAiPipeline:
    """FFmpeg + Hailo 통합 파이프라인."""
    
    def __init__(self, video_device: str, hef_path: str):
        self.video_device = video_device
        self.hef_path = hef_path
        self.frame_queue = Queue(maxsize=3)  # 과부하 방지
        self.result_queue = Queue()
        
    def start(self):
        """파이프라인 시작."""
        # FFmpeg 프로세스
        self.ffmpeg_proc = Process(target=self._ffmpeg_capture)
        self.ffmpeg_proc.start()
        
        # AI 추론 프로세스
        self.ai_proc = Process(
            target=self._ai_worker,
            args=(self.hef_path, self.frame_queue, self.result_queue)
        )
        self.ai_proc.start()
        
    def _ffmpeg_capture(self):
        """FFmpeg로 프레임 캡처."""
        cmd = [
            'ffmpeg',
            '-f', 'v4l2',
            '-input_format', 'mjpeg',
            '-video_size', '640x480',  # AI 처리용 다운스케일
            '-framerate', '30',
            '-i', self.video_device,
            '-f', 'rawvideo',
            '-pix_fmt', 'rgb24',
            '-'  # stdout으로 출력
        ]
        
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, bufsize=10**8)
        
        while True:
            # 640x480x3 = 921,600 bytes per frame
            raw_frame = proc.stdout.read(640 * 480 * 3)
            if not raw_frame:
                break
                
            frame = np.frombuffer(raw_frame, dtype=np.uint8)
            frame = frame.reshape((480, 640, 3))
            
            # 큐가 가득 차면 가장 오래된 프레임 버림
            if self.frame_queue.full():
                try:
                    self.frame_queue.get_nowait()
                except:
                    pass
            self.frame_queue.put(frame)
            
    def _ai_worker(self, hef_path, frame_queue, result_queue):
        """별도 프로세스에서 AI 추론."""
        # Hailo 초기화 (프로세스별)
        import hailo_platform as hailo
        
        device = hailo.HailoDevice()
        network = device.load_hef(hef_path)[0]
        
        with network.activate():
            while True:
                frame = frame_queue.get()
                # ... 추론 로직 ...
                detections = self._infer(frame, network)
                result_queue.put(detections)

    def get_results(self):
        """최신 결과 가져오기."""
        results = []
        while not self.result_queue.empty():
            results.append(self.result_queue.get_nowait())
        return results
```

## 모델 선택

| 모델 | 해상도 | FPS | 용도 |
|------|--------|-----|------|
| YOLOv8n | 640x640 | 30+ | 빠른 감지 |
| YOLOv8s | 640x640 | 30 | 균형 |
| YOLOv8m | 640x640 | 20 | 정확도 우선 |
| YOLOv5n | 640x640 | 30+ | 레거시 호환 |

### 모델 컴파일
```bash
# ONNX → HEF 변환
hailo compiler yolov8n.onnx \
    --calib-path calib_data/ \
    --hw-arch hailo8l \
    -o yolov8n.hef
```

## 성능 튜닝

### 배치 크기
```python
# 배치 추론 (처리량 증가, 지연 증가)
batch_size = 4
frames = [frame_queue.get() for _ in range(batch_size)]
results = network.infer_batch(frames)
```

### 프레임 스킵
```python
# AI 처리가 늦을 경우 선택적 스킵
SKIP_EVERY_N = 2  # 30fps → 15fps 처리

frame_count = 0
while True:
    frame = capture()
    frame_count += 1
    
    if frame_count % SKIP_EVERY_N == 0:
        if not input_queue.full():
            input_queue.put(frame)
```

## 문제 해결

| 증상 | 원인 | 해결책 |
|------|------|--------|
| PCIe 인식 실패 | 펌웨어 문제 | `hailortcli fw-control update` |
| 낮은 FPS | 동기 처리 | 비동기 큐 구조 적용 |
| 메모리 부족 | 배치 크기 과대 | 배치=1로 감소 |
| 열 방출 | 지속적 고부하 | 히트싱크/팬 확인 |

## 참고

- [Hailo Developer Zone](https://hailo.ai/developer-zone/)
- [HailoRT Python API](https://hailo.ai/developer-zone/documentation/hailort/latest/)
- [TAPPAS 파이프라인 예시](https://github.com/hailo-ai/tappas)