/**
 * @file main.cpp
 * @brief RPI_MediaServer 메인 진입점 (HTTP API 지원)
 * 
 * @section 목적
 * - 시스템 초기화 및 메인 이벤트 루프 실행
 * - FFmpeg 파이프라인과 Hailo-8L AI 탐지기의 조정
 * - HTTP API를 통한 런타임 모드 변경 지원
 * - 안전한 종료 처리 (시그널 핸들링)
 * 
 * @section 아키텍처 역할
 * @code
 * [main.cpp] ──► [Pipeline] ──► [V4L2 캡처/인코딩/RTSP]
 *      │
 *      ├──► [AI Detector] ──► [Hailo-8L 추론]
 *      │
 *      └──► [HTTP Server] ──► [API 요청 처리]
 * @endcode
 */

#include <iostream>
#include <csignal>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include "pipeline/pipeline.hpp"
#include "ai/detector.hpp"
#include "utils/config.hpp"
#include "utils/logger.hpp"
#include "utils/screen_capture.hpp"
#include "httplib.h"

// 전역 상태
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_restart_pipeline{false};
static std::atomic<utils::VideoMode> g_current_mode{utils::VideoMode::MIXING_2X2};
static std::mutex g_config_mutex;
static utils::Config g_config;

namespace {

bool uses_wayland_capture(utils::VideoMode mode) {
    return mode == utils::VideoMode::ONLY_FRAMEBUFFER;
}

bool unsupported_on_wayland_v1(utils::VideoMode mode) {
    return mode == utils::VideoMode::PIP_FB_RIGHT_TOP;
}

}

/**
 * @brief 시그널 핸들러 (SIGINT, SIGTERM 처리)
 * @param sig 수신된 시그널 번호
 */
void signal_handler(int sig) {
    logger::info("Received signal {}, shutting down...", sig);
    g_running = false;
}

/**
 * @brief HTTP API 서버 설정 및 실행
 * @param port HTTP 서버 포트
 */
void run_http_server(int port = 8081) {
    httplib::Server server;
    
    // 정적 Web UI 제공
    server.set_base_dir("./web");
    
    // 현재 상태 조회 API
    server.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(g_config_mutex);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        
        std::string mode_str;
        switch (g_current_mode.load()) {
            case utils::VideoMode::MIXING_2X2: mode_str = "2x2"; break;
            case utils::VideoMode::ONLY_USB: mode_str = "usb"; break;
            case utils::VideoMode::ONLY_HDMI: mode_str = "hdmi"; break;
            case utils::VideoMode::ONLY_CSI: mode_str = "csi"; break;
            case utils::VideoMode::ONLY_FRAMEBUFFER: mode_str = "screen"; break;
            case utils::VideoMode::PIP_FB_RIGHT_TOP: mode_str = "pip"; break;
            default: mode_str = "unknown";
        }
        
        res.set_content(
            "{\n"
            "  \"status\": \"running\",\n"
            "  \"mode\": \"" + mode_str + "\",\n"
            "  \"mode_id\": " + std::to_string(static_cast<int>(g_current_mode.load())) + ",\n"
            "  \"resolution\": {\n"
            "    \"width\": " + std::to_string(g_config.width) + ",\n"
            "    \"height\": " + std::to_string(g_config.height) + ",\n"
            "    \"fps\": " + std::to_string(g_config.fps) + "\n"
            "  }\n"
            "}", "application/json"
        );
    });
    
    // 모드 변경 API
    server.Post("/api/mode", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Content-Type", "application/json");
        
        std::string mode = req.get_param_value("mode");
        if (mode.empty()) {
            // JSON body에서 파싱 시도
            auto it = req.headers.find("Content-Type");
            if (it != req.headers.end() && it->second.find("application/json") != std::string::npos) {
                // 간단한 JSON 파싱
                size_t mode_pos = req.body.find("\"mode\"");
                if (mode_pos != std::string::npos) {
                    size_t colon_pos = req.body.find(":", mode_pos);
                    size_t quote_pos = req.body.find("\"", colon_pos);
                    size_t end_quote = req.body.find("\"", quote_pos + 1);
                    if (quote_pos != std::string::npos && end_quote != std::string::npos) {
                        mode = req.body.substr(quote_pos + 1, end_quote - quote_pos - 1);
                    }
                }
            }
        }
        
        utils::VideoMode new_mode;
        if (mode == "2x2") {
            new_mode = utils::VideoMode::MIXING_2X2;
        } else if (mode == "usb") {
            new_mode = utils::VideoMode::ONLY_USB;
        } else if (mode == "hdmi") {
            new_mode = utils::VideoMode::ONLY_HDMI;
        } else if (mode == "csi") {
            new_mode = utils::VideoMode::ONLY_CSI;
        } else if (mode == "fb" || mode == "screen") {
            new_mode = utils::VideoMode::ONLY_FRAMEBUFFER;
        } else if (mode == "pip") {
            new_mode = utils::VideoMode::PIP_FB_RIGHT_TOP;
        } else {
            res.status = 400;
            res.set_content("{\"error\": \"Invalid mode. Use: 2x2, usb, hdmi, csi, screen, fb, pip\"}", "application/json");
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(g_config_mutex);
            if (g_current_mode.load() != new_mode) {
                auto old_mode = g_current_mode.load();
                g_current_mode.store(new_mode);
                g_config.video_mode = new_mode;
                g_restart_pipeline.store(true);
                logger::info("Mode change requested: {} -> {}", 
                    static_cast<int>(old_mode), 
                    static_cast<int>(new_mode));
            }
        }
        
        res.set_content("{\"success\": true, \"mode\": \"" + mode + "\", \"message\": \"Mode changed, capture pipeline restarting...\"}", "application/json");
    });
    
    // OPTIONS 처리 (CORS preflight)
    server.Options("/api/.*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 200;
    });
    
    logger::info("HTTP API server starting on port {}", port);
    server.listen("0.0.0.0", port);
}

