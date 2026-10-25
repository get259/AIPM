#ifndef CAR_PROTOCOL_H
#define CAR_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    CAR_FORWARD = 0x01,
    CAR_BACKWARD = 0x02,
    CAR_LEFT = 0x04,
    CAR_RIGHT = 0x08,
    CAR_FIRE = 0x10,
    CAR_ALLOWED = 0x1F
};
typedef struct {
    uint8_t bytes[4];
    uint8_t used;
} CarParser;

uint8_t Car_Crc8(const uint8_t *data, size_t length);
void CarParser_Reset(CarParser *parser);
/* 仅在完整合法帧到达时返回 true，失败时不改变 state。 */
bool CarParser_Feed(CarParser *parser, uint8_t byte, uint8_t *state);

#endif
