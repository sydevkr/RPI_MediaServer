/**
 * @file logger.hpp
 * @brief 스레드 안전 로깅 시스템
 * 
 * @section 목적
 * - 타임스탬프와 로그 레벨을 포함한 콘솔 출력
 * - 멀티스레드 환경에서 안전한 로그 출력 (mutex 보호)
 * - 템플릿 기반 간단한 포맷팅 지원
 * 
 * @section 사용 예시
 * @code
 * logger::init(logger::Level::Info);
 * logger::info("Server started on port {}", 8554);
 * logger::error("Failed to open device: {}", "/dev/video0");
 * @endcode
 */

#pragma once

#include <string>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>

namespace logger {

/**
 * @brief 로그 레벨 열거형
 */
enum class Level { Debug, Info, Warning, Error };

/**
 * @brief 로거 초기화
 * @param level 출력할 최소 로그 레벨
 */
void init(Level level = Level::Info);

/**
 * @brief 로그 레벨 변경
 * @param level 새로운 로그 레벨
 */
void set_level(Level level);

/**
 * @brief 타입을 문자열로 변환 (내부용)
 */
template<typename T>
std::string to_str(T&& val) {
    using Decayed = typename std::decay<T>::type;
    if constexpr (std::is_same_v<Decayed, std::string>) {
        return std::forward<T>(val);
    } else if constexpr (std::is_same_v<Decayed, const char*>) {
        return std::string(val);
    } else {
        return std::to_string(std::forward<T>(val));
    }
}

/**
 * @brief 간단한 포맷 문자열 구현 ("{}" 자리 표시자 교체)
 * @param fmt 형식 문자열
 * @param args 교체할 인자들
 * @return 포맷팅된 문자열
 */
template<typename... Args>
std::string format_impl(const std::string& fmt, Args&&... args) {
    std::string result = fmt;
    std::string replacements[] = {to_str(std::forward<Args>(args))...};
    size_t idx = 0;
    size_t pos = 0;
    
    // C++17 fold expression 대신 루프 사용
    for (const auto& rep : replacements) {
        pos = result.find("{}", pos);
        if (pos == std::string::npos) break;
        result.replace(pos, 2, rep);
        pos += rep.length();
    }
    
    return result;
}

/**
 * @brief Debug 레벨 로그 출력
 * @param fmt_str 형식 문자열
 * @param args 인자들
 */
template<typename... Args>
void debug(const std::string& fmt_str, Args&&... args) {
    log(Level::Debug, format_impl(fmt_str, std::forward<Args>(args)...));
}

/**
 * @brief Info 레벨 로그 출력
 * @param fmt_str 형식 문자열
 * @param args 인자들
 */
template<typename... Args>
void info(const std::string& fmt_str, Args&&... args) {
    log(Level::Info, format_impl(fmt_str, std::forward<Args>(args)...));
}

/**
 * @brief Warning 레벨 로그 출력
 * @param fmt_str 형식 문자열
 * @param args 인자들
 */
template<typename... Args>
void warn(const std::string& fmt_str, Args&&... args) {
    log(Level::Warning, format_impl(fmt_str, std::forward<Args>(args)...));
}

/**
 * @brief Error 레벨 로그 출력
 * @param fmt_str 형식 문자열
 * @param args 인자들
 */
template<typename... Args>
void error(const std::string& fmt_str, Args&&... args) {
    log(Level::Error, format_impl(fmt_str, std::forward<Args>(args)...));
}

/**
 * @brief 내부 로그 출력 함수
 * @param level 로그 레벨
 * @param msg 출력 메시지
 */
void log(Level level, const std::string& msg);

} // namespace logger