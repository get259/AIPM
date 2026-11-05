#include "car_motion.h"
#include "car_protocol.h"
#ifndef CAR_TEST_HOST
#include "car_uart.h"
#endif
#include "car_config.h"
#ifndef CAR_TEST_HOST
#include "stm32f10x.h"
#include "fire_servo.h"
#endif
#include "car_fire.h"
#ifndef CAR_TEST_HOST
#include "fire_led.h"

void USART1_IRQHandler(void);
void TIM2_IRQHandler(void);
#endif

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

#ifndef CAR_TEST_HOST
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
#endif
    /* 上电保持熄灭，即使尚未触发就超过一次灯效时长也不能自动亮灯。 */
    CarFire fire = {0};
    uint8_t levels[CAR_FIRE_LED_COUNT];
    CarFire_Update(&fire, 100, levels);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CarFire_Update(&fire, 10000, levels);
    CHECK(!fire.active && !fire.hold_full);
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
    /* 以下单独检查同一轮灯序，先重置独立状态。 */
    fire = (CarFire){0};
    CHECK(CarFire_Trigger(&fire, 100));
    /* 反向发射每隔 50ms 增加一颗满亮灯，已点亮的灯不能再熄灭。 */
    for (unsigned step = 0; step < 10; ++step) {
        CarFire_Update(&fire, 3900U + step * 50U, levels);
        for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i)
            CHECK(levels[i] == (i <= step ? 32U : 0U));
        /* 每步最后一毫秒仍保持相同灯数，直到下一步再点亮一颗。 */
        CarFire_Update(&fire, 3949U + step * 50U, levels);
        for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i)
            CHECK(levels[i] == (i <= step ? 32U : 0U));
    }
    CarFire_Update(&fire, 4399U, levels);
    CHECK(levels[9] == 32);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 32);
    CarFire_Update(&fire, 100U + CAR_FIRE_DURATION_MS, levels);
    CHECK(!fire.active && fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 32);
    /* 全亮保持 2 秒，最后一毫秒仍亮，到期立即全部熄灭且不再发声。 */
    CarFire_Update(&fire, 100U + CAR_FIRE_RESET_MS - 1U, levels);
    CHECK(!fire.active && fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 32);
    CHECK(!CarFire_BuzzerHigh(&fire, 100U + CAR_FIRE_RESET_MS - 1U));
    CarFire_Update(&fire, 100U + CAR_FIRE_RESET_MS, levels);
    CHECK(!fire.active && !fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CarFire_Update(&fire, 10000, levels);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(!CarFire_BuzzerHigh(&fire, 10000));
    /* 时间计数再次绕回起点附近时，保持全灭，不重新点亮或发声。 */
    CarFire_Update(&fire, 101, levels);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(!CarFire_BuzzerHigh(&fire, 3900));
    /* 即使跳过整个发射及保持区间，下次更新也应直接恢复全灭。 */
    CHECK(CarFire_Trigger(&fire, 20000));
    CarFire_Update(&fire, 20000U + CAR_FIRE_RESET_MS + 1000U, levels);
    CHECK(!fire.active && !fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    /* 灯已进入保持阶段，舵机仍在运行：重复请求不得重启或延长转动。 */
    CHECK(CarFire_Trigger(&fire, 30000));
    CarFire_Update(&fire, 30000U + CAR_FIRE_DURATION_MS, levels);
    CHECK(fire.hold_full);
    CHECK(!CarFire_Trigger(&fire, 35000));
    CHECK(fire.started_ms == 30000);
    CHECK(!CarFire_Trigger(&fire, 30000U + CAR_FIRE_SERVO_END_MS - 1U));
    CHECK(CarFire_Trigger(&fire, 30000U + CAR_FIRE_SERVO_END_MS));
    CHECK(fire.active && !fire.hold_full);
    CarFire_Update(&fire, 30000U + CAR_FIRE_SERVO_END_MS + 1000U, levels);
    CHECK(fire.active && !fire.hold_full);
    CHECK(levels[0] > 0 && levels[0] < 32);
    CarFire_Update(&fire, 30000U + CAR_FIRE_SERVO_END_MS + CAR_FIRE_RESET_MS, levels);
    CHECK(!fire.active && !fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(CarFire_Trigger(&fire, 0xfffffff0U));
    CHECK(!fire.hold_full);
    CarFire_Update(&fire, 0xfffffff0U, levels);
    CHECK(levels[0] == 2 && levels[9] == 32);
    for (unsigned i = 1; i < 9; ++i) CHECK(levels[i] == 0);
    CHECK(CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS));
    CHECK(!CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS + 50U));
    CHECK(CarFire_BuzzerHigh(&fire, 0xfffffff0U + CAR_FIRE_CHARGE_MS + 100U));
    CarFire_Update(&fire, 0xfffffff0U + CAR_FIRE_DURATION_MS, levels);
    CHECK(!fire.active && fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 32);
    CarFire_Update(&fire, 0xfffffff0U + CAR_FIRE_RESET_MS - 1U, levels);
    CHECK(fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 32);
    CarFire_Update(&fire, 0xfffffff0U + CAR_FIRE_RESET_MS, levels);
    CHECK(!fire.active && !fire.hold_full);
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) CHECK(levels[i] == 0);
    CHECK(!CarFire_Trigger(&fire, 7000));
    CHECK(CarFire_Trigger(&fire, 10000));
    CHECK(!fire.hold_full);
    /* 空闲无脉冲；按最新要求使用 2000us，延迟后恰好运行 5000ms。 */
    CarFire servo = {0};
    CHECK(CAR_FIRE_SERVO_STOP_US == 0U);
    CHECK(CAR_FIRE_SERVO_RUN_US == 2000U);
    CHECK(CarFire_ServoPulseUs(&servo, 0) == 0U);
    CHECK(CarFire_ServoPulseUs(&servo, 100000U) == 0U);
    CHECK(CarFire_ServoPulseUs(&servo, CAR_FIRE_SERVO_START_MS) == CAR_FIRE_SERVO_STOP_US);
    CHECK(CarFire_Trigger(&servo, 10000));
    unsigned servo_run_ms = 0;
    for (unsigned t = 0; t <= CAR_FIRE_SERVO_END_MS + 100U; ++t) {
        CarFire_Update(&servo, 10000U + t, levels);
        const uint16_t pulse = CarFire_ServoPulseUs(&servo, 10000U + t);
        const bool running = t >= CAR_FIRE_SERVO_START_MS && t < CAR_FIRE_SERVO_END_MS;
        CHECK(pulse == (running ? CAR_FIRE_SERVO_RUN_US : CAR_FIRE_SERVO_STOP_US));
        if (running) ++servo_run_ms;
        if (t < CAR_FIRE_SERVO_END_MS && t % 1000U == 0U)
            CHECK(!CarFire_Trigger(&servo, 10000U + t));
    }
    CHECK(servo_run_ms == 5000U && !servo.servo_pending);
    /* 完成后的计数回绕不能使旧一轮舵机重新转起来。 */
    CHECK(CarFire_ServoPulseUs(&servo, 10000U + CAR_FIRE_SERVO_START_MS) == CAR_FIRE_SERVO_STOP_US);
    CHECK(CarFire_Trigger(&servo, 0xfffffff0U));
    CHECK(CarFire_ServoPulseUs(&servo, 0xfffffff0U + CAR_FIRE_SERVO_START_MS - 1U) == CAR_FIRE_SERVO_STOP_US);
    CHECK(CarFire_ServoPulseUs(&servo, 0xfffffff0U + CAR_FIRE_SERVO_START_MS) == CAR_FIRE_SERVO_RUN_US);
    CHECK(CarFire_ServoPulseUs(&servo, 0xfffffff0U + CAR_FIRE_SERVO_END_MS - 1U) == CAR_FIRE_SERVO_RUN_US);
    CHECK(CarFire_ServoPulseUs(&servo, 0xfffffff0U + CAR_FIRE_SERVO_END_MS) == CAR_FIRE_SERVO_STOP_US);
    CHECK(!CarFire_Trigger(&servo, 0xfffffff0U + CAR_FIRE_SERVO_END_MS - 1U));
    CHECK(CarFire_Trigger(&servo, 0xfffffff0U + CAR_FIRE_SERVO_END_MS));
    CHECK(CarFire_ServoPulseUs(&servo, servo.started_ms) == CAR_FIRE_SERVO_STOP_US);
