#include "state_machine.h"
#include "program.h"
#include "drv_EC11.h"
#include "gpio.h"
#include "tim.h"

#define VOUT_TARGET_MV_DEFAULT         36000U
#define VOUT_ABS_MAX_MV_DEFAULT        42000U
#define VOUT_PI_KP_Q15                 128
#define VOUT_PI_KI_Q15                 2
#define BOOST_DUTY_PERMILLE_PI_MIN     50U
#define BOOST_DUTY_PERMILLE_PI_MAX     700U
#define BOOST_DUTY_PERMILLE_SOFT_START 50U
#define LIGHT_DAC_TARGET_DEFAULT       50U   /* 上电默认5.0%[permille]，先给最小可见亮度，避免开机即大电流冲灯珠。 */
#define LIGHT_DAC_STEP_PERMILLE        10U   /* 每个detent调1% [permille]，手感细但不至于转很多圈。 */
#define SOFT_START_STEP_MV             12U

volatile SystemState_t sys_state = SYS_INIT;
volatile uint32_t vout_target_mv = VOUT_TARGET_MV_DEFAULT;
volatile uint32_t vout_ctrl_ref_mv = 0U;
volatile int32_t vout_ctrl_err_mv = 0;
volatile uint32_t vout_abs_max_mv = VOUT_ABS_MAX_MV_DEFAULT;
volatile uint8_t vout_ovp_active = 0U;
volatile uint16_t light_dac_target_permille = LIGHT_DAC_TARGET_DEFAULT;
drv_pid_pi_t vout_pi;

static void state_machine_apply_light_dac_delta(int16_t detent_delta)
{
    int32_t next_target;

    if (detent_delta == 0) {
        return;
    }

    next_target = (int32_t)light_dac_target_permille +
                  ((int32_t)detent_delta * (int32_t)LIGHT_DAC_STEP_PERMILLE);

    if (next_target < 0) {
        next_target = 0;
    } else if (next_target > 1000) {
        next_target = 1000;
    }

    light_dac_target_permille = (uint16_t)next_target;
}

static void state_machine_enter_standby(void)
{
    /* 待机时先切断驱动，再清掉PWM和电流参考，避免关机过程又给功率级补一个脉冲。 */
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_RESET);
    boost_set_low_side_duty_permille(0U);
    light_set_dac_duty_permille(0U);
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_RESET);

    vout_ctrl_ref_mv = 0U;
    vout_ctrl_err_mv = 0;
    vout_ovp_active = 0U;

    /* 待机时把PI动态量清零，调试时看到的就是“关机状态”，不会残留上一次积分。 */
    vout_pi.ref = 0;
    vout_pi.feedback = (int32_t)vout_mv_lpf;
    vout_pi.error = 0;
    vout_pi.p_out = 0;
    vout_pi.i_out = 0;
    vout_pi.output = 0;
    vout_pi.i_term_q15 = 0;

    sys_state = SYS_STANDBY;
}

static void state_machine_enter_fault_overvoltage(void)
{
    /* 绝对过压用瞬时工程量[mV]判定，优先关断Boost，不能等闭环自己慢慢拉回。 */
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_RESET);
    boost_set_low_side_duty_permille(0U);
    light_set_dac_duty_permille(0U);

    /* 过压往往发生在有功状态，故障锁定期间保持风扇开启，避免热量滞留在灯板和线性MOS上。 */
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);

    vout_ctrl_ref_mv = 0U;
    vout_ctrl_err_mv = 0;
    vout_ovp_active = 1U;

    vout_pi.ref = 0;
    vout_pi.feedback = (int32_t)vout_mv_lpf;
    vout_pi.error = 0;
    vout_pi.p_out = 0;
    vout_pi.i_out = 0;
    vout_pi.output = 0;
    vout_pi.i_term_q15 = 0;

    sys_state = SYS_FAULT;
}

