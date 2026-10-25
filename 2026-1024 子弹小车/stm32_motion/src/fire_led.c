#include "stm32f10x.h"
#include "car_fire.h"
#include "fire_led.h"

static CarFire fire;
static volatile uint32_t milliseconds;
static volatile uint8_t levels[CAR_FIRE_LED_COUNT];
static volatile bool buzzer_high;
static uint32_t rendered_ms = (uint32_t)-1;
static const uint16_t pins[CAR_FIRE_LED_COUNT] = {
    GPIO_Pin_10, GPIO_Pin_1, GPIO_Pin_7, GPIO_Pin_6, GPIO_Pin_5,
    GPIO_Pin_4, GPIO_Pin_3, GPIO_Pin_2, GPIO_Pin_1, GPIO_Pin_0
};

void FireLed_Init(void)
{
    GPIO_InitTypeDef gpio;
    TIM_TimeBaseInitTypeDef timer;
    NVIC_InitTypeDef irq;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    /* 先置高再配置推挽，确保上电和未发射时全部熄灭。 */
    GPIO_SetBits(GPIOA, 0x00ffU);
    GPIO_SetBits(GPIOB, GPIO_Pin_10 | GPIO_Pin_1);
    /* PB0 蜂鸣器高有效，先拉低再启用输出，避免上电误响。 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_0);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    gpio.GPIO_Pin = 0x00ffU;
    GPIO_Init(GPIOA, &gpio);
    gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_1 | GPIO_Pin_0;
    GPIO_Init(GPIOB, &gpio);
    /* TIM2 仅提供中断：20kHz / 32 级 = 625Hz 软件 PWM，不占 TIM4 电机通道。 */
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = 71U;
    timer.TIM_Period = 49U;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    irq.NVIC_IRQChannel = TIM2_IRQn;
    irq.NVIC_IRQChannelPreemptionPriority = 2;
    irq.NVIC_IRQChannelSubPriority = 0;
    irq.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&irq);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

void FireLed_Trigger(void)
{
    (void)CarFire_Trigger(&fire, milliseconds);
    rendered_ms = (uint32_t)-1;
}

void FireLed_Poll(void)
{
    const uint32_t now = milliseconds;
    uint8_t next[CAR_FIRE_LED_COUNT];
    if (rendered_ms == now) return;
    rendered_ms = now;
    CarFire_Update(&fire, now, next);
    const bool next_buzzer = CarFire_BuzzerHigh(&fire, now);
    uint32_t mask;
    __asm volatile ("mrs %0, primask" : "=r" (mask) :: "memory");
    __disable_irq();
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) levels[i] = next[i];
    buzzer_high = next_buzzer;
    __asm volatile ("msr primask, %0" :: "r" (mask) : "memory");
}

void TIM2_IRQHandler(void)
{
    static uint8_t phase, ticks;
    uint16_t low_a = 0, low_b = 0;
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET) return;
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) {
        if (phase < levels[i]) {
            if (i < 2) low_b |= pins[i];
            else low_a |= pins[i];
        }
    }
    /* 灯排低有效、蜂鸣器高有效，分别编码；不影响串口和电机 GPIO。 */
    GPIOA->BSRR = (uint32_t)low_a << 16 | (0x00ffU & ~low_a);
    const uint32_t buzzer_bits = buzzer_high ? GPIO_Pin_0 : (uint32_t)GPIO_Pin_0 << 16;
    GPIOB->BSRR = (uint32_t)low_b << 16 | ((GPIO_Pin_10 | GPIO_Pin_1) & ~low_b) | buzzer_bits;
    phase = (phase + 1U) & (CAR_FIRE_PWM_STEPS - 1U);
    if (++ticks == 20U) { ticks = 0; ++milliseconds; }
}
