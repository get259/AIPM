#include "stm32f10x.h"
#include "car_fire.h"
#include "fire_servo.h"

void FireServo_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    TIM_OCInitTypeDef channel;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_5;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_ResetBits(GPIOB, GPIO_Pin_5);
    GPIO_Init(GPIOB, &gpio);
    /* 只启用 TIM3_CH2/PB5；PB0、PB1 保持普通输出，继续控制蜂鸣器/灯。 */
    GPIO_PinRemapConfig(GPIO_PartialRemap_TIM3, ENABLE);
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 71U; /* 72MHz / 72 = 1MHz，比较值单位为微秒。 */
    timer.TIM_Period = CAR_FIRE_SERVO_PERIOD_US - 1U;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_OCStructInit(&channel);
    channel.TIM_OCMode = TIM_OCMode_PWM1;
    channel.TIM_OutputState = TIM_OutputState_Enable;
    channel.TIM_OCPolarity = TIM_OCPolarity_High;
    channel.TIM_Pulse = CAR_FIRE_SERVO_STOP_US; /* 上电 CCR2=0，PB5 恒低无脉冲。 */
    TIM_OC2Init(TIM3, &channel);
    TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM3, ENABLE);
    TIM_GenerateEvent(TIM3, TIM_EventSource_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOB, &gpio);
    TIM_Cmd(TIM3, ENABLE);
}

void FireServo_SetPulse(uint16_t pulse_us)
{
    TIM_SetCompare2(TIM3, pulse_us);
}
