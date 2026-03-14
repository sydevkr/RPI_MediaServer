/**
 * @file config.hpp
 * @brief 설정 파일 파서 및 설정 구조체
 * 
 * @section 목적
 * - INI 형식 설정 파일 파싱 (key=value)
 * - 전체 시스템 설정을 담는 Config 구조체 정의
 * - 비디오 모드, 해상도, bitrate, AI 설정 등 관리
 * 
 * @section 설정 파일 형식 (config.ini)
 * @code
 * # 주석
 * video_mode=1
 * hdmi_device=/dev/video0
 * width=1920
 * height=1080
 * bitrate=4000000
 * ai_model=/path/to/model.hef
 * @endcode
 */

#pragma once

#include <string>
#include "logger.hpp"

namespace utils {

/**
 * @brief 비디오 모드 열거형
 * 
 * 6가지 화면 모드 지원:
 * 1. 2x2 Mixing (4개 입력 동시)
 * 2-5. 단일 입력 모드 (HDMI, CSI, USB, Screen Capture)
 * 6. PIP (Screen Capture + Camera)
 */
enum class VideoMode {
    MIXING_2X2 = 1,         ///< 4개 입력 2x2 배치 (HDMI, CSI, USB, Screen)
    ONLY_HDMI = 2,          ///< HDMI 단일 입력
    ONLY_CSI = 3,           ///< CSI Camera 단일 입력
    ONLY_USB = 4,           ///< USB Camera 단일 입력
    ONLY_FRAMEBUFFER = 5,   ///< Wayland 화면 캡처 단일 입력
    PIP_FB_RIGHT_TOP = 6    ///< Wayland 화면 메인 + Camera PIP 우측 상단 (미구현)
};

/**
 * @brief 시스템 설정 구조체
 * 
 * 모든 설정 값의 기본값 포함
 */
struct Config {
    // 비디오 설정
    VideoMode video_mode = VideoMode::ONLY_HDMI;   ///< 비디오 모드 (1-6)
    std::string hdmi_device = "/dev/video0";       ///< HDMI 캡처 디바이스
    std::string csi_device = "/dev/video1";        ///< CSI Camera 디바이스
    std::string usb_device = "/dev/video2";        ///< USB Camera 디바이스
    std::string fb_device = "";                    ///< 레거시 FrameBuffer/X11 입력 경로
    int fb_width = 1920;                           ///< Wayland 화면 캡처 너비
    int fb_height = 1080;                          ///< Wayland 화면 캡처 높이
    int width = 1920;                              ///< 출력 너비
    int height = 1080;                             ///< 출력 높이
    int fps = 30;                                  ///< 프레임레이트
    bool show_input_labels = true;                 ///< 2x2 INPUT 타입 라벨 오버레이
    bool show_wallclock_overlay = true;            ///< 송출 지연 측정용 현재 시각 오버레이
    
    // 인코딩 설정
    int bitrate = 4000000;                         ///< 비트레이트 (4Mbps)
    std::string codec = "libx264";                 ///< 코덱 (소프트웨어)
    int gop_size = 30;                             ///< GOP 크기 (1초)
    
    // 스트리밍 설정
    std::string rtsp_url = "rtsp://localhost:8554/live";  ///< RTSP 출력 URL
    std::string wf_recorder_path = "/usr/bin/wf-recorder"; ///< Wayland 캡처 실행 파일
    std::string wayland_display = "";              ///< WAYLAND_DISPLAY override
    std::string xdg_runtime_dir = "";              ///< XDG_RUNTIME_DIR override
    
    // AI 설정
    std::string ai_model = "yolov8s_person.hef";  ///< Hailo HEF 모델 경로
    float confidence_threshold = 0.5f;             ///< 탐지 신뢰도 임계값
    int ai_queue_size = 3;                         ///< AI 입력 큐 크기
    
    // 로깅 설정
    logger::Level log_level = logger::Level::Info; ///< 로그 레벨
};

/**
 * @brief 설정 파일 로드
 * @param path 설정 파일 경로 (config.ini)
 * @return 파싱된 Config 구조체
 * 
 * 파일이 없으면 기본값 사용, 파싱 에러 시 해당 항목은 기본값 유지
 */
Config load_config(const std::string& path);

} // namespace utils
