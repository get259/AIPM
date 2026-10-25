#ifndef MOTOR_H
#define MOTOR_H
#include <stdint.h>
void Motor_Init(void);
/* 左右侧分别为 -100 到 100，不延时、不设定动作持续时间。 */
void Motor_Set(int16_t left, int16_t right);
void Motor_Stop(void);
void Motor_Forward(uint8_t speed);
void Motor_Backward(uint8_t speed);
/* 前进中差速转向：两侧均输出正向 PWM，内侧降低占空比。 */
void Motor_Left(uint8_t speed);
void Motor_Right(uint8_t speed);
#endif
