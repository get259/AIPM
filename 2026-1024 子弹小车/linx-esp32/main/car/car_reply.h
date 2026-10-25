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
    // 语音控制可在结束事件之前取出完整合法动作；每轮最多成功一次。
    bool TakeAction(uint8_t& state);
    // STT 表示下一句输入；清理上一句缓冲，但保留尚未收到的旧 stop 计数。
    void NextInput();
    bool HasPendingStops() const { return pending_stops_ != 0; }
    Result Finish(bool complete, uint8_t& state);
    void Reset();

private:
    std::array<char, kMaxBytes> text_{};
    size_t length_ = 0;
    bool active_ = false;
    bool overflow_ = false;
    bool action_taken_ = false;
    uint32_t pending_stops_ = 0;
    void ClearCurrent();
};
