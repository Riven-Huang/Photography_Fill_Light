#ifndef _PROGRAM_
#define _PROGRAM_

#include <stdint.h>
#include "state_machine.h"

/* 采样调试镜像 / Debug snapshot：全部使用mV，delta为有符号量，便于观察Vout与Vds的瞬时压差。 */
/* 闭环调试镜像 / Debug snapshot：这里放控制实际使用的低通工程量[mV]。 */
typedef struct {
    uint32_t vout_mv;
    uint32_t vds_mv;
    int32_t vout_minus_vds_mv;
} boost_voltage_debug_t;

/* 对外量测量：adc_raw 用LSB，pin_mv / mv 用mV。 */
extern volatile uint16_t adc_buffer[2];
extern volatile uint16_t vout_adc_raw;
extern volatile uint16_t vds_adc_raw;
extern volatile uint16_t vout_adc_raw_lpf;
extern volatile uint16_t vds_adc_raw_lpf;
extern volatile uint32_t vout_pin_mv;
extern volatile uint32_t vds_pin_mv;
extern volatile uint32_t vout_mv;
extern volatile uint32_t vds_mv;
extern volatile uint32_t vout_mv_lpf;
extern volatile uint32_t vds_mv_lpf;
extern volatile boost_voltage_debug_t boost_voltage_debug;

/* 当前实际应用到Boost下管的有效占空比 [permille]。 */
extern volatile uint16_t boost_duty_permille;
extern volatile uint16_t light_dac_output_permille;
extern volatile uint16_t light_dac_compare_counts;

/* 接口约束：
   hardware_config() 在外设初始化完成后调用；
   program_task() 在主循环调用；
   boost_set_low_side_duty_permille() 只在TIM1互补PWM启动后调用。 */
void hardware_config(void);
void program_task(void);
void boost_set_low_side_duty_permille(uint16_t duty_permille);
void light_set_dac_duty_permille(uint16_t duty_permille);

#endif // _PROGRAM_
//
// End of File
//
