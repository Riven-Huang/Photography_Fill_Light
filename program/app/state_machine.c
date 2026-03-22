#include "state_machine.h"
#include "program.h"
#include "drv_EC11.h"
#include "gpio.h"
#include "tim.h"

#define VOUT_TARGET_MV_DEFAULT         36000U
#define VDS_TARGET_MV_DAC_LOW         1000U  /* 低亮度时给 Q5 留更大调节余量[mV] / low current keeps more analog authority for a stable current loop. */
#define VDS_TARGET_MV_DAC_HIGH         500U  /* 满亮时把 Q5 压到接近全开[mV] / high power should minimize linear drop instead of burning heat on Q5. */
#define VOUT_SOFT_MAX_MV_DEFAULT     39000U  /* 软件母线顶[mV] / normal control must stop asking for more headroom before the 42V hard shutdown takes over. */
#define VOUT_ABS_MAX_MV_DEFAULT      42000U
#define VOUT_PI_KP_Q15                 128
#define VOUT_PI_KI_Q15                 2
#define BOOST_DUTY_PERMILLE_PI_MIN     50U
#define BOOST_DUTY_PERMILLE_PI_MAX     700U
#define BOOST_DUTY_PERMILLE_SOFT_START 50U
#define LIGHT_DAC_TARGET_DEFAULT       50U   /* 上电默认 5.0% [permille] / start from visible-low current to avoid cold inrush on COB. */
#define LIGHT_DAC_STEP_PERMILLE        10U   /* 每个 detent 改 1.0% [permille] / fine enough for dimming, coarse enough to avoid endless spinning. */
#define VDS_SOFT_START_STEP_MV         12U   /* 余量软启动步进[mV] / ramp headroom slowly so Boost and linear MOS do not overshoot together. */

volatile SystemState_t sys_state = SYS_INIT;
volatile uint32_t vout_target_mv = VOUT_TARGET_MV_DEFAULT; /* 标称 COB 工作点[mV]，当前仅保留给调试参考，不再作为运行态硬目标。 */
volatile uint32_t vout_ctrl_ref_mv = 0U;
volatile int32_t vout_ctrl_err_mv = 0;
volatile uint32_t vds_target_req_mv = 0U;
volatile uint32_t vds_target_mv = 0U;
volatile uint32_t vds_ctrl_ref_mv = 0U;
volatile uint32_t vout_soft_max_mv = VOUT_SOFT_MAX_MV_DEFAULT;
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

static uint32_t state_machine_calc_requested_vds_target_mv(uint16_t light_dac_permille)
{
    uint32_t span_mv;
    uint32_t reduce_mv;

    if (light_dac_permille > 1000U) {
        light_dac_permille = 1000U;
    }

    span_mv = VDS_TARGET_MV_DAC_LOW - VDS_TARGET_MV_DAC_HIGH;
    reduce_mv = ((uint32_t)light_dac_permille * span_mv + 500U) / 1000U;

    /* 亮度越高，目标余量越低；这样高功率时 Q5 会尽量接近全开，少在线性区白白烧压差。 */
    return VDS_TARGET_MV_DAC_LOW - reduce_mv;
}

static int32_t state_machine_estimate_vled_mv(void)
{
    int32_t vled_est_mv;

    vled_est_mv = (int32_t)vout_mv_lpf - (int32_t)vds_mv_lpf;
    if (vled_est_mv < 0) {
        vled_est_mv = 0;
    }

    return vled_est_mv;
}

static uint32_t state_machine_limit_vds_target_by_vout_soft_max(uint32_t req_vds_mv)
{
    int32_t vled_est_mv;
    int32_t vds_limit_mv;

    vled_est_mv = state_machine_estimate_vled_mv();
    vds_limit_mv = (int32_t)vout_soft_max_mv - vled_est_mv;
    if (vds_limit_mv <= 0) {
        /* 如果估算的 LED 压降已经逼近软件母线顶，就不允许余量环再继续抬 VOUT。 */
        return 0U;
    }

    if ((uint32_t)vds_limit_mv < req_vds_mv) {
        return (uint32_t)vds_limit_mv;
    }

    return req_vds_mv;
}

static void state_machine_update_headroom_target(void)
{
    vds_target_req_mv = state_machine_calc_requested_vds_target_mv(light_dac_target_permille);
    vds_target_mv = state_machine_limit_vds_target_by_vout_soft_max(vds_target_req_mv);
}

static uint32_t state_machine_build_dynamic_vout_ref(uint32_t vds_ref_mv)
{
    uint32_t vout_ref_mv;
    uint32_t vout_limit_mv;

    /* 当前 ADC_VDS 按 Q5 drain-to-gnd[mV] 理解，因此 Boost 目标是“LED 实际压降 + 当前允许余量”。 */
    vout_ref_mv = (uint32_t)state_machine_estimate_vled_mv() + vds_ref_mv;

    vout_limit_mv = vout_soft_max_mv;
    if (vout_limit_mv > vout_abs_max_mv) {
        vout_limit_mv = vout_abs_max_mv;
    }

    if (vout_ref_mv > vout_limit_mv) {
        /* 正常闭环的参考值只允许追到软件母线顶；42V 绝对上限仍单独保留给硬保护逻辑。 */
        vout_ref_mv = vout_limit_mv;
    }

    return vout_ref_mv;
}