static void state_machine_enter_soft_start(void)
{
    /* 软启动从当前实测Vout起步，避免输出电容残压存在时参考突然回零再猛拉高。 */
    vout_ctrl_ref_mv = vout_mv_lpf;
    if (vout_ctrl_ref_mv > vout_target_mv) {
        vout_ctrl_ref_mv = vout_target_mv;
    }

    drv_pid_pi_reset(&vout_pi, (int32_t)BOOST_DUTY_PERMILLE_SOFT_START);
    boost_set_low_side_duty_permille(BOOST_DUTY_PERMILLE_SOFT_START);
    light_set_dac_duty_permille(light_dac_target_permille);

    /* 先把占空比和电流参考放到软启动初值，再打开驱动，避免开机瞬间直接顶到目标。 */
    /* 灯珠发热重，进入有功状态前就先开风扇，避免软启动阶段已经开始积热。 */
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_SET);
    sys_state = SYS_SOFT_START;
}

static void state_machine_update_voltage_loop(uint32_t ref_mv)
{
    vout_ctrl_ref_mv = ref_mv;
    boost_set_low_side_duty_permille((uint16_t)drv_pid_pi_step(&vout_pi,
                                                               (int32_t)vout_ctrl_ref_mv,
                                                               (int32_t)vout_mv_lpf));
    vout_ctrl_err_mv = vout_pi.error;
}

/* 功率级状态机入口；调用前必须已经完成硬件初始化和ADC数据更新。 */
int16_t light_rotate_delta;
void State_M(void)
{
    uint8_t power_toggle_event;

    if (sys_state == SYS_INIT) {
        drv_ec11_init(&ec11_dev);
        drv_pid_pi_init(&vout_pi,
                        VOUT_PI_KP_Q15,
                        VOUT_PI_KI_Q15,
                        (int32_t)BOOST_DUTY_PERMILLE_PI_MIN,
                        (int32_t)BOOST_DUTY_PERMILLE_PI_MAX,
                        (int32_t)BOOST_DUTY_PERMILLE_SOFT_START);
        state_machine_enter_standby();
        return;
    }

    drv_ec11_task(&ec11_dev);
    power_toggle_event = drv_ec11_take_long_press_event(&ec11_dev);

    if (((sys_state == SYS_SOFT_START) || (sys_state == SYS_RUNNING)) &&
        (vout_mv >= vout_abs_max_mv)) {
        state_machine_enter_fault_overvoltage();
        return;
    }

    switch (sys_state) {
    case SYS_STANDBY:
        /* 待机时清掉旋钮累计量，防止关机状态误旋转在开机时一次性跳亮度。 */
        (void)drv_ec11_take_rotate_delta(&ec11_dev);
        if (power_toggle_event != 0U) {
            state_machine_enter_soft_start();
        }
        break;

    case SYS_SOFT_START:
        if (power_toggle_event != 0U) {
            state_machine_enter_standby();
            break;
        }

        /* Soft-start阶段也必须保持风扇开启，避免COB先升温再进入稳态。 */
        HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
        light_rotate_delta = drv_ec11_take_rotate_delta(&ec11_dev);
        state_machine_apply_light_dac_delta(light_rotate_delta);
        light_set_dac_duty_permille(light_dac_target_permille);

        if (vout_ctrl_ref_mv < vout_target_mv) {
            vout_ctrl_ref_mv += SOFT_START_STEP_MV;
            if (vout_ctrl_ref_mv > vout_target_mv) {
                vout_ctrl_ref_mv = vout_target_mv;
            }
        }

        state_machine_update_voltage_loop(vout_ctrl_ref_mv);

        if (vout_ctrl_ref_mv >= vout_target_mv) {
            sys_state = SYS_RUNNING;
        }
        break;

    case SYS_RUNNING:
        if (power_toggle_event != 0U) {
            state_machine_enter_standby();
            break;
        }

        /* 运行态强制保持风扇开启，当前项目没有温控闭环，靠状态机保证散热不中断。 */
        HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
        light_rotate_delta = drv_ec11_take_rotate_delta(&ec11_dev);
        state_machine_apply_light_dac_delta(light_rotate_delta);
        light_set_dac_duty_permille(light_dac_target_permille);
        state_machine_update_voltage_loop(vout_target_mv);
        break;

    case SYS_FAULT:
        (void)drv_ec11_take_rotate_delta(&ec11_dev);
        if (power_toggle_event != 0U) {
            state_machine_enter_standby();
        }
        break;

    default:
        state_machine_enter_standby();
        break;
    }
}

//
// End of File
//
