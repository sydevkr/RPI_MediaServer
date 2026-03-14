/**
 * @file detector.cpp
 * @brief Hailo-8L AI 객체 탐지 구현
 * 
 * @section 목적
 * Hailo RT API를 사용한 실시간 객체 인식 구현
 * - YUV420P 프레임을 RGB로 변환
 * - Hailo-8L TPU에서 YOLOv8s 추론 수행
 * - 결과를 비동기적으로 메인 스레드에 전달
 * 
 * @section 아키텍처 연동
 * @code
 * [main.cpp] ──► enqueue() ──► [Input Queue] ──► [Worker Thread]
 *                                                   │
 *                                           [run_inference()]
 *                                               │
 *                                       [YUV→RGB 변환]
 *                                               │
 *                                       [Hailo-8L 추론]
 *                                               │
 *                                       [NMS, 결과 파싱]
 *                                               │
 * [get_results()] ◄── [latest_results_] ◄──────┘
 * @endcode
 */

#include "detector.hpp"
#include "utils/logger.hpp"
#include "utils/config.hpp"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#ifdef HAS_HAILORT
#include <hailo/hailort.hpp>
#endif

namespace ai {

/**
 * @brief 생성자 - 모델 경로 저장
 */
Detector::Detector(const std::string& model_path) 
    : model_path_(model_path) {}

/**
 * @brief 소멸자 - 정지 및 리소스 해제 보장
 */
Detector::~Detector() {
    stop();
}

/**
 * @brief 탐지기 시작
 * @return true 성공
 * 
 * 1. Hailo RT 초기화 (VDevice 생성)
 * 2. HEF 모델 로드 및 configure
 * 3. 가상 스트림 생성
 * 4. 워커 스레드 시작
 */
bool Detector::start() {
    if (running_) return true;
    
    if (!init_hailo()) {
        logger::error("Failed to initialize Hailo");
        return false;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&Detector::inference_loop, this);
    
    logger::info("AI Detector started with model: {}", model_path_);
    return true;
}

/**
 * @brief 탐지기 정지 및 리소스 해제
 */
void Detector::stop() {
    running_ = false;
    queue_cv_.notify_all();  // 대기 중인 스레드 깨우기
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // Hailo 리소스 정리
    // TODO: VDevice, NetworkGroup 정리
    
    logger::info("AI Detector stopped");
}

/**
 * @brief Hailo RT 초기화
 * @return true 성공
 * 
 * Hailo Runtime 순서:
 * 1. VDevice 생성 (PCIe 연결)
 * 2. HEF 파일 로드 및 configure_network_group
 * 3. Input/Output 가상 스트림 생성
 * 4. 모델 입력 크기 확인
 */
bool Detector::init_hailo() {
#ifdef HAS_HAILORT
    try {
        // Hailo 가상 디바이스 생성
        auto vdevice_exp = hailo::VDevice::create();
        if (!vdevice_exp) {
            logger::error("Failed to create VDevice: {}", vdevice_exp.status());
            return false;
        }
        
        auto vdevice = vdevice_exp.release();
        
        // 네트워크 그룹 설정 (HEF 모델 로드)
        auto network_group_exp = hailo::configure_network_group(*vdevice, model_path_);
        if (!network_group_exp) {
            logger::error("Failed to configure network: {}", network_group_exp.status());
            return false;
        }
        
        // 가상 스트림 생성
        auto input_vstreams_exp = network_group_exp->create_input_vstreams();
        auto output_vstreams_exp = network_group_exp->create_output_vstreams();
        
        if (!input_vstreams_exp || !output_vstreams_exp) {
            logger::error("Failed to create virtual streams");
            return false;
        }
        
        // 첫 번째 스트림 정보 저장
        auto& input_vstream = input_vstreams_exp.value()[0];
        auto& output_vstream = output_vstreams_exp.value()[0];
        
        auto input_shape = input_vstream.get_shape();
        model_input_width_ = input_shape.width;
        model_input_height_ = input_shape.height;
        
        logger::info("Hailo model input: {}x{}", model_input_width_, model_input_height_);
        
        vdevice_ = vdevice;
        
        return true;
        
    } catch (const std::exception& e) {
        logger::error("Hailo exception: {}", e.what());
        return false;
    }
#else
    // Hailo 미지원: 시뮬레이션 모드
    logger::warn("Hailo not available, running in simulation mode");
    model_input_width_ = 640;
    model_input_height_ = 640;
    return true;
#endif
}

/**
 * @brief 프레임을 입력 큐에 추가 (thread-safe)
 * @param frame 입력 프레임 (AVFrame* - 소유권 이전됨)
 * @param drop_if_full 큐 full 시 처리 방식
 * @return true 성공
 * 
 * 생산자 역할. 큐가 full일 때:
 * - drop_if_full=true: 가장 오래된 프레임 제거
 * - drop_if_full=false: 실패 반환
 */
bool Detector::enqueue(AVFrame* frame, bool drop_if_full) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (!frame) return false;
    
