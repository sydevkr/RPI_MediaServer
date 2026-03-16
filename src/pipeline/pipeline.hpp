/**
 * @file pipeline.hpp
 * @brief FFmpeg 기반 실시간 영상 처리 파이프라인
 * 
 * @section 목적
 * - V4L2 비디오 캡처 (HDMI/Webcam)
 * - 4가지 비디오 모드 지원 (단일/분할/PIP)
 * - 하드웨어 H264 인코딩 (h264_v4l2m2m)
 * - RTSP 스트리밍 출력 (MediaMTX)
 * 
 * @section 아키텍처 역할
 * @code
 * [V4L2 입력] ──► [FFmpeg Demux] ──► [Decoder] ──► [Filter Graph]
 *                                                    │
 *                              [AI 오버레이] ◄──────┘
 *                                    │
 *                                    ▼
 *                              [Encoder: h264_v4l2m2m] ──► [RTSP Output]
 * @endcode
 * 
 * @section 주요 구성요소
 * - RAII 래퍼: AVFrame, AVCodecContext 등 FFmpeg 객체 자동 관리
 * - Filter Graph: 비디오 모드별 필터 체인 (scale, hstack, overlay)
 * - Hardware Encoding: Raspberry Pi 4 VideoCore VI 활용
 */

#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>
#include "utils/config.hpp"
#include "ai/types.hpp"

namespace pipeline {

/**
 * @brief FFmpeg AVFrame RAII 래퍼
 */
struct AVFrameDeleter {
    void operator()(AVFrame* f) { av_frame_free(&f); }
};

/**
 * @brief FFmpeg AVCodecContext RAII 래퍼
 */
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* c) { avcodec_free_context(&c); }
};

/**
 * @brief FFmpeg 입력 AVFormatContext RAII 래퍼
 */
struct AVInputFormatContextDeleter {
    void operator()(AVFormatContext* f) {
        if (!f) return;
        avformat_close_input(&f);
    }
};

/**
 * @brief FFmpeg 출력 AVFormatContext RAII 래퍼
 */
struct AVOutputFormatContextDeleter {
    void operator()(AVFormatContext* f) {
        if (!f) return;
        if (!(f->oformat->flags & AVFMT_NOFILE) && f->pb) {
            avio_closep(&f->pb);
        }
        avformat_free_context(f);
    }
};

/**
 * @brief FFmpeg AVFilterGraph RAII 래퍼
 */
struct AVFilterGraphDeleter {
    void operator()(AVFilterGraph* g) { avfilter_graph_free(&g); }
};

/**
 * @brief FFmpeg AVPacket RAII 래퍼
 */
struct AVPacketDeleter {
    void operator()(AVPacket* p) { av_packet_free(&p); }
};

using FramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using InputFormatCtxPtr = std::unique_ptr<AVFormatContext, AVInputFormatContextDeleter>;
using OutputFormatCtxPtr = std::unique_ptr<AVFormatContext, AVOutputFormatContextDeleter>;
using FilterGraphPtr = std::unique_ptr<AVFilterGraph, AVFilterGraphDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;

/**
 * @class Pipeline
 * @brief FFmpeg 기반 영상 처리 파이프라인
 * 
 * V4L2 입력 → 디코딩 → 필터 그래프 → 인코딩 → RTSP 출력의 전체 흐름 관리
 */
class Pipeline {
public:
    /**
     * @brief 생성자
     * @param cfg 설정 구조체 (비디오 모드, 해상도, bitrate 등)
     */
    explicit Pipeline(const utils::Config& cfg);
    
    /**
     * @brief 소멸자 (자동 리소스 정리)
     */
    ~Pipeline();
    
    /**
     * @brief 전체 파이프라인 초기화
     * @return true 성공, false 실패
     * 
     * 초기화 순서: 입력 → 디코더 → 필터 그래프 → 인코더 → 출력
     */
    bool init();
    
    /**
     * @brief 파이프라인 종료 및 리소스 해제
     */
    void shutdown();
    
    /**
     * @brief 프레임 캡처 및 필터 적용
     * @return 필터링된 프레임 (nullptr if 실패)
     * 
     * V4L2 → 디코딩 → 필터 그래프 처리까지 수행
     */
    FramePtr capture();
    
    /**
     * @brief AI 탐지 결과를 프레임에 오버레이
     * @param detections 탐지된 객체 목록
     * 
     * YUV420P 프레임의 Y 플레인에 흰색 바운딩 박스 그리기
     */
    void overlay_detections(const std::vector<ai::Detection>& detections);
    
    /**
     * @brief 캡처한 프레임을 내부 버퍼에 설정
     * @param frame 캡처된 프레임
     */
    void set_filtered_frame(FramePtr frame);
    
    /**
     * @brief 프레임 인코딩 및 RTSP 전송
     * @return true 성공, false 실패
     */
    bool encode_and_send();
    
private:
    /**
     * @brief V4L2 입력 초기화
     * @return true 성공
     */
    bool init_input();
    
