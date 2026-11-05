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
/* 发射结束后全亮保持 2 秒，再自动恢复上电时的全灭状态。 */
#define CAR_FIRE_HOLD_MS 2000U
#define CAR_FIRE_RESET_MS (CAR_FIRE_DURATION_MS + CAR_FIRE_HOLD_MS)
#define CAR_FIRE_PWM_STEPS 32U
/* PB0 高有效：发射开始时响两次，每次 50ms，中间间隔 50ms。 */
#define CAR_FIRE_BUZZER_PULSE_MS 50U
#define CAR_FIRE_BUZZER_GAP_MS 50U
/* 连续旋转舵机：蓄力完成后转 5 秒；空闲保持低电平，不发送未校准中位。 */
#define CAR_FIRE_SERVO_START_MS CAR_FIRE_CHARGE_MS
#define CAR_FIRE_SERVO_RUN_MS 5000U
#define CAR_FIRE_SERVO_END_MS (CAR_FIRE_SERVO_START_MS + CAR_FIRE_SERVO_RUN_MS)
#define CAR_FIRE_SERVO_PERIOD_US 20000U
/* PWM1 的比较值 0 使 PB5 恒低；它不切断舵机 VCC。 */
#define CAR_FIRE_SERVO_STOP_US 0U
/* 保留空闲无脉冲；按最新实测要求，将 R2 的 1000us 反向为 2000us。 */
#define CAR_FIRE_SERVO_RUN_US 2000U

typedef struct {
    uint32_t started_ms;
    bool active;
    bool hold_full; /* 全亮保持期间置位，到时或下一次触发时清除。 */
    bool servo_pending; /* 包含蓄力等待及转动阶段，到第 8.8 秒清除。 */
} CarFire;

/* 每个有效 fire=on 帧是一次请求；等待舵机停止后才接受下一轮。 */
bool CarFire_Trigger(CarFire *fire, uint32_t now_ms);
void CarFire_Update(CarFire *fire, uint32_t now_ms, uint8_t levels[CAR_FIRE_LED_COUNT]);
bool CarFire_BuzzerHigh(const CarFire *fire, uint32_t now_ms);
uint16_t CarFire_ServoPulseUs(const CarFire *fire, uint32_t now_ms);
#endif
