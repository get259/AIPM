#ifndef CAR_FIRE_H
#define CAR_FIRE_H
#include <stdbool.h>
#include <stdint.h>

#define CAR_FIRE_LED_COUNT 10U
#define CAR_FIRE_ROUND1_MS 1800U
#define CAR_FIRE_ROUND2_MS 1200U
#define CAR_FIRE_ROUND3_MS 800U
#define CAR_FIRE_CHARGE_MS (CAR_FIRE_ROUND1_MS + CAR_FIRE_ROUND2_MS + CAR_FIRE_ROUND3_MS)
#define CAR_FIRE_SHOT_MS 500U
#define CAR_FIRE_DURATION_MS (CAR_FIRE_CHARGE_MS + CAR_FIRE_SHOT_MS)
#define CAR_FIRE_PWM_STEPS 32U
/* PB0 高有效：发射开始时响两次，每次 50ms，中间间隔 50ms。 */
#define CAR_FIRE_BUZZER_PULSE_MS 50U
#define CAR_FIRE_BUZZER_GAP_MS 50U

typedef struct {
    uint32_t started_ms;
    bool active;
} CarFire;

/* 每个有效 fire=on 帧是一次请求；忙时忽略，完成后不自行重复。 */
bool CarFire_Trigger(CarFire *fire, uint32_t now_ms);
void CarFire_Update(CarFire *fire, uint32_t now_ms, uint8_t levels[CAR_FIRE_LED_COUNT]);
bool CarFire_BuzzerHigh(const CarFire *fire, uint32_t now_ms);
#endif
