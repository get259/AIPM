#include "car_reply.h"
#include "ActionJson.h"

#include <cstring>

void CarReply::Begin() {
    // 保留原工程对重复 start 的处理，避免擦掉已经收到的句子。
    if (active_) return;
    length_ = 0;
    overflow_ = false;
    active_ = true;
}

void CarReply::Append(const char* text, size_t length) {
    if (!active_ || overflow_) return;
    if (length > kMaxBytes - length_ || (length != 0 && text == nullptr)) {
        overflow_ = true;
        return;
    }
    if (length != 0) std::memcpy(text_.data() + length_, text, length);
    length_ += length;
}

CarReply::Result CarReply::Finish(bool complete, uint8_t& state) {
    Result result = Result::Ignored;
    if (active_ && complete && !overflow_ && length_ != 0) {
        uint8_t parsed = 0;
        if (parseActionJson(text_.data(), length_, parsed)) {
            state = parsed;
            result = Result::Valid;
        } else {
            state = 0;
            result = Result::Invalid;
        }
    }
    Reset();
    return result;
}

void CarReply::Reset() {
    length_ = 0;
    active_ = false;
    overflow_ = false;
}
