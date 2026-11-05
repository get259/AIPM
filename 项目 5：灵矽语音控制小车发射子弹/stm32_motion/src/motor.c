#include "motor.h"
#include "car_motion.h"
#include "car_protocol.h"
#include "stm32f10x.h"

static int16_t Clamp(int16_t value)
{
    if (value > 100) return 100;
    if (value < -100) return -100;
    return value;
}

void Motor_Set(int16_t left, int16_t right)
{
    left = Clamp(left);
    right = Clamp(right);
    /* 暂停更新事件，保证四个预装载值在同一个更新事件生效。 */
    TIM_UpdateDisableConfig(TIM4, ENABLE);
    TIM_SetCompare1(TIM4, left > 0 ? (uint16_t)left : 0);
    TIM_SetCompare2(TIM4, left < 0 ? (uint16_t)-left : 0);
    TIM_SetCompare3(TIM4, right > 0 ? (uint16_t)right : 0);
    TIM_SetCompare4(TIM4, right < 0 ? (uint16_t)-right : 0);
    TIM_UpdateDisableConfig(TIM4, DISABLE);
    TIM_GenerateEvent(TIM4, TIM_EventSource_Update);
}

void Motor_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef channel;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9;
    /* 先以普通输出拉低，定时器零占空比就绪后才接管引脚。 */
    GPIO_ResetBits(GPIOB, gpio.GPIO_Pin);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Period = 99;
    timer.TIM_Prescaler = 35;
    TIM_TimeBaseInit(TIM4, &timer);
    TIM_OCStructInit(&channel);
    channel.TIM_OCMode = TIM_OCMode_PWM1;
    channel.TIM_OutputState = TIM_OutputState_Enable;
    channel.TIM_Pulse = 0;
    channel.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC1Init(TIM4, &channel);
    TIM_OC2Init(TIM4, &channel);
    TIM_OC3Init(TIM4, &channel);
    TIM_OC4Init(TIM4, &channel);
    TIM_OC1PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC2PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC3PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_OC4PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);
    Motor_Stop();
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);
    TIM_Cmd(TIM4, ENABLE);
}

void Motor_Stop(void) { Motor_Set(0, 0); }
void Motor_Forward(uint8_t speed) { Motor_Set(speed, speed); }
void Motor_Backward(uint8_t speed) { Motor_Set(-(int16_t)speed, -(int16_t)speed); }
void Motor_Left(uint8_t speed)
{
    const CarMotion motion = CarMotion_Decode(CAR_LEFT, speed);
    Motor_Set(motion.left, motion.right);
}
void Motor_Right(uint8_t speed)
{
    const CarMotion motion = CarMotion_Decode(CAR_RIGHT, speed);
    Motor_Set(motion.left, motion.right);
}
