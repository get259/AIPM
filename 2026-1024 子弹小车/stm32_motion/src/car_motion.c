#include "car_motion.h"
#include "car_protocol.h"
#include "car_config.h"

CarMotion CarMotion_DecodeConfigured(uint8_t state)
{
    const uint8_t movement = state & (CAR_FORWARD | CAR_BACKWARD | CAR_LEFT | CAR_RIGHT);
    const uint8_t speed = movement == CAR_FORWARD ? CAR_FORWARD_SPEED_PERCENT : CAR_SPEED_PERCENT;
    return CarMotion_Decode(state, speed);
}

CarMotion CarMotion_Decode(uint8_t state, uint8_t speed)
{
    CarMotion motion = {0, 0};
    const uint8_t drive = state & (CAR_FORWARD | CAR_BACKWARD);
    const uint8_t turn = state & (CAR_LEFT | CAR_RIGHT);
    const int16_t duty = speed > 100 ? 100 : speed;
    int16_t inner = (int16_t)(duty * CAR_TURN_INNER_PERCENT / 100);
    /* 保留非零输出；1% 的极低占空比无法再细分出更小的非零值。 */
    if (duty > 0 && inner == 0) inner = 1;
    if ((state & (uint8_t)~CAR_ALLOWED) != 0 ||
        drive == (CAR_FORWARD | CAR_BACKWARD) || turn == (CAR_LEFT | CAR_RIGHT)) {
        return motion;
    }
    /* 转向按车头朝向定义；倒车时交换内外侧以保留相同转向含义。 */
    if (drive == CAR_BACKWARD) {
        motion.left = turn == CAR_RIGHT ? -inner : -duty;
        motion.right = turn == CAR_LEFT ? -inner : -duty;
    } else if (drive == CAR_FORWARD || turn != 0) {
        motion.left = turn == CAR_LEFT ? inner : duty;
        motion.right = turn == CAR_RIGHT ? inner : duty;
    }
    return motion;
}
