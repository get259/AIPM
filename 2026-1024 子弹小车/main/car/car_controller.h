#pragma once

#include "car_reply.h"

// 由 Application 主任务串行调用，网络线程只通过 Schedule 投递事件。
class CarController {
public:
    void Initialize();
    void BeginReply() { reply_.Begin(); }
    bool AppendReply(const std::string& text);
    bool FinishReply(bool complete);
    void NextInput() { reply_.NextInput(); }
    void DiscardReply() { reply_.Reset(); }

private:
    CarReply reply_;
    uint8_t state_ = 0;
    bool led_ready_ = false;
    bool uart_ready_ = false;
    void ApplyState(uint8_t state);
};
