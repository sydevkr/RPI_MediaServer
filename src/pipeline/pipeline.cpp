/**
 * @file pipeline.cpp
 * @brief FFmpeg 파이프라인 구현
 * 
 * @section 목적
 * V4L2 캡처부터 RTSP 스트리밍까지 전체 영상 처리 파이프라인 구현
 * 
 * @section 주요 기능
 * - 4가지 비디오 모드 (HDMI/Webcam/Side-by-Side/PIP)
 * - 하드웨어 H264 인코딩 (Raspberry Pi VideoCore VI)
 * - 실시간 필터 그래프 처리
 * - AI 탐지 결과 오버레이
 * 
 * @section 처리 흐름
 * @code
 * V4L2 ──► [demux] ──► [decode] ──► [filter] ──► [overlay] ──► [encode] ──► [RTSP]
 *         init_input()  decode        filter_graph   overlay      h264_v4l2    output
 * @endcode
 */

#include "pipeline.hpp"
#include "utils/logger.hpp"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace pipeline {

/**
 * @brief 생성자 - FFmpeg 글로벌 초기화
 */
Pipeline::Pipeline(const utils::Config& cfg) : cfg_(cfg) {
    // V4L2 및 네트워크 초기화
    avdevice_register_all();
    avformat_network_init();
    const auto [resolved_width, resolved_height] =
        utils::resolve_output_resolution(cfg_, cfg_.video_mode);
    output_width_ = resolved_width;
    output_height_ = resolved_height;
    output_bitrate_ = utils::resolve_output_bitrate(cfg_, cfg_.video_mode);
}

/**
 * @brief 소멸자 - 자동 정리
 */
Pipeline::~Pipeline() {
    shutdown();
}

/**
 * @brief 파이프라인 종료 및 인코더 플러시
 * 
 */
void Pipeline::shutdown() {
    if (initialized_ && encoder_ctx_ && output_ctx_ && out_stream_) {
        // 인코더 플러시 (남은 프레임 처리)
        avcodec_send_frame(encoder_ctx_.get(), nullptr);
        PacketPtr pkt(av_packet_alloc());
        while (avcodec_receive_packet(encoder_ctx_.get(), pkt.get()) == 0) {
            av_packet_rescale_ts(pkt.get(), encoder_ctx_->time_base, out_stream_->time_base);
            av_interleaved_write_frame(output_ctx_.get(), pkt.get());
            av_packet_unref(pkt.get());
        }
        av_write_trailer(output_ctx_.get());
    }

    filtered_frame_.reset();
    encoded_frame_.reset();
    black_frame_fb_.reset();
    black_frame_csi_.reset();
    black_frame_hdmi_.reset();
    framebuffer_snapshot_frame_.reset();
    framebuffer_snapshot_mtime_ = 0;

    encoder_ctx_.reset();
    decoder_ctx_.reset();
    filter_graph_.reset();
    output_ctx_.reset();
    input_ctx_.reset();

    buffersrc_ctx_ = nullptr;
    buffersink_ctx_ = nullptr;
    fb_src_ctx_ = nullptr;
    csi_src_ctx_ = nullptr;
    hdmi_src_ctx_ = nullptr;
    out_stream_ = nullptr;
    codecpar_ = nullptr;
    video_stream_idx_ = -1;
    initialized_ = false;
    pts_2x2_ = 0;

    logger::info("Pipeline shutdown complete");
}

/**
 * @brief 전체 파이프라인 초기화
 * 
 * 초기화 순서: 입력 → 디코더 → 필터 그래프 → 인코더 → 출력
 */
bool Pipeline::init() {
    if (!init_input()) {
        logger::error("Input initialization failed");
        return false;
    }
    if (!init_filter_graph()) {
        logger::error("Filter graph initialization failed");
        return false;
    }
    if (!init_encoder()) {
        logger::error("Encoder initialization failed");
        return false;
    }
    if (!init_output()) {
        logger::error("Output initialization failed");
        return false;
    }
    
    // 프레임 버퍼 할당
    filtered_frame_ = FramePtr(av_frame_alloc());
    encoded_frame_ = FramePtr(av_frame_alloc());
    
    initialized_ = true;
    logger::info("Pipeline initialized successfully");
    return true;
}

/**
 * @brief 비디오 입력 초기화
 * 
 * 비디오 모드에 따라 적절한 입력 소스 선택:
 * - V4L2 (HDMI, CSI, USB): /dev/videoN
 * - Wayland 화면 캡처는 main.cpp의 wf-recorder 경로에서 처리
 */
