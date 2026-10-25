#pragma once

#include "car_reply.h"
#include <mutex>

// 由 Application 主任务串行调用，网络线程只通过 Schedule 投递事件。
class CarController {
public:
    void Initialize();
    void BeginReply() { reply_.Begin(); }
    bool AppendReply(const std::string& text);
    bool FinishReply(bool complete);
    void NextInput() { reply_.NextInput(); std::lock_guard<std::mutex> lock(output_mutex_); manual_override_ = false; }
    void DiscardReply() { reply_.Reset(); }
    // 方向和发射分开处理；单次发射保留当前运动位，输出共用同一把锁。
    void ApplyRemoteState(uint8_t state) { ApplyState(state & 0x0f, true); }
    void FireRemote();

private:
    CarReply reply_;
    uint8_t state_ = 0;
    bool led_ready_ = false;
    bool uart_ready_ = false;
    std::mutex output_mutex_;
    bool manual_override_ = false;
    void ApplyState(uint8_t state, bool manual = false);
    void ApplyStateLocked(uint8_t state, bool manual);
};
