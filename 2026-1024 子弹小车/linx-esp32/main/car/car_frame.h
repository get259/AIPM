#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

uint8_t CarCrc8(const uint8_t* data, size_t length);
std::array<uint8_t, 4> BuildCarFrame(uint8_t state);
// 每字节按高位到低位显示 8 个二进制字符，字节间空格，末尾含空字符。
std::array<char, 36> FormatCarFrameBinary(const std::array<uint8_t, 4>& frame);