    /**
     * @brief FFmpeg 필터 그래프 초기화
     * @return true 성공
     */
    bool init_filter_graph();
    
    /**
     * @brief H264 하드웨어 인코더 초기화
     * @return true 성공
     */
    bool init_encoder();
    
    /**
     * @brief RTSP 출력 초기화
     * @return true 성공
     */
    bool init_output();
    
    /**
     * @brief 비디오 모드별 FFmpeg 필터 문자열 생성
     * @return 필터 문자열
     */
    std::string build_filter_string();
    
    /**
     * @brief 2x2 Mix 모드용 필터 그래프 초기화
     * @return true 성공
     * 
     * 1개 USB 입력 + 3개 검은 프레임 → xstack → 2x2 출력
     */
    bool init_filter_graph_2x2();
    
    /**
     * @brief 검은 프레임 생성 (재사용용)
     * @param width 프레임 너비
     * @param height 프레임 높이
     * @return 검은 프레임
     */
    FramePtr create_black_frame(int width, int height);
    
    /**
     * @brief 2x2 Mix 모드용 텍스트 오버레이
     * @param frame 2x2 합성된 프레임
     * 
     * 3개 비활성화 영역에 텍스트 표시
     */
    void overlay_2x2_labels(FramePtr& frame);

    /**
     * @brief 최신 FrameBuffer 스냅샷을 2x2 FB 슬롯에 합성
     * @param frame 2x2 합성된 프레임
     */
    void overlay_framebuffer_snapshot(FramePtr& frame);

    /**
     * @brief 최신 FrameBuffer 스냅샷을 셀 크기로 디코드/캐시
     * @param width 목표 셀 너비
     * @param height 목표 셀 높이
     * @return 성공 시 YUV420P 프레임, 실패 시 nullptr
     */
    FramePtr load_framebuffer_snapshot_frame(int width, int height);
    
    /**
     * @brief 프레임에 현재 시각 오버레이
     * @param frame 출력 프레임
     *
     * end-to-end 지연 측정을 위해 HH:MM:SS 형식 시각을 좌상단에 표시
     */
    void overlay_wallclock(FramePtr& frame);
    
    const utils::Config& cfg_;
    int output_width_ = 0;          ///< 현재 모드에 적용된 출력 너비
    int output_height_ = 0;         ///< 현재 모드에 적용된 출력 높이
    int output_bitrate_ = 0;        ///< 현재 모드에 적용된 인코더 비트레이트
    
    // 입력
    InputFormatCtxPtr input_ctx_;      ///< V4L2 입력 포맷 컨텍스트
    int video_stream_idx_ = -1;        ///< 비디오 스트림 인덱스
    AVCodecParameters* codecpar_ = nullptr;  ///< 코덱 파라미터
    
    // 디코더
    CodecCtxPtr decoder_ctx_;          ///< 디코더 컨텍스트
    
    // 필터 그래프
    FilterGraphPtr filter_graph_;      ///< 필터 그래프
    AVFilterContext* buffersrc_ctx_ = nullptr;   ///< 입력 버퍼 소스 (USB)
    AVFilterContext* buffersink_ctx_ = nullptr;  ///< 출력 버퍼 싱크
    
    // 2x2 Mix 모드용 추가 버퍼 소스 (검은 프레임 전송용)
    AVFilterContext* fb_src_ctx_ = nullptr;      ///< FrameBuffer 버퍼 소스
    AVFilterContext* csi_src_ctx_ = nullptr;     ///< CSI 버퍼 소스
    AVFilterContext* hdmi_src_ctx_ = nullptr;    ///< HDMI 버퍼 소스
    
    // 인코더
    CodecCtxPtr encoder_ctx_;          ///< H264 인코더 컨텍스트
    
    // 출력
    OutputFormatCtxPtr output_ctx_;    ///< RTSP 출력 컨텍스트
    AVStream* out_stream_ = nullptr;   ///< 출력 스트림
    
    // 프레임 버퍼
    FramePtr filtered_frame_;          ///< 필터링된 프레임
    FramePtr encoded_frame_;           ///< 인코딩용 프레임
    
    // 2x2 Mix 모드용 검은 프레임 (재사용)
    FramePtr black_frame_fb_;          ///< FrameBuffer 자리 검은 프레임
    FramePtr black_frame_csi_;         ///< CSI 자리 검은 프레임
    FramePtr black_frame_hdmi_;        ///< HDMI 자리 검은 프레임
    FramePtr framebuffer_snapshot_frame_; ///< 최신 FrameBuffer 스냅샷 캐시
    time_t framebuffer_snapshot_mtime_ = 0; ///< 캐시된 스냅샷 mtime
    
    bool initialized_ = false;         ///< 초기화 완료 플래그
};

} // namespace pipeline
