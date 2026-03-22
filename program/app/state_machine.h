#ifndef _STATE_MACHINE_
#define _STATE_MACHINE_

#include <stdint.h>
#include "drv_pid.h"

typedef enum {
    SYS_INIT = 0,
    SYS_STANDBY,
    SYS_SOFT_START,
    SYS_RUNNING,
    SYS_FAULT
} SystemState_t;

extern volatile SystemState_t sys_state;
extern volatile uint32_t vout_target_mv;
extern volatile uint32_t vout_ctrl_ref_mv;
extern volatile int32_t vout_ctrl_err_mv;
extern volatile uint32_t vds_target_req_mv;
extern volatile uint32_t vds_target_mv;
extern volatile uint32_t vds_ctrl_ref_mv;
extern volatile uint32_t vout_soft_max_mv;
extern volatile uint32_t vout_abs_max_mv;
extern volatile uint8_t vout_ovp_active;
extern volatile uint16_t light_dac_target_permille;
extern drv_pid_pi_t vout_pi;

/* 状态机入口 / State machine tick；调用前必须已经完成外设初始化并拿到一组新的 ADC 工程量。 */
void State_M(void);

#endif // _STATE_MACHINE_
//
// End of File
//
