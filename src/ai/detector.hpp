/**
 * @file detector.hpp
 * @brief Hailo-8L AI Kit 기반 실시간 객체 탐지 엔진
 * 
 * @section 목적
 * - Raspberry Pi 5 AI Kit의 Hailo-8L 가속기를 활용한 실시간 객체 인식
 * - 비동기 처리를 통한 메인루프 블로킹 방지
 * - 사람(Person) 탐지에 최적화
 * 
 * @section 아키텍처 역할
 * @code
 * [Pipeline::capture()] ──► [AI Input Queue] ──► [Detector Worker Thread]
 *                                                    │
 *                                           [Hailo-8L Inference]
 *                                                    │
 * [Pipeline::overlay_detections()] ◄── [Detection Results]
 * @endcode
 * 
 * @section 처리 흐름
 * 1. main.cpp에서 프레임을 enqueue()로 전달
 * 2. 별도 스레드에서 Hailo-8L로 추론 수행
 * 3. get_results()로 최신 탐지 결과 조회 (non-blocking)
 */

#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>

extern "C" {
#include <libavutil/frame.h>
}

#include "ai/types.hpp"

namespace ai {

/**
 * @class Detector
 * @brief Hailo-8L 비동기 객체 탐지기
 * 
 * 생산자-소비자 패턴 기반의 비동기 AI 추론 엔진
 */
class Detector {
public:
    /**
     * @brief 생성자
     * @param model_path Hailo HEF 모델 파일 경로
     */
    explicit Detector(const std::string& model_path);
    
    /**
     * @brief 소멸자 (자동 정지 및 리소스 해제)
     */
    ~Detector();
    
    /**
     * @brief 탐지기 시작
     * @return true 성공, false 실패
     * 
     * Hailo RT 초기화 및 워커 스레드 시작
     */
    bool start();
    
    /**
     * @brief 탐지기 정지
     */
    void stop();
    
    /**
     * @brief 프레임을 입력 큐에 추가 (thread-safe)
     * @param frame 입력 프레임 (AVFrame*)
     * @param drop_if_full 큐 full 시 오래된 프레임 drop 여부
     * @return true 성공, false 실패
     * 
     * non-blocking, 큐가 full이면 drop_if_full에 따라 처리
     */
    bool enqueue(AVFrame* frame, bool drop_if_full = true);
    
    /**
     * @brief 최신 탐지 결과 조회 (thread-safe)
     * @return 탐지된 객체 목록
     * 
     * non-blocking, 결과가 없으면 빈 벡터 반환
     */
    std::vector<Detection> get_results();
    
private:
    /**
     * @brief 워커 스레드 메인 루프
     * 
     * 큐에서 프레임을 꺼내 Hailo 추론 수행
     */
    void inference_loop();
    
    /**
     * @brief Hailo RT 초기화
     * @return true 성공
     * 
     * VDevice 생성, 네트워크 로드, 가상 스트림 설정
     */
    bool init_hailo();
    
    /**
     * @brief 단일 프레임 추론 수행
     * @param frame 입력 AVFrame
     * 
     * YUV420P → RGB 변환 → Hailo 추론 → 결과 파싱
     */
    void run_inference(AVFrame* frame);
    
    std::string model_path_;           ///< HEF 모델 파일 경로
    
    // 스레딩
    std::thread worker_thread_;        ///< 추론 워커 스레드
    std::atomic<bool> running_{false}; ///< 실행 중 플래그
    
    // 입력 큐 (생산자-소비자)
    struct FrameDeleter {
        void operator()(AVFrame* f) { av_frame_free(&f); }
    };
    std::queue<std::unique_ptr<AVFrame, FrameDeleter>> input_queue_; ///< 입력 프레임 큐
    std::mutex queue_mutex_;           ///< 큐 접근 뮤텍스
    std::condition_variable queue_cv_; ///< 큐 조건 변수
    size_t max_queue_size_ = 3;        ///< 최대 큐 크기 (지연 제한)
    
    // 결과
    std::vector<Detection> latest_results_; ///< 최신 탐지 결과
    std::mutex results_mutex_;         ///< 결과 접근 뮤텍스
    
    // Hailo RT 리소스 (PIMPL 패턴으로 캡슐화)
    void* vdevice_ = nullptr;          ///< Hailo 가상 디바이스
    void* network_group_ = nullptr;    ///< 네트워크 그룹
    void* input_vstream_ = nullptr;    ///< 입력 가상 스트림
    void* output_vstream_ = nullptr;   ///< 출력 가상 스트림
    
    // 모델 정보
    int model_input_width_ = 640;      ///< 모델 입력 너비 (YOLOv8s 기본값)
    int model_input_height_ = 640;     ///< 모델 입력 높이
};

} // namespace ai