bool Pipeline::init_input() {
    std::string input_url;
    const AVInputFormat* input_fmt = nullptr;
    
    // 비디오 모드별 입력 설정
    switch (cfg_.video_mode) {
        case utils::VideoMode::MIXING_2X2:
            // 2x2 모드: USB 카메라를 주 입력으로 사용
            logger::info("MIXING_2X2 mode: using USB camera as primary input");
            input_url = cfg_.usb_device;
            input_fmt = av_find_input_format("v4l2");
            break;
        case utils::VideoMode::ONLY_HDMI:
            input_url = cfg_.hdmi_device;
            input_fmt = av_find_input_format("v4l2");
            break;
        case utils::VideoMode::ONLY_CSI:
            input_url = cfg_.csi_device;
            input_fmt = av_find_input_format("v4l2");
            break;
        case utils::VideoMode::ONLY_USB:
            input_url = cfg_.usb_device;
            input_fmt = av_find_input_format("v4l2");
            break;
        case utils::VideoMode::ONLY_FRAMEBUFFER:
            logger::error("ONLY_FRAMEBUFFER mode is handled by the Wayland capture path, not the FFmpeg pipeline");
            break;
        case utils::VideoMode::PIP_FB_RIGHT_TOP:
            logger::error("PIP_FB_RIGHT_TOP is not supported in the Wayland capture v1 path");
            break;
        default:
            logger::error("Unknown video mode");
            return false;
    }

    if (!input_fmt) {
        return false;
    }
    
    // 입력 포맷별 옵션 설정
    AVDictionary* opts = nullptr;
    
    // V4L2 옵션
    av_dict_set(&opts, "video_size",
                (std::to_string(output_width_) + "x" + std::to_string(output_height_)).c_str(), 0);
    av_dict_set(&opts, "framerate", std::to_string(cfg_.fps).c_str(), 0);
    av_dict_set(&opts, "input_format", "yuyv422", 0);
    av_dict_set(&opts, "thread_queue_size", "64", 0);
    
    // 입력 파일 열기
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, input_url.c_str(), input_fmt, &opts) < 0) {
        logger::error("Failed to open input: {}", input_url);
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    input_ctx_.reset(ctx);
    
    // 스트림 정보 조회
    if (avformat_find_stream_info(input_ctx_.get(), nullptr) < 0) {
        logger::error("Failed to find stream info");
        return false;
    }
    
    // 비디오 스트림 찾기
    for (unsigned i = 0; i < input_ctx_->nb_streams; i++) {
        if (input_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx_ = i;
            codecpar_ = input_ctx_->streams[i]->codecpar;
            break;
        }
    }
    
    if (video_stream_idx_ == -1) {
        logger::error("No video stream found");
        return false;
    }
    
    // 디코더 초기화
    const AVCodec* decoder = avcodec_find_decoder(codecpar_->codec_id);
    if (!decoder) {
        logger::error("Decoder not found");
        return false;
    }
    
    decoder_ctx_.reset(avcodec_alloc_context3(decoder));
    if (!decoder_ctx_) {
        logger::error("Failed to alloc decoder context");
        return false;
    }
    
    if (avcodec_parameters_to_context(decoder_ctx_.get(), codecpar_) < 0) {
        logger::error("Failed to copy codec params");
        return false;
    }
    
    decoder_ctx_->thread_count = 4;
    if (avcodec_open2(decoder_ctx_.get(), decoder, nullptr) < 0) {
        logger::error("Failed to open decoder");
        return false;
    }
    
    logger::info("Input initialized: {}x{} @ {}fps", 
                 codecpar_->width, codecpar_->height, cfg_.fps);
    return true;
}

/**
 * @brief 비디오 모드별 FFmpeg 필터 문자열 생성
 * 
 * - Mode 2-5: 단일 입력 scale
 * - Mode 6: overlay로 PIP 구현 (FrameBuffer + Camera)
 * - Mode 1: 2x2 Mixing (USB 활성, 나머지 검은 화면 + 텍스트)
 */
std::string Pipeline::build_filter_string() {
    std::string filter;
    
    switch (cfg_.video_mode) {
        case utils::VideoMode::ONLY_HDMI:
        case utils::VideoMode::ONLY_CSI:
        case utils::VideoMode::ONLY_USB:
        case utils::VideoMode::ONLY_FRAMEBUFFER:
            // 단일 입력: hflip(좌우반전) + scale 적용
            filter = "hflip,scale=" + std::to_string(output_width_) + ":" + std::to_string(output_height_);
            break;
        case utils::VideoMode::MIXING_2X2:
            // 2x2 Mixing: USB만 활성, 나머지 검은 화면
            // color 필터 사용 (안정적)
            {
                int cell_w = output_width_ / 2;
                int cell_h = output_height_ / 2;
                std::string fps_str = std::to_string(cfg_.fps);
                
                // USB 입력 - 실제 영상 스케일링 (좌상단)
                filter = "[0:v]hflip,scale=" + std::to_string(cell_w) + ":" + std::to_string(cell_h) + "[usb];";
                
                // FrameBuffer - 검은 화면 (우상단)
                filter += "color=c=black:s=" + std::to_string(cell_w) + "x" + std::to_string(cell_h)
                       + ":r=" + fps_str + "[fb];";
                
                // CSI - 검은 화면 (좌하단)
                filter += "color=c=black:s=" + std::to_string(cell_w) + "x" + std::to_string(cell_h)
                       + ":r=" + fps_str + "[csi];";
                
                // HDMI - 검은 화면 (우하단)
                filter += "color=c=black:s=" + std::to_string(cell_w) + "x" + std::to_string(cell_h)
                       + ":r=" + fps_str + "[hdmi];";
                
                // 2x2 xstack 레이아웃
                filter += "[usb][fb][csi][hdmi]xstack=inputs=4:layout=0_0|w0_0|0_h0|w0_h0";
            }
            break;
        case utils::VideoMode::PIP_FB_RIGHT_TOP:
            // PIP: Camera를 480x270으로 축소 → 우측 상단 오버레이
            filter = "[1:v]scale=480:270[pip];[0:v][pip]overlay=W-w-10:10:format=auto";
            break;
    }
    
    // 인코더용 YUV420P 변환 추가
    filter += ",format=yuv420p";
    
    return filter;
}

