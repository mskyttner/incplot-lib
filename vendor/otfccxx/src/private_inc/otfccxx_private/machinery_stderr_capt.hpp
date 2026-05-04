#pragma once

#include <string>
#include <vector>

namespace otfccxx {
namespace detail {

class _stderrCapture {
public:
    _stderrCapture();
    ~_stderrCapture();


    void
    restore();
    std::vector<std::string>
    read_lines();

private:
    int  pipe_fd_[2]   = {-1, -1};
    int  saved_stderr_ = -1;
    bool restored_     = false;
};
} // namespace detail
} // namespace otfccxx