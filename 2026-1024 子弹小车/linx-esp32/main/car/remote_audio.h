#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>

// 单轮 PCM 分帧；网络长度先校验，再复制，不能由声明长度任意分配内存。
class RemoteAudio {
public:
    static constexpr size_t kSamples = 960;
    void Reset(uint32_t recording = 0) { recording_ = recording; sequence_ = 0; tail_.clear(); }
    bool Append(uint32_t recording, uint32_t sequence, const uint8_t* bytes, size_t length,
                std::vector<int16_t>& frame) {
        frame.clear();
        if (!recording_ || recording != recording_ || sequence != sequence_ || !bytes ||
            !length || length > kSamples * 2 || length % 2 || tail_.size() + length / 2 > kSamples) return false;
        for (size_t i = 0; i < length; i += 2)
            tail_.push_back(static_cast<int16_t>(uint16_t(bytes[i]) | (uint16_t(bytes[i + 1]) << 8)));
        ++sequence_;
        if (tail_.size() == kSamples) frame.swap(tail_);
        return true;
    }
    std::vector<int16_t> Finish() {
        auto result = std::move(tail_);
        if (!result.empty()) result.resize(kSamples, 0);
        Reset();
        return result;
    }
private:
    uint32_t recording_ = 0;
    uint32_t sequence_ = 0;
    std::vector<int16_t> tail_;
};