/**
 * @brief FFmpeg 필터 그래프 초기화
 * 
 * buffer src → filter chain → buffer sink
 * 
 * @note 다중 입력 모드에서는 입력 큐를 별도 관리
 */
bool Pipeline::init_filter_graph() {
    filter_graph_.reset(avfilter_graph_alloc());
    if (!filter_graph_) return false;
    
    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* buffersink = avfilter_get_by_name("buffersink");
    
    // 출력 버퍼 싱크 (공통)
    if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out", 
                                      nullptr, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create buffer sink");
        return false;
    }
    
    // 2x2 Mix 모드: filter string 파싱 방식 사용
    if (cfg_.video_mode == utils::VideoMode::MIXING_2X2) {
        return init_filter_graph_2x2();
    }
    
    // 단일 입력 모드 (기존 로직)
    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             decoder_ctx_->width, decoder_ctx_->height, decoder_ctx_->pix_fmt,
             input_ctx_->streams[video_stream_idx_]->time_base.num,
             input_ctx_->streams[video_stream_idx_]->time_base.den,
             codecpar_->sample_aspect_ratio.num, 
             codecpar_->sample_aspect_ratio.den);
    
    if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in", args, 
                                      nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create buffer source");
        return false;
    }
    
    // 필터 문자열 파싱
    std::string filter_str = build_filter_string();
    logger::info("Filter graph: {}", filter_str);
    
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    
    if (!outputs || !inputs) {
        logger::error("Failed to allocate filter inout");
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        return false;
    }
    
    // 출력 연결 (buffersink)
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    
    // 입력 연결 (buffersrc)
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    
    if (avfilter_graph_parse_ptr(filter_graph_.get(), filter_str.c_str(), 
                                  &inputs, &outputs, nullptr) < 0) {
        logger::error("Failed to parse filter graph");
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        return false;
    }
    
    if (avfilter_graph_config(filter_graph_.get(), nullptr) < 0) {
        logger::error("Failed to configure filter graph");
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        return false;
    }
    
    // 메모리 해제
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    
    return true;
}

/**
 * @brief 2x2 Mix 모드용 필터 그래프 초기화 (color 필터 방식)
 * 
 * USB + color(color=black) 3개 → xstack → 2x2 출력
 */
