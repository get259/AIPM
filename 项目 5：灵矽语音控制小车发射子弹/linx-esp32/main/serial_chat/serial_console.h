#pragma once

#include <atomic>
#include <functional>
#include <string>

class SerialConsole {
public:
    bool Start(std::function<void(std::string)> on_line);
    void Release() { pending_.store(false); }

private:
    void ReadLoop();
    std::function<void(std::string)> on_line_;
    std::atomic<bool> pending_{false};
    bool uart_ready_ = false;
    bool usb_ready_ = false;
};