    if (input_queue_.size() >= max_queue_size_) {
        if (drop_if_full) {
            // 가장 오래된 프레임 제거 (최신 유지 정책)
            input_queue_.pop();
            logger::debug("AI queue full, dropping oldest frame");
        } else {
            return false;
        }
    }
    
    // AVFrame 복제하여 큐에 저장
    std::unique_ptr<AVFrame, FrameDeleter> frame_ptr(av_frame_alloc());
    if (av_frame_ref(frame_ptr.get(), frame) < 0) {
        return false;
    }
    
    input_queue_.push(std::move(frame_ptr));
    queue_cv_.notify_one();  // 워커 스레드 깨우기
    return true;
}

/**
 * @brief 최신 탐지 결과 조회 (thread-safe)
 * @return 탐지 결과 복사본
 * 
 * 소비자 역할. non-blocking.
 */
std::vector<Detection> Detector::get_results() {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return latest_results_;
}

/**
 * @brief 워커 스레드 메인 루프
 * 
 * 큐 모니터링 → 프레임 추출 → 추론 → 결과 업데이트 반복
 */
void Detector::inference_loop() {
    while (running_) {
        std::unique_ptr<AVFrame, FrameDeleter> frame;
        
        // 큐에서 프레임 대기 (조건 변수)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !input_queue_.empty() || !running_; 
            });
            
            if (!running_) break;
            if (input_queue_.empty()) continue;
            
            frame = std::move(input_queue_.front());
            input_queue_.pop();
        }
        
        if (frame) {
            run_inference(frame.get());
        }
    }
}

/**
 * @brief 단일 프레임 추론 수행
 * @param frame 입력 AVFrame (YUV420P)
 * 
 * 처리 단계:
 * 1. FFmpeg SWS로 YUV420P → RGB 변환
 * 2. Hailo 입력 버퍼에 복사
 * 3. Hailo 추론 실행
 * 4. 출력 파싱 (NMS, threshold 적용)
 * 5. 결과 저장
 */
void Detector::run_inference(AVFrame* frame) {
    // SWS 컨텍스트 생성 (YUV → RGB)
    SwsContext* sws_ctx = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        model_input_width_, model_input_height_, AV_PIX_FMT_RGB24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!sws_ctx) {
        logger::warn("Failed to create SWS context");
        return;
    }
    
    // RGB 버퍼 할당
    uint8_t* rgb_data[4] = {nullptr};
    int rgb_linesize[4] = {0};
    av_image_alloc(rgb_data, rgb_linesize, 
                   model_input_width_, model_input_height_, AV_PIX_FMT_RGB24, 32);
    
    // 색상 공간 변환
    sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
              rgb_data, rgb_linesize);
    
    // 결과 벡터
    std::vector<Detection> results;
    
    // TODO: 실제 Hailo 추론 코드
    // 현재는 시뮬레이션 모드
    // 
    // hailo_status status = hailo_infer(...);
    // if (status == HAILO_SUCCESS) {
    //     // 출력 파싱 (NMS, threshold)
    // }
    
    // AI 기능 비활성화 - 탐지 결과 없음
    // Hailo-8L 장비 도착 후 실제 구현 예정
    
    // 결과 업데이트 (mutex 보호)
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        latest_results_ = std::move(results);
    }
    
    // 리소스 정리
    av_freep(&rgb_data[0]);
    sws_freeContext(sws_ctx);
}

} // namespace ai