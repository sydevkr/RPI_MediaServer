/**
 * @file config.cpp
 * @brief 설정 파일 파서 구현
 * 
 * @section 목적
 * INI 형식 설정 파일을 파싱하여 Config 구조체로 변환
 * 
 * @section 지원 형식
 * - key=value 형태
 * - # 또는 ;로 시작하는 주석
 * - 공백 무시 (trim)
 * - 지원하지 않는 키는 무시
 * 
 * @section 처리 흐름
 * 파일 열기 → 라인 단위 파싱 → 키-값 분리 → 값 변환 → Config에 저장
 */

#include "config.hpp"
#include <fstream>
#include <sstream>

namespace utils {

/**
 * @brief 설정 파일 로드 및 파싱
 * @param path 설정 파일 경로
 * @return 파싱된 Config 구조체 (파일 없으면 기본값)
 * 
 * 파싱 과정:
 * 1. 파일 열기 시도 (실패시 기본값 반환)
 * 2. 한 줄씩 읽어 파싱
 * 3. 주석/빈 줄 건너뛰기
 * 4. key=value 분리 및 trim
 * 5. 알려진 키에 대해 타입 변환 및 저장
 */
Config load_config(const std::string& path) {
    Config cfg;  // 기본값으로 초기화
    
    std::ifstream file(path);
    if (!file.is_open()) {
        logger::warn("Config file {} not found, using defaults", path);
        return cfg;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // 주석 및 빈 줄 건너뛰기
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // key=value 파싱
        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // 앞뒤 공백 제거 (trim)
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // 키별 처리
            if (key == "video_mode") {
                int mode = std::stoi(value);
                if (mode >= 1 && mode <= 6) {
                    cfg.video_mode = static_cast<VideoMode>(mode);
                }
            } else if (key == "hdmi_device") {
                cfg.hdmi_device = value;
            } else if (key == "csi_device") {
                cfg.csi_device = value;
            } else if (key == "usb_device") {
                cfg.usb_device = value;
            } else if (key == "fb_device") {
                cfg.fb_device = value;
            } else if (key == "fb_width") {
                cfg.fb_width = std::stoi(value);
            } else if (key == "fb_height") {
                cfg.fb_height = std::stoi(value);
            } else if (key == "mix_width") {
                cfg.mix_width = std::stoi(value);
            } else if (key == "mix_height") {
                cfg.mix_height = std::stoi(value);
            } else if (key == "single_width") {
                cfg.single_width = std::stoi(value);
            } else if (key == "single_height") {
                cfg.single_height = std::stoi(value);
            } else if (key == "width") {
                // Legacy fallback: apply the same output size to both mode groups.
                cfg.mix_width = std::stoi(value);
                cfg.single_width = cfg.mix_width;
            } else if (key == "height") {
                // Legacy fallback: apply the same output size to both mode groups.
                cfg.mix_height = std::stoi(value);
                cfg.single_height = cfg.mix_height;
            } else if (key == "fps") {
                cfg.fps = std::stoi(value);
            } else if (key == "show_input_labels") {
                cfg.show_input_labels = (value == "1" || value == "true" || value == "yes" || value == "on");
            } else if (key == "show_wallclock_overlay") {
                cfg.show_wallclock_overlay = (value == "1" || value == "true" || value == "yes" || value == "on");
            } else if (key == "mix_bitrate") {
                cfg.mix_bitrate = std::stoi(value);
            } else if (key == "single_bitrate") {
                cfg.single_bitrate = std::stoi(value);
            } else if (key == "bitrate") {
                // Legacy fallback: apply the same bitrate to both mode groups.
                cfg.mix_bitrate = std::stoi(value);
                cfg.single_bitrate = cfg.mix_bitrate;
            } else if (key == "codec") {
                cfg.codec = value;
            } else if (key == "gop_size") {
                cfg.gop_size = std::stoi(value);
            } else if (key == "rtsp_url") {
                cfg.rtsp_url = value;
            } else if (key == "wf_recorder_path") {
                cfg.wf_recorder_path = value;
            } else if (key == "wayland_display") {
                cfg.wayland_display = value;
            } else if (key == "xdg_runtime_dir") {
                cfg.xdg_runtime_dir = value;
            } else if (key == "framebuffer_snapshot_path") {
                cfg.framebuffer_snapshot_path = value;
            } else if (key == "framebuffer_snapshot_interval_sec") {
                cfg.framebuffer_snapshot_interval_sec = std::stoi(value);
            } else if (key == "framebuffer_snapshot_quality") {
                cfg.framebuffer_snapshot_quality = std::stoi(value);
            } else if (key == "ai_model") {
                cfg.ai_model = value;
            } else if (key == "confidence_threshold") {
                cfg.confidence_threshold = std::stof(value);
            } else if (key == "log_level") {
                int lvl = std::stoi(value);
                if (lvl >= 0 && lvl <= 3) {
                    cfg.log_level = static_cast<logger::Level>(lvl);
                }
            }
            // 알 수 없는 키는 무시
        }
    }
    
    logger::info("Config loaded from {}", path);
    return cfg;
}

std::pair<int, int> resolve_output_resolution(const Config& cfg, VideoMode mode) {
    switch (mode) {
        case VideoMode::MIXING_2X2:
            return {cfg.mix_width, cfg.mix_height};
        case VideoMode::ONLY_HDMI:
        case VideoMode::ONLY_CSI:
        case VideoMode::ONLY_USB:
        case VideoMode::ONLY_FRAMEBUFFER:
        case VideoMode::PIP_FB_RIGHT_TOP:
        default:
            return {cfg.single_width, cfg.single_height};
    }
}

int resolve_output_bitrate(const Config& cfg, VideoMode mode) {
    switch (mode) {
        case VideoMode::MIXING_2X2:
            return cfg.mix_bitrate;
        case VideoMode::ONLY_HDMI:
        case VideoMode::ONLY_CSI:
        case VideoMode::ONLY_USB:
        case VideoMode::ONLY_FRAMEBUFFER:
        case VideoMode::PIP_FB_RIGHT_TOP:
        default:
            return cfg.single_bitrate;
    }
}

} // namespace utils
