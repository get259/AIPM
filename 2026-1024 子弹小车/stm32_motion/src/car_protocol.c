#include "car_protocol.h"

uint8_t Car_Crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (uint8_t)((crc << 1) ^ ((crc & 0x80U) ? 0x07U : 0U));
        }
    }
    return crc;
}

void CarParser_Reset(CarParser *parser)
{
    parser->used = 0;
}

bool CarParser_Feed(CarParser *parser, uint8_t byte, uint8_t *state)
{
    /* 固定四字节滑动窗口，校验失败仍能重新寻找重叠帧头。 */
    parser->bytes[parser->used++] = byte;
    if (parser->used < 4) {
        return false;
    }
    if (parser->bytes[0] == 0xAA && parser->bytes[1] == 0x55 &&
        (parser->bytes[2] & (uint8_t)~CAR_ALLOWED) == 0 &&
        Car_Crc8(parser->bytes, 3) == parser->bytes[3]) {
        *state = parser->bytes[2];
        parser->used = 0;
        return true;
    }
    parser->bytes[0] = parser->bytes[1];
    parser->bytes[1] = parser->bytes[2];
    parser->bytes[2] = parser->bytes[3];
    parser->used = 3;
    return false;
}
