#ifndef FIRE_SERVO_H
#define FIRE_SERVO_H
#include <stdint.h>

void FireServo_Init(void);
void FireServo_SetPulse(uint16_t pulse_us);
#endif
