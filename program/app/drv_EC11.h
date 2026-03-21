#ifndef _DRV_EC11_
#define _DRV_EC11_

#include <stdint.h>

typedef struct {
    uint8_t a_raw;
    uint8_t b_raw;
    uint8_t key_raw;
    uint8_t key_stable;
    uint8_t key_pressed;
    uint8_t long_press_latched;
    uint8_t long_press_event;
    uint8_t ab_prev;
    int8_t phase_accum;
    int16_t rotate_delta;
    uint16_t debounce_cnt;
    uint16_t press_cnt;
} drv_ec11_t;

extern drv_ec11_t ec11_dev;

/* EC11输入初始化；调用前必须已经完成GPIO初始化。 */
void drv_ec11_init(drv_ec11_t *ec11);

/* 按固定控制节拍更新EC11输入；当前默认按TIM14的2 kHz节拍设计。 */
void drv_ec11_task(drv_ec11_t *ec11);

/* 取走一次性长按事件；取走后事件会清零。 */
uint8_t drv_ec11_take_long_press_event(drv_ec11_t *ec11);

/* 取走累计旋转增量；正负方向由A/B相位顺序决定。 */
int16_t drv_ec11_take_rotate_delta(drv_ec11_t *ec11);

#endif // _DRV_EC11_
//
// End of File
//
