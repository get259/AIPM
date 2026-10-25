#include "stm32f10x.h"
#include "car_config.h"
#include "car_motion.h"
#include "car_protocol.h"
#include "car_uart.h"
#include "motor.h"
#include "fire_led.h"

int main(void)
{
    CarParser parser = {0};
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    Motor_Init();
    CarUart_Init();
    FireLed_Init();
    for (;;) {
        uint8_t byte;
        uint8_t state;
        bool data_lost;
        const bool available = CarUart_Read(&byte, &data_lost);
        if (data_lost) {
            CarParser_Reset(&parser);
        }
        if (available && CarParser_Feed(&parser, byte, &state)) {
            const CarMotion motion = CarMotion_DecodeConfigured(state);
            Motor_Set(motion.left, motion.right);
            if (state & CAR_FIRE) FireLed_Trigger();
        }
        FireLed_Poll();
        /* 硬件 PWM 持续输出；无阻塞延时、循迹、避障或自动停车计时。 */
    }
}
