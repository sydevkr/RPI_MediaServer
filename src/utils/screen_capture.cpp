#include "screen_capture.hpp"
#include "logger.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace utils {

namespace {

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

    if (access(cfg_.wf_recorder_path.c_str(), X_OK) != 0) {
        logger::error("wf-recorder not executable: {}", cfg_.wf_recorder_path);
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

    const std::string framerate = std::to_string(cfg_.fps);
    std::vector<std::string> args_storage = {
        cfg_.wf_recorder_path,
        "--muxer=rtsp",
        "--codec=libx264",
        "--file=" + cfg_.rtsp_url,
        "--framerate=" + framerate
    };
    std::vector<char*> args;
    args.reserve(args_storage.size() + 1);
    for (auto& value : args_storage) {
        args.push_back(value.data());
    }
    args.push_back(nullptr);

    child_pid_ = fork();
    if (child_pid_ < 0) {
        logger::error("Failed to fork wf-recorder: {}", std::strerror(errno));
        child_pid_ = -1;
        return false;
    }

    if (child_pid_ == 0) {
        set_env_if_present("WAYLAND_DISPLAY", cfg_.wayland_display);
        set_env_if_present("XDG_RUNTIME_DIR", cfg_.xdg_runtime_dir);
        execv(cfg_.wf_recorder_path.c_str(), args.data());
        std::fprintf(stderr, "Failed to exec wf-recorder: %s\n", std::strerror(errno));
        _exit(127);
    }

    logger::info("Started wf-recorder (PID {}) publishing to {}", child_pid_, cfg_.rtsp_url);
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
        logger::error("wf-recorder exited with status {}", status);
    } else {
        logger::error("Failed to poll wf-recorder: {}", std::strerror(errno));
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
    logger::info("Stopped wf-recorder (PID {})", pid);
}

} // namespace utils