/**
 * @brief 메인 함수
 * @param argc 명령행 인자 개수
 * @param argv 명령행 인자 배열 (config 파일 경로)
 * @return 0 성공, 1 실패
 */
int main(int argc, char** argv) {
    // 시그널 핸들러 등록 (Ctrl+C, kill)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // 1. 설정 파일 로드 (기본값: config.ini)
        {
            std::lock_guard<std::mutex> lock(g_config_mutex);
            g_config = utils::load_config(argc > 1 ? argv[1] : "config.ini");
            g_current_mode.store(g_config.video_mode);
        }
        logger::init(g_config.log_level);
        
        logger::info("RPI_MediaServer starting...");
        logger::info("Video mode: {}", static_cast<int>(g_current_mode.load()));
        
        // 2. AI 탐지기 초기화 (비동기 스레드)
        auto detector = std::make_unique<ai::Detector>(g_config.ai_model);
        detector->start();
        
        // 3. HTTP API 서버 스레드 시작
        std::thread http_thread(run_http_server, 8081);
        
        // 4. 메인 처리 루프 (파이프라인 재시작 지원)
        logger::info("Entering main loop...");
        
        while (g_running) {
            utils::Config current_config;
            utils::VideoMode current_mode;
            {
                std::lock_guard<std::mutex> lock(g_config_mutex);
                current_config = g_config;
                current_mode = g_current_mode.load();
            }

            if (unsupported_on_wayland_v1(current_mode)) {
                logger::error("Mode {} is not supported in the Wayland capture v1 path", static_cast<int>(current_mode));
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }

            if (uses_wayland_capture(current_mode)) {
                utils::ScreenCapture capture(current_config);
                if (!capture.start()) {
                    logger::error("Wayland screen capture initialization failed");
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    continue;
                }

                logger::info("Wayland screen capture initialized, mode: {}", static_cast<int>(current_mode));
                g_restart_pipeline.store(false);

                while (g_running && !g_restart_pipeline.load()) {
                    if (!capture.is_running()) {
                        logger::error("Screen capture stopped unexpectedly, restarting...");
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }

                logger::info("Stopping Wayland screen capture...");
                capture.stop();

                if (g_restart_pipeline.load()) {
                    logger::info("Restarting capture path with new mode: {}",
                        static_cast<int>(g_current_mode.load()));
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                continue;
            }

            // 4.1 현재 설정으로 Pipeline 생성 및 초기화
            std::unique_ptr<pipeline::Pipeline> pipe;
            {
                pipe = std::make_unique<pipeline::Pipeline>(current_config);
            }
            
            if (!pipe->init()) {
                logger::error("Pipeline initialization failed");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            
            logger::info("Pipeline initialized, mode: {}", static_cast<int>(g_current_mode.load()));
            g_restart_pipeline.store(false);
            
            // 4.2 프레임 처리 루프
            while (g_running && !g_restart_pipeline.load()) {
                // 프레임 캡처
                auto frame = pipe->capture();
                if (!frame) continue;
                
                // AI 입력 큐에 프레임 전달
                detector->enqueue(frame.get(), /*drop_if_full=*/true);
                
                // 캡처한 프레임을 파이프라인 내부 버퍼에 설정
                pipe->set_filtered_frame(std::move(frame));
                
                // AI 결과 가져와서 오버레이
                auto detections = detector->get_results();
                pipe->overlay_detections(detections);
                
                // 인코딩 및 RTSP 스트리밍
                if (!pipe->encode_and_send()) {
                    logger::error("Encoding failed, restarting pipeline...");
                    break;
                }
            }
            
            // 4.3 Pipeline 정리 (재시작 또는 종료 전)
            logger::info("Shutting down pipeline...");
            pipe->shutdown();
            
            if (g_restart_pipeline.load()) {
                logger::info("Restarting pipeline with new mode: {}", 
                    static_cast<int>(g_current_mode.load()));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        
        // 5. 정리
        logger::info("Shutting down...");
        detector->stop();
        
        // HTTP 서버 종료 (강제 종료)
        // Note: httplib의 graceful shutdown은 별도 처리 필요
        
        logger::info("Shutdown complete");
        return 0;
        
    } catch (const std::exception& e) {
        logger::error("Fatal error: {}", e.what());
        return 1;
    }
}
