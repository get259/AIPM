#include "car_frame.h"
#include "ActionJson.h"

uint8_t CarCrc8(const uint8_t* data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint8_t>((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0));
        }
    }
    return crc;
}

std::array<uint8_t, 4> BuildCarFrame(uint8_t state) {
    // 即使调用方误传预留位，线上数据仍只包含已经约定的五个动作位。
    std::array<uint8_t, 4> frame{0xAA, 0x55, static_cast<uint8_t>(state & ActionBits::All), 0};
    frame[3] = CarCrc8(frame.data(), 3);
    return frame;
}

std::array<char, 36> FormatCarFrameBinary(const std::array<uint8_t, 4>& frame) {
    std::array<char, 36> text{};
    for (size_t byte = 0; byte < frame.size(); ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            text[byte * 9 + bit] = (frame[byte] & (0x80 >> bit)) ? '1' : '0';
        }
        text[byte * 9 + 8] = byte + 1 < frame.size() ? ' ' : '\0';
    }
    return text;
}
