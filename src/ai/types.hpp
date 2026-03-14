/**
 * @file types.hpp
 * @brief AI 모듈 공용 타입 정의
 * 
 * 순환 참조 방지를 위해 Detection 구조체를 별도 분리
 */

#pragma once

#include <string>
#include <vector>

namespace ai {

/**
 * @struct Detection
 * @brief 객체 탐지 결과 구조체
 * 
 * 좌표는 0.0~1.0 범위의 정규화된 값
 */
struct Detection {
    float x1, y1, x2, y2;  ///< 정규화된 바운딩 박스 좌표 (0-1)
    float confidence;       ///< 신뢰도 점수 (0-1)
    int class_id;          ///< 클래스 ID (0=person)
    std::string label;     ///< 클래스 레이블
};

} // namespace ai