#ifndef CAR_TEST_HOST
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
    CHECK(TIM3->CCR2 == CAR_FIRE_SERVO_STOP_US);
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
    CHECK(TIM3->CCR2 == CAR_FIRE_SERVO_RUN_US);
    /* 完成后真实 PWM 输出持续拉低全部灯脚，蜂鸣器保持低电平。 */
    for (unsigned ms = 3952U; ms <= CAR_FIRE_DURATION_MS + 1U; ++ms) {
        for (unsigned tick = 0; tick < 20; ++tick) {
            TIM2->SR = TIM_IT_Update;
            TIM2_IRQHandler();
        }
        FireLed_Poll();
    }
    for (unsigned tick = 0; tick < CAR_FIRE_PWM_STEPS; ++tick) {
        TIM2->SR = TIM_IT_Update;
        TIM2_IRQHandler();
        CHECK(GPIOA->BSRR == 0x00ff0000U);
        CHECK(GPIOB->BSRR == ((uint32_t)(GPIO_Pin_10 | GPIO_Pin_1 | GPIO_Pin_0) << 16));
    }
    /* 保持时间结束后，真实输出将全部低有效灯脚置高，蜂鸣器仍置低。 */
    for (unsigned ms = 0; ms <= CAR_FIRE_HOLD_MS; ++ms) {
        for (unsigned tick = 0; tick < 20; ++tick) {
            TIM2->SR = TIM_IT_Update;
            TIM2_IRQHandler();
        }
        FireLed_Poll();
    }
    TIM2->SR = TIM_IT_Update;
    TIM2_IRQHandler();
    CHECK(GPIOA->BSRR == 0x00ffU);
    CHECK(GPIOB->BSRR == (GPIO_Pin_10 | GPIO_Pin_1 | ((uint32_t)GPIO_Pin_0 << 16)));
    CHECK(TIM3->CCR2 == CAR_FIRE_SERVO_RUN_US); /* 灯全灭后，舵机仍要完成五秒。 */
    for (unsigned ms = CAR_FIRE_RESET_MS + 3U; ms <= CAR_FIRE_SERVO_END_MS + 2U; ++ms) {
        for (unsigned tick = 0; tick < 20; ++tick) {
            TIM2->SR = TIM_IT_Update;
            TIM2_IRQHandler();
        }
        FireLed_Poll();
    }
    CHECK(TIM3->CCR2 == CAR_FIRE_SERVO_STOP_US);
#endif
    return 0;
}

#ifdef CAR_TEST_HOST
#include <stdio.h>
int main(void)
{
    const int result = Test_Run();
    if (result) fprintf(stderr, "FAIL: test_logic.c:%d\n", result);
    else puts("PASS: protocol/CRC/motion, original lights/buzzer, delayed 5000ms continuous-servo run, busy requests and clock wraparound");
    return result ? 1 : 0;
}
#endif
