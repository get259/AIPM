#pragma once
#include <stddef.h>
#include <stdint.h>

// 与 STM32 约定的状态字节；允许多个动作位同时为 1。
namespace ActionBits {
constexpr uint8_t Forward = 0x01;    // bit 0：前进。
constexpr uint8_t Backward = 0x02;   // bit 1：后退。
constexpr uint8_t TurnLeft = 0x04;   // bit 2：左转。
constexpr uint8_t TurnRight = 0x08;  // bit 3：右转。
constexpr uint8_t Fire = 0x10;       // bit 4：STM32 单次蓄力重炮，同时兼容 ESP32 本地 LED。
constexpr uint8_t All = 0x1F;        // bit 5～7 预留为 0。
}  // 动作状态位定义。

// 解析五个固定字段的 on/off JSON，不申请堆内存、不复制字符串。
// 字段必须齐全且不重复，允许空白和乱序，拒绝额外字段、转义和非法值。
// 左右转分别兼容 turn_left/turnleft、turn_right/turnright；别名视为同一字段。
// length 不含末尾的 '\0'；失败返回 false，并保持 output 原值不变。
bool parseActionJson(const char* buf, size_t length, uint8_t& output);
