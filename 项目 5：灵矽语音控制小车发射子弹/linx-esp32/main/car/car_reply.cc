#include "car_reply.h"
#include "ActionJson.h"

#include <cstring>

void CarReply::Begin() {
    // 保留原工程对重复 start 的处理，避免擦掉已经收到的句子。
    if (active_) return;
    length_ = 0;
    overflow_ = false;
    action_taken_ = false;
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

bool CarReply::TakeAction(uint8_t& state) {
    if (!active_ || overflow_ || action_taken_ || length_ == 0) return false;
    uint8_t parsed = 0;
    // 校验当前全部文本，不能仅凭右大括号或一个合法前缀执行。
    if (!parseActionJson(text_.data(), length_, parsed)) return false;
    action_taken_ = true;
    ++pending_stops_;
    state = parsed;
    return true;
}

void CarReply::NextInput() {
    // 旧回复未完成时也丢弃其缓冲，之后旧 stop 不能结束新回复。
    if (active_ && !action_taken_) ++pending_stops_;
    ClearCurrent();
}

CarReply::Result CarReply::Finish(bool complete, uint8_t& state) {
    // 同一连接的结束事件按轮次顺序消耗；保留下一轮已经开始的缓冲。
    if (pending_stops_ != 0) {
        --pending_stops_;
        if (pending_stops_ == 0 && action_taken_) ClearCurrent();
        return Result::Ignored;
    }
    Result result = Result::Ignored;
    // 已提前执行的轮次只清理，不因结束、后续文本或中断再次输出状态。
    if (active_ && !action_taken_ && complete && !overflow_ && length_ != 0) {
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
    pending_stops_ = 0;
    ClearCurrent();
}

void CarReply::ClearCurrent() {
    length_ = 0;
    active_ = false;
    overflow_ = false;
    action_taken_ = false;
}