bool Pipeline::init_filter_graph_2x2() {

    const AVFilter* buffersrc = avfilter_get_by_name("buffer");
    const AVFilter* color = avfilter_get_by_name("color");
    const AVFilter* scale = avfilter_get_by_name("scale");
    const AVFilter* hflip = avfilter_get_by_name("hflip");
    const AVFilter* format = avfilter_get_by_name("format");
    const AVFilter* xstack = avfilter_get_by_name("xstack");

    int cell_w = output_width_ / 2;
    int cell_h = output_height_ / 2;
    std::string fps_str = std::to_string(cfg_.fps);
    
    // 1. USB 입력 버퍼 소스 생성
    // time_base를 1/fps로 설정해 color 필터(r=fps)와 PTS 단위를 통일
    // V4L2 원본 타임스탬프(시스템 부팅 기준 절대시각)를 그대로 쓰면
    // color 필터(PTS=0 시작)와 수천만 프레임 차이가 생겨 xstack이 블로킹됨
    char args[512];
    const int sar_num = (codecpar_->sample_aspect_ratio.num > 0) ? codecpar_->sample_aspect_ratio.num : 1;
    const int sar_den = (codecpar_->sample_aspect_ratio.den > 0) ? codecpar_->sample_aspect_ratio.den : 1;
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=1/%d:pixel_aspect=%d/%d",
             decoder_ctx_->width, decoder_ctx_->height, decoder_ctx_->pix_fmt,
             cfg_.fps,
             sar_num, sar_den);
    
    if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "usb_in", args, 
                                      nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create USB buffer source");
        return false;
    }
    
    // 2. USB 필터 체인: hflip → scale
    AVFilterContext* hflip_ctx = nullptr;
    AVFilterContext* scale_usb_ctx = nullptr;
    
    if (avfilter_graph_create_filter(&hflip_ctx, hflip, "hflip", nullptr, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create hflip filter");
        return false;
    }
    
    char scale_args[64];
    snprintf(scale_args, sizeof(scale_args), "%d:%d", cell_w, cell_h);
    if (avfilter_graph_create_filter(&scale_usb_ctx, scale, "scale_usb", scale_args, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create scale filter for USB");
        return false;
    }
    
    // 연결: usb_in → hflip → scale_usb
    if (avfilter_link(buffersrc_ctx_, 0, hflip_ctx, 0) < 0) {
        logger::error("Failed to link usb_src to hflip");
        return false;
    }
    if (avfilter_link(hflip_ctx, 0, scale_usb_ctx, 0) < 0) {
        logger::error("Failed to link hflip to scale_usb");
        return false;
    }
    
    // 3. color 필터 3개 생성 (검은 화면)
    char color_args[256];
    snprintf(color_args, sizeof(color_args), "c=black:s=%dx%d:r=%d", cell_w, cell_h, cfg_.fps);
    
    AVFilterContext* fb_color_ctx = nullptr;
    AVFilterContext* csi_color_ctx = nullptr;
    AVFilterContext* hdmi_color_ctx = nullptr;
    
    if (avfilter_graph_create_filter(&fb_color_ctx, color, "fb_color", color_args, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create fb color source");
        return false;
    }
    if (avfilter_graph_create_filter(&csi_color_ctx, color, "csi_color", color_args, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create csi color source");
        return false;
    }
    if (avfilter_graph_create_filter(&hdmi_color_ctx, color, "hdmi_color", color_args, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create hdmi color source");
        return false;
    }
    
    // 4. xstack 필터 (4개 입력)
    char xstack_args[256];
    snprintf(xstack_args, sizeof(xstack_args), "inputs=4:layout=0_0|w0_0|0_h0|w0_h0");
    
    AVFilterContext* xstack_ctx = nullptr;
    if (avfilter_graph_create_filter(&xstack_ctx, xstack, "xstack", xstack_args, nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create xstack filter");
        return false;
    }
    
    // 5. format 필터 (YUV420P)
    AVFilterContext* format_ctx = nullptr;
    if (avfilter_graph_create_filter(&format_ctx, format, "format", "yuv420p", nullptr, filter_graph_.get()) < 0) {
        logger::error("Failed to create format filter");
        return false;
    }
    
    // 6. 연결: xstack → format → buffersink
    if (avfilter_link(xstack_ctx, 0, format_ctx, 0) < 0) {
        logger::error("Failed to link xstack to format");
        return false;
    }
    if (avfilter_link(format_ctx, 0, buffersink_ctx_, 0) < 0) {
        logger::error("Failed to link format to buffersink");
        return false;
    }
    
    // 7. 연결: scale_usb → xstack[0]
    if (avfilter_link(scale_usb_ctx, 0, xstack_ctx, 0) < 0) {
        logger::error("Failed to link scale_usb to xstack[0]");
        return false;
    }
    
    // 8. 연결: fb_color → xstack[1]
    if (avfilter_link(fb_color_ctx, 0, xstack_ctx, 1) < 0) {
        logger::error("Failed to link fb_color to xstack[1]");
        return false;
    }
    
    // 9. 연결: csi_color → xstack[2]
    if (avfilter_link(csi_color_ctx, 0, xstack_ctx, 2) < 0) {
        logger::error("Failed to link csi_color to xstack[2]");
        return false;
    }
    
    // 10. 연결: hdmi_color → xstack[3]
    if (avfilter_link(hdmi_color_ctx, 0, xstack_ctx, 3) < 0) {
        logger::error("Failed to link hdmi_color to xstack[3]");
        return false;
    }
    
    // 11. 그래프 설정
    if (avfilter_graph_config(filter_graph_.get(), nullptr) < 0) {
        logger::error("Failed to configure 2x2 filter graph");
        return false;
    }
    
    logger::info("2x2 Mix filter graph initialized (color filter): {}x{} per cell", cell_w, cell_h);
    return true;
}

/**
 * @brief H264 소프트웨어 인코더 초기화 (libx264)
 * 
 * libx264 소프트웨어 인코딩 - low-latency 설정
 */
bool Pipeline::init_encoder() {

    const AVCodec* encoder = avcodec_find_encoder_by_name(cfg_.codec.c_str());
    if (!encoder) {
        logger::error("Encoder {} not found", cfg_.codec);
        return false;
    }
    
    encoder_ctx_.reset(avcodec_alloc_context3(encoder));
    if (!encoder_ctx_) {
        logger::error("Failed to alloc encoder context");
        return false;
    }
    
    // 인코딩 파라미터 설정
    encoder_ctx_->width = output_width_;
    encoder_ctx_->height = output_height_;
    encoder_ctx_->time_base = {1, cfg_.fps};
    encoder_ctx_->framerate = {cfg_.fps, 1};
    encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx_->gop_size = cfg_.gop_size;
    encoder_ctx_->max_b_frames = 0;  // B-frame 없음 (지연 감소)
    
    // libx264 옵션 (low-latency)
    AVDictionary* opts = nullptr;
    if (cfg_.codec == "libx264") {
        av_dict_set(&opts, "preset", "ultrafast", 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
    } else {
        // 하드웨어 인코더 옵션 (향후 확장)
        av_dict_set(&opts, "repeat_sequence_header", "1", 0);
    }
    
    // 비트레이트 제어 (CBR)
    encoder_ctx_->bit_rate = output_bitrate_;
    encoder_ctx_->rc_min_rate = output_bitrate_;
    encoder_ctx_->rc_max_rate = output_bitrate_;
    encoder_ctx_->rc_buffer_size = output_bitrate_ / cfg_.fps;
    
    if (avcodec_open2(encoder_ctx_.get(), encoder, &opts) < 0) {
        logger::error("Failed to open encoder");
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    
    logger::info("Encoder initialized: {} {}x{} @ {}bps",
                 cfg_.codec, output_width_, output_height_, output_bitrate_);
    return true;
}

/**
 * @brief RTSP 출력 초기화 (MediaMTX)
 */
bool Pipeline::init_output() {
    AVFormatContext* ctx = nullptr;
    if (avformat_alloc_output_context2(&ctx, nullptr, "rtsp", 
                                        cfg_.rtsp_url.c_str()) < 0) {
        logger::error("Failed to alloc output context");
        return false;
    }
    output_ctx_.reset(ctx);
    
    // 출력 스트림 생성
    out_stream_ = avformat_new_stream(output_ctx_.get(), nullptr);
    if (!out_stream_) {
        logger::error("Failed to create output stream");
        return false;
    }
    
    // 인코더 파라미터 복사
    if (avcodec_parameters_from_context(out_stream_->codecpar, encoder_ctx_.get()) < 0) {
        logger::error("Failed to copy encoder params");
        return false;
    }
    
    out_stream_->time_base = encoder_ctx_->time_base;
    
    // RTSP TCP 전송 설정
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    
    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx_->pb, cfg_.rtsp_url.c_str(), AVIO_FLAG_WRITE) < 0) {
            logger::error("Failed to open output URL");
            av_dict_free(&opts);
            return false;
        }
    }
    
    if (avformat_write_header(output_ctx_.get(), &opts) < 0) {
        logger::error("Failed to write header");
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);
    
    logger::info("Output initialized: {}", cfg_.rtsp_url);
    return true;
}

/**
 * @brief 프레임 캡처 및 필터 적용
 * 
 * V4L2 → 디코딩 → 필터 그래프 → 출력
 * @return 필터링된 프레임, 실패시 nullptr
 */
FramePtr Pipeline::capture() {

    auto capture_started = std::chrono::steady_clock::now();
    
    // std::unique_ptr<AVPacket, AVPacketDeleter>
    PacketPtr pkt(av_packet_alloc());
    if (!pkt) return nullptr;

    static int dropped_decode_frames = 0;
    static int dropped_filter_frames = 0;
    
    int ret;
    
    while ((ret = av_read_frame(input_ctx_.get(), pkt.get())) >= 0) {
        if (pkt->stream_index == video_stream_idx_) {
            // 패킷을 디코더로 전송
            ret = avcodec_send_packet(decoder_ctx_.get(), pkt.get());
            av_packet_unref(pkt.get());
            
            if (ret < 0) {
                logger::error("Error sending packet to decoder");
                return nullptr;
            }

            // 디코딩된 프레임 수신 - 2x2에서는 최신 프레임만 유지
            FramePtr frame;
            while (true) {
                FramePtr candidate(av_frame_alloc());
                ret = avcodec_receive_frame(decoder_ctx_.get(), candidate.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    logger::error("Error receiving frame from decoder");
                    return nullptr;
                }

                if (cfg_.video_mode == utils::VideoMode::MIXING_2X2 && frame) {
                    dropped_decode_frames++;
                }
                frame = std::move(candidate);

                if (cfg_.video_mode != utils::VideoMode::MIXING_2X2) {
                    break;
                }
            }

            if (!frame) {
                continue;
            }
            
            // 2x2 모드: buffersrc time_base(1/fps)와 일치하도록 PTS 정규화
            // V4L2 원본 PTS(시스템 부팅 기준 절대시각)를 그대로 사용하면
            // color 필터(PTS=0 시작)와 PTS 불일치로 xstack이 장시간 블로킹됨
            if (cfg_.video_mode == utils::VideoMode::MIXING_2X2) {
                frame->pts = pts_2x2_++;
            }

            // 필터 그래프에 프레임 추가
            if (av_buffersrc_add_frame_flags(buffersrc_ctx_, frame.get(),
                                              AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                logger::warn("Failed to add frame to buffer source");
                continue;
            }
            
            // 필터링된 프레임 수신 - 2x2에서는 최신 결과만 유지
            // 마지막 프레임만 읽는다. 
            FramePtr result;
            while (true) {
                FramePtr candidate(av_frame_alloc());
                ret = av_buffersink_get_frame(buffersink_ctx_, candidate.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    logger::error("Error from buffer sink");
                    return nullptr;
                }

                if (cfg_.video_mode == utils::VideoMode::MIXING_2X2 && result) {
                    dropped_filter_frames++;
                }
                result = std::move(candidate);

                if (cfg_.video_mode != utils::VideoMode::MIXING_2X2) {
                    break;
                }
            }

            if (!result) {
                continue;
            }

            if (cfg_.video_mode == utils::VideoMode::MIXING_2X2) {
                overlay_framebuffer_snapshot(result);
            }
            if (cfg_.video_mode == utils::VideoMode::MIXING_2X2 && cfg_.show_input_labels) {
                overlay_2x2_labels(result);
            }
            if (cfg_.show_wallclock_overlay) {
                overlay_wallclock(result);
            }

            static int capture_log_counter = 0;
            capture_log_counter++;
            if (capture_log_counter % 120 == 0) {
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - capture_started).count();
                logger::debug("Capture path latency: {} ms, dropped decode={}, dropped filter={}",
                              elapsed_ms, dropped_decode_frames, dropped_filter_frames);
            }

            return result;
        }
        av_packet_unref(pkt.get());
    }
    
    return nullptr;
}

/**
 * @brief 캡처한 프레임을 내부 버퍼에 설정
 * @param frame 캡처된 프레임
 */
void Pipeline::set_filtered_frame(FramePtr frame) {
    filtered_frame_ = std::move(frame);
}

/**
 * @brief 2x2 Mix 모드용 텍스트 오버레이
 * 
 * 4개 셀 모두에 간단한 텍스트 표시 (확대된 5x7 비트맵 폰트 + 2x 스케일링)
 * @param frame 2x2 합성된 프레임
 */
void Pipeline::overlay_2x2_labels(FramePtr& frame) {
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) return;
    
    int width = frame->width;
    int height = frame->height;
    int cell_w = width / 2;
    int cell_h = height / 2;
    
    // Y 플레인 접근
    uint8_t* y_plane = frame->data[0];
    int y_stride = frame->linesize[0];
    
    // 확대된 5x7 폰트 (A-Z) - 각 행은 5비트로 표현 (상위 5비트 사용)
    // 2x 스케일링 적용으로 실제 출력은 10x14 픽셀
    const uint8_t font[26][7] = {
        {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // A
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}, // B
        {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}, // C
        {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110}, // D
        {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}, // E
        {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}, // F
        {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110}, // G
        {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // H
        {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // I
        {0b00011, 0b00001, 0b00001, 0b00001, 0b00001, 0b10001, 0b01110}, // J
        {0b10001, 0b10001, 0b10010, 0b11100, 0b10010, 0b10001, 0b10001}, // K
        {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // L
        {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
        {0b10001, 0b11001, 0b10101, 0b10101, 0b10011, 0b10001, 0b10001}, // N
        {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // O
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}, // P
        {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}, // Q
        {0b11110, 0b10001, 0b10001, 0b11110, 0b10010, 0b10001, 0b10001}, // R
        {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}, // S
        {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}, // T
        {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}, // U
        {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}, // V
        {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}, // W
        {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}, // X
        {0b10001, 0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100}, // Y
        {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}, // Z
    };
    
    const int SCALE = 2; // 2x 스케일링 (굵기 증가)
    
    auto draw_char = [&](int x, int y, char c) {
        int idx = -1;
        if (c >= 'A' && c <= 'Z') idx = c - 'A';
        else if (c >= 'a' && c <= 'z') idx = c - 'a';
        else return;
        
        // 5x7 폰트를 2x 스케일링하여 그리기
        for (int row = 0; row < 7; row++) {
            uint8_t row_data = font[idx][row];
            for (int col = 0; col < 5; col++) {
                // 왼쪽에서 오른쪽으로 (MSB가 왼쪽)
                if (row_data & (0b10000 >> col)) {
                    // 2x 스케일링 적용
                    for (int sy = 0; sy < SCALE; sy++) {
                        for (int sx = 0; sx < SCALE; sx++) {
                            int px = x + col * SCALE + sx;
                            int py = y + row * SCALE + sy;
                            if (px >= 0 && px < width && py >= 0 && py < height) {
                                y_plane[py * y_stride + px] = 255; // 흰색
                            }
                        }
                    }
                }
            }
        }
    };
    
    auto draw_string = [&](int cx, int cy, const char* str) {
        int len = strlen(str);
        int char_width = 5 * SCALE + 2; // 5픽셀 x 스케일 + 간격
        int char_height = 7 * SCALE;
        int total_width = len * char_width - 2; // 마지막 간격 제외
        int start_x = cx - total_width / 2;
        int start_y = cy - char_height / 2;
        
        for (int i = 0; i < len; i++) {
            draw_char(start_x + i * char_width, start_y, str[i]);
        }
    };
    
    // 4개 셀 중앙에 텍스트 표시
    // 좌상단: USB
    draw_string(cell_w / 2, cell_h / 2, "USB");
    // 우상단: FB
    draw_string(cell_w + cell_w / 2, cell_h / 2, "FB");
    // 좌하단: CSI
    draw_string(cell_w / 2, cell_h + cell_h / 2, "CSI");
    // 우하단: HDMI
    draw_string(cell_w + cell_w / 2, cell_h + cell_h / 2, "HDMI");
}

FramePtr Pipeline::load_framebuffer_snapshot_frame(int width, int height) {
    struct stat st {};
    if (stat(cfg_.framebuffer_snapshot_path.c_str(), &st) != 0) {
        return nullptr;
    }

    if (framebuffer_snapshot_frame_ && framebuffer_snapshot_mtime_ == st.st_mtime) {
        FramePtr cached(av_frame_clone(framebuffer_snapshot_frame_.get()));
        if (cached) {
            return cached;
        }
    }

    AVFormatContext* input_ctx = nullptr;
    if (avformat_open_input(&input_ctx, cfg_.framebuffer_snapshot_path.c_str(), nullptr, nullptr) < 0) {
        logger::warn("Failed to open framebuffer snapshot: {}", cfg_.framebuffer_snapshot_path);
        return nullptr;
    }

    InputFormatCtxPtr image_input(input_ctx);
    if (avformat_find_stream_info(image_input.get(), nullptr) < 0) {
        logger::warn("Failed to find snapshot stream info");
        return nullptr;
    }

    int stream_index = -1;
    for (unsigned i = 0; i < image_input->nb_streams; ++i) {
        if (image_input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream_index = static_cast<int>(i);
            break;
        }
    }
    if (stream_index < 0) {
        return nullptr;
    }

    AVCodecParameters* codecpar = image_input->streams[stream_index]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        logger::warn("No decoder for framebuffer snapshot");
        return nullptr;
    }

    CodecCtxPtr decoder_ctx(avcodec_alloc_context3(decoder));
    if (!decoder_ctx) {
        return nullptr;
    }
    if (avcodec_parameters_to_context(decoder_ctx.get(), codecpar) < 0) {
        return nullptr;
    }
    if (avcodec_open2(decoder_ctx.get(), decoder, nullptr) < 0) {
        return nullptr;
    }

    PacketPtr packet(av_packet_alloc());
    if (!packet) {
        return nullptr;
    }

    FramePtr decoded;
    while (av_read_frame(image_input.get(), packet.get()) >= 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet.get());
            continue;
        }

        if (avcodec_send_packet(decoder_ctx.get(), packet.get()) < 0) {
            av_packet_unref(packet.get());
            break;
        }
        av_packet_unref(packet.get());

        decoded = FramePtr(av_frame_alloc());
        const int receive_result = avcodec_receive_frame(decoder_ctx.get(), decoded.get());
        if (receive_result == 0) {
            break;
        }
        decoded.reset();
        if (receive_result != AVERROR(EAGAIN)) {
            break;
        }
    }

    if (!decoded) {
        return nullptr;
    }

    FramePtr scaled(av_frame_alloc());
    if (!scaled) {
        return nullptr;
    }
    scaled->format = AV_PIX_FMT_YUV420P;
    scaled->width = width;
    scaled->height = height;
    if (av_frame_get_buffer(scaled.get(), 32) < 0) {
        return nullptr;
    }

    SwsContext* sws_ctx = sws_getContext(
        decoded->width, decoded->height, static_cast<AVPixelFormat>(decoded->format),
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        return nullptr;
    }

    sws_scale(sws_ctx, decoded->data, decoded->linesize, 0, decoded->height, scaled->data, scaled->linesize);
    sws_freeContext(sws_ctx);

    framebuffer_snapshot_mtime_ = st.st_mtime;
    framebuffer_snapshot_frame_ = FramePtr(av_frame_clone(scaled.get()));
    return scaled;
}

void Pipeline::overlay_framebuffer_snapshot(FramePtr& frame) {
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) {
        return;
    }

    const int cell_w = frame->width / 2;
    const int cell_h = frame->height / 2;
    FramePtr snapshot = load_framebuffer_snapshot_frame(cell_w, cell_h);
    if (!snapshot) {
        return;
    }

    const int luma_x = cell_w;
    const int luma_y = 0;
    for (int y = 0; y < cell_h; ++y) {
        std::memcpy(
            frame->data[0] + (luma_y + y) * frame->linesize[0] + luma_x,
            snapshot->data[0] + y * snapshot->linesize[0],
            cell_w);
    }

    const int chroma_w = cell_w / 2;
    const int chroma_h = cell_h / 2;
    const int chroma_x = luma_x / 2;
    const int chroma_y = luma_y / 2;
    for (int y = 0; y < chroma_h; ++y) {
        std::memcpy(
            frame->data[1] + (chroma_y + y) * frame->linesize[1] + chroma_x,
            snapshot->data[1] + y * snapshot->linesize[1],
            chroma_w);
        std::memcpy(
            frame->data[2] + (chroma_y + y) * frame->linesize[2] + chroma_x,
            snapshot->data[2] + y * snapshot->linesize[2],
            chroma_w);
    }
}

void Pipeline::overlay_wallclock(FramePtr& frame) {
    if (!frame || frame->format != AV_PIX_FMT_YUV420P) return;

    int width = frame->width;
    int height = frame->height;
    uint8_t* y_plane = frame->data[0];
    int y_stride = frame->linesize[0];

    const bool is_mix_mode = (cfg_.video_mode == utils::VideoMode::MIXING_2X2);
    const bool is_usb_single_mode = (cfg_.video_mode == utils::VideoMode::ONLY_USB);
    const int scale = is_mix_mode ? 1 : (is_usb_single_mode ? 4 : 3);
    const int char_width = 5 * scale + 2;
    const int char_height = 7 * scale;
    const int margin_x = is_mix_mode ? 8 : 16;
    const int margin_y = is_mix_mode ? 8 : 12;

    auto glyph_for_char = [](char c) -> const uint8_t* {
        static const uint8_t digits[10][7] = {
            {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}, // 0
            {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
            {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111}, // 2
            {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110}, // 3
            {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}, // 4
            {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110}, // 5
            {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}, // 6
            {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}, // 7
            {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}, // 8
            {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b11100}, // 9
        };
        static const uint8_t colon[7] = {
            0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100, 0b00000
        };
        static const uint8_t letter_x[7] = {
            0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b00000
        };
        static const uint8_t space[7] = {
            0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000
        };

        if (c >= '0' && c <= '9') return digits[c - '0'];
        if (c == ':') return colon;
        if (c == 'x' || c == 'X') return letter_x;
        return space;
    };

    auto draw_char = [&](int x, int y, char c) {
        const uint8_t* glyph = glyph_for_char(c);
        for (int row = 0; row < 7; row++) {
            uint8_t row_data = glyph[row];
            for (int col = 0; col < 5; col++) {
                if (row_data & (0b10000 >> col)) {
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int px = x + col * scale + sx;
                            int py = y + row * scale + sy;
                            if (px >= 0 && px < width && py >= 0 && py < height) {
                                y_plane[py * y_stride + px] = 255;
                            }
                        }
                    }
                }
            }
        }
    };

    auto draw_box = [&](int x, int y, int w, int h) {
        for (int py = y; py < y + h && py < height; py++) {
            if (py < 0) continue;
            for (int px = x; px < x + w && px < width; px++) {
                if (px < 0) continue;
                y_plane[py * y_stride + px] = 24;
            }
        }
    };

    auto now = std::time(nullptr);
    std::tm local_tm{};
    localtime_r(&now, &local_tm);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%H:%M:%S")
        << ' '
        << frame->width
        << 'x'
        << frame->height;
    std::string label = oss.str();

    const int box_padding_x = is_mix_mode ? 4 : 8;
    const int box_padding_y = is_mix_mode ? 4 : 8;
    int text_width = static_cast<int>(label.size()) * char_width - 2;
    int box_width = std::min(width - margin_x, text_width + box_padding_x * 2);
    int box_height = char_height + box_padding_y * 2;
    int box_x = margin_x;
    int box_y = margin_y;
    if (!is_mix_mode && !is_usb_single_mode) {
        box_x = std::max(0, width - margin_x - box_width);
    }
    draw_box(box_x, box_y, box_width, box_height);

    int text_x = box_x + box_padding_x;
    int text_y = box_y + box_padding_y;
    for (size_t i = 0; i < label.size(); i++) {
        draw_char(text_x + static_cast<int>(i) * char_width, text_y, label[i]);
    }
}

/**
 * @brief AI 탐지 결과를 프레임에 오버레이 (바운딩 박스)
 * 
 * YUV420P Y 플레인에 흰색 사각형 그리기
 * @param detections 탐지된 객체 목록 (정규화된 좌표 0-1)
 */
void Pipeline::overlay_detections(const std::vector<ai::Detection>& detections) {
    if (!filtered_frame_ || filtered_frame_->format != AV_PIX_FMT_YUV420P) return;
    
    // Y 플레인 접근 (흑백 박스로 성능 최적화)
    uint8_t* y_plane = filtered_frame_->data[0];
    int y_stride = filtered_frame_->linesize[0];
    int width = filtered_frame_->width;
    int height = filtered_frame_->height;
    
    for (const auto& det : detections) {
        if (det.confidence < cfg_.confidence_threshold) continue;
        
        // 정규화 좌표 → 픽셀 좌표
        int x1 = static_cast<int>(det.x1 * width);
        int y1 = static_cast<int>(det.y1 * height);
        int x2 = static_cast<int>(det.x2 * width);
        int y2 = static_cast<int>(det.y2 * height);
        
        // 경계 클램프
        x1 = std::max(0, std::min(x1, width-1));
        y1 = std::max(0, std::min(y1, height-1));
        x2 = std::max(0, std::min(x2, width-1));
        y2 = std::max(0, std::min(y2, height-1));
        
        // 흰색 테두리 (Y=255)
        uint8_t color = 255;
        
        // 상하 테두리
        for (int x = x1; x <= x2; x++) {
            y_plane[y1 * y_stride + x] = color;
            y_plane[y2 * y_stride + x] = color;
        }
        // 좌우 테두리
        for (int y = y1; y <= y2; y++) {
            y_plane[y * y_stride + x1] = color;
            y_plane[y * y_stride + x2] = color;
        }
    }
}

/**
 * @brief 프레임 인코딩 및 RTSP 전송
 * 
 * YUV420P → H264 → RTSP 패킷 전송
 * @return true 성공, false 실패
 */
bool Pipeline::encode_and_send() {
    if (!filtered_frame_) return false;
    auto encode_started = std::chrono::steady_clock::now();
    
    // PTS 설정 (30fps 기준)
    static int64_t pts = 0;
    filtered_frame_->pts = pts++;
    
    // 프레임을 인코더로 전송
    int ret = avcodec_send_frame(encoder_ctx_.get(), filtered_frame_.get());
    if (ret < 0) {
        logger::error("Error sending frame to encoder");
        return false;
    }
    
    // 인코딩된 패킷 수신 및 전송
    PacketPtr pkt(av_packet_alloc());
    while ((ret = avcodec_receive_packet(encoder_ctx_.get(), pkt.get())) >= 0) {
        // 타임스탬프 변환
        av_packet_rescale_ts(pkt.get(), encoder_ctx_->time_base, out_stream_->time_base);
        pkt->stream_index = out_stream_->index;
        
        // RTSP로 전송
        ret = av_interleaved_write_frame(output_ctx_.get(), pkt.get());
        av_packet_unref(pkt.get());
        
        if (ret < 0) {
            logger::error("Error writing packet");
            return false;
        }
    }
    
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        logger::error("Error receiving packet from encoder");
        return false;
    }

    static int encode_log_counter = 0;
    encode_log_counter++;
    if (encode_log_counter % 120 == 0) {
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - encode_started).count();
        logger::debug("Encode/send latency: {} ms", elapsed_ms);
    }
    
    return true;
}

} // namespace pipeline
