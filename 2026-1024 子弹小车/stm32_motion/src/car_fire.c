#include "car_fire.h"

bool CarFire_Trigger(CarFire *fire, uint32_t now_ms)
{
    if (fire->active && (uint32_t)(now_ms - fire->started_ms) < CAR_FIRE_DURATION_MS) return false;
    fire->started_ms = now_ms;
    fire->active = true;
    return true;
}

void CarFire_Update(CarFire *fire, uint32_t now_ms, uint8_t levels[CAR_FIRE_LED_COUNT])
{
    const uint32_t t = now_ms - fire->started_ms;
    if (t >= CAR_FIRE_DURATION_MS) fire->active = false;
    for (unsigned i = 0; i < CAR_FIRE_LED_COUNT; ++i) levels[i] = 0;
    if (!fire->active) return;

    if (t < CAR_FIRE_CHARGE_MS) {
        /* 对齐 HTML 演示的连续升亮曲线，跨轮不重置 PB10。 */
        levels[0] = (uint8_t)(2U + (30U * t + CAR_FIRE_CHARGE_MS / 2U) / CAR_FIRE_CHARGE_MS);
        unsigned elapsed = t, duration = CAR_FIRE_ROUND1_MS;
        if (t >= CAR_FIRE_ROUND1_MS + CAR_FIRE_ROUND2_MS) {
            elapsed -= CAR_FIRE_ROUND1_MS + CAR_FIRE_ROUND2_MS;
            duration = CAR_FIRE_ROUND3_MS;
        } else if (t >= CAR_FIRE_ROUND1_MS) {
            elapsed -= CAR_FIRE_ROUND1_MS;
            duration = CAR_FIRE_ROUND2_MS;
        }
        /* PA0 → PB10：前一颗直接熄灭；PB10 只保留蓄能亮度。 */
        const unsigned index = CAR_FIRE_LED_COUNT - 1U - elapsed * CAR_FIRE_LED_COUNT / duration;
        if (index != 0U) levels[index] = CAR_FIRE_PWM_STEPS;
    } else {
        /* 满亮核心反向发射，PB10 → PA0，红蓝均可输出 32/32。 */
        const unsigned index = (t - CAR_FIRE_CHARGE_MS) * CAR_FIRE_LED_COUNT / CAR_FIRE_SHOT_MS;
        levels[index] = CAR_FIRE_PWM_STEPS;
    }
}

bool CarFire_BuzzerHigh(const CarFire *fire, uint32_t now_ms)
{
    const uint32_t t = now_ms - fire->started_ms;
    if (!fire->active || t < CAR_FIRE_CHARGE_MS || t >= CAR_FIRE_DURATION_MS) return false;
    const uint32_t elapsed = t - CAR_FIRE_CHARGE_MS;
    const uint32_t second = CAR_FIRE_BUZZER_PULSE_MS + CAR_FIRE_BUZZER_GAP_MS;
    return elapsed < CAR_FIRE_BUZZER_PULSE_MS ||
           (elapsed >= second && elapsed < second + CAR_FIRE_BUZZER_PULSE_MS);
}
