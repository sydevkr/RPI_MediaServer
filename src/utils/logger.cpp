/**
 * @file logger.cpp
 * @brief 스레드 안전 로깅 시스템 구현
 * 
 * @section 목적
 * logger.hpp의 구현부. 타임스탬프와 로그 레벨을 포함한
 * 스레드 안전한 콘솔 출력 기능 제공
 * 
 * @section 출력 형식
 * [HH:MM:SS][LVL] 메시지
 * 예: [14:32:01][INF] Server started
 */

#include "logger.hpp"

namespace logger {

static Level g_level = Level::Info;    ///< 전역 로그 레벨
static std::mutex g_mutex;              ///< 출력 동기화용 뮤텍스

/**
 * @brief 로거 초기화
 * @param level 최소 출력 로그 레벨
 */
void init(Level level) {
    g_level = level;
}

/**
 * @brief 로그 레벨 변경
 * @param level 새로운 로그 레벨
 */
void set_level(Level level) {
    g_level = level;
}

/**
 * @brief 로그 레벨을 문자열로 변환
 * @param level 로그 레벨
 * @return 3문자 약어 (DBG, INF, WRN, ERR)
 */
const char* level_str(Level level) {
    switch (level) {
        case Level::Debug:   return "DBG";
        case Level::Info:    return "INF";
        case Level::Warning: return "WRN";
        case Level::Error:   return "ERR";
    }
    return "UNK";
}

/**
 * @brief 로그 메시지 출력 (thread-safe)
 * @param level 로그 레벨
 * @param msg 출력할 메시지
 * 
 * 설정된 로그 레벨 이상의 메시지만 출력.
 * 타임스탬프와 로그 레벨 태그를 포함하여 stderr로 출력.
 */
void log(Level level, const std::string& msg) {
    // 로그 레벨 필터링
    if (level < g_level) return;
    
    // 스레드 안전 출력
    std::lock_guard<std::mutex> lock(g_mutex);
    
    // 현재 시간
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    // 출력: [HH:MM:SS][LVL] 메시지
    std::cerr << "[" << std::put_time(std::localtime(&time), "%H:%M:%S")
              << "][" << level_str(level) << "] " << msg << "\n";
}

} // namespace logger