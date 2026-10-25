#include "car_motion.h"
#include "car_protocol.h"
#include "car_uart.h"
#include "car_config.h"
#include "stm32f10x.h"
#include "car_fire.h"
#include "fire_led.h"

void USART1_IRQHandler(void);
void TIM2_IRQHandler(void);

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static void Frame(uint8_t state, uint8_t out[4])
{
    out[0] = 0xAA; out[1] = 0x55; out[2] = state;
    out[3] = Car_Crc8(out, 3);
}

int Test_Run(void)
{
    CarParser parser = {0};
    uint8_t state = 0xFF;
    uint8_t frame[4];
    const uint8_t vectors[][4] = {
        {0xAA, 0x55, 0x00, 0x82},
        {0xAA, 0x55, 0x15, 0xE9},
        {0xAA, 0x55, 0x1F, 0xDF}
    };
    for (unsigned v = 0; v < 3; ++v) {
        CHECK(Car_Crc8(vectors[v], 3) == vectors[v][3]);
    }
    for (unsigned s = 0; s < 32; ++s) {
        Frame((uint8_t)s, frame);
        state = 0xFF;
        for (unsigned i = 0; i < 3; ++i) {
            CHECK(!CarParser_Feed(&parser, frame[i], &state));
            CHECK(state == 0xFF);
        }
        CHECK(CarParser_Feed(&parser, frame[3], &state));
        CHECK(state == s);
    }
    /* 任意一位传输错误均不可生成有效指令，之后可恢复。 */
    Frame(CAR_FORWARD, frame);
    for (unsigned bit = 0; bit < 32; ++bit) {
        CarParser_Reset(&parser);
        state = 0xFF;
        for (unsigned i = 0; i < 4; ++i) {
            uint8_t byte = frame[i];
            if (i == bit / 8) byte ^= (uint8_t)(1U << (bit % 8));
            CHECK(!CarParser_Feed(&parser, byte, &state));
        }
        CHECK(state == 0xFF);
        for (unsigned i = 0; i < 3; ++i) CHECK(!CarParser_Feed(&parser, frame[i], &state));
        CHECK(CarParser_Feed(&parser, frame[3], &state));
        CHECK(state == CAR_FORWARD);
    }
    /* CRC 正确但使用预留位的帧也应拒绝。 */
    for (unsigned s = 32; s < 256; ++s) {
        CarParser_Reset(&parser);
        Frame((uint8_t)s, frame);
        for (unsigned i = 0; i < 4; ++i) CHECK(!CarParser_Feed(&parser, frame[i], &state));
    }
    /* 噪声、半帧、重叠帧头、数据丢失后的恢复。 */
    const uint8_t noise[] = {0x00, 0xAA, 0xAA, 0x55, 0xAA};
    CarParser_Reset(&parser);
    for (unsigned i = 0; i < sizeof(noise); ++i) CHECK(!CarParser_Feed(&parser, noise[i], &state));
    Frame(0, frame);
    for (unsigned i = 0; i < 3; ++i) CHECK(!CarParser_Feed(&parser, frame[i], &state));
    CHECK(CarParser_Feed(&parser, frame[3], &state));
    CHECK(state == 0);
    CHECK(!CarParser_Feed(&parser, 0xAA, &state));
    CHECK(!CarParser_Feed(&parser, 0x55, &state));
    CarParser_Reset(&parser);
    for (unsigned i = 0; i < 3; ++i) CHECK(!CarParser_Feed(&parser, frame[i], &state));
    CHECK(CarParser_Feed(&parser, frame[3], &state));

    /* 每行依次对应运动位 0..15；LED 不改变运动。 */
    const int16_t expected[16][2] = {
        {0,0}, {70,70}, {-70,-70}, {0,0},
        {56,70}, {56,70}, {-70,-56}, {0,0},
        {70,56}, {70,56}, {-56,-70}, {0,0},
        {0,0}, {0,0}, {0,0}, {0,0}
    };
    for (unsigned s = 0; s < 32; ++s) {
        const CarMotion motion = CarMotion_Decode((uint8_t)s, 70);
        CHECK(motion.left == expected[s & 15][0]);
        CHECK(motion.right == expected[s & 15][1]);
        const CarMotion configured = CarMotion_DecodeConfigured((uint8_t)s);
        const bool straight_forward = (s & 15) == CAR_FORWARD;
        CHECK(configured.left == (straight_forward ? 80 : expected[s & 15][0]));
        CHECK(configured.right == (straight_forward ? 80 : expected[s & 15][1]));
    }
    for (unsigned s = 32; s < 256; ++s) {
        const CarMotion invalid = CarMotion_DecodeConfigured((uint8_t)s);
        CHECK(invalid.left == 0 && invalid.right == 0);
    }
    CHECK(CarMotion_Decode(CAR_FORWARD, 255).left == 100);
    CHECK(CarMotion_Decode(CAR_BACKWARD, 255).right == -100);
    CHECK(CarMotion_Decode(CAR_FORWARD, 0).left == 0);
    CHECK(CarMotion_Decode(0x81, 70).left == 0);
    CHECK(CarMotion_Decode(CAR_LEFT, 0).left == 0);
    CHECK(CarMotion_Decode(CAR_LEFT, 0).right == 0);
    CHECK(CarMotion_Decode(CAR_LEFT, 1).left == 1);
    CHECK(CarMotion_Decode(CAR_RIGHT, 1).right == 1);
    CHECK(CarMotion_Decode(CAR_LEFT, 255).left == 80);
    CHECK(CarMotion_Decode(CAR_LEFT, 255).right == 100);
    CHECK(CarMotion_Decode(CAR_RIGHT, 255).left == 100);
    CHECK(CarMotion_Decode(CAR_RIGHT, 255).right == 80);
    for (unsigned speed = 2; speed <= 100; ++speed) {
        const CarMotion left = CarMotion_Decode(CAR_LEFT, (uint8_t)speed);
        const CarMotion right = CarMotion_Decode(CAR_RIGHT, (uint8_t)speed);
        CHECK(left.left > 0 && left.left < left.right);
        CHECK(right.right > 0 && right.right < right.left);
    }

    /* 模拟 USART 寄存器输入，验证真实中断函数与队列读写代码。 */
    uint8_t byte = 0;
    bool lost = false;
    CHECK(!CarUart_Read(&byte, &lost));
    CHECK(!lost);
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned i = 0; i < CAR_RX_CAPACITY - 1; ++i) {
            USART1->SR = USART_SR_RXNE;
            USART1->DR = i;
            USART1_IRQHandler();
        }
        for (unsigned i = 0; i < CAR_RX_CAPACITY - 1; ++i) {
            CHECK(CarUart_Read(&byte, &lost));
            CHECK(!lost && byte == i);
        }
        CHECK(!CarUart_Read(&byte, &lost));
    }
    for (unsigned i = 0; i < CAR_RX_CAPACITY; ++i) {
        USART1->SR = USART_SR_RXNE;
        USART1->DR = i;
        USART1_IRQHandler();
    }
    CHECK(!CarUart_Read(&byte, &lost));
    CHECK(lost);
    CHECK(!CarUart_Read(&byte, &lost));
    CHECK(!lost);
    USART1->SR = USART_SR_RXNE | USART_SR_ORE;
    USART1->DR = 0xAA;
    USART1_IRQHandler();
    CHECK(!CarUart_Read(&byte, &lost));
    CHECK(lost);
    USART1->SR = USART_SR_RXNE;
    USART1->DR = 0x55;
    USART1_IRQHandler();
    CHECK(CarUart_Read(&byte, &lost));
    CHECK(!lost && byte == 0x55);
    /* 执行真实灯效状态机：忙时不重启，单次结束清零，时钟回绕仍可正常结束。 */
    CarFire fire = {0};
    uint8_t levels[CAR_FIRE_LED_COUNT];
    CarFire_Update(&fire, 100, levels);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(CarFire_Trigger(&fire, 100));
    CHECK(!CarFire_Trigger(&fire, 200));
    CHECK(fire.started_ms == 100);
    /* 三轮均从 PA0 到 PB10，核对十颗灯的先后次序及无拖尾。 */
    const unsigned starts[] = {0U, 1800U, 3000U};
    const unsigned step_ms[] = {180U, 120U, 80U};
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned step = 0; step < 10; ++step) {
            const unsigned t = starts[round] + step * step_ms[round];
            CarFire_Update(&fire, 100U + t, levels);
            const unsigned index = 9U - step;
            for (unsigned i = 1; i < CAR_FIRE_LED_COUNT; ++i)
                CHECK(levels[i] == (i == index ? 32U : 0U));
        }
    }
    /* 核心跨轮单调升亮；发射前不响，发射时恰有两次高脉冲。 */
    unsigned previous_core = 0, rising_edges = 0, high_ms = 0;
    bool previous_high = false;
    for (unsigned t = 0; t <= CAR_FIRE_DURATION_MS; ++t) {
        CarFire_Update(&fire, 100U + t, levels);
        if (t < CAR_FIRE_CHARGE_MS) {
            CHECK(levels[0] >= previous_core);
            previous_core = levels[0];
        }
        const bool high = CarFire_BuzzerHigh(&fire, 100U + t);
        const bool expected_high = (t >= 3800U && t < 3850U) || (t >= 3900U && t < 3950U);
        CHECK(high == expected_high);
        if (high && !previous_high) ++rising_edges;
        if (high) ++high_ms;
        previous_high = high;
        for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] <= 32U);
        /* 除 PB10 外，全程只允许全亮或全灭，不能出现渐暗余光。 */
        for (unsigned i = 1; i < CAR_FIRE_LED_COUNT; ++i)
            CHECK(levels[i] == 0U || levels[i] == CAR_FIRE_PWM_STEPS);
    }
    CHECK(rising_edges == 2 && high_ms == 100);
    CHECK(CarFire_Trigger(&fire, 100));
    /* 反向发射依次走过 PB10 到 PA0，每颗全亮 50ms。 */
    for (unsigned step = 0; step < 10; ++step) {
        CarFire_Update(&fire, 3900U + step * 50U, levels);
        for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i)
            CHECK(levels[i] == (i == step ? 32U : 0U));
    }
    CarFire_Update(&fire, 4399U, levels);
    CHECK(levels[9] == 32);
    for (unsigned i = 0; i < 9; ++i) CHECK(levels[i] == 0);
    CarFire_Update(&fire, 100U + CAR_FIRE_DURATION_MS, levels);
    CHECK(!fire.active);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CarFire_Update(&fire, 10000, levels);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(CarFire_Trigger(&fire, 0xfffffff0U));
    CHECK(CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS));
    CHECK(!CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS + 50U));
    CHECK(CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS + 100U));
    CarFire_Update(&fire, 0xfffffff0U + CAR_FIRE_DURATION_MS, levels);
    CHECK(!fire.active);
    CHECK(CarFire_Trigger(&fire, 7000));
    /* 执行真实 PWM 中断，核对低有效、占空比和不会写到灯排之外的引脚。 */
    TIM2->DIER = TIM_IT_Update;
    TIM2->SR = TIM_IT_Update;
    TIM2_IRQHandler();
    CHECK(GPIOA->BSRR == 0x00ffU);
    CHECK(GPIOB->BSRR == (GPIO_Pin_10 | GPIO_Pin_1 | ((uint32_t)GPIO_Pin_0 << 16)));
    FireLed_Trigger();
    /* 前进到 100ms 后冻结亮度缓存，检查 PA0 满亮的完整 PWM 周期。 */
    for (unsigned tick = 0; tick < 2000; ++tick) {
        TIM2->SR = TIM_IT_Update;
        TIM2_IRQHandler();
    }
    FireLed_Poll();
    CarFire pwm_reference = {0};
    CHECK(CarFire_Trigger(&pwm_reference, 0));
    CarFire_Update(&pwm_reference, 100, levels);
    CHECK(levels[9] > 0);
    unsigned pa0_on = 0;
    for (unsigned tick = 0; tick < 32; ++tick) {
        TIM2->SR = TIM_IT_Update;
        TIM2_IRQHandler();
        CHECK((GPIOA->BSRR & ~0x00ff00ffU) == 0);
        CHECK((GPIOB->BSRR & ~0x04030403U) == 0);
        CHECK((GPIOB->BSRR & 0x00010001U) == 0x00010000U); /* 蓄力期间 PB0 低。 */
        if (GPIOA->BSRR & ((uint32_t)GPIO_Pin_0 << 16)) ++pa0_on;
        CHECK((GPIOA->BSRR & ((uint32_t)GPIO_Pin_0 << 16)) == 0 ||
              (GPIOA->BSRR & GPIO_Pin_0) == 0);
    }
    CHECK(pa0_on == levels[9]);
    /* 执行真实中断和主循环更新，检查 PB0 不被低有效灯排 PWM 覆盖。 */
    unsigned buzzer_edges = 0;
    bool was_high = false;
    for (unsigned ms = 101; ms <= 3951U; ++ms) {
        FireLed_Poll();
        for (unsigned tick = 0; tick < 20; ++tick) {
            TIM2->SR = TIM_IT_Update;
            TIM2_IRQHandler();
        }
        const uint32_t bits = GPIOB->BSRR;
        CHECK((bits & ~0x04030403U) == 0);
        const bool high = (bits & GPIO_Pin_0) != 0;
        CHECK((bits & ((uint32_t)GPIO_Pin_0 << 16)) == (high ? 0U : 0x00010000U));
        if (high && !was_high) ++buzzer_edges;
        was_high = high;
    }
    CHECK(buzzer_edges == 2 && !was_high);
    return 0;
}
