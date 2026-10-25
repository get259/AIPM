#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// WebSocket 消息边界与请求重放校验，不依赖 ESP-IDF，便于直接验证真实解析逻辑。
class RemoteWire {
public:
    enum class Result { More, Complete, Invalid };
    static constexpr size_t kLimit = 1932;
    Result Append(uint8_t type, bool final, const uint8_t* bytes, size_t length) {
        if ((type != 0 && type != 1 && type != 2) || length > kLimit - message_.size() || (length && !bytes))
            return Result::Invalid;
        if (type == 0) {
            if (!receiving_) return Result::Invalid;
        } else {
            if (receiving_) return Result::Invalid;
            receiving_ = true;
            type_ = type;
        }
        if (length) message_.insert(message_.end(), bytes, bytes + length);
        if (!final) return Result::More;
        receiving_ = false;
        return Result::Complete;
    }
    uint8_t Type() const { return type_; }
    std::vector<uint8_t> Take() {
        std::vector<uint8_t> result;
        result.swap(message_);
        return result;
    }
    bool AcceptRequest(uint32_t id) {
        if (!id || id <= last_request_) return false;
        last_request_ = id;
        return true;
    }
private:
    bool receiving_ = false;
    uint8_t type_ = 0;
    uint32_t last_request_ = 0;
    std::vector<uint8_t> message_;
};