static void state_machine_enter_standby(void)
{
    /* 待机时先切断驱动，再清掉 PWM 和电流参考，避免关机过程又给功率级补一个脉冲。 */
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_RESET);
    boost_set_low_side_duty_permille(0U);
    light_set_dac_duty_permille(0U);
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_RESET);

    vout_ctrl_ref_mv = 0U;
    vout_ctrl_err_mv = 0;
    vds_target_req_mv = 0U;
    vds_ctrl_ref_mv = 0U;
    vds_target_mv = 0U;
    vout_ovp_active = 0U;

    /* 余量环在待机时必须清积分，否则下次上电会把上一次热机工况的占空比残留带进来。 */
    vout_pi.ref = 0;
    vout_pi.feedback = (int32_t)vds_mv_lpf;
    vout_pi.error = 0;
    vout_pi.p_out = 0;
    vout_pi.i_out = 0;
    vout_pi.output = 0;
    vout_pi.i_term_q15 = 0;

    sys_state = SYS_STANDBY;
}

static void state_machine_enter_fault_overvoltage(void)
{
    /* 绝对过压直接关断 Boost，不能指望余量环自己慢慢拉回。 */
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_RESET);
    boost_set_low_side_duty_permille(0U);
    light_set_dac_duty_permille(0U);

    /* 过压后风扇继续转，避免 COB 和线性 MOS 上已经积累的热量滞留。 */
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);

    vout_ctrl_ref_mv = 0U;
    vout_ctrl_err_mv = 0;
    vds_target_req_mv = 0U;
    vds_ctrl_ref_mv = 0U;
    vds_target_mv = 0U;
    vout_ovp_active = 1U;

    vout_pi.ref = 0;
    vout_pi.feedback = (int32_t)vds_mv_lpf;
    vout_pi.error = 0;
    vout_pi.p_out = 0;
    vout_pi.i_out = 0;
    vout_pi.output = 0;
    vout_pi.i_term_q15 = 0;

    sys_state = SYS_FAULT;
}

static void state_machine_enter_soft_start(void)
{
    /* 软启动阶段先根据当前亮度命令算出目标余量，再从 0mV 缓慢拉起，避免开机瞬间又顶满母线又拉大电流。 */
    state_machine_update_headroom_target();
    vds_ctrl_ref_mv = 0U;
    vout_ctrl_ref_mv = state_machine_build_dynamic_vout_ref(vds_ctrl_ref_mv);

    drv_pid_pi_reset(&vout_pi, (int32_t)BOOST_DUTY_PERMILLE_SOFT_START);
    boost_set_low_side_duty_permille(BOOST_DUTY_PERMILLE_SOFT_START);
    light_set_dac_duty_permille(light_dac_target_permille);

    /* 先写入安全初值再开驱动，避免 MP1907 一上使能就顶到旧占空比。 */
    HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MP1907_EN_GPIO_Port, MP1907_EN_Pin, GPIO_PIN_SET);
    sys_state = SYS_SOFT_START;
}

static void state_machine_update_voltage_loop(uint32_t vds_ref_mv)
{
    vds_ctrl_ref_mv = state_machine_limit_vds_target_by_vout_soft_max(vds_ref_mv);
    vout_ctrl_ref_mv = state_machine_build_dynamic_vout_ref(vds_ctrl_ref_mv);

    /* 这里直接调 Q5 drain-to-gnd 余量[mV]；39V 软件顶会折算到这个参考值里，避免 PI 仍在暗中继续顶高母线。 */
    boost_set_low_side_duty_permille((uint16_t)drv_pid_pi_step(&vout_pi,
                                                               (int32_t)vds_ctrl_ref_mv,
                                                               (int32_t)vds_mv_lpf));
    vout_ctrl_err_mv = vout_pi.error;
}

/* 功率级状态机入口 / State machine tick；调用前必须已经完成硬件初始化并拿到一组新的 ADC 工程量。 */
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

        /* Soft-start 阶段也必须保持风扇开启，避免 COB 先升温再进入稳态。 */
        HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
        light_rotate_delta = drv_ec11_take_rotate_delta(&ec11_dev);
        state_machine_apply_light_dac_delta(light_rotate_delta);
        light_set_dac_duty_permille(light_dac_target_permille);
        state_machine_update_headroom_target();

        if (vds_ctrl_ref_mv < vds_target_mv) {
            vds_ctrl_ref_mv += VDS_SOFT_START_STEP_MV;
            if (vds_ctrl_ref_mv > vds_target_mv) {
                vds_ctrl_ref_mv = vds_target_mv;
            }
        } else if (vds_ctrl_ref_mv > vds_target_mv) {
            /* 软启动过程中如果用户突然把亮度拧高，对应余量目标会下降；这里直接跟随，避免继续按旧目标把 Q5 压差做大。 */
            vds_ctrl_ref_mv = vds_target_mv;
        }

        state_machine_update_voltage_loop(vds_ctrl_ref_mv);

        if (vds_ctrl_ref_mv >= vds_target_mv) {
            sys_state = SYS_RUNNING;
        }
        break;

    case SYS_RUNNING:
        if (power_toggle_event != 0U) {
            state_machine_enter_standby();
            break;
        }

        /* 运行态强制保持风扇开启，当前项目还没有温控闭环，先保证功率器件不裸奔。 */
        HAL_GPIO_WritePin(FAN_CTRL_GPIO_Port, FAN_CTRL_Pin, GPIO_PIN_SET);
        light_rotate_delta = drv_ec11_take_rotate_delta(&ec11_dev);
        state_machine_apply_light_dac_delta(light_rotate_delta);
        light_set_dac_duty_permille(light_dac_target_permille);
        state_machine_update_headroom_target();
        state_machine_update_voltage_loop(vds_target_mv);
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
