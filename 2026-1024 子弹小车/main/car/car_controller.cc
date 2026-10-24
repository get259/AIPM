#include "car_controller.h"
#include "car_frame.h"
#include "ActionJson.h"
#include "sdkconfig.h"

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>

namespace {
constexpr const char* kTag = "Car";
}

void CarController::Initialize() {
#if CONFIG_CAR_CONTROL
    const int led = CONFIG_CAR_LED_GPIO;
    const int tx = CONFIG_CAR_UART_TX_GPIO;
    const int rx = CONFIG_CAR_UART_RX_GPIO;
    const bool pins_overlap = led >= 0 && (led == tx || led == rx);
    if (pins_overlap) {
        ESP_LOGE(kTag, "LED and UART GPIO overlap; hardware outputs disabled");
        return;
    }
    if (led >= 0 && GPIO_IS_VALID_OUTPUT_GPIO(led)) {
        // 先写关闭电平，再启用输出方向，避免初始化时短暂点亮低有效 LED。
        gpio_set_level(static_cast<gpio_num_t>(led), 1);
        led_ready_ = gpio_set_direction(static_cast<gpio_num_t>(led), GPIO_MODE_OUTPUT) == ESP_OK;
    }
    if (tx >= 0 && rx >= 0 && tx != rx && GPIO_IS_VALID_OUTPUT_GPIO(tx) && GPIO_IS_VALID_GPIO(rx)) {
        const auto port = static_cast<uart_port_t>(CONFIG_CAR_UART_PORT);
        if (uart_is_driver_installed(port)) {
            ESP_LOGE(kTag, "UART%d is already in use; car UART disabled", CONFIG_CAR_UART_PORT);
        } else {
            uart_config_t config = {};
            config.baud_rate = 115200;
            config.data_bits = UART_DATA_8_BITS;
            config.parity = UART_PARITY_DISABLE;
            config.stop_bits = UART_STOP_BITS_1;
            config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
            config.source_clk = UART_SCLK_DEFAULT;
            if (uart_driver_install(port, 256, 256, 0, nullptr, 0) == ESP_OK) {
                uart_ready_ = uart_param_config(port, &config) == ESP_OK &&
                    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK;
                if (!uart_ready_) uart_driver_delete(port);
            }
        }
    }
    ESP_LOGI(kTag, "Ready: LED=%s, UART=%s (unconfigured outputs remain disabled)",
             led_ready_ ? "enabled" : "disabled", uart_ready_ ? "enabled" : "disabled");
#endif
}

void CarController::FinishReply(bool complete) {
    uint8_t parsed = 0;
    const auto result = reply_.Finish(complete, parsed);
    if (result == CarReply::Result::Ignored) {
        ESP_LOGW(kTag, "Incomplete, aborted or oversized reply ignored");
        return;
    }
    if (result == CarReply::Result::Invalid) {
        ESP_LOGW(kTag, "Invalid action JSON; applying zero state");
    }
    ApplyState(parsed);
}

void CarController::ApplyState(uint8_t state) {
    state_ = state & ActionBits::All;
#if CONFIG_CAR_CONTROL
    if (led_ready_) {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_CAR_LED_GPIO), (state_ & ActionBits::Fire) ? 0 : 1);
    }
    const auto frame = BuildCarFrame(state_);
    if (uart_ready_) {
        // 全零状态也必须发送，不能做 if (state_) 过滤。
        const int written = uart_write_bytes(static_cast<uart_port_t>(CONFIG_CAR_UART_PORT), frame.data(), frame.size());
        if (written != static_cast<int>(frame.size())) ESP_LOGE(kTag, "UART frame write failed");
    }
    ESP_LOGI(kTag, "state=0x%02X, frame=%02X %02X %02X %02X%s", state_,
             frame[0], frame[1], frame[2], frame[3], uart_ready_ ? "" : " (UART disabled)");
#endif
}
