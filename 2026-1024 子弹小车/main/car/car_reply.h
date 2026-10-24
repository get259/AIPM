#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// 独立于 ESP-IDF 的整轮回复缓冲，便于在电脑端验证边界行为。
class CarReply {
public:
    static constexpr size_t kMaxBytes = 8192;
    enum class Result { Ignored, Valid, Invalid };

    void Begin();
    void Append(const char* text, size_t length);
    void Append(const std::string& text) { Append(text.data(), text.size()); }
    Result Finish(bool complete, uint8_t& state);
    void Reset();

private:
    std::array<char, kMaxBytes> text_{};
    size_t length_ = 0;
    bool active_ = false;
    bool overflow_ = false;
};
