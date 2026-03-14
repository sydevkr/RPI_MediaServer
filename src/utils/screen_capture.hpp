#pragma once

#include <sys/types.h>
#include <string>
#include "config.hpp"

namespace utils {

class ScreenCapture {
public:
    explicit ScreenCapture(const Config& cfg);
    ~ScreenCapture();

    bool start();
    bool is_running();
    void stop();

private:
    bool validate_environment() const;

    const Config& cfg_;
    pid_t child_pid_ = -1;
};

} // namespace utils
