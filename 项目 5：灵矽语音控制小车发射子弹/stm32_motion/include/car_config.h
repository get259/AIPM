#ifndef CAR_CONFIG_H
#define CAR_CONFIG_H

/* 沿用参考工程的 70% PWM；这不是闭环车速，需上板校准。 */
#ifndef CAR_SPEED_PERCENT
#define CAR_SPEED_PERCENT 70
#endif
#if CAR_SPEED_PERCENT < 0 || CAR_SPEED_PERCENT > 100
#error "CAR_SPEED_PERCENT must be between 0 and 100"
#endif
/* 仅前进直行使用 80% PWM，转弯和后退仍使用 CAR_SPEED_PERCENT。 */
#ifndef CAR_FORWARD_SPEED_PERCENT
#define CAR_FORWARD_SPEED_PERCENT 80
#endif
#if CAR_FORWARD_SPEED_PERCENT < 0 || CAR_FORWARD_SPEED_PERCENT > 100
#error "CAR_FORWARD_SPEED_PERCENT must be between 0 and 100"
#endif
/* 用户确认的调试值：外侧 70% 时内侧 56%，不代表实测轮速比例。 */
#ifndef CAR_TURN_INNER_PERCENT
#define CAR_TURN_INNER_PERCENT 80
#endif
#if CAR_TURN_INNER_PERCENT < 1 || CAR_TURN_INNER_PERCENT > 99
#error "CAR_TURN_INNER_PERCENT must be between 1 and 99"
#endif
#define CAR_UART_BAUD 115200U
#define CAR_RX_CAPACITY 128U

#endif
