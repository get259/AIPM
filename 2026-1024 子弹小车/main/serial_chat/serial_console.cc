#include "serial_console.h"
#include "serial_line.h"
#include "sdkconfig.h"

#include <cstdio>
#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#endif

bool SerialConsole::Start(std::function<void(std::string)> on_line) {
    on_line_ = std::move(on_line);
#if CONFIG_ESP_CONSOLE_UART
    // 使用启动阶段已经配置的控制台引脚和波特率，不接管小车业务 UART。
    const auto port = static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
    if (!uart_is_driver_installed(port)) {
        uart_ready_ = uart_driver_install(port, 4096, 0, 0, nullptr, 0) == ESP_OK;
    } else {
        printf("[串口测试] 控制台 UART 已被占用，不接管接收\n");
    }
#endif
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {};
        config.tx_buffer_size = 1024;
        config.rx_buffer_size = 4096;
        usb_ready_ = usb_serial_jtag_driver_install(&config) == ESP_OK;
        if (usb_ready_) usb_serial_jtag_vfs_use_driver();
    } else {
        printf("[串口测试] USB Serial/JTAG 已被占用，不接管接收\n");
    }
#endif
    if (!uart_ready_ && !usb_ready_) return false;
    return xTaskCreate([](void* arg) {
        static_cast<SerialConsole*>(arg)->ReadLoop();
    }, "serial_chat", 4096, this, 2, nullptr) == pdPASS;
}

void SerialConsole::ReadLoop() {
    // 两个端口分别组帧，不能把不同端口的半行合成一个请求。
    SerialLine uart_line, usb_line;
    auto consume = [this](SerialLine& parser, const unsigned char* bytes, int count) {
        for (int i = 0; i < count; ++i) {
            std::string text;
            const auto result = parser.Push(bytes[i], text);
            if (result == SerialLine::Result::None) continue;
            if (result == SerialLine::Result::Empty) printf("[串口测试] 空行已忽略\n");
            else if (result == SerialLine::Result::TooLong) printf("[串口测试] 超过 1024 字节，整行已丢弃\n");
            else if (result == SerialLine::Result::Invalid) printf("[串口测试] 请发送 UTF-8 文本，不要使用十六进制模式\n");
            else if (pending_.exchange(true)) printf("[串口测试] 正在处理上一条，请等待回复结束\n");
            else on_line_(std::move(text));
        }
    };
    while (true) {
        unsigned char buffer[128];
#if CONFIG_ESP_CONSOLE_UART
        if (uart_ready_) {
            const int count = uart_read_bytes(static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM), buffer, sizeof(buffer), 0);
            consume(uart_line, buffer, count);
        }
#endif
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
        if (usb_ready_) {
            const int count = usb_serial_jtag_read_bytes(buffer, sizeof(buffer), 0);
            consume(usb_line, buffer, count);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
