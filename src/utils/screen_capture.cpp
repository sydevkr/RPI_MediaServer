#include "screen_capture.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace utils {

namespace {

constexpr const char* kGrimPath = "/usr/bin/grim";
constexpr const char* kFfmpegPath = "/usr/bin/ffmpeg";

const char* get_env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

void set_env_if_present(const std::string& key, const std::string& value) {
    if (!value.empty()) {
        setenv(key.c_str(), value.c_str(), 1);
    }
}

} // namespace

ScreenCapture::ScreenCapture(const Config& cfg) : cfg_(cfg) {}

ScreenCapture::~ScreenCapture() {
    stop();
}

bool ScreenCapture::validate_environment() const {
    const char* session_type = get_env_or_empty("XDG_SESSION_TYPE");
    const std::string effective_session =
        cfg_.wayland_display.empty() ? session_type : "wayland";

    if (effective_session != "wayland") {
        logger::error("Wayland session required for screen capture, current XDG_SESSION_TYPE={}", session_type);
        return false;
    }

    const std::string effective_wayland_display =
        cfg_.wayland_display.empty() ? get_env_or_empty("WAYLAND_DISPLAY") : cfg_.wayland_display;
    if (effective_wayland_display.empty()) {
        logger::error("WAYLAND_DISPLAY is not set; start from the local GUI session or configure wayland_display");
        return false;
    }

    const std::string effective_runtime_dir =
        cfg_.xdg_runtime_dir.empty() ? get_env_or_empty("XDG_RUNTIME_DIR") : cfg_.xdg_runtime_dir;
    if (effective_runtime_dir.empty()) {
        logger::error("XDG_RUNTIME_DIR is not set; start from the local GUI session or configure xdg_runtime_dir");
        return false;
    }

    if (access(kGrimPath, X_OK) != 0) {
        logger::error("grim not executable: {}", kGrimPath);
        return false;
    }

    if (access(kFfmpegPath, X_OK) != 0) {
        logger::error("ffmpeg not executable: {}", kFfmpegPath);
        return false;
    }

    return true;
}

bool ScreenCapture::start() {
    if (child_pid_ > 0) {
        return true;
    }

    if (!validate_environment()) {
        return false;
    }

    const std::string snapshot_path = cfg_.framebuffer_snapshot_path;
    const std::string tmp_png = snapshot_path + ".tmp.png";
    const std::string tmp_jpg = snapshot_path + ".tmp.jpg";
    const int interval_sec = std::max(1, cfg_.framebuffer_snapshot_interval_sec);
    const int quality = std::max(2, cfg_.framebuffer_snapshot_quality);

    std::ostringstream command;
    command
        << "set -e\n"
        << "while true; do\n"
        << "  " << kGrimPath << " '" << tmp_png << "'\n"
        << "  " << kFfmpegPath << " -loglevel error -y -i '" << tmp_png << "' -q:v " << quality
        << " '" << tmp_jpg << "'\n"
        << "  mv -f '" << tmp_jpg << "' '" << snapshot_path << "'\n"
        << "  rm -f '" << tmp_png << "'\n"
        << "  sleep " << interval_sec << "\n"
        << "done";

    child_pid_ = fork();
    if (child_pid_ < 0) {
        logger::error("Failed to fork framebuffer snapshot loop: {}", std::strerror(errno));
        child_pid_ = -1;
        return false;
    }

    if (child_pid_ == 0) {
        set_env_if_present("WAYLAND_DISPLAY", cfg_.wayland_display);
        set_env_if_present("XDG_RUNTIME_DIR", cfg_.xdg_runtime_dir);
        set_env_if_present("XDG_SESSION_TYPE", "wayland");
        execl("/bin/bash", "/bin/bash", "-lc", command.str().c_str(), static_cast<char*>(nullptr));
        std::fprintf(stderr, "Failed to exec framebuffer snapshot loop: %s\n", std::strerror(errno));
        _exit(127);
    }

    logger::info("Started framebuffer snapshot loop (PID {}) writing to {}",
                 child_pid_, cfg_.framebuffer_snapshot_path);
    return true;
}

bool ScreenCapture::is_running() {
    if (child_pid_ <= 0) {
        return false;
    }

    int status = 0;
    const pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result == 0) {
        return true;
    }

    if (result == child_pid_) {
        logger::error("FrameBuffer snapshot loop exited with status {}", status);
    } else {
        logger::error("Failed to poll FrameBuffer snapshot loop: {}", std::strerror(errno));
    }

    child_pid_ = -1;
    return false;
}

void ScreenCapture::stop() {
    if (child_pid_ <= 0) {
        return;
    }

    const pid_t pid = child_pid_;
    child_pid_ = -1;

    kill(pid, SIGTERM);
    int status = 0;
    waitpid(pid, &status, 0);
    logger::info("Stopped framebuffer snapshot loop (PID {})", pid);
}

} // namespace utils
