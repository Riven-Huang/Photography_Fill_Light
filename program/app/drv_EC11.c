#include "drv_EC11.h"
#include "main.h"

#define EC11_KEY_ACTIVE_LEVEL       GPIO_PIN_RESET
#define EC11_KEY_DEBOUNCE_TICKS     40U
#define EC11_KEY_LONG_PRESS_TICKS   2000U
#define EC11_ROTATE_DIR_SIGN        -1  /* 只翻业务方向符号，保留quadrature判向表，避免连带改坏去抖。 */

drv_ec11_t ec11_dev;

/* EC11的A/B是quadrature输入，用transition table滤掉抖动造成的非法跳变。 */
static int8_t drv_ec11_decode_phase_step(uint8_t prev_ab, uint8_t curr_ab)
{
    static const int8_t phase_table[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    return phase_table[((prev_ab & 0x03U) << 2) | (curr_ab & 0x03U)];
}

static uint8_t drv_ec11_pin_to_u8(GPIO_PinState pin_state)
{
    return (pin_state == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t drv_ec11_read_key_pressed(void)
{
    return (HAL_GPIO_ReadPin(EC11_D_GPIO_Port, EC11_D_Pin) == EC11_KEY_ACTIVE_LEVEL) ? 1U : 0U;
}

/* EC11输入初始化；调用前必须已经完成GPIO初始化。 */
void drv_ec11_init(drv_ec11_t *ec11)
{
    uint8_t ab_now;

    if (ec11 == 0) {
        return;
    }

    ec11->a_raw = drv_ec11_pin_to_u8(HAL_GPIO_ReadPin(EC11_A_GPIO_Port, EC11_A_Pin));
    ec11->b_raw = drv_ec11_pin_to_u8(HAL_GPIO_ReadPin(EC11_B_GPIO_Port, EC11_B_Pin));
    ab_now = (uint8_t)((ec11->a_raw << 1) | ec11->b_raw);
    ec11->key_raw = drv_ec11_read_key_pressed();
    ec11->key_stable = ec11->key_raw;
    ec11->key_pressed = ec11->key_raw;
    ec11->long_press_latched = 0U;
    ec11->long_press_event = 0U;
    ec11->ab_prev = ab_now;
    ec11->phase_accum = 0;
    ec11->rotate_delta = 0;
    ec11->debounce_cnt = 0U;
    ec11->press_cnt = 0U;
}

/* 按固定控制节拍更新EC11输入；当前默认按TIM14的2 kHz节拍设计。 */
void drv_ec11_task(drv_ec11_t *ec11)
{
    uint8_t ab_now;
    uint8_t key_sample;
    int8_t phase_step;

    if (ec11 == 0) {
        return;
    }

    ec11->a_raw = drv_ec11_pin_to_u8(HAL_GPIO_ReadPin(EC11_A_GPIO_Port, EC11_A_Pin));
    ec11->b_raw = drv_ec11_pin_to_u8(HAL_GPIO_ReadPin(EC11_B_GPIO_Port, EC11_B_Pin));
    ab_now = (uint8_t)((ec11->a_raw << 1) | ec11->b_raw);
    phase_step = drv_ec11_decode_phase_step(ec11->ab_prev, ab_now);
    ec11->ab_prev = ab_now;

    if (phase_step != 0) {
        ec11->phase_accum += phase_step;

        /* 满4个edge才确认1个detent，避免半格抖动就把亮度改掉。 */
        if (ec11->phase_accum >= 4) {
            ec11->phase_accum = 0;
            ec11->rotate_delta += EC11_ROTATE_DIR_SIGN;
        } else if (ec11->phase_accum <= -4) {
            ec11->phase_accum = 0;
            ec11->rotate_delta -= EC11_ROTATE_DIR_SIGN;
        }
    }

    key_sample = drv_ec11_read_key_pressed();

    if (key_sample != ec11->key_raw) {
        ec11->key_raw = key_sample;
        ec11->debounce_cnt = 0U;
    } else if (ec11->debounce_cnt < EC11_KEY_DEBOUNCE_TICKS) {
        ec11->debounce_cnt++;
    } else if (ec11->key_stable != ec11->key_raw) {
        ec11->key_stable = ec11->key_raw;
        ec11->key_pressed = ec11->key_stable;
        ec11->press_cnt = 0U;
        ec11->long_press_latched = 0U;
    }

    if (ec11->key_pressed != 0U) {
        if (ec11->press_cnt < EC11_KEY_LONG_PRESS_TICKS) {
            ec11->press_cnt++;
        }

        /* 长按事件只触发一次，避免按住不放时在状态机里反复开关机。 */
        if ((ec11->press_cnt >= EC11_KEY_LONG_PRESS_TICKS) &&
            (ec11->long_press_latched == 0U)) {
            ec11->long_press_latched = 1U;
            ec11->long_press_event = 1U;
        }
    }
}

/* 取走一次性长按事件；取走后事件会清零。 */
uint8_t drv_ec11_take_long_press_event(drv_ec11_t *ec11)
{
    uint8_t event_value;

    if (ec11 == 0) {
        return 0U;
    }

    event_value = ec11->long_press_event;
    ec11->long_press_event = 0U;

    return event_value;
}

/* 每个控制节拍只消费一次detent增量；正负方向取决于A/B实际接线顺序。 */
int16_t drv_ec11_take_rotate_delta(drv_ec11_t *ec11)
{
    int16_t delta_value;

    if (ec11 == 0) {
        return 0;
    }

    delta_value = ec11->rotate_delta;
    ec11->rotate_delta = 0;

    return delta_value;
}

//
// End of File
